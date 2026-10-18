// Copyright Epic Games, Inc. All Rights Reserved.

// QA for standalone and multi-process tests, not in shipping builds. Python only exists in the editor, so a
// game process (host or client) runs a plain text script instead:
//   facility.QA.Script <file>
// One console command per line, run in order on this machine. "wait <seconds>" pauses, "waitplayers <n> <timeout>"
// holds until n players are in the run, "shot <name>" saves a screenshot, "#" starts a comment, "quit" closes the process. Pass it at launch with -ExecCmds="facility.QA.Script <file>".
// facility.QA.Observe logs what this machine sees (FACILITY_QA_SEEN lines), so a client's view can be checked
// from its own log, next to the host's FACILITY_QA_CHECK lines.

#if !UE_BUILD_SHIPPING

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityTenant.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "TheFacility.h"

namespace FacilityQAScript
{
	/** A script being played on this machine. Commands run in the world that is current when they come up */
	struct FRunner
	{
		TArray<FString> Lines;
		int32 Next = 0;
		double ResumeTime = 0.0;
		/** Real time a waitplayers line started waiting, or -1 */
		double WaitStarted = -1.0;
		FString Name;
	};

	static TSharedPtr<FRunner> Active;
	static FTSTicker::FDelegateHandle TickHandle;

	static UWorld* CurrentGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
			{
				if (UWorld* World = Context.World())
				{
					return World;
				}
			}
		}
		return nullptr;
	}

	static bool Step(float)
	{
		if (!Active.IsValid())
		{
			return false;
		}

		// real time, so a map change (which restarts world time) doesn't stall the script
		const double Now = FPlatformTime::Seconds();
		while (Active->Next < Active->Lines.Num() && Now >= Active->ResumeTime)
		{
			const FString Line = Active->Lines[Active->Next++].TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
			{
				continue;
			}

			if (Line.StartsWith(TEXT("wait ")))
			{
				Active->ResumeTime = Now + FCString::Atof(*Line.RightChop(5));
				break;
			}

			UWorld* World = CurrentGameWorld();

			// waitplayers <count> <timeout>: holds the script until that many players are in the run
			if (Line.StartsWith(TEXT("waitplayers ")))
			{
				TArray<FString> Parts;
				Line.ParseIntoArrayWS(Parts);
				const int32 Wanted = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 2;
				const double Timeout = Parts.IsValidIndex(2) ? FCString::Atof(*Parts[2]) : 60.0;
				const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
				const int32 Have = GameState ? GameState->PlayerArray.Num() : 0;
				if (Have < Wanted && Active->WaitStarted >= 0.0 && Now - Active->WaitStarted < Timeout)
				{
					--Active->Next;
					break;
				}
				if (Have < Wanted && Active->WaitStarted < 0.0)
				{
					Active->WaitStarted = Now;
					--Active->Next;
					break;
				}
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SCRIPT %s: %d of %d players after %.1f s"), *Active->Name, Have, Wanted, Active->WaitStarted < 0.0 ? 0.0 : Now - Active->WaitStarted);
				Active->WaitStarted = -1.0;
				continue;
			}

			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SCRIPT %s: %s"), *Active->Name, *Line);
			if (Line == TEXT("quit"))
			{
				Active.Reset();
				FPlatformMisc::RequestExit(false, TEXT("facility.QA.Script"));
				return false;
			}

			if (Line.StartsWith(TEXT("shot ")))
			{
				// lands in Saved/Screenshots/<platform>, named as given
				FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / Line.RightChop(5).TrimStartAndEnd() + TEXT(".png"), true, false);
				continue;
			}

			// through the local player when there is one, so viewport commands (HighResShot, show flags) work too
			if (APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr; PlayerController && PlayerController->IsLocalController())
			{
				PlayerController->ConsoleCommand(Line);
			}
			else if (GEngine)
			{
				GEngine->Exec(World, *Line);
			}
		}

		if (Active.IsValid() && Active->Next >= Active->Lines.Num())
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SCRIPT %s: finished"), *Active->Name);
			Active.Reset();
			return false;
		}
		return true;
	}

	static FString Life(const AFacilityCharacter* Player)
	{
		return !Player ? TEXT("Missing") : Player->IsRetained() ? TEXT("Retained") : Player->IsDowned() ? TEXT("Downed") : TEXT("Standing");
	}
}

using namespace FacilityQAScript;

static FAutoConsoleCommand CmdQAScript(
	TEXT("facility.QA.Script"),
	TEXT("QA: plays a text file of console commands on this machine, one per line. \"wait <s>\" pauses, \"quit\" exits. facility.QA.Script <file>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			return;
		}

		const FString Path = FString::Join(Args, TEXT(" "));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogTheFacility, Error, TEXT("FACILITY_QA_SCRIPT can't read %s"), *Path);
			return;
		}

		Active = MakeShared<FRunner>();
		Active->Name = FPaths::GetBaseFilename(Path);
		Text.ParseIntoArrayLines(Active->Lines, false);
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SCRIPT %s: %d lines"), *Active->Name, Active->Lines.Num());

		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Step));
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAMenu(
	TEXT("facility.QA.Menu"),
	TEXT("QA: does what a menu button does. facility.QA.Menu Host | Join <address> | Pause | Leave"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UFacilityGameInstance* GameInstance = World ? World->GetGameInstance<UFacilityGameInstance>() : nullptr;
		if (!GameInstance || Args.IsEmpty())
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Menu: needs UFacilityGameInstance and an action"));
			return;
		}

		if (Args[0] == TEXT("Host"))
		{
			GameInstance->HostRun(GameInstance->GetPlayerName());
		}
		else if (Args[0] == TEXT("Join") && Args.Num() > 1)
		{
			GameInstance->JoinRun(Args[1], GameInstance->GetPlayerName());
		}
		else if (Args[0] == TEXT("Pause"))
		{
			GameInstance->TogglePauseMenu(World->GetFirstPlayerController());
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN pause menu open=%d"), GameInstance->IsPauseMenuOpen());
		}
		else if (Args[0] == TEXT("State"))
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN menus %s"), *GameInstance->DescribeMenus());
		}
		else if (Args[0] == TEXT("Leave"))
		{
			GameInstance->LeaveRun();
		}
	}));

static FAutoConsoleCommandWithWorld CmdQAObserve(
	TEXT("facility.QA.Observe"),
	TEXT("QA, any machine: logs what this machine sees of the run, the local player, the creatures and the doors"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World)
		{
			return;
		}

		const TCHAR* Machine = World->GetNetMode() == NM_Client ? TEXT("client") : World->GetNetMode() == NM_ListenServer ? TEXT("host") : TEXT("standalone");
		const AFacilityGameState* GameState = World->GetGameState<AFacilityGameState>();
		const APlayerController* PC = World->GetFirstPlayerController();
		const AFacilityCharacter* Me = PC ? Cast<AFacilityCharacter>(PC->GetPawn()) : nullptr;

		FString Keys;
		if (Me)
		{
			for (const FName& Key : Me->GetHeldKeys())
			{
				Keys += Key.ToString() + TEXT(" ");
			}
		}

		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s me=%s life=%s at %s carrying [%s] players=%d run=%s power=%s ledger=%d"),
			Machine, PC && PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("-"), *Life(Me),
			Me ? *Me->GetActorLocation().ToCompactString() : TEXT("-"), *Keys.TrimEnd(),
			GameState ? GameState->PlayerArray.Num() : 0,
			GameState ? *StaticEnum<EFacilityRunState>()->GetNameStringByValue((int64)GameState->GetRunState()) : TEXT("-"),
			GameState && GameState->IsPowerRestored() ? TEXT("on") : TEXT("off"), GameState ? GameState->GetLedger().Num() : 0);

		for (TActorIterator<AFacilityTenant> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s tenant %s hidden=%d at %s"), Machine,
				*StaticEnum<ETenantState>()->GetNameStringByValue((int64)It->GetTenantState()), It->IsHidden(), *It->GetActorLocation().ToCompactString());
		}
		for (TActorIterator<AFacilityMarked> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s marked %s visible_here=%d i_am_marked=%d"), Machine,
				*StaticEnum<EMarkedState>()->GetNameStringByValue((int64)It->GetMarkedState()), It->GetMesh() && It->GetMesh()->IsVisible(), It->IsLocalPlayerMarked());
		}
		for (TActorIterator<AFacilityDoor> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s door %s open=%d locked=%d broken=%d"), Machine,
				*It->GetName(), It->IsOpen(), It->IsLocked(), It->IsBroken());
		}
	}));

#endif

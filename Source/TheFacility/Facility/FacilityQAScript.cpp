// Copyright Epic Games, Inc. All Rights Reserved.

// QA for standalone and multi-process tests, not in shipping builds. Python only exists in the editor, so a
// game process (host or client) runs a plain text script instead:
//   facility.QA.Script <file>
// One console command per line, run in order on this machine. "wait <seconds>" pauses, "waitplayers <n> <timeout>"
// holds until n players are in the run, "shot <name>" saves a screenshot, "#" starts a comment, "quit" closes the process. Pass it at launch with -ExecCmds="facility.QA.Script <file>".
// facility.QA.Observe logs what this machine sees (FACILITY_QA_SEEN lines), so a client's view can be checked
// from its own log, next to the host's FACILITY_QA_CHECK lines.

#if !UE_BUILD_SHIPPING

#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDirector.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityTenant.h"
#include "Facility/FacilityVoiceComponent.h"
#include "Interfaces/VoiceInterface.h"
#include "OnlineSubsystemUtils.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "AudioCaptureCore.h"
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
	TEXT("QA: does what a menu button does. facility.QA.Menu Host | Join <address> | Pause | Settings [0|1] | Set <Look|Brightness|Master|Voice> <value> | Leave | State"),
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
		else if (Args[0] == TEXT("Settings"))
		{
			GameInstance->ShowMenuSettings(Args.Num() < 2 || Args[1] != TEXT("0"));
		}
		else if (Args[0] == TEXT("Set") && Args.Num() > 2)
		{
			// what the settings sliders do: Look, Brightness, Master, Voice
			const float Value = FCString::Atof(*Args[2]);
			if (Args[1] == TEXT("Look")) { GameInstance->SetLookSensitivity(Value); }
			else if (Args[1] == TEXT("Brightness")) { GameInstance->SetBrightness(Value); }
			else if (Args[1] == TEXT("Master")) { GameInstance->SetMasterVolume(Value); }
			else if (Args[1] == TEXT("Voice")) { GameInstance->SetVoiceVolume(Value); }
			else if (Args[1] == TEXT("Effects")) { GameInstance->SetEffectsVolume(Value); }
			else if (Args[1] == TEXT("Retro")) { GameInstance->SetRetroLevel(FMath::RoundToInt(Value)); }
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN settings look %.2f brightness %.2f master %.2f voice %.2f effects %.2f retro %d"),
				GameInstance->GetLookSensitivity(), GameInstance->GetBrightness(), GameInstance->GetMasterVolume(), GameInstance->GetVoiceVolume(),
				GameInstance->GetEffectsVolume(), GameInstance->GetRetroLevel());
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

static FAutoConsoleCommandWithWorldAndArgs CmdQAMicSample(
	TEXT("facility.QA.MicSample"),
	TEXT("QA: reads the raw microphone for Seconds and logs each second's peak and average level (0..1). facility.QA.MicSample <Seconds>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UFacilityGameInstance> Instance = World ? World->GetGameInstance<UFacilityGameInstance>() : nullptr;
		if (!Instance.IsValid())
		{
			return;
		}

		struct FWindow { double Start = 0.0; double Next = 0.0; double End = 0.0; float Peak = 0.0f; double Sum = 0.0; int64 Count = 0; FString State; };
		TSharedRef<FWindow> Window = MakeShared<FWindow>();
		Window->Start = FPlatformTime::Seconds();
		Window->Next = Window->Start + 1.0;
		Window->End = Window->Start + (Args.IsEmpty() ? 10.0 : FCString::Atof(*Args[0]));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Instance, Window](float)
		{
			if (!Instance.IsValid())
			{
				return false;
			}
			TArray<int16> Samples;
			Window->State = Instance->ReadMicSamples(Samples);
			for (const int16 Sample : Samples)
			{
				const float Level = FMath::Abs(Sample) / 32768.0f;
				Window->Peak = FMath::Max(Window->Peak, Level);
				Window->Sum += Level;
				++Window->Count;
			}
			const double Now = FPlatformTime::Seconds();
			if (Now >= Window->Next)
			{
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_MIC t=%3.0f peak=%.4f avg=%.4f samples=%lld state=%s"), Now - Window->Start, Window->Peak,
					Window->Count > 0 ? Window->Sum / Window->Count : 0.0, Window->Count, *Window->State);
				Window->Next += 1.0;
				Window->Peak = 0.0f;
				Window->Sum = 0.0;
				Window->Count = 0;
			}
			return Now < Window->End;
		}));
	}));

static FAutoConsoleCommand CmdQAWasapiSample(
	TEXT("facility.QA.WasapiSample"),
	TEXT("QA: reads the default microphone through the engine's WASAPI capture (not the voice path) for Seconds, logging each second's peak. facility.QA.WasapiSample <Seconds>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		struct FProbe
		{
			Audio::FAudioCapture Capture;
			FCriticalSection Lock;
			float Peak = 0.0f;
			int64 Frames = 0;
			double Start = 0.0, Next = 0.0, End = 0.0;
		};
		TSharedRef<FProbe> Probe = MakeShared<FProbe>();
		Audio::FCaptureDeviceInfo Info;
		const bool bInfo = Probe->Capture.GetCaptureDeviceInfo(Info);
		Audio::FAudioCaptureDeviceParams Params;
		TWeakPtr<FProbe> Weak = Probe;
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const bool bOpen = Probe->Capture.OpenCaptureStream(Params, [Weak](const float* Audio, int32 NumFrames, int32 NumChannels, int32, double, bool)
		{
			if (TSharedPtr<FProbe> Pinned = Weak.Pin())
			{
				FScopeLock ScopeLock(&Pinned->Lock);
				for (int32 Index = 0; Index < NumFrames * NumChannels; ++Index)
				{
					Pinned->Peak = FMath::Max(Pinned->Peak, FMath::Abs(Audio[Index]));
				}
				Pinned->Frames += NumFrames;
			}
		}, 1024);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		const bool bStarted = bOpen && Probe->Capture.StartStream();
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_WASAPI device=%s rate=%d channels=%d open=%d started=%d"), bInfo ? *Info.DeviceName : TEXT("?"), bInfo ? Info.PreferredSampleRate : 0, bInfo ? Info.InputChannels : 0, bOpen, bStarted);
		Probe->Start = FPlatformTime::Seconds();
		Probe->Next = Probe->Start + 1.0;
		Probe->End = Probe->Start + (Args.IsEmpty() ? 10.0 : FCString::Atof(*Args[0]));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float)
		{
			const double Now = FPlatformTime::Seconds();
			if (Now >= Probe->Next)
			{
				FScopeLock ScopeLock(&Probe->Lock);
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_WASAPI t=%3.0f peak=%.4f frames=%lld"), Now - Probe->Start, Probe->Peak, Probe->Frames);
				Probe->Peak = 0.0f;
				Probe->Frames = 0;
				Probe->Next += 1.0;
			}
			if (Now < Probe->End)
			{
				return true;
			}
			Probe->Capture.StopStream();
			Probe->Capture.CloseStream();
			return false;
		}));
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

		for (TActorIterator<AFacilityCharacter> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s player %s body_hidden=%d hiding=%d at %s"), Machine,
				It->GetPlayerState() ? *It->GetPlayerState()->GetPlayerName() : *It->GetName(), It->IsHidden() || (It->GetMesh() && It->GetMesh()->bHiddenInGame), It->IsHiding(), *It->GetActorLocation().ToCompactString());
			if (const UFacilityVoiceComponent* Voice = It->GetVoice())
			{
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s voice %s mode=%s mic=%d talker_here=%d server_muted_for_me=%d retained=%d amplitude_on_server=%.3f voice_reports=%d"), Machine,
					It->GetPlayerState() ? *It->GetPlayerState()->GetPlayerName() : *It->GetName(), *StaticEnum<EFacilityVoiceMode>()->GetNameStringByValue((int64)Voice->GetVoiceMode()),
					Voice->HasMicrophone(), Voice->HasTalker(), PC && It->GetPlayerState() && It->GetPlayerState()->GetUniqueId().IsValid() && const_cast<APlayerController*>(PC)->IsPlayerMuted(*It->GetPlayerState()->GetUniqueId().GetUniqueNetId()), It->IsRetained(), Voice->GetLastAmplitude(), Voice->GetNumVoiceReports());
			}
		}
		if (IOnlineVoicePtr VoiceInterface = Online::GetVoiceInterface(World); VoiceInterface.IsValid())
		{
			FString DebugState = VoiceInterface->GetVoiceDebugState();
			DebugState.ReplaceInline(TEXT("\n"), TEXT(" | "));
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s voice_engine local_talking=%d %s"), Machine, VoiceInterface->IsLocalPlayerTalking(0), *DebugState.Left(600));
			if (UFacilityGameInstance* Instance = World->GetGameInstance<UFacilityGameInstance>())
			{
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s mic_meter=%.4f"), Machine, Instance->GetMicLevel());
			}
		}
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
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s sounds_played=%d last=%s"), Machine, FacilityAudio::GetPlayedCount(), *FacilityAudio::GetLastPlayed());
		if (const AFacilityDirector* Director = AFacilityDirector::Get(World))
		{
			float Age = 0.0f;
			const FString& Whisper = Director->GetWhisper(Age);
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s director attention=%d tier=%d darkened_lights=%d whisper=\"%s\" %.0fs ago"), Machine,
				Director->GetAttention(), Director->GetTier(), Director->GetNumDarkened(), *Whisper, Age);
		}
		for (TActorIterator<AFacilityDoor> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_SEEN %s door %s open=%d locked=%d broken=%d held=%d"), Machine,
				*It->GetName(), It->IsOpen(), It->IsLocked(), It->IsBroken(), It->IsHeldShut());
		}
	}));

#endif

// Copyright Epic Games, Inc. All Rights Reserved.

// QA console commands, not in shipping builds. They let a script (Saved/QA/harness) drive a run on the host
// through the same server paths players use, and check the outcome. Each check logs one line:
//   FACILITY_QA_CHECK PASS|FAIL <what> expected <x> got <y>
// Players are addressed by index in the host's controller list: 0 is the host.

#if !UE_BUILD_SHIPPING

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityInteractable.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityMovementComponent.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityTenant.h"
#include "Camera/CameraActor.h"
#include "Containers/Ticker.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "TheFacility.h"

namespace FacilityQA
{
	static bool IsHost(UWorld* World, const TCHAR* Command)
	{
		if (World && World->GetNetMode() != NM_Client)
		{
			return true;
		}
		UE_LOG(LogTheFacility, Warning, TEXT("%s: run it on the host"), Command);
		return false;
	}

	static AFacilityCharacter* GetPlayer(UWorld* World, int32 Index)
	{
		int32 Current = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Current)
		{
			if (Current == Index)
			{
				return It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
			}
		}
		return nullptr;
	}

	static int32 ArgInt(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	static float ArgFloat(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	/** Actor whose editor label or object name contains Name, ignoring case. Labels exist in editor builds only */
	template<typename T>
	static T* FindActor(UWorld* World, const FString& Name)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
#if WITH_EDITOR
			if (It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
#endif
			if (It->GetName().Contains(Name))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** 1 if the last check or probe passed, 0 if not. Scripts poll it through facility.QA.Probe to wait for something */
	static TAutoConsoleVariable<int32> CVarLastResult(TEXT("facility.QA.LastResult"), 0, TEXT("QA: 1 if the last facility.QA.Expect or Probe passed"));

	/** Probes check without logging */
	static bool bQuiet = false;

	static void Check(const FString& What, const FString& Expected, const FString& Got)
	{
		const bool bPass = Expected.Equals(Got, ESearchCase::IgnoreCase);
		// through the variable, not a C++ ref: a ref-backed console variable reads a cached copy
		CVarLastResult->Set(bPass ? 1 : 0, ECVF_SetByCode);
		if (bQuiet)
		{
			return;
		}
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_CHECK %s %s expected %s got %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *What, *Expected, *Got);
	}

	static FString PlayerLife(const AFacilityCharacter* Player)
	{
		return !Player ? TEXT("Missing") : Player->IsRetained() ? TEXT("Retained") : Player->IsDowned() ? TEXT("Downed") : TEXT("Standing");
	}

	static FString DoorState(const AFacilityDoor* Door)
	{
		if (!Door)
		{
			return TEXT("Missing");
		}
		if (Door->IsBroken())
		{
			return TEXT("Broken");
		}
		return FString(Door->IsOpen() ? TEXT("Open") : TEXT("Closed")) + (Door->IsLocked() ? TEXT("Locked") : TEXT(""));
	}
}

using namespace FacilityQA;

static FAutoConsoleCommandWithWorldAndArgs CmdQAInteract(
	TEXT("facility.QA.Interact"),
	TEXT("QA, host: player uses an actor, as if they had pressed interact on it, wherever they are. facility.QA.Interact <ActorLabel> [Player]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.Interact")) || Args.IsEmpty())
		{
			return;
		}

		AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
		AActor* Target = FindActor<AActor>(World, Args[0]);
		if (!Player || !Target || !Target->Implements<UFacilityInteractable>())
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Interact: no player or no interactable called %s"), *Args[0]);
			return;
		}

		if (!Player->IsStanding() || !IFacilityInteractable::Execute_CanInteract(Target, Player))
		{
			UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Interact: %s refused by %s"), *Player->GetName(), *Target->GetName());
			return;
		}

		UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Interact: %s uses %s"), *Player->GetName(), *Target->GetName());
		IFacilityInteractable::Execute_Interact(Target, Player);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQATake(
	TEXT("facility.QA.Take"),
	TEXT("QA, host: player takes the first untaken pickup granting this item, signing for it if it is an Offering. facility.QA.Take <ItemId> [Player]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.Take")) || Args.IsEmpty())
		{
			return;
		}

		AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
		for (TActorIterator<AFacilityPickup> It(World); It && Player; ++It)
		{
			if (It->GetGrantedKeyId() == FName(*Args[0]) && IFacilityInteractable::Execute_CanInteract(*It, Player))
			{
				UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Take: %s takes %s"), *Player->GetName(), *It->GetName());
				IFacilityInteractable::Execute_Interact(*It, Player);
				return;
			}
		}
		UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Take: no player or no pickup granting %s"), *Args[0]);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAGive(
	TEXT("facility.QA.Give"),
	TEXT("QA, host: gives an item without signing for it. facility.QA.Give <ItemId> [Player]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Give")) && !Args.IsEmpty() ? GetPlayer(World, ArgInt(Args, 1, 0)) : nullptr;
		if (Player)
		{
			Player->GiveKey(FName(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQATeleport(
	TEXT("facility.QA.Teleport"),
	TEXT("QA, host: moves a player. facility.QA.Teleport <Player> X Y Z [Yaw]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Teleport")) && Args.Num() >= 4 ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr;
		if (!Player)
		{
			return;
		}

		const FRotator Facing(0.0f, ArgFloat(Args, 4, 0.0f), 0.0f);
		Player->TeleportTo(FVector(ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f), ArgFloat(Args, 3, 0.0f)), Facing);
		if (AController* Controller = Player->GetController())
		{
			Controller->SetControlRotation(Facing);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAMove(
	TEXT("facility.QA.Move"),
	TEXT("QA, host's own pawn only: walks toward a point for a while. facility.QA.Move <Player> X Y Seconds [Gait: 0 walk, 1 sprint, 2 sneak]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Move")) && Args.Num() >= 4 ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr;
		if (!Player || !Player->IsLocallyControlled())
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Move: needs a player the host controls"));
			return;
		}

		const FVector Target(ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f), 0.0f);
		const double EndTime = World->GetTimeSeconds() + ArgFloat(Args, 3, 1.0f);
		const bool bSprint = ArgInt(Args, 4, 0) == 1;
		const bool bSneak = ArgInt(Args, 4, 0) == 2;
		if (UFacilityMovementComponent* Movement = Player->GetFacilityMovement())
		{
			Movement->SetWantsToSprint(bSprint);
			Movement->SetWantsToSneak(bSneak);
		}

		// input has to be added every frame, like a held key
		TWeakObjectPtr<AFacilityCharacter> WeakPlayer = Player;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakPlayer, Target, EndTime](float)
		{
			AFacilityCharacter* Walker = WeakPlayer.Get();
			if (!Walker || !Walker->GetWorld())
			{
				return false;
			}

			const FVector ToTarget = (Target - Walker->GetActorLocation()) * FVector(1.0f, 1.0f, 0.0f);
			if (Walker->GetWorld()->GetTimeSeconds() >= EndTime || ToTarget.SizeSquared() < FMath::Square(40.0f) || !Walker->IsStanding())
			{
				if (UFacilityMovementComponent* Movement = Walker->GetFacilityMovement())
				{
					Movement->SetWantsToSprint(false);
					Movement->SetWantsToSneak(false);
				}
				return false;
			}

			Walker->AddMovementInput(ToTarget.GetSafeNormal());
			return true;
		}));
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQANoise(
	TEXT("facility.QA.Noise"),
	TEXT("QA, host: a sound the Tenant can hear. facility.QA.Noise X Y Z Radius"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (IsHost(World, TEXT("facility.QA.Noise")) && Args.Num() >= 4)
		{
			AFacilityTenant::ReportNoise(World, FVector(ArgFloat(Args, 0, 0.0f), ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f)), ArgFloat(Args, 3, 0.0f), nullptr);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQALock(
	TEXT("facility.QA.Lock"),
	TEXT("QA, host: player turns the master key in a door, if they could. facility.QA.Lock <DoorLabel> [Player]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Lock")) && !Args.IsEmpty() ? GetPlayer(World, ArgInt(Args, 1, 0)) : nullptr;
		AFacilityDoor* Door = Player ? FindActor<AFacilityDoor>(World, Args[0]) : nullptr;
		if (!Door)
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Lock: no player or no door called %s"), Args.IsEmpty() ? TEXT("") : *Args[0]);
			return;
		}
		if (!Door->CanToggleLock(Player))
		{
			UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Lock: %s can't lock or unlock %s now"), *Player->GetName(), *Door->GetName());
			return;
		}
		Door->ToggleLock(Player);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQADown(
	TEXT("facility.QA.Down"),
	TEXT("QA, host: downs a player, or helps them up. facility.QA.Down <Player> [1|0]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Down")) ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr)
		{
			Player->SetDowned(ArgInt(Args, 1, 1) != 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAExpect(
	TEXT("facility.QA.Expect"),
	TEXT("QA, host: logs PASS or FAIL. facility.QA.Expect Tenant|Marked <State> | Player <Index> Standing|Downed|Retained | Door <Label> Open|Closed|ClosedLocked|Broken | Run <State> | Power On|Off | Signed <Id> | Holds <Index> <Item> 1|0 | Near <Index> X Y Radius"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.Expect")) || Args.Num() < 2)
		{
			return;
		}

		const AFacilityGameState* GameState = World->GetGameState<AFacilityGameState>();
		const FString& What = Args[0];
		const FString Line = FString::Join(Args, TEXT(" "));

		if (What == TEXT("Tenant"))
		{
			TActorIterator<AFacilityTenant> It(World);
			Check(Line, Args[1], It ? StaticEnum<ETenantState>()->GetNameStringByValue((int64)It->GetTenantState()) : TEXT("Missing"));
		}
		else if (What == TEXT("Marked"))
		{
			TActorIterator<AFacilityMarked> It(World);
			Check(Line, Args[1], It ? StaticEnum<EMarkedState>()->GetNameStringByValue((int64)It->GetMarkedState()) : TEXT("Missing"));
		}
		else if (What == TEXT("Player") && Args.Num() >= 3)
		{
			Check(Line, Args[2], PlayerLife(GetPlayer(World, ArgInt(Args, 1, 0))));
		}
		else if (What == TEXT("Door") && Args.Num() >= 3)
		{
			Check(Line, Args[2], DoorState(FindActor<AFacilityDoor>(World, Args[1])));
		}
		else if (What == TEXT("Run") && GameState)
		{
			Check(Line, Args[1], StaticEnum<EFacilityRunState>()->GetNameStringByValue((int64)GameState->GetRunState()));
		}
		else if (What == TEXT("Power") && GameState)
		{
			Check(Line, Args[1], GameState->IsPowerRestored() ? TEXT("On") : TEXT("Off"));
		}
		else if (What == TEXT("Signed") && GameState)
		{
			const bool bSigned = GameState->GetLedger().ContainsByPredicate([&Args](const FFacilitySignature& Entry) { return Entry.SignatureId == FName(*Args[1]); });
			Check(Line, TEXT("1"), bSigned ? TEXT("1") : TEXT("0"));
		}
		else if (What == TEXT("Holds") && Args.Num() >= 4)
		{
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			Check(Line, Args[3], Player && Player->HasKey(FName(*Args[2])) ? TEXT("1") : TEXT("0"));
		}
		else if (What == TEXT("Near") && Args.Num() >= 5)
		{
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			const float Distance = Player ? FVector::Dist2D(Player->GetActorLocation(), FVector(ArgFloat(Args, 2, 0.0f), ArgFloat(Args, 3, 0.0f), 0.0f)) : -1.0f;
			Check(Line, TEXT("1"), Player && Distance <= ArgFloat(Args, 4, 0.0f) ? TEXT("1") : FString::Printf(TEXT("0 (%.0f cm away)"), Distance));
		}
		else
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.QA.Expect: don't know how to check \"%s\""), *Line);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAView(
	TEXT("facility.QA.View"),
	TEXT("QA, host: looks through a fixed camera, for screenshots. facility.QA.View X Y Z Pitch Yaw, or facility.QA.View off"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = IsHost(World, TEXT("facility.QA.View")) ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return;
		}

		static TWeakObjectPtr<ACameraActor> Camera;
		if (Args.Num() < 5)
		{
			PC->SetViewTarget(PC->GetPawn());
			if (Camera.IsValid())
			{
				Camera->Destroy();
			}
			return;
		}

		if (!Camera.IsValid() || Camera->GetWorld() != World)
		{
			Camera = World->SpawnActor<ACameraActor>();
		}
		Camera->SetActorLocationAndRotation(FVector(ArgFloat(Args, 0, 0.0f), ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f)), FRotator(ArgFloat(Args, 3, 0.0f), ArgFloat(Args, 4, 0.0f), 0.0f));
		PC->SetViewTarget(Camera.Get());
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQANavPath(
	TEXT("facility.QA.NavPath"),
	TEXT("QA, host: logs whether the navmesh has a path between two points. facility.QA.NavPath X1 Y1 X2 Y2"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.NavPath")) || Args.Num() < 4)
		{
			return;
		}

		const FVector Start(ArgFloat(Args, 0, 0.0f), ArgFloat(Args, 1, 0.0f), 100.0f);
		const FVector End(ArgFloat(Args, 2, 0.0f), ArgFloat(Args, 3, 0.0f), 100.0f);
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, End);
		const bool bFound = Path && Path->IsValid() && !Path->IsPartial();
		UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE navpath %s -> %s: %s, %d points, %.0f cm"), *Start.ToCompactString(), *End.ToCompactString(),
			bFound ? TEXT("found") : Path && Path->IsPartial() ? TEXT("PARTIAL") : TEXT("NONE"), Path ? Path->PathPoints.Num() : 0, Path ? Path->GetPathLength() : 0.0f);
		CVarLastResult->Set(bFound ? 1 : 0, ECVF_SetByCode);
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAProbe(
	TEXT("facility.QA.Probe"),
	TEXT("QA, host: facility.QA.Expect without the log line. The result is in facility.QA.LastResult, for scripts that wait on something"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		bQuiet = true;
		if (IConsoleObject* Expect = IConsoleManager::Get().FindConsoleObject(TEXT("facility.QA.Expect")))
		{
			Expect->AsCommand()->Execute(Args, World, *GLog);
		}
		bQuiet = false;
	}));

static FAutoConsoleCommandWithWorld CmdQADump(
	TEXT("facility.QA.Dump"),
	TEXT("QA, host: logs the state of the run, the players, the creatures and the doors"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.Dump")))
		{
			return;
		}

		if (const AFacilityGameState* GameState = World->GetGameState<AFacilityGameState>())
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE run %s power %s seed %d time %.1f signatures %d"),
				*StaticEnum<EFacilityRunState>()->GetNameStringByValue((int64)GameState->GetRunState()), GameState->IsPowerRestored() ? TEXT("on") : TEXT("off"),
				GameState->GetRunSeed(), GameState->GetRunTime(), GameState->GetLedger().Num());
		}

		int32 Index = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Index)
		{
			const AFacilityCharacter* Player = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
			FString Keys;
			if (Player)
			{
				for (const FName& Key : Player->GetHeldKeys())
				{
					Keys += Key.ToString() + TEXT(" ");
				}
			}
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE player %d %s %s at %s carrying [%s]"), Index, *GetNameSafe(Player), *PlayerLife(Player),
				Player ? *Player->GetActorLocation().ToCompactString() : TEXT("-"), *Keys.TrimEnd());
		}

		for (TActorIterator<AFacilityTenant> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE tenant %s at %s sated %d hidden %d"),
				*StaticEnum<ETenantState>()->GetNameStringByValue((int64)It->GetTenantState()), *It->GetActorLocation().ToCompactString(), It->IsSated(), It->IsHidden());
		}
		for (TActorIterator<AFacilityMarked> It(World); It; ++It)
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE marked %s at %s mark %s"),
				*StaticEnum<EMarkedState>()->GetNameStringByValue((int64)It->GetMarkedState()), *It->GetActorLocation().ToCompactString(), *GetNameSafe(It->GetMark()));
		}
		for (TActorIterator<AFacilityDoor> It(World); It; ++It)
		{
#if WITH_EDITOR
			const FString Label = It->GetActorLabel();
#else
			const FString Label = It->GetName();
#endif
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE door %s %s"), *Label, *DoorState(*It));
		}
	}));

#endif

// Copyright Epic Games, Inc. All Rights Reserved.

// QA console commands, not in shipping builds. They let a script (Saved/QA/harness) drive a run on the host
// through the same server paths players use, and check the outcome. Each check logs one line:
//   FACILITY_QA_CHECK PASS|FAIL <what> expected <x> got <y>
// Players are addressed by index in the host's controller list: 0 is the host.

#if !UE_BUILD_SHIPPING

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityContainer.h"
#include "Facility/FacilityExit.h"
#include "Facility/FacilityDirector.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityFuseBox.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityHidingSpot.h"
#include "Facility/FacilityInteractable.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityMovementComponent.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityTenant.h"
#include "Facility/FacilityThrown.h"
#include "Facility/FacilityClerk.h"
#include "Facility/FacilityGuest.h"
#include "Facility/FacilityResident.h"
#include "Facility/FacilityNight.h"
#include "Facility/FacilityShift.h"
#include "Facility/FacilityMarkedAIController.h"
#include "Facility/FacilityVoiceComponent.h"
#include "Camera/CameraActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	/** Attention remembered by facility.QA.MarkAttention */
	static int32 MarkedAttention = 0;

	/** Probes check without logging */
	static bool bQuiet = false;

	static void Check(const FString& What, const FString& Expected, const FString& Got)
	{
		// "2|3" accepts either
		TArray<FString> Accepted;
		Expected.ParseIntoArray(Accepted, TEXT("|"));
		const bool bPass = Expected.Equals(Got, ESearchCase::IgnoreCase) || Accepted.ContainsByPredicate([&Got](const FString& One) { return One.Equals(Got, ESearchCase::IgnoreCase); });
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
		const FName Item(*Args[0]);
		const auto TryTake = [World, Player, Item]()
		{
			for (TActorIterator<AFacilityPickup> It(World); It && Player; ++It)
			{
				if (It->GetGrantedKeyId() == Item && IFacilityInteractable::Execute_CanInteract(*It, Player))
				{
					UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Take: %s takes %s"), *Player->GetName(), *It->GetName());
					IFacilityInteractable::Execute_Interact(*It, Player);
					return true;
				}
			}
			return false;
		};
		if (TryTake())
		{
			return;
		}

		// on a night with containers the item is hidden in one: search it (at once, from wherever the player is), then take it
		for (TActorIterator<AFacilityContainer> It(World); It && Player; ++It)
		{
			if (!It->IsSearched() && It->GetContentKeyId() == Item)
			{
				UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Take: %s finds %s in %s"), *Player->GetName(), *Args[0], *It->GetName());
				It->CompleteSearchNow(Player);
				if (TryTake())
				{
					return;
				}
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
	TEXT("QA, host: moves a player. facility.QA.Teleport <Player> X Y Z [Yaw] [Pitch]"),
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
			// the look can tilt too (negative pitch looks down), so a screenshot can aim at a drawer
			Controller->SetControlRotation(FRotator(ArgFloat(Args, 5, 0.0f), Facing.Yaw, 0.0f));
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

static FAutoConsoleCommandWithWorldAndArgs CmdQAAttention(
	TEXT("facility.QA.Attention"),
	TEXT("QA, host: adds attention to the Facility director. facility.QA.Attention <Amount>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityDirector* Director = IsHost(World, TEXT("facility.QA.Attention")) ? AFacilityDirector::Get(World) : nullptr;
		if (Director && !Args.IsEmpty())
		{
			Director->AddAttention(ArgFloat(Args, 0, 0.0f), TEXT("QA"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAPlaceCreature(
	TEXT("facility.QA.PlaceCreature"),
	TEXT("QA, host: moves a creature, facing Yaw. facility.QA.PlaceCreature Marked|Tenant X Y Yaw"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!IsHost(World, TEXT("facility.QA.PlaceCreature")) || Args.Num() < 4)
		{
			return;
		}

		AActor* Creature = nullptr;
		if (Args[0] == TEXT("Marked"))
		{
			TActorIterator<AFacilityMarked> It(World);
			Creature = It ? *It : nullptr;
		}
		else if (Args[0] == TEXT("Clerk"))
		{
			TActorIterator<AFacilityClerk> It(World);
			Creature = It ? *It : nullptr;
		}
		else if (Args[0] == TEXT("Guest"))
		{
			TActorIterator<AFacilityGuest> It(World);
			Creature = It ? *It : nullptr;
		}
		else
		{
			TActorIterator<AFacilityTenant> It(World);
			Creature = It ? *It : nullptr;
		}
		if (Creature)
		{
			// a dormant creature has no collision: placed, it would fall through the floor, so it is pinned standing
			ACharacter* Body = Cast<ACharacter>(Creature);
			const bool bPinned = Body && !Creature->GetActorEnableCollision();
			const FVector Location(ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f), bPinned ? 125.0f : Creature->GetActorLocation().Z);
			Creature->TeleportTo(Location, FRotator(0.0f, ArgFloat(Args, 3, 0.0f), 0.0f), false, bPinned);
			if (bPinned)
			{
				Body->GetCharacterMovement()->StopMovementImmediately();
				Body->GetCharacterMovement()->DisableMovement();
			}
			if (APawn* Pawn = Cast<APawn>(Creature); Pawn && Pawn->GetController())
			{
				Pawn->GetController()->SetControlRotation(FRotator(0.0f, ArgFloat(Args, 3, 0.0f), 0.0f));
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAVoice(
	TEXT("facility.QA.Voice"),
	TEXT("QA, host: a TEST SIGNAL standing in for a microphone: the server treats the player as speaking at Amplitude (0..1). facility.QA.Voice <Player> <Amplitude> <Seconds>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Voice")) && Args.Num() >= 3 ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr;
		if (Player && Player->GetVoice())
		{
			UE_LOG(LogTheFacility, Display, TEXT("facility.QA.Voice: test signal for %s at %.2f for %.1f s (carries %.0f cm)"), *Player->GetName(), ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f), Player->GetVoice()->RadiusForAmplitude(ArgFloat(Args, 1, 0.0f)));
			Player->GetVoice()->ForceAmplitude(ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAVoiceState(
	TEXT("facility.QA.VoiceState"),
	TEXT("QA, host: sets a player's voice mode as if their client reported it. facility.QA.VoiceState <Player> <0 open|1 push-to-talk|2 off> <Microphone 0|1>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.VoiceState")) && Args.Num() >= 3 ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr;
		if (Player && Player->GetVoice())
		{
			Player->GetVoice()->ForceVoiceState(static_cast<EFacilityVoiceMode>(FMath::Clamp(ArgInt(Args, 1, 0), 0, 2)), ArgInt(Args, 2, 1) != 0);
		}
	}));

static FAutoConsoleCommandWithWorld CmdQAMarkAttention(
	TEXT("facility.QA.MarkAttention"),
	TEXT("QA, host: remembers the Facility's attention now, for Expect AttentionGainAtMost / AttentionGainAtLeast"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (const AFacilityDirector* Director = IsHost(World, TEXT("facility.QA.MarkAttention")) ? AFacilityDirector::Get(World) : nullptr)
		{
			MarkedAttention = Director->GetAttention();
			UE_LOG(LogTheFacility, Display, TEXT("facility.QA.MarkAttention: %d"), MarkedAttention);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQARetain(
	TEXT("facility.QA.Retain"),
	TEXT("QA, host: the Facility takes a player for the rest of the run. facility.QA.Retain <Player>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Retain")) ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr)
		{
			Player->Retain();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs CmdQAUnhide(
	TEXT("facility.QA.Unhide"),
	TEXT("QA, host: a hidden player asks to come out, as if they pressed interact. facility.QA.Unhide <Player>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFacilityCharacter* Player = IsHost(World, TEXT("facility.QA.Unhide")) ? GetPlayer(World, ArgInt(Args, 0, 0)) : nullptr;
		if (Player && Player->GetHidingSpot())
		{
			Player->GetHidingSpot()->RequestLeave(Player);
		}
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
		else if (What == TEXT("Altitude") && Args.Num() >= 4)
		{
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			const float Z = Player ? Player->GetActorLocation().Z : -1.0e9f;
			Check(Line, TEXT("1"), Z >= ArgFloat(Args, 2, 0.0f) && Z <= ArgFloat(Args, 3, 0.0f) ? TEXT("1") : FString::Printf(TEXT("0 (z %.0f)"), Z));
		}
		else if ((What == TEXT("VoiceHeard") || What == TEXT("Breathing")) && Args.Num() >= 3)
		{
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			const UFacilityVoiceComponent* Voice = Player ? Player->GetVoice() : nullptr;
			const int32 Count = !Voice ? -1 : What == TEXT("VoiceHeard") ? Voice->GetNumVoiceReports() : Voice->GetNumBreaths();
			Check(Line, Args[2], Count > 0 ? TEXT("1") : FString::Printf(TEXT("0 (%d)"), Count));
		}
		else if ((What == TEXT("AttentionGainAtMost") || What == TEXT("AttentionGainAtLeast")) && Args.Num() >= 2)
		{
			const AFacilityDirector* Director = AFacilityDirector::Get(World);
			const int32 Gain = Director ? Director->GetAttention() - MarkedAttention : -999;
			const bool bOk = Director && (What == TEXT("AttentionGainAtMost") ? Gain <= ArgInt(Args, 1, 0) : Gain >= ArgInt(Args, 1, 0));
			Check(Line, TEXT("1"), bOk ? TEXT("1") : FString::Printf(TEXT("0 (gained %d)"), Gain));
		}
		else if (What == TEXT("AttentionAtMost") && Args.Num() >= 2)
		{
			const AFacilityDirector* Director = AFacilityDirector::Get(World);
			const int32 Attention = Director ? Director->GetAttention() : -1;
			Check(Line, TEXT("1"), Director && Attention <= ArgInt(Args, 1, 0) ? TEXT("1") : FString::Printf(TEXT("0 (%d)"), Attention));
		}
		else if (What == TEXT("NavPath") && Args.Num() >= 5)
		{
			const FVector Start(ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f), 100.0f);
			const FVector End(ArgFloat(Args, 3, 0.0f), ArgFloat(Args, 4, 0.0f), 100.0f);
			const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, End);
			const bool bFound = Path && Path->IsValid() && !Path->IsPartial();
			Check(Line, TEXT("1"), bFound ? TEXT("1") : TEXT("0"));
			if (!bQuiet)
			{
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE path length %.0f cm"), Path ? Path->GetPathLength() : -1.0f);
			}
		}
		else if (What == TEXT("Hiding") && Args.Num() >= 3)
		{
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			Check(Line, Args[2], Player && Player->IsHiding() ? TEXT("1") : TEXT("0"));
		}
		else if (What == TEXT("Director"))
		{
			const AFacilityDirector* Director = AFacilityDirector::Get(World);
			Check(Line, Args[1], Director ? FString::FromInt(Director->GetTier()) : TEXT("Missing"));
		}
		else if (What == TEXT("Fuses"))
		{
			TActorIterator<AFacilityFuseBox> Box(World);
			Check(Line, Args[1], Box ? FString::FromInt(Box->GetFusesFitted()) : TEXT("Missing"));
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
		else if (What == TEXT("Gate"))
		{
			// Chained, Unchained (the chain is off, or there never was one) or Open
			const AFacilityExit* Exit = FindActor<AFacilityExit>(World, TEXT("Exit"));
			const FString State = !Exit ? TEXT("Missing") : Exit->IsOpen() ? TEXT("Open") : Exit->IsChainedShut() ? TEXT("Chained") : TEXT("Unchained");
			Check(Line, Args[1], State);
		}
		else if (What == TEXT("Searched") && Args.Num() >= 3)
		{
			const AFacilityContainer* Container = FindActor<AFacilityContainer>(World, Args[1]);
			Check(Line, Args[2], Container ? (Container->IsSearched() ? TEXT("1") : TEXT("0")) : TEXT("Missing"));
		}
		else if (What == TEXT("SearchedCount"))
		{
			int32 Count = 0;
			for (TActorIterator<AFacilityContainer> It(World); It; ++It)
			{
				Count += It->IsSearched() ? 1 : 0;
			}
			Check(Line, Args[1], FString::FromInt(Count));
		}
		else if (What == TEXT("Containers"))
		{
			int32 Count = 0;
			for (TActorIterator<AFacilityContainer> It(World); It; ++It)
			{
				++Count;
			}
			Check(Line, Args[1], FString::FromInt(Count));
		}
		else if (What == TEXT("LockedRooms") || What == TEXT("BrokenRooms"))
		{
			// rooms (door lock groups) with a door still locked by the night, or with a door broken in
			TSet<FName> Rooms;
			for (TActorIterator<AFacilityDoor> It(World); It; ++It)
			{
				const bool bCounts = What == TEXT("LockedRooms") ? It->IsLocked() && !It->IsHeldShut() && !It->IsBroken() : It->IsBroken();
				if (bCounts && !It->GetLockGroup().IsNone())
				{
					Rooms.Add(It->GetLockGroup());
				}
			}
			Check(Line, Args[1], FString::FromInt(Rooms.Num()));
		}
		else if (What == TEXT("Found") && Args.Num() >= 3)
		{
			// Yes: lying out after a search, or carried. Hidden: still in a container. NotTonight: the night didn't hide one
			const FName Item(*Args[1]);
			FString State = TEXT("NotTonight");
			for (TActorIterator<AFacilityContainer> It(World); It; ++It)
			{
				if (It->GetContentKeyId() == Item)
				{
					State = TEXT("Hidden");
				}
			}
			for (TActorIterator<AFacilityPickup> It(World); It; ++It)
			{
				if (It->GetGrantedKeyId() == Item)
				{
					State = TEXT("Yes");
				}
			}
			for (TActorIterator<AFacilityCharacter> It(World); It; ++It)
			{
				if (It->HasKey(Item))
				{
					State = TEXT("Yes");
				}
			}
			// "Yes|NotTonight" accepts either
			TArray<FString> Accepted;
			Args[2].ParseIntoArray(Accepted, TEXT("|"));
			Check(Line, Accepted.Contains(State) ? State : Args[2], State);
		}
		else if (What == TEXT("PlayerCount"))
		{
			int32 Count = 0;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				Count += It->Get() && Cast<AFacilityCharacter>(It->Get()->GetPawn()) ? 1 : 0;
			}
			Check(Line, Args[1], FString::FromInt(Count));
		}
		else if (What == TEXT("Night"))
		{
			Check(Line, Args[1], GameState ? FString::FromInt(GameState->GetNight()) : TEXT("Missing"));
		}
		else if (What == TEXT("Clerk") || What == TEXT("Guest"))
		{
			FString Got = TEXT("Missing");
			if (What == TEXT("Clerk"))
			{
				TActorIterator<AFacilityClerk> It(World);
				Got = It ? StaticEnum<EClerkState>()->GetNameStringByValue((int64)It->GetClerkState()) : Got;
			}
			else
			{
				TActorIterator<AFacilityGuest> It(World);
				Got = It ? StaticEnum<EGuestState>()->GetNameStringByValue((int64)It->GetGuestState()) : Got;
			}
			Check(Line, Args[1], Got);
		}
		else if (What == TEXT("CreatureNear") && Args.Num() >= 5)
		{
			const AActor* Creature = nullptr;
			if (Args[1] == TEXT("Clerk")) { TActorIterator<AFacilityClerk> It(World); Creature = It ? *It : nullptr; }
			else if (Args[1] == TEXT("Guest")) { TActorIterator<AFacilityGuest> It(World); Creature = It ? *It : nullptr; }
			else if (Args[1] == TEXT("Marked")) { TActorIterator<AFacilityMarked> It(World); Creature = It ? *It : nullptr; }
			else { TActorIterator<AFacilityTenant> It(World); Creature = It ? *It : nullptr; }
			const float Distance = Creature ? FVector::Dist2D(Creature->GetActorLocation(), FVector(ArgFloat(Args, 2, 0.0f), ArgFloat(Args, 3, 0.0f), 0.0f)) : -1.0f;
			Check(Line, TEXT("1"), Creature && Distance <= ArgFloat(Args, 4, 0.0f) ? TEXT("1") : FString::Printf(TEXT("0 (%.0f cm)"), Distance));
		}
		else if (What == TEXT("GuestFrozen"))
		{
			TActorIterator<AFacilityGuest> It(World);
			Check(Line, Args[1], It ? (It->IsFrozen() ? TEXT("1") : TEXT("0")) : TEXT("Missing"));
		}
		else if (What == TEXT("Awake") || What == TEXT("Offerings") || What == TEXT("Bound"))
		{
			// tonight's plan, worked out from the seed the way every machine does
			const FacilityNight::FSite Site = FacilityNight::GatherSite(World);
			const FacilityNight::FPlan Plan = FacilityNight::Plan(GameState ? GameState->GetRunSeed() : 0, Site, GameState ? GameState->GetNight() : 1);
			// "2|3" accepts either
			const auto CheckAny = [&Line](const FString& Expected, const FString& Got)
			{
				TArray<FString> Accepted;
				Expected.ParseIntoArray(Accepted, TEXT("|"));
				Check(Line, Accepted.Contains(Got) ? Got : Expected, Got);
			};
			if (What == TEXT("Awake"))
			{
				CheckAny(Args[1], FString::FromInt(Plan.Creatures.Num()));
			}
			else if (What == TEXT("Offerings"))
			{
				CheckAny(Args[1], FString::FromInt(Plan.GetOfferings().Num()));
			}
			else if (Args.Num() >= 3)
			{
				Check(Line, Args[2], FacilityShift::CreatureName(Plan.BoundTo(FName(*Args[1]))).Replace(TEXT(" "), TEXT("")));
			}
		}
		else if (What == TEXT("Residents") || What == TEXT("ResidentsReacted"))
		{
			int32 Count = 0;
			for (TActorIterator<AFacilityResident> It(World); It; ++It)
			{
				Count += What == TEXT("Residents") ? (It->IsShown() ? 1 : 0) : (It->HasReacted() ? 1 : 0);
			}
			Check(Line, Args[1], FString::FromInt(Count));
		}
		else if (What == TEXT("Treasures"))
		{
			int32 Count = 0;
			for (TActorIterator<AFacilityPickup> It(World); It; ++It)
			{
				Count += FacilityShift::IsTreasure(It->GetGrantedKeyId()) ? 1 : 0;
			}
			Check(Line, Args[1], FString::FromInt(Count));
		}
		else if (What == TEXT("Score") || What == TEXT("Records") || What == TEXT("ShiftOver") || What == TEXT("NextNight"))
		{
			FString Got = TEXT("Missing");
			if (GameState)
			{
				const TArray<FFacilityNightRecord>& Records = GameState->GetShiftRecords();
				Got = What == TEXT("Records") ? FString::FromInt(Records.Num())
					: What == TEXT("ShiftOver") ? FString(GameState->IsShiftOver() ? TEXT("1") : TEXT("0"))
					: What == TEXT("NextNight") ? FString(GameState->GetNextNightTime() > 0.0 ? TEXT("1") : TEXT("0"))
					: Records.IsEmpty() ? TEXT("none") : FString::FromInt(Records.Last().Score);
			}
			Check(Line, Args[1], Got);
		}
		else if (What == TEXT("MarkedSees") && Args.Num() >= 3)
		{
			// by the same test its hunt uses: range (shorter in the dark), cover for a sneaking player, line of sight
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			TActorIterator<AFacilityMarked> It(World);
			const AFacilityMarkedAIController* Brain = It ? Cast<AFacilityMarkedAIController>(It->GetController()) : nullptr;
			const FString Seen = !Player || !Brain ? TEXT("Missing") : Brain->CanSeeForQA(Player) ? TEXT("1") : TEXT("0");
			Check(Line, Args[2], Seen);
			if (Brain && Player && !bQuiet)
			{
				UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE marked sight: %s"), *Brain->DescribeSightForQA(Player));
			}
		}
		else if (What == TEXT("TenantNear") && Args.Num() >= 4)
		{
			TActorIterator<AFacilityTenant> It(World);
			const float Distance = It ? FVector::Dist2D(It->GetActorLocation(), FVector(ArgFloat(Args, 1, 0.0f), ArgFloat(Args, 2, 0.0f), 0.0f)) : -1.0f;
			Check(Line, TEXT("1"), It && Distance <= ArgFloat(Args, 3, 0.0f) ? TEXT("1") : FString::Printf(TEXT("0 (%.0f cm)"), Distance));
		}
		else if (What == TEXT("Footstep") && Args.Num() >= 3)
		{
			// which footstep this machine last played for that player
			const AFacilityCharacter* Player = GetPlayer(World, ArgInt(Args, 1, 0));
			Check(Line, Args[2], Player ? Player->GetLastFootstep().ToString() : TEXT("Missing"));
		}
		else if (What == TEXT("Throws"))
		{
			Check(Line, Args[1], FString::FromInt(AFacilityThrown::GetBreakCount()));
		}
		else if (What == TEXT("Clues"))
		{
			// containers giving off a fuse's hum or the key's jingle on this machine
			int32 Count = 0;
			for (TActorIterator<AFacilityContainer> It(World); It; ++It)
			{
				Count += It->IsGivingClue() ? 1 : 0;
			}
			Check(Line, Args[1], FString::FromInt(Count));
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
		if (const AFacilityDirector* Director = AFacilityDirector::Get(World))
		{
			UE_LOG(LogTheFacility, Display, TEXT("FACILITY_QA_STATE director attention %d tier %d"), Director->GetAttention(), Director->GetTier());
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

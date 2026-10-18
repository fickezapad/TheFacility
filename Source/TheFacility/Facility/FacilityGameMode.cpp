// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGameMode.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityExit.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityHUD.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityTenant.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "TheFacility.h"

static TAutoConsoleVariable<int32> CVarFacilitySeed(
	TEXT("facility.Seed"),
	0,
	TEXT("Seed for the next run's placement. 0 = random. A ?Seed= URL option takes precedence"));

static FAutoConsoleCommandWithWorld CmdFacilityRestart(
	TEXT("facility.Restart"),
	TEXT("Host only: starts the run over on the same map"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (AFacilityGameMode* GameMode = World ? World->GetAuthGameMode<AFacilityGameMode>() : nullptr)
		{
			GameMode->RestartRun();
		}
		else
		{
			UE_LOG(LogTheFacility, Log, TEXT("facility.Restart: run it on the host, in a level using a FacilityGameMode"));
		}
	}));

AFacilityGameMode::AFacilityGameMode()
{
	GameStateClass = AFacilityGameState::StaticClass();
	HUDClass = AFacilityHUD::StaticClass();
}

void AFacilityGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	RunSeed = UGameplayStatics::GetIntOption(Options, TEXT("Seed"), 0);
	if (RunSeed == 0)
	{
		RunSeed = CVarFacilitySeed.GetValueOnGameThread();
	}
	if (RunSeed == 0)
	{
		RunSeed = FMath::RandRange(1, 999999);
	}
}

void AFacilityGameMode::StartPlay()
{
	Super::StartPlay();

	if (AFacilityGameState* FacilityGameState = GetGameState<AFacilityGameState>())
	{
		FacilityGameState->StartRun(RunSeed);
	}

	ApplySeededPlacement();

	GetWorldTimerManager().SetTimer(EvaluateTimer, this, &AFacilityGameMode::EvaluateRun, EvaluateInterval, true);
}

FString AFacilityGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	// the menu already limits it to letters, digits, - and _; trimmed again because a client can send anything
	FString PlayerName = UGameplayStatics::ParseOption(Options, TEXT("PlayerName")).Left(20);
	PlayerName.ReplaceInline(TEXT(" "), TEXT("_"));
	if (!PlayerName.IsEmpty())
	{
		ChangeName(NewPlayerController, PlayerName, false);
	}
	return Result;
}

void AFacilityGameMode::ApplySeededPlacement()
{
	TMap<FName, TArray<AFacilityPickup*>> Groups;
	for (TActorIterator<AFacilityPickup> It(GetWorld()); It; ++It)
	{
		if (!It->GetPlacementGroup().IsNone())
		{
			Groups.FindOrAdd(It->GetPlacementGroup()).Add(*It);
		}
	}

	if (Groups.IsEmpty())
	{
		return;
	}

	// sorted by name so the same seed always picks the same spots
	FRandomStream Stream(RunSeed);
	Groups.KeySort(FNameLexicalLess());
	for (TPair<FName, TArray<AFacilityPickup*>>& Group : Groups)
	{
		Group.Value.Sort([](const AFacilityPickup& A, const AFacilityPickup& B) { return A.GetName() < B.GetName(); });
		const int32 Keep = Stream.RandRange(0, Group.Value.Num() - 1);

		for (int32 Index = 0; Index < Group.Value.Num(); ++Index)
		{
			if (Index != Keep)
			{
				Group.Value[Index]->Destroy();
			}
		}

		UE_LOG(LogTheFacility, Log, TEXT("Run seed %d: %s placed at %s (%d of %d)"), RunSeed, *Group.Key.ToString(),
			*Group.Value[Keep]->GetActorLocation().ToCompactString(), Keep + 1, Group.Value.Num());
	}
}

void AFacilityGameMode::EvaluateRun()
{
	AFacilityGameState* FacilityGameState = GetGameState<AFacilityGameState>();
	if (!FacilityGameState || !FacilityGameState->IsRunInProgress() || bRestarting)
	{
		return;
	}

	TArray<AFacilityCharacter*> Players;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFacilityCharacter* Character = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr)
		{
			Players.Add(Character);
		}
	}

	AFacilityExit* Exit = nullptr;
	for (TActorIterator<AFacilityExit> It(GetWorld()); It; ++It)
	{
		Exit = *It;
		break;
	}

	// maps without a way out are test maps: they never end
	if (Players.IsEmpty() || !Exit)
	{
		return;
	}

	const auto NameOf = [FacilityGameState](const AFacilityCharacter* Character)
	{
		return FacilityGameState->MakeSignerName(Character->GetPlayerState());
	};

	// lost: nobody is left in the run
	if (!Players.ContainsByPredicate([](const AFacilityCharacter* Character) { return !Character->IsRetained(); }))
	{
		TArray<FString> LeftBehind;
		for (const AFacilityCharacter* Character : Players)
		{
			LeftBehind.Add(NameOf(Character));
		}
		FinishRun(EFacilityRunState::Retained, {}, LeftBehind);
		return;
	}

	// escaped: the gate is open and everyone still standing is through it, for a moment
	bool bAllStandingOut = FacilityGameState->IsPowerRestored();
	int32 NumStanding = 0;
	for (const AFacilityCharacter* Character : Players)
	{
		if (Character->IsStanding())
		{
			++NumStanding;
			bAllStandingOut &= Exit->IsInExitZone(Character->GetActorLocation());
		}
	}

	if (!bAllStandingOut || NumStanding == 0)
	{
		EscapeHeldFor = 0.0f;
		return;
	}

	EscapeHeldFor += EvaluateInterval;
	if (EscapeHeldFor < EscapeHoldTime)
	{
		return;
	}

	TArray<FString> Escaped, LeftBehind;
	for (const AFacilityCharacter* Character : Players)
	{
		(Character->IsStanding() ? Escaped : LeftBehind).Add(NameOf(Character));
	}
	FinishRun(EFacilityRunState::Escaped, Escaped, LeftBehind);
}

void AFacilityGameMode::FinishRun(EFacilityRunState Outcome, const TArray<FString>& Escaped, const TArray<FString>& LeftBehind)
{
	AFacilityGameState* FacilityGameState = GetGameState<AFacilityGameState>();
	if (!FacilityGameState)
	{
		return;
	}

	FacilityGameState->EndRun(Outcome, Escaped, LeftBehind);

	for (TActorIterator<AFacilityMarked> It(GetWorld()); It; ++It)
	{
		It->ShutDown();
	}
	for (TActorIterator<AFacilityTenant> It(GetWorld()); It; ++It)
	{
		It->ShutDown();
	}
}

void AFacilityGameMode::RestartRun()
{
	if (bRestarting)
	{
		return;
	}

	bRestarting = true;
	GetWorldTimerManager().ClearTimer(EvaluateTimer);
	UE_LOG(LogTheFacility, Log, TEXT("Run: restarting the night"));

	// reloads the current map for the host and every client; the new run rolls a new seed
	GetWorld()->ServerTravel(TEXT("?restart"));
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TheFacilityGameMode.h"
#include "Facility/FacilityGameState.h"
#include "FacilityGameMode.generated.h"

/**
 *  Game mode for Facility runs.
 *  Uses the Facility game state and debug HUD; pawn and controller are set in the Blueprint subclass.
 *
 *  Runs the night on the server: seeded placement at the start, then a regular check of how the run stands:
 *  everyone standing through the open gate is an escape; nobody left in the run is a loss.
 *  Maps without a FacilityExit simply never end, so the M1 and M2 test maps behave as before.
 */
UCLASS()
class THEFACILITY_API AFacilityGameMode : public ATheFacilityGameMode
{
	GENERATED_BODY()

public:

	AFacilityGameMode();

	/** Server: starts the night over on the same map, for everyone */
	void RestartRun();

protected:

	/** Everyone standing must stay inside the exit zone this long for the escape to count */
	UPROPERTY(EditAnywhere, Category="Run", meta=(ClampMin=0, Units="s"))
	float EscapeHoldTime = 1.5f;

	/** Seed for this run: from the ?Seed= URL option, else facility.Seed, else random */
	int32 RunSeed = 0;

	float EscapeHeldFor = 0.0f;

	bool bRestarting = false;

	FTimerHandle EvaluateTimer;

	static constexpr float EvaluateInterval = 0.25f;

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	virtual void StartPlay() override;

	/** Names players from the PlayerName URL option the menu sends. The engine's own Name option is overwritten by the platform nickname */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	/** Keeps one pickup per placement group, chosen by the run seed, and removes the others */
	void ApplySeededPlacement();

	/** Checks for an escape or a loss */
	void EvaluateRun();

	/** Ends the run on the server: records the outcome and withdraws the creatures */
	void FinishRun(EFacilityRunState Outcome, const TArray<FString>& Escaped, const TArray<FString>& LeftBehind);
};

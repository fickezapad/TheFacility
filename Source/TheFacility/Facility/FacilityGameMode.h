// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TheFacilityGameMode.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityShift.h"
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

	/** Server: starts a new shift on the same map, for everyone. bSameNight replays this shift's seed from its first night */
	void RestartRun(bool bSameNight = false);

	/** Server: on to the next night of the shift (after an escape), for everyone */
	void AdvanceNight();

	int32 GetNight() const { return Night; }

protected:

	/** Everyone standing must stay inside the exit zone this long for the escape to count */
	UPROPERTY(EditAnywhere, Category="Run", meta=(ClampMin=0, Units="s"))
	float EscapeHoldTime = 1.5f;

	/** Seed for this night: the shift's night seed */
	int32 RunSeed = 0;

	/** The shift's seed (from "another night", the ?Seed= URL option, facility.Seed, or chance) and this night of it */
	int32 ShiftSeed = 0;
	int32 Night = 1;

	/** Seconds between an escape and the next night of the shift */
	UPROPERTY(EditAnywhere, Category="Run", meta=(ClampMin=1, Units="s"))
	float NextNightDelay = 12.0f;

	/** The creatures awake tonight, for the record */
	TArray<EFacilityCreature> NightCreatures;

	FTimerHandle NextNightTimer;

	/** Points for the night: everyone who got out, plus every Offering whose (last) signer got out */
	FFacilityNightRecord MakeRecord(EFacilityRunState Outcome, const TArray<FString>& Escaped) const;

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

	/** On maps with containers: plans this night from the seed (locked rooms, creature starts, hidden items) and applies it */
	void ApplyNightPlan();

	/** Checks for an escape or a loss */
	void EvaluateRun();

	/** Ends the run on the server: records the outcome and withdraws the creatures */
	void FinishRun(EFacilityRunState Outcome, const TArray<FString>& Escaped, const TArray<FString>& LeftBehind);
};

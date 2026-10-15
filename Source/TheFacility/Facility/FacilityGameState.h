// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FacilityGameState.generated.h"

class APlayerState;

/** One transgression recorded by the Facility */
USTRUCT(BlueprintType)
struct FFacilitySignature
{
	GENERATED_BODY()

	/** What was signed, e.g. MusicBox. Later used by the Director to decide which threats become eligible */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	FName SignatureId;

	/** Area of the map the signature happened in, used by environmental responses */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	FName ZoneId;

	/** Player who signed */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	TObjectPtr<APlayerState> Signer = nullptr;

	/** Readable signer label fixed at signing time, unique among the players present then. Survives the signer leaving */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	FString SignerName;

	/** Server world time when it was signed */
	UPROPERTY(BlueprintReadOnly, Category="Facility")
	double ServerTime = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFacilitySignatureDelegate, const FFacilitySignature&, Signature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFacilityPowerDelegate);

/** How the run stands. Decided by the server only */
UENUM(BlueprintType)
enum class EFacilityRunState : uint8
{
	InProgress,
	/** Everyone still standing reached the gate. Downed or retained players were left behind */
	Escaped,
	/** Nobody is left standing: the Facility kept the whole crew */
	Retained
};

/**
 *  Game state for Facility runs.
 *  Holds the Facility ledger (every signature made this run) and the run itself: power, outcome, seed and time.
 *  The server is the only writer; every machine reads it and reacts.
 */
UCLASS()
class THEFACILITY_API AFacilityGameState : public AGameStateBase
{
	GENERATED_BODY()

public:

	/** Server only. Adds a signature to the ledger */
	void RecordSignature(FName SignatureId, FName ZoneId, APlayerState* Signer);

	UFUNCTION(BlueprintPure, Category="Facility")
	const TArray<FFacilitySignature>& GetLedger() const { return Ledger; }

	/** Returns true if anything has been signed in the given zone */
	UFUNCTION(BlueprintPure, Category="Facility")
	bool HasSignatureInZone(FName ZoneId) const;

	/** Called on every machine once per new ledger entry */
	UPROPERTY(BlueprintAssignable, Category="Facility")
	FFacilitySignatureDelegate OnSignatureRecorded;

	/** Called on every machine when the motel's power comes back */
	UPROPERTY(BlueprintAssignable, Category="Facility")
	FFacilityPowerDelegate OnPowerRestored;

	UFUNCTION(BlueprintPure, Category="Facility|Run")
	bool IsPowerRestored() const { return bPowerRestored; }

	UFUNCTION(BlueprintPure, Category="Facility|Run")
	EFacilityRunState GetRunState() const { return RunState; }

	UFUNCTION(BlueprintPure, Category="Facility|Run")
	bool IsRunInProgress() const { return RunState == EFacilityRunState::InProgress; }

	/** Seed the server used for this run's placement. Same seed, same layout */
	UFUNCTION(BlueprintPure, Category="Facility|Run")
	int32 GetRunSeed() const { return RunSeed; }

	/** Seconds since the run started, frozen when it ends */
	UFUNCTION(BlueprintPure, Category="Facility|Run")
	float GetRunTime() const;

	/** Server world time the run started */
	double GetRunStartTime() const { return RunStartTime; }

	const TArray<FString>& GetEscapedNames() const { return EscapedNames; }
	const TArray<FString>& GetLeftBehindNames() const { return LeftBehindNames; }
	const FString& GetPowerRestoredBy() const { return PowerRestoredBy; }

	/** Server only */
	void StartRun(int32 Seed);

	/** Server only. Turns the power back on, once */
	void RestorePower(APlayerState* By);

	/** Server only. Ends the run with an outcome, once */
	void EndRun(EFacilityRunState Outcome, const TArray<FString>& Escaped, const TArray<FString>& LeftBehind);

	/** Readable, unique player label, as used in the ledger */
	FString MakeSignerName(const APlayerState* Signer) const;

protected:

	UPROPERTY(ReplicatedUsing=OnRep_Ledger)
	TArray<FFacilitySignature> Ledger;

	UPROPERTY(ReplicatedUsing=OnRep_PowerRestored)
	bool bPowerRestored = false;

	UPROPERTY(Replicated)
	FString PowerRestoredBy;

	UPROPERTY(Replicated)
	EFacilityRunState RunState = EFacilityRunState::InProgress;

	UPROPERTY(Replicated)
	int32 RunSeed = 0;

	/** Server world time the run started and ended */
	UPROPERTY(Replicated)
	double RunStartTime = 0.0;

	UPROPERTY(Replicated)
	double RunEndTime = 0.0;

	UPROPERTY(Replicated)
	TArray<FString> EscapedNames;

	UPROPERTY(Replicated)
	TArray<FString> LeftBehindNames;

	/** True once OnPowerRestored has fired on this machine */
	bool bPowerBroadcast = false;

	UFUNCTION()
	void OnRep_PowerRestored();

	void BroadcastPower();

	/** How many entries have already been broadcast on this machine */
	int32 NumBroadcast = 0;

	UFUNCTION()
	void OnRep_Ledger();

	void BroadcastNewEntries();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

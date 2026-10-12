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

/**
 *  Game state for Facility runs.
 *  Holds the Facility ledger: every signature made this run.
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

protected:

	UPROPERTY(ReplicatedUsing=OnRep_Ledger)
	TArray<FFacilitySignature> Ledger;

	/** Player name, made unique with the player id when another player has the same name (e.g. two PIE windows on one PC) */
	FString MakeSignerName(const APlayerState* Signer) const;

	/** How many entries have already been broadcast on this machine */
	int32 NumBroadcast = 0;

	UFUNCTION()
	void OnRep_Ledger();

	void BroadcastNewEntries();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityZoneLights.generated.h"

class AFacilityGameState;
class USoundBase;
struct FFacilitySignature;

/**
 *  The Facility's first response: when anything is signed in this zone, its lights flicker and die.
 *  Not replicated itself. Every machine derives the power state from the replicated ledger,
 *  so clients and late joiners always agree with the server.
 */
UCLASS()
class THEFACILITY_API AFacilityZoneLights : public AActor
{
	GENERATED_BODY()

protected:

	/** Zone this actor powers. Matches ZoneId on Offerings */
	UPROPERTY(EditAnywhere, Category="Zone")
	FName ZoneId;

	/** Light actors in this zone. All light components on them are switched */
	UPROPERTY(EditInstanceOnly, Category="Zone")
	TArray<TObjectPtr<AActor>> LightActors;

	/** Played at this actor's location when the zone loses power */
	UPROPERTY(EditAnywhere, Category="Zone")
	TObjectPtr<USoundBase> ChimeSound;

	/** How long the lights flicker before dying */
	UPROPERTY(EditAnywhere, Category="Zone", meta=(ClampMin=0, Units="s"))
	float FlickerDuration = 1.5f;

	/** Signatures older than this when they reach a machine (e.g. on late join) skip the flicker and chime */
	UPROPERTY(EditAnywhere, Category="Zone", meta=(ClampMin=0, Units="s"))
	float FreshSignatureWindow = 3.0f;

	bool bPowered = true;

	float FlickerTimeLeft = 0.0f;

	FTimerHandle BindTimer;

	/** Game state lookups made so far while waiting for it to replicate */
	int32 BindAttempts = 0;

	/** Give up waiting for the game state after this many lookups */
	static constexpr int32 MaxBindAttempts = 50;

	static constexpr float BindRetryInterval = 0.2f;

	FTimerHandle FlickerTimer;

public:

	AFacilityZoneLights();

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Binds to the game state, retrying until it has replicated to this machine */
	void TryBindToGameState();

	UFUNCTION()
	void HandleSignatureRecorded(const FFacilitySignature& Signature);

	void FlickerStep();

	void SetLightsVisible(bool bVisible);

	void CutPower(bool bAnimate);
};

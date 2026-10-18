// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityFlicker.generated.h"

/**
 *  Makes failing things fail: a motel sign, a buzzing lamp. Every so often the targets stutter off and on,
 *  and now and then they go out for a few seconds.
 *
 *  Purely cosmetic and local: each machine flickers on its own, nothing is replicated and nothing in gameplay reads it.
 *  It switches the visibility of every light and text component on its target actors.
 */
UCLASS()
class THEFACILITY_API AFacilityFlicker : public AActor
{
	GENERATED_BODY()

protected:

	/** Actors whose lights and text flicker */
	UPROPERTY(EditInstanceOnly, Category="Flicker")
	TArray<TObjectPtr<AActor>> Targets;

	/** Seconds between bursts, at random within this range */
	UPROPERTY(EditAnywhere, Category="Flicker", meta=(ClampMin=0.1, Units="s"))
	float MinCalm = 2.0f;

	UPROPERTY(EditAnywhere, Category="Flicker", meta=(ClampMin=0.1, Units="s"))
	float MaxCalm = 7.0f;

	/** Chance that a burst ends in a longer outage */
	UPROPERTY(EditAnywhere, Category="Flicker", meta=(ClampMin=0, ClampMax=1))
	float OutageChance = 0.15f;

	UPROPERTY(EditAnywhere, Category="Flicker", meta=(ClampMin=0, Units="s"))
	float OutageTime = 3.0f;

	/** Stays dark until the motel's power is restored (for lamps on the main circuit) */
	UPROPERTY(EditAnywhere, Category="Flicker")
	bool bNeedsMainPower = false;

	float TimeToNext = 0.0f;
	int32 StuttersLeft = 0;
	bool bLit = true;

public:

	AFacilityFlicker();

protected:

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	void SetLit(bool bInLit);

	bool HasPower() const;
};

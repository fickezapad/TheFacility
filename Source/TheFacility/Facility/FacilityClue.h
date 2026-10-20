// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FacilityClue.generated.h"

class UAudioComponent;
class UPointLightComponent;
class USoundBase;

/**
 *  How a hidden item gives itself away to someone close by, so finding it is following a clue rather than opening
 *  everything: a fuse hums and now and then sparks, the gate key jingles. Heard within about 6 m, through walls muffled.
 *
 *  Purely cosmetic and local: the owner (a container, or the item lying out) decides the item on the server and
 *  replicates it; every machine then plays the clue itself. Nothing on a dedicated server.
 */
UCLASS(ClassGroup=(Facility))
class THEFACILITY_API UFacilityClueComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	UFacilityClueComponent();

	/** The item whose clue to give off: Fuse or GateKey; anything else (or None) is silent */
	void SetItem(FName ItemId);

	FName GetItem() const { return Item; }

	/** QA: true if this machine is giving off a clue right now */
	bool IsActive() const { return !Item.IsNone() && bActive; }

protected:

	/** How far the clue carries */
	UPROPERTY(EditAnywhere, Category="Clue", meta=(Units="cm"))
	float Range = 600.0f;

	UPROPERTY()
	TObjectPtr<USoundBase> HumSound;

	UPROPERTY()
	TObjectPtr<USoundBase> SparkSound;

	UPROPERTY()
	TObjectPtr<USoundBase> JingleSound;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Hum;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> SparkLight;

	FName Item;

	bool bActive = false;

	FTimerHandle ClueTimer;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** A spark or a jingle, then the next one a few seconds later */
	void Pulse();

	void Stop();
};

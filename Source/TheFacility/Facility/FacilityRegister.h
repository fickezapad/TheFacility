// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityRegister.generated.h"

class UStaticMeshComponent;

/**
 *  The register open on the office counter. Looking at it (aiming at it, close) shows tonight's page: every Offering
 *  in the motel tonight, what it is worth, and a cryptic line about what taking it wakes. The page is worked out on each
 *  machine from the night's seed, the same everywhere; nothing about it is sent.
 */
UCLASS()
class THEFACILITY_API AFacilityRegister : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	/** What is aimed at: an invisible box over the open book */
	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Box;

public:

	AFacilityRegister();

	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override { return true; }
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override {}
};

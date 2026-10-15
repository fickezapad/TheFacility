// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityFuseBox.generated.h"

class UStaticMeshComponent;

/**
 *  The motel's fuse box. A player carrying a fuse fits it, and the power comes back for everyone:
 *  lights on the main circuit turn on and the exit gate opens.
 *  The power state lives on the game state, so the box itself only needs to replicate as an interactable.
 */
UCLASS()
class THEFACILITY_API AFacilityFuseBox : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

protected:

	/** Item that has to be fitted */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fuse Box")
	FName RequiredItemId = TEXT("Fuse");

public:

	AFacilityFuseBox();

	// IFacilityInteractable
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

protected:

	bool IsPowerRestored() const;

	bool PawnHasFuse(const APawn* InstigatorPawn) const;
};

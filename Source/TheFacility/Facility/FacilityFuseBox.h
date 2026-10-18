// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityFuseBox.generated.h"

class UStaticMeshComponent;

/**
 *  The motel's fuse box. Players carrying fuses fit them one at a time; with the last one the power comes back for everyone:
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

	/** Fuses it takes before the power comes back. Each one is fitted separately, by whoever carries it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fuse Box", meta=(ClampMin=1))
	int32 FusesRequired = 1;

	/** Fuses fitted so far */
	UPROPERTY(ReplicatedUsing=OnRep_FusesFitted, BlueprintReadOnly, Category="Fuse Box")
	int32 FusesFitted = 0;

	/** A fuse seating */
	UPROPERTY(EditAnywhere, Category="Fuse Box|Sound")
	TObjectPtr<class USoundBase> FitSound;

	/** The last fuse: the building hums back to life */
	UPROPERTY(EditAnywhere, Category="Fuse Box|Sound")
	TObjectPtr<class USoundBase> PowerOnSound;

	/** How far fitting a fuse that doesn't yet bring the power back carries */
	UPROPERTY(EditAnywhere, Category="Fuse Box", meta=(ClampMin=0, Units="cm"))
	float FitNoiseRadius = 1200.0f;

	/** How far the power coming back carries, for the Tenant */
	UPROPERTY(EditAnywhere, Category="Fuse Box", meta=(ClampMin=0, Units="cm"))
	float PowerNoiseRadius = 3000.0f;

public:

	AFacilityFuseBox();

	int32 GetFusesRequired() const { return FusesRequired; }

	int32 GetFusesFitted() const { return FusesFitted; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// IFacilityInteractable
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

protected:

	UFUNCTION()
	void OnRep_FusesFitted();

	/** Local: the fuse just fitted is heard here */
	void PlayFitSound();

	bool IsPowerRestored() const;

	bool PawnHasFuse(const APawn* InstigatorPawn) const;
};

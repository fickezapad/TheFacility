// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityDoor.generated.h"

class UStaticMeshComponent;

/**
 *  A door that opens on interact and can require a key.
 *  Open and locked state are owned by the server and replicated; each machine animates the swing locally.
 */
UCLASS()
class THEFACILITY_API AFacilityDoor : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> Root;

	/** Rotates to swing the door; sits on the hinge edge */
	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> DoorMesh;

protected:

	/** Key needed to unlock. None means the door is never locked */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Door")
	FName RequiredKeyId;

	/** Yaw of the hinge when fully open. Any angle works, including 180 and beyond; negative swings the other way */
	UPROPERTY(EditAnywhere, Category="Door", meta=(Units="deg"))
	float OpenYaw = 90.0f;

	/** Swing speed */
	UPROPERTY(EditAnywhere, Category="Door", meta=(ClampMin=0, Units="deg"))
	float SwingDegreesPerSecond = 180.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Open, BlueprintReadOnly, Category="Door")
	bool bOpen = false;

	/** 0 = closed, 1 = fully open. Local animation state on every machine */
	float OpenAlpha = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Door")
	bool bLocked = false;

public:

	AFacilityDoor();

	bool IsOpen() const { return bOpen; }

	bool IsLocked() const { return bLocked; }

	// IFacilityInteractable
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool PawnHasKey(const APawn* InstigatorPawn) const;

	UFUNCTION()
	void OnRep_Open();

	/** Starts ticking until the swing reaches the current open state */
	void StartSwing();
};

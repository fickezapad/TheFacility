// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityDoor.generated.h"

class UStaticMeshComponent;

/**
 *  A door that opens on interact and can require a key.
 *  Whoever carries the master key can lock and unlock any closed door, and open any locked one.
 *  The Tenant beats locked doors down: a broken door hangs open for the rest of the run.
 *  Open, locked and broken state are owned by the server and replicated; each machine animates the swing locally.
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

	/** Swing away from whoever opens the door, so it never swings into them and pins them behind it */
	UPROPERTY(EditAnywhere, Category="Door")
	bool bSwingAwayFromOpener = true;

	/** Swing speed */
	UPROPERTY(EditAnywhere, Category="Door", meta=(ClampMin=0, Units="deg"))
	float SwingDegreesPerSecond = 180.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Open, BlueprintReadOnly, Category="Door")
	bool bOpen = false;

	/** 0 = closed, 1 = fully open. Local animation state on every machine */
	float OpenAlpha = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Door")
	bool bLocked = false;

	/** Swings to -OpenYaw this time, chosen by the server when the door opens */
	UPROPERTY(Replicated)
	bool bSwingReversed = false;

	/** Beaten down: open for good, can't be closed or locked again */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Door")
	bool bBroken = false;

	/** How far opening or closing it carries, for the Tenant */
	UPROPERTY(EditAnywhere, Category="Door|Noise", meta=(ClampMin=0, Units="cm"))
	float SwingNoiseRadius = 900.0f;

	/** How far beating it down carries */
	UPROPERTY(EditAnywhere, Category="Door|Noise", meta=(ClampMin=0, Units="cm"))
	float BreakNoiseRadius = 2000.0f;

	/** How far turning the master key in it carries */
	UPROPERTY(EditAnywhere, Category="Door|Noise", meta=(ClampMin=0, Units="cm"))
	float LockNoiseRadius = 300.0f;

public:

	AFacilityDoor();

	bool IsOpen() const { return bOpen; }

	bool IsLocked() const { return bLocked; }

	bool IsBroken() const { return bBroken; }

	/** Item id of the master key, which opens and locks every door */
	static const FName MasterKeyId;

	/** True if this pawn may lock or unlock this door with the master key right now. Any machine */
	bool CanToggleLock(const APawn* InstigatorPawn) const;

	/** Server only. Locks or unlocks the closed door with the master key */
	void ToggleLock(APawn* InstigatorPawn);

	/** Server only. Beaten down from either side: unlocked, swung open and left that way */
	void BreakOpen(APawn* Breaker);

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

	/** Server: opens the door, swinging away from Opener if that is enabled */
	void Open(const APawn* Opener);

	/** Server: the door is heard by the Tenant */
	void ReportDoorNoise(float Radius, const APawn* Source);
};

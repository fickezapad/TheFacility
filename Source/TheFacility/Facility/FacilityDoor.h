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

	/** Doors of one room share a group (Room1..Room7). The night's seed picks which groups start locked; see AFacilityGameMode */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Door")
	FName LockGroup;

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
	UPROPERTY(ReplicatedUsing=OnRep_Slammed, BlueprintReadOnly, Category="Door")
	bool bBroken = false;

	/** Locked by the Facility for a while, not by anyone's key. Lets go by itself */
	UPROPERTY(ReplicatedUsing=OnRep_Slammed, BlueprintReadOnly, Category="Door")
	bool bHeldShut = false;

	/** Hinges, on every open and close */
	UPROPERTY(EditAnywhere, Category="Door|Sound")
	TObjectPtr<class USoundBase> CreakSound;

	/** Slammed and held by the Facility, or beaten down */
	UPROPERTY(EditAnywhere, Category="Door|Sound")
	TObjectPtr<class USoundBase> SlamSound;

	FTimerHandle HoldTimer;

	/** How far it carries when the Facility slams it */
	UPROPERTY(EditAnywhere, Category="Door|Noise", meta=(ClampMin=0, Units="cm"))
	float SlamNoiseRadius = 1200.0f;

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

	FName GetLockGroup() const { return LockGroup; }

	/** Server only, at the start of a night: locks the door so only the master key (or the Tenant) gets it open */
	void LockForNight();

	/** True if this pawn may lock or unlock this door with the master key right now. Any machine */
	bool CanToggleLock(const APawn* InstigatorPawn) const;

	/** Server only. Locks or unlocks the closed door with the master key */
	void ToggleLock(APawn* InstigatorPawn);

	/** Server only. Beaten down from either side: unlocked, swung open and left that way */
	void BreakOpen(APawn* Breaker);

	/** Server only. The Facility slams it and holds it locked for Duration. The master key and the Tenant still get through */
	void HoldShut(float Duration);

	bool IsHeldShut() const { return bHeldShut; }

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

	/** Clients: a slam (held shut, or broken) is heard */
	UFUNCTION()
	void OnRep_Slammed();

	/** Local: plays the slam here */
	void PlaySlam();

	/** Starts ticking until the swing reaches the current open state */
	void StartSwing();

	/** Server: an open slab is an obstacle for navigation, a closed one is not */
	void UpdateNavigationBlocking();

	/** Server: opens the door, swinging away from Opener if that is enabled */
	void Open(const APawn* Opener);

	/** Server: the door is heard by the Tenant */
	void ReportDoorNoise(float Radius, const APawn* Source);

	/** Server: the Facility lets go of it, unless someone has turned a key in it since */
	void ReleaseHold();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

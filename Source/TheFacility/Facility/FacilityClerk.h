// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FacilityClerk.generated.h"

class AFacilityCharacter;
class AFacilityDoor;
class UPointLightComponent;
class USoundBase;
struct FFacilitySignature;

UENUM(BlueprintType)
enum class EClerkState : uint8
{
	/** Not awake tonight, or not yet: hidden and without collision */
	Dormant,
	/** The bell has rung; it comes out in a moment */
	Manifesting,
	/** Walks the rooms with its lamp, door to door */
	Patrolling,
	/** Saw a flashlight and goes to it */
	Approaching,
	/** Took someone: walks back to the desk, takes nobody for a while */
	Sated
};

/**
 *  The Night Clerk. Woken by the Offerings bound to it tonight, it rings the reception bell and walks the motel's
 *  rooms with a lamp. Doors it passes lock themselves until it has gone. Whoever stands still in its lamplight is taken
 *  (downed). A lit flashlight draws it from across a room.
 *
 *  One rule, one counterplay: keep moving, go dark. Hiding works too: it doesn't look in closets.
 *  Server-authoritative; its state and lamp replicate, every machine hears its bell and steps.
 */
UCLASS()
class THEFACILITY_API AFacilityClerk : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UPointLightComponent> Lamp;

protected:

	UPROPERTY(ReplicatedUsing=OnRep_State)
	EClerkState State = EClerkState::Dormant;

	/** Its lamplight: standing still in it this long is being taken */
	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="cm"))
	float LampRadius = 430.0f;

	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="s"))
	float StillTimeToTake = 1.2f;

	/** Moving slower than this counts as standing still */
	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="cm/s"))
	float StillSpeed = 45.0f;

	/** A lit flashlight this close, in sight, draws it */
	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="cm"))
	float AttractRange = 1100.0f;

	/** Closed doors this close lock themselves */
	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="cm"))
	float DoorLockRange = 380.0f;

	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="cm/s"))
	float PatrolSpeed = 150.0f;

	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="cm/s"))
	float ApproachSpeed = 250.0f;

	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="s"))
	float ManifestDelay = 4.0f;

	UPROPERTY(EditAnywhere, Category="Clerk", meta=(Units="s"))
	float SatedTime = 20.0f;

	UPROPERTY()
	TObjectPtr<USoundBase> BellSound;

	UPROPERTY()
	TObjectPtr<USoundBase> StepSound;

	/** Tonight's Offerings that wake it; empty and asleep, nothing does */
	TArray<FName> ArmedBy;
	bool bAwakeTonight = false;
	bool bArmed = false;
	bool bShutDown = false;

	FTransform HomeTransform;
	TArray<FVector> Posts;
	FVector BellLocation = FVector::ZeroVector;
	TMap<TWeakObjectPtr<AFacilityCharacter>, float> StillFor;
	TArray<TWeakObjectPtr<AFacilityDoor>> LockedDoors;
	TWeakObjectPtr<AFacilityCharacter> DrawnTo;
	float StateTime = 0.0f;
	float WaitAtPost = 0.0f;
	float NextBell = 0.0f;
	float StepTimer = 0.0f;
	int32 LastPost = INDEX_NONE;
	FTimerHandle ThinkTimer;
	FTimerHandle StateTimer;

	static constexpr float ThinkInterval = 0.1f;

public:

	AFacilityClerk();

	EClerkState GetClerkState() const { return State; }

	bool IsPresent() const { return State != EClerkState::Dormant && State != EClerkState::Manifesting; }

	/** Server only, at the start of a night */
	void SetNightBinding(const TArray<FName>& SignatureIds, bool bAwake);

	const TArray<FName>& GetArmedBy() const { return ArmedBy; }

	/** Server only: wakes it now (QA, or a bound Offering) */
	void Wake(const FString& Reason);

	/** Server only: the run is over */
	void ShutDown();

	/** QA, server: how long a player has stood still in its light */
	float GetStillTime(const AFacilityCharacter* Player) const;

	/** True if Location is in its lamplight with nothing in between. Any machine, for the HUD as well */
	bool IsInLamplight(const FVector& Location) const;

	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void HandleSignatureRecorded(const FFacilitySignature& Signature);

	void BindToLedger();

	void SetClerkState(EClerkState NewState);

	void Appear();

	void Think();

	/** Locks closed doors it is near, and unlocks the ones it locked once it is past them */
	void UpdateDoors();

	/** Whoever stands still in its lamplight long enough is taken */
	bool TryTake();

	void MoveTowards(const FVector& Location, float Acceptance);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_RingBell(FVector_NetQuantize Where);
};

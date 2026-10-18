// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Facility/FacilityTenant.h"
#include "FacilityTenantAIController.generated.h"

class AFacilityDoor;

/**
 *  Server-only brain for The Tenant. A small explicit state machine, thought through a few times a second:
 *  Wandering -> Investigating (far sound) or Rushing (near sound) -> Listening (got there) -> Wandering.
 *  It never looks; it only follows sounds, and catches whoever it walks into or hears moving within reach.
 */
UCLASS()
class THEFACILITY_API AFacilityTenantAIController : public AAIController
{
	GENERATED_BODY()

protected:

	/** A sound closer than this makes it run instead of walk */
	UPROPERTY(EditAnywhere, Category="Tenant|Hearing", meta=(ClampMin=0, Units="cm"))
	float RushDistance = 700.0f;

	/** Catches a standing player this close who is moving, if nothing is between them */
	UPROPERTY(EditAnywhere, Category="Tenant|Catch", meta=(ClampMin=0, Units="cm"))
	float CatchRange = 150.0f;

	/** Below this speed a player counts as keeping still */
	UPROPERTY(EditAnywhere, Category="Tenant|Catch", meta=(ClampMin=0, Units="cm/s"))
	float StillSpeed = 60.0f;

	/** Walking into a player catches them, still or not */
	UPROPERTY(EditAnywhere, Category="Tenant|Catch", meta=(ClampMin=0, Units="cm"))
	float BumpRange = 95.0f;

	/** How long it stands and listens where a sound was */
	UPROPERTY(EditAnywhere, Category="Tenant|Hearing", meta=(ClampMin=0, Units="s"))
	float ListenTime = 4.0f;

	/** How far it strolls from where it is while wandering */
	UPROPERTY(EditAnywhere, Category="Tenant|Wander", meta=(ClampMin=0, Units="cm"))
	float WanderRadius = 1200.0f;

	UPROPERTY(EditAnywhere, Category="Tenant|Speed", meta=(ClampMin=0, Units="cm/s"))
	float WanderSpeed = 140.0f;

	UPROPERTY(EditAnywhere, Category="Tenant|Speed", meta=(ClampMin=0, Units="cm/s"))
	float InvestigateSpeed = 260.0f;

	/** Faster than a walking player, slower than a sprinting one */
	UPROPERTY(EditAnywhere, Category="Tenant|Speed", meta=(ClampMin=0, Units="cm/s"))
	float RushSpeed = 430.0f;

	/** It opens a closed, unlocked door after standing at it this long */
	UPROPERTY(EditAnywhere, Category="Tenant|Doors", meta=(ClampMin=0, Units="s"))
	float DoorOpenDelay = 0.6f;

	/** How long it beats on a locked door before it gives */
	UPROPERTY(EditAnywhere, Category="Tenant|Doors", meta=(ClampMin=0, Units="s"))
	float BreakTime = 4.0f;

	/** Moving but not getting anywhere for this long: it gives up on where it was going */
	UPROPERTY(EditAnywhere, Category="Tenant|Wander", meta=(ClampMin=0, Units="s"))
	float StuckTime = 3.0f;

	static constexpr float ThinkInterval = 0.1f;

	TWeakObjectPtr<AFacilityTenant> Tenant;

	FTimerHandle ThinkTimer;

	ETenantState LastState = ETenantState::Dormant;

	/** Where it is going while investigating or rushing */
	FVector Destination = FVector::ZeroVector;

	/** State to go back to once a door is broken */
	ETenantState StateBeforeBreaking = ETenantState::Wandering;

	TWeakObjectPtr<AFacilityDoor> DoorBeingBroken;

	float StateTime = 0.0f;
	float DoorWaitTime = 0.0f;
	float StuckFor = 0.0f;
	bool bLoggedNoNavigation = false;

public:

	AFacilityTenantAIController();

protected:

	virtual void OnPossess(APawn* InPawn) override;

	virtual void OnUnPossess() override;

	void Think();

	void Enter(ETenantState NewState);

	/** Catches the first player it is touching, or hears moving within reach. Returns true if it caught someone */
	bool TryCatch();

	/** Heads for a new sound, walking or running depending on how close it is */
	void FollowNoise(const FVector& Location);

	/** Opens a closed, unlocked door right in front of it. Returns a locked door in the way, if any */
	AFacilityDoor* HandleDoorAhead();

	/** Moves to a location, falling back to a straight line if there is no navigation path */
	void MoveTowards(const FVector& Location, float AcceptanceRadius);

	/** A random reachable point near Origin, or Origin itself if there is no navigation */
	FVector RandomPointNear(const FVector& Origin, float Radius) const;

	void SetSpeed(float Speed);
};

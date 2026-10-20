// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Facility/FacilityMarked.h"
#include "FacilityMarkedAIController.generated.h"

class AFacilityCharacter;

/**
 *  Server-only brain for The Marked. A small explicit state machine, thought through a few times a second:
 *  Hunting (drift toward roughly where the mark is) -> Chasing (mark sensed) -> Searching (mark lost) -> Hunting.
 *  It only ever hunts its mark. Everyone else is ignored, but their bodies still block it.
 */
UCLASS()
class THEFACILITY_API AFacilityMarkedAIController : public AAIController
{
	GENERATED_BODY()

protected:

	/** How far it can see its mark */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="cm"))
	float SightRange = 2500.0f;

	/** How far it can see a mark whose flashlight is off: the beam is what gives you away in the dark */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="cm"))
	float DarkSightRange = 600.0f;

	/** A sneaking mark is hidden from it by anything solid this close to them, between them and it, at crouching height */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="cm"))
	float CoverDistance = 150.0f;

	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="cm"))
	float CoverHeight = 70.0f;

	/** Half of its field of view */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, ClampMax=180, Units="deg"))
	float SightHalfAngle = 75.0f;

	/** Within this distance it knows where its mark is, even through walls, and sees them even behind it. It only chases what it sees */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="cm"))
	float SenseRadius = 350.0f;

	/** Out of sight for this long and it starts searching */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="s"))
	float LoseSightTime = 2.5f;

	/** How long it searches before going back to hunting */
	UPROPERTY(EditAnywhere, Category="Marked|Search", meta=(ClampMin=0, Units="s"))
	float SearchTime = 12.0f;

	/** Radius of the search around the last known position */
	UPROPERTY(EditAnywhere, Category="Marked|Search", meta=(ClampMin=0, Units="cm"))
	float SearchRadius = 600.0f;

	/** While hunting it heads for a random point this close to its mark: it knows roughly, not exactly */
	UPROPERTY(EditAnywhere, Category="Marked|Hunt", meta=(ClampMin=0, Units="cm"))
	float HuntRadius = 500.0f;

	/** How often it updates its hunting destination */
	UPROPERTY(EditAnywhere, Category="Marked|Hunt", meta=(ClampMin=0.1, Units="s"))
	float HuntRepathInterval = 6.0f;

	UPROPERTY(EditAnywhere, Category="Marked|Speed", meta=(ClampMin=0, Units="cm/s"))
	float HuntSpeed = 220.0f;

	UPROPERTY(EditAnywhere, Category="Marked|Speed", meta=(ClampMin=0, Units="cm/s"))
	float SearchSpeed = 280.0f;

	/** Chase speed as a share of the mark's walk speed, so a mark who keeps moving can stay ahead */
	UPROPERTY(EditAnywhere, Category="Marked|Speed", meta=(ClampMin=0))
	float ChaseSpeedRatio = 0.85f;

	/** Catches the mark within this distance, if nothing is between them */
	UPROPERTY(EditAnywhere, Category="Marked|Catch", meta=(ClampMin=0, Units="cm"))
	float CatchRange = 130.0f;

	/** It opens a closed, unlocked door after standing at it this long */
	UPROPERTY(EditAnywhere, Category="Marked|Doors", meta=(ClampMin=0, Units="s"))
	float DoorOpenDelay = 0.8f;

	static constexpr float ThinkInterval = 0.1f;

	TWeakObjectPtr<AFacilityMarked> Marked;

	FTimerHandle ThinkTimer;

	EMarkedState LastState = EMarkedState::Dormant;
	FVector LastKnownLocation = FVector::ZeroVector;
	float TimeSinceSeen = 0.0f;
	float StateTime = 0.0f;
	float TimeToRepath = 0.0f;
	float DoorWaitTime = 0.0f;
	bool bLoggedNoNavigation = false;

	/** If it had its mark in sight this recently when they hid, it saw them go in */
	UPROPERTY(EditAnywhere, Category="Marked|Senses", meta=(ClampMin=0, Units="s"))
	float SawHideWindow = 1.0f;

	/** The mark's current hiding spot, while they hide, and whether it saw them get in */
	TWeakObjectPtr<class AFacilityHidingSpot> MarkHideSpot;
	bool bSawMarkHide = false;

	/** Handles a mark who is hiding. Returns true if that took this think */
	bool ThinkAboutHiddenMark(AFacilityMarked* Creature, AFacilityCharacter* Target, EMarkedState State);

public:

	AFacilityMarkedAIController();

	/** QA, server: whether it can see this player right now, by the same test the hunt uses */
	bool CanSeeForQA(const AFacilityCharacter* Target) const;

	/** QA: why it does or doesn't: distance, facing, flashlight, cover, line of sight */
	FString DescribeSightForQA(const AFacilityCharacter* Target) const;

	/** Server: something solid is right in front of a sneaking player, between them and From */
	bool IsInCover(const AFacilityCharacter* Target, const FVector& From) const;

protected:

	virtual void OnPossess(APawn* InPawn) override;

	virtual void OnUnPossess() override;

	void Think();

	/** True if it can see its mark right now: in its field of view, or close, with nothing in between */
	bool CanSee(const AFacilityCharacter* Target, float Distance) const;

	void Enter(EMarkedState NewState);

	/** Moves to a location, falling back to a straight line if there is no navigation path */
	void MoveTowards(const FVector& Destination, float AcceptanceRadius);

	/** A random reachable point near Origin, or Origin itself if there is no navigation */
	FVector RandomPointNear(const FVector& Origin, float Radius) const;

	void SetSpeed(float Speed);

	/** Opens a closed, unlocked door right in front of it */
	void OpenDoorAhead();
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMarkedAIController.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "TheFacility.h"

AFacilityMarkedAIController::AFacilityMarkedAIController()
{
}

void AFacilityMarkedAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Marked = Cast<AFacilityMarked>(InPawn);
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AFacilityMarkedAIController::Think, ThinkInterval, true);
}

void AFacilityMarkedAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	Marked.Reset();

	Super::OnUnPossess();
}

void AFacilityMarkedAIController::Think()
{
	AFacilityMarked* Creature = Marked.Get();
	if (!Creature)
	{
		return;
	}

	const EMarkedState State = Creature->GetMarkedState();
	if (State != LastState)
	{
		// entered a new state, from here or from the creature itself (appearing, withdrawing)
		LastState = State;
		StateTime = 0.0f;
		TimeToRepath = 0.0f;
		if (State == EMarkedState::Searching)
		{
			MoveTowards(LastKnownLocation, 50.0f);
		}
	}

	StateTime += ThinkInterval;
	TimeToRepath -= ThinkInterval;

	if (!Creature->IsPresent() || State == EMarkedState::Stunned)
	{
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		TimeSinceSeen = 0.0f;
		return;
	}

	if (!IsValid(Creature->GetMark()))
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Marked: its mark left the game, going dormant"));
		StopMovement();
		Creature->SetMarkedState(EMarkedState::Dormant);
		return;
	}

	// mark downed or between pawns: wait where it is
	AFacilityCharacter* Target = Creature->GetHuntablePawn();
	if (!Target)
	{
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		return;
	}

	const float Distance = FVector::Dist(GetPawn()->GetActorLocation(), Target->GetActorLocation());
	const bool bSees = CanSee(Target, Distance);
	// close enough to know where the mark is, but maybe not able to reach them (a wall, a locked door)
	const bool bNear = Distance <= SenseRadius;
	if (bSees || bNear)
	{
		LastKnownLocation = Target->GetActorLocation();
	}
	TimeSinceSeen = bSees ? 0.0f : TimeSinceSeen + ThinkInterval;

	OpenDoorAhead();

	const bool bIdle = GetMoveStatus() == EPathFollowingStatus::Idle;

	switch (State)
	{
	case EMarkedState::Hunting:
		if (bSees)
		{
			Enter(EMarkedState::Chasing);
			break;
		}
		if (bNear)
		{
			// the mark is close but out of sight: comb the area instead of pressing against a wall
			Enter(EMarkedState::Searching);
			break;
		}
		SetSpeed(HuntSpeed);
		if (TimeToRepath <= 0.0f || bIdle)
		{
			MoveTowards(RandomPointNear(Target->GetActorLocation(), HuntRadius), 50.0f);
			TimeToRepath = HuntRepathInterval;
		}
		break;

	case EMarkedState::Chasing:
	{
		if (Distance <= CatchRange && bSees)
		{
			StopMovement();
			Creature->CatchMark();
			return;
		}
		if (TimeSinceSeen > LoseSightTime)
		{
			Enter(EMarkedState::Searching);
			break;
		}
		// paced against the mark's walk, so a mark who sprints gains ground until they run out of breath
		const UFacilityMovementComponent* TargetMovement = Cast<UFacilityMovementComponent>(Target->GetCharacterMovement());
		SetSpeed((TargetMovement ? TargetMovement->GetWalkSpeed() : Target->GetCharacterMovement()->MaxWalkSpeed) * ChaseSpeedRatio);
		if (TimeToRepath <= 0.0f || bIdle)
		{
			// toward where it last knew the mark was, which is where the mark is while it can still see them
			MoveTowards(LastKnownLocation, 30.0f);
			TimeToRepath = 0.3f;
		}
		break;
	}

	case EMarkedState::Searching:
		if (bSees)
		{
			Enter(EMarkedState::Chasing);
			break;
		}
		if (StateTime > SearchTime)
		{
			Enter(EMarkedState::Hunting);
			break;
		}
		SetSpeed(SearchSpeed);
		if (bIdle)
		{
			MoveTowards(RandomPointNear(LastKnownLocation, SearchRadius), 50.0f);
		}
		break;

	default:
		break;
	}
}

bool AFacilityMarkedAIController::CanSee(const AFacilityCharacter* Target, float Distance) const
{
	if (Distance > SightRange)
	{
		return false;
	}

	// beyond arm's reach it only sees what is in front of it
	if (Distance > SenseRadius)
	{
		const APawn* Self = GetPawn();
		const FVector ToTarget = (Target->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal();
		if (FVector::DotProduct(Self->GetActorForwardVector(), ToTarget) < FMath::Cos(FMath::DegreesToRadians(SightHalfAngle)))
		{
			return false;
		}
	}

	return LineOfSightTo(Target);
}

void AFacilityMarkedAIController::Enter(EMarkedState NewState)
{
	if (AFacilityMarked* Creature = Marked.Get())
	{
		Creature->SetMarkedState(NewState);
	}
}

void AFacilityMarkedAIController::MoveTowards(const FVector& Destination, float AcceptanceRadius)
{
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Destination, AcceptanceRadius, true, true, true, false, nullptr, true);
	if (Result != EPathFollowingRequestResult::Failed)
	{
		return;
	}

	// no navigation path: walk straight at it rather than freeze. Walls will stop it
	if (!bLoggedNoNavigation)
	{
		bLoggedNoNavigation = true;
		UE_LOG(LogTheFacility, Warning, TEXT("The Marked: no navigation path to %s, moving in a straight line. Does the level have a NavMeshBoundsVolume?"), *Destination.ToCompactString());
	}
	MoveToLocation(Destination, AcceptanceRadius, true, false, false, false, nullptr, true);
}

FVector AFacilityMarkedAIController::RandomPointNear(const FVector& Origin, float Radius) const
{
	if (const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Point;
		if (NavSystem->GetRandomReachablePointInRadius(Origin, Radius, Point))
		{
			return Point.Location;
		}
	}

	const FVector2D Offset = FMath::RandPointInCircle(Radius);
	return Origin + FVector(Offset.X, Offset.Y, 0.0f);
}

void AFacilityMarkedAIController::SetSpeed(float Speed)
{
	if (const ACharacter* Self = Cast<ACharacter>(GetPawn()))
	{
		Self->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}
}

void AFacilityMarkedAIController::OpenDoorAhead()
{
	const APawn* Self = GetPawn();
	const FVector Start = Self->GetActorLocation();
	const FVector End = Start + Self->GetActorForwardVector() * 120.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityMarkedDoor), false, Self);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		AFacilityDoor* Door = Cast<AFacilityDoor>(Hit.GetActor());
		if (Door && !Door->IsOpen() && !Door->IsLocked())
		{
			DoorWaitTime += ThinkInterval;
			if (DoorWaitTime >= DoorOpenDelay)
			{
				DoorWaitTime = 0.0f;
				UE_LOG(LogTheFacility, Log, TEXT("The Marked opens %s"), *Door->GetName());
				IFacilityInteractable::Execute_Interact(Door, GetPawn());
			}
			return;
		}
	}

	DoorWaitTime = 0.0f;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityTenantAIController.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityTenant.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "TheFacility.h"

AFacilityTenantAIController::AFacilityTenantAIController()
{
}

void AFacilityTenantAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Tenant = Cast<AFacilityTenant>(InPawn);
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AFacilityTenantAIController::Think, ThinkInterval, true);
}

void AFacilityTenantAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	Tenant.Reset();

	Super::OnUnPossess();
}

void AFacilityTenantAIController::Think()
{
	AFacilityTenant* Creature = Tenant.Get();
	if (!Creature)
	{
		return;
	}

	ETenantState State = Creature->GetTenantState();
	if (State != LastState)
	{
		// entered a new state, from here or from the creature itself (coming out, catching someone)
		LastState = State;
		StateTime = 0.0f;
		StuckFor = 0.0f;
		DoorWaitTime = 0.0f;
	}
	StateTime += ThinkInterval;

	if (!Creature->IsPresent())
	{
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		return;
	}

	if (TryCatch())
	{
		return;
	}

	if (State == ETenantState::Breaking)
	{
		AFacilityDoor* Door = DoorBeingBroken.Get();
		if (Door && Door->IsLocked() && !Door->IsOpen() && StateTime < BreakTime)
		{
			return;
		}

		if (Door && Door->IsLocked() && !Door->IsOpen())
		{
			UE_LOG(LogTheFacility, Log, TEXT("The Tenant breaks down %s"), *Door->GetName());
			Door->BreakOpen(GetPawn());
		}
		DoorBeingBroken.Reset();

		// back to whatever it was doing on the other side
		Enter(StateBeforeBreaking);
		if (StateBeforeBreaking != ETenantState::Wandering)
		{
			MoveTowards(Destination, 60.0f);
		}
		return;
	}

	// a new sound beats whatever it was doing
	FVector Heard;
	if (!Creature->IsSated() && Creature->TakeNewNoise(Heard))
	{
		FollowNoise(Heard);
		State = Creature->GetTenantState();
	}

	if (State != ETenantState::Listening)
	{
		if (AFacilityDoor* LockedDoor = HandleDoorAhead())
		{
			if (State == ETenantState::Wandering)
			{
				// a locked door only matters to it when something is making noise behind it: stroll somewhere else
				MoveTowards(RandomPointNear(GetPawn()->GetActorLocation(), WanderRadius), 80.0f);
			}
			else
			{
				StopMovement();
				DoorBeingBroken = LockedDoor;
				StateBeforeBreaking = State;
				Enter(ETenantState::Breaking);
				return;
			}
		}
	}

	const bool bIdle = GetMoveStatus() == EPathFollowingStatus::Idle;
	const bool bStalled = !bIdle && GetPawn()->GetVelocity().SizeSquared2D() < FMath::Square(20.0f);
	StuckFor = bStalled ? StuckFor + ThinkInterval : 0.0f;

	switch (State)
	{
	case ETenantState::Wandering:
		SetSpeed(WanderSpeed);
		if (bIdle || StuckFor > StuckTime)
		{
			// sated, it drifts back toward its room; otherwise anywhere near where it is
			const FVector Origin = Creature->IsSated() ? Creature->GetHomeTransform().GetLocation() : GetPawn()->GetActorLocation();
			MoveTowards(RandomPointNear(Origin, WanderRadius), 80.0f);
			StuckFor = 0.0f;
		}
		break;

	case ETenantState::Investigating:
	case ETenantState::Rushing:
		SetSpeed(State == ETenantState::Rushing ? RushSpeed : InvestigateSpeed);
		if (bIdle || StuckFor > StuckTime || FVector::Dist2D(GetPawn()->GetActorLocation(), Destination) < 100.0f)
		{
			UE_LOG(LogTheFacility, Log, TEXT("The Tenant stops %.0f cm from the sound: %s"), FVector::Dist2D(GetPawn()->GetActorLocation(), Destination),
				StuckFor > StuckTime ? TEXT("stuck") : bIdle ? TEXT("path ended") : TEXT("arrived"));
			StopMovement();
			Enter(ETenantState::Listening);
		}
		break;

	case ETenantState::Listening:
		if (!bIdle)
		{
			StopMovement();
		}
		if (StateTime >= ListenTime)
		{
			Creature->ForgetNoise();
			Enter(ETenantState::Wandering);
		}
		break;

	default:
		break;
	}
}

bool AFacilityTenantAIController::TryCatch()
{
	AFacilityTenant* Creature = Tenant.Get();
	if (!Creature || Creature->IsSated())
	{
		return false;
	}

	const FVector Here = GetPawn()->GetActorLocation();
	for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
	{
		AFacilityCharacter* Player = *It;
		if (!Player->IsStanding())
		{
			continue;
		}

		const float Distance = FVector::Dist(Here, Player->GetActorLocation());
		if (Distance > CatchRange)
		{
			continue;
		}

		// within reach it hears footsteps; touching, it feels them. Never through a wall or a closed door
		const bool bMoving = Player->GetVelocity().SizeSquared2D() > FMath::Square(StillSpeed);
		if ((Distance <= BumpRange || bMoving) && LineOfSightTo(Player))
		{
			StopMovement();
			Creature->CatchPlayer(Player);
			return true;
		}
	}

	return false;
}

void AFacilityTenantAIController::FollowNoise(const FVector& Location)
{
	Destination = Location;
	const float Distance = FVector::Dist(GetPawn()->GetActorLocation(), Location);
	const ETenantState NewState = Distance <= RushDistance ? ETenantState::Rushing : ETenantState::Investigating;
	Enter(NewState);
	SetSpeed(NewState == ETenantState::Rushing ? RushSpeed : InvestigateSpeed);
	MoveTowards(Location, 60.0f);
}

AFacilityDoor* AFacilityTenantAIController::HandleDoorAhead()
{
	const APawn* Self = GetPawn();
	const FVector Start = Self->GetActorLocation();
	const FVector End = Start + Self->GetActorForwardVector() * 130.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityTenantDoor), false, Self);
	FHitResult Hit;
	AFacilityDoor* Door = nullptr;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		Door = Cast<AFacilityDoor>(Hit.GetActor());
	}

	if (!Door || Door->IsOpen() || Door->IsBroken())
	{
		DoorWaitTime = 0.0f;
		return nullptr;
	}

	DoorWaitTime += ThinkInterval;
	if (DoorWaitTime < DoorOpenDelay)
	{
		return nullptr;
	}
	DoorWaitTime = 0.0f;

	if (Door->IsLocked())
	{
		return Door;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Tenant opens %s"), *Door->GetName());
	IFacilityInteractable::Execute_Interact(Door, GetPawn());
	return nullptr;
}

void AFacilityTenantAIController::Enter(ETenantState NewState)
{
	if (AFacilityTenant* Creature = Tenant.Get())
	{
		Creature->SetTenantState(NewState);
	}
}

void AFacilityTenantAIController::MoveTowards(const FVector& Location, float AcceptanceRadius)
{
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Location, AcceptanceRadius, true, true, true, false, nullptr, true);
	if (Result != EPathFollowingRequestResult::Failed)
	{
		return;
	}

	// no navigation path: walk straight at it rather than freeze. Walls will stop it
	if (!bLoggedNoNavigation)
	{
		bLoggedNoNavigation = true;
		UE_LOG(LogTheFacility, Warning, TEXT("The Tenant: no navigation path to %s, moving in a straight line. Does the level have a NavMeshBoundsVolume?"), *Location.ToCompactString());
	}
	else
	{
		UE_LOG(LogTheFacility, Verbose, TEXT("The Tenant: no navigation path to %s"), *Location.ToCompactString());
	}
	MoveToLocation(Location, AcceptanceRadius, true, false, false, false, nullptr, true);
}

FVector AFacilityTenantAIController::RandomPointNear(const FVector& Origin, float Radius) const
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

void AFacilityTenantAIController::SetSpeed(float Speed)
{
	if (const ACharacter* Self = Cast<ACharacter>(GetPawn()))
	{
		Self->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}
}

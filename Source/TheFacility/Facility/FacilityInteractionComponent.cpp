// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityInteractionComponent.h"
#include "Facility/FacilityInteractable.h"
#include "Facility/FacilityCharacter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "TheFacility.h"

UFacilityInteractionComponent::UFacilityInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;

	// needed so the owning client can send Server_Interact
	SetIsReplicatedByDefault(true);
}

void UFacilityInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// focus is only meaningful for the player looking through this pawn
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		FocusedActor.Reset();
		return;
	}

	FocusedActor = TraceForInteractable();
}

FText UFacilityInteractionComponent::GetFocusedPrompt() const
{
	AActor* Target = FocusedActor.Get();
	if (!Target)
	{
		return FText::GetEmpty();
	}

	return IFacilityInteractable::Execute_GetInteractPrompt(Target, Cast<APawn>(GetOwner()));
}

void UFacilityInteractionComponent::TryInteract()
{
	if (AActor* Target = FocusedActor.Get())
	{
		// on a listen server host this runs locally straight away
		Server_Interact(Target);
	}
}

void UFacilityInteractionComponent::Server_Interact_Implementation(AActor* Target)
{
	APawn* Pawn = Cast<APawn>(GetOwner());

	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(Pawn);
	if (Character && Character->IsDowned())
	{
		UE_LOG(LogTheFacility, Log, TEXT("Interact rejected, player is down: %s -> %s"), *GetNameSafe(Pawn), *GetNameSafe(Target));
		return;
	}

	if (!Pawn || !ValidateTarget(Target))
	{
		UE_LOG(LogTheFacility, Log, TEXT("Interact rejected: %s -> %s"), *GetNameSafe(Pawn), *GetNameSafe(Target));
		return;
	}

	if (!IFacilityInteractable::Execute_CanInteract(Target, Pawn))
	{
		UE_LOG(LogTheFacility, Log, TEXT("Interact refused by target: %s -> %s"), *GetNameSafe(Pawn), *GetNameSafe(Target));
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("Interact accepted: %s -> %s"), *GetNameSafe(Pawn), *GetNameSafe(Target));
	IFacilityInteractable::Execute_Interact(Target, Pawn);
}

bool UFacilityInteractionComponent::ValidateTarget(AActor* Target) const
{
	if (!IsValid(Target) || !Target->Implements<UFacilityInteractable>())
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetOwnerView(ViewLocation, ViewRotation))
	{
		return false;
	}

	// range check against the target's bounds, so large actors like doors are measured from their nearest edge
	const FBox Bounds = Target->GetComponentsBoundingBox();
	const float MaxRange = InteractRange + ServerRangeTolerance;
	if (Bounds.IsValid && Bounds.ComputeSquaredDistanceToPoint(ViewLocation) > FMath::Square(MaxRange))
	{
		UE_LOG(LogTheFacility, Log, TEXT("Interact out of range: %.0f cm > %.0f cm"), FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(ViewLocation)), MaxRange);
		return false;
	}

	if (!Bounds.IsValid)
	{
		return HasLineOfSight(Target, ViewLocation, Target->GetActorLocation());
	}

	// line of sight: the centre first, then points pulled in from the corners of the bounds,
	// so an object that is only partly hidden (on a shelf, behind a rail) can still be reached
	const FVector Center = Bounds.GetCenter();
	if (HasLineOfSight(Target, ViewLocation, Center))
	{
		return true;
	}

	const FVector Extent = Bounds.GetExtent() * 0.8f;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Offset(
			(Corner & 1) ? Extent.X : -Extent.X,
			(Corner & 2) ? Extent.Y : -Extent.Y,
			(Corner & 4) ? Extent.Z : -Extent.Z);

		if (HasLineOfSight(Target, ViewLocation, Center + Offset))
		{
			return true;
		}
	}

	UE_LOG(LogTheFacility, Log, TEXT("Interact has no line of sight to %s"), *GetNameSafe(Target));
	return false;
}

bool UFacilityInteractionComponent::HasLineOfSight(const AActor* Target, const FVector& ViewLocation, const FVector& Point) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityInteractLOS), false, GetOwner());
	FHitResult Hit;

	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Point, TraceChannel, Params))
	{
		return Hit.GetActor() == Target;
	}

	// nothing blocked the line; the point is inside the target's bounds, so it is visible
	return true;
}

AActor* UFacilityInteractionComponent::TraceForInteractable() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		return nullptr;
	}

	// the local player aims with the camera, so trace from it
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityInteractFocus), false, GetOwner());
	FHitResult Hit;
	const FVector End = ViewLocation + ViewRotation.Vector() * InteractRange;

	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, TraceChannel, Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (HitActor && HitActor->Implements<UFacilityInteractable>())
		{
			return HitActor;
		}
	}

	return nullptr;
}

bool UFacilityInteractionComponent::GetOwnerView(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}

	// eye location plus replicated aim; matches on the owning client and the server
	OutLocation = Pawn->GetPawnViewLocation();
	OutRotation = Pawn->GetBaseAimRotation();
	return true;
}

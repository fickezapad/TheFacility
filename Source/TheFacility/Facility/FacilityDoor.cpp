// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityDoor.h"
#include "Facility/FacilityCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AFacilityDoor::AFacilityDoor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	// only ticks while swinging
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);
	Hinge->SetMobility(EComponentMobility::Movable);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Hinge);
	DoorMesh->SetMobility(EComponentMobility::Movable);
	DoorMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	// navigation runs through the doorway whether the door is open or not; AI opens doors itself
	DoorMesh->SetCanEverAffectNavigation(false);

	// placeholder slab, 100cm wide and 210cm tall, swinging from its left edge
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		DoorMesh->SetStaticMesh(CubeMesh.Object);
		DoorMesh->SetRelativeLocation(FVector(0.0f, 50.0f, 105.0f));
		DoorMesh->SetRelativeScale3D(FVector(0.1f, 1.0f, 2.1f));
	}
}

void AFacilityDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityDoor, bOpen);
	DOREPLIFETIME(AFacilityDoor, bLocked);
	DOREPLIFETIME(AFacilityDoor, bSwingReversed);
}

void AFacilityDoor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		bLocked = !RequiredKeyId.IsNone();
	}
}

void AFacilityDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// animate in 0..1 space and derive the yaw, so angles of 180 or more never wrap or fight the target
	const float Target = bOpen ? 1.0f : 0.0f;
	const float AlphaPerSecond = SwingDegreesPerSecond / FMath::Max(FMath::Abs(OpenYaw), 1.0f);
	OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, Target, DeltaSeconds, AlphaPerSecond);

	const bool bArrived = FMath::IsNearlyEqual(OpenAlpha, Target, KINDA_SMALL_NUMBER);
	if (bArrived)
	{
		OpenAlpha = Target;
	}

	Hinge->SetRelativeRotation(FRotator(0.0f, (bSwingReversed ? -OpenYaw : OpenYaw) * OpenAlpha, 0.0f));

	if (bArrived)
	{
		SetActorTickEnabled(false);
	}
}

void AFacilityDoor::OnRep_Open()
{
	StartSwing();
}

void AFacilityDoor::StartSwing()
{
	SetActorTickEnabled(true);
}

void AFacilityDoor::Open(const APawn* Opener)
{
	// pick a side only from fully closed: a door caught mid-swing keeps going the way it was
	if (bSwingAwayFromOpener && Opener && OpenAlpha <= KINDA_SMALL_NUMBER)
	{
		// closed, the slab lies along local Y and a positive yaw swings it toward local -X
		const float OpenerSide = GetActorTransform().InverseTransformPosition(Opener->GetActorLocation()).X;
		bSwingReversed = (OpenYaw > 0.0f) == (OpenerSide < 0.0f);
	}

	bOpen = true;
	StartSwing();
}

bool AFacilityDoor::PawnHasKey(const APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return Character && Character->HasKey(RequiredKeyId);
}

bool AFacilityDoor::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return !bLocked || PawnHasKey(InstigatorPawn);
}

FText AFacilityDoor::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	if (bLocked)
	{
		return PawnHasKey(InstigatorPawn) ? NSLOCTEXT("Facility", "DoorUnlock", "Unlock") : NSLOCTEXT("Facility", "DoorLocked", "Locked");
	}

	return bOpen ? NSLOCTEXT("Facility", "DoorClose", "Close") : NSLOCTEXT("Facility", "DoorOpen", "Open");
}

void AFacilityDoor::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bLocked)
	{
		if (!PawnHasKey(InstigatorPawn))
		{
			return;
		}

		bLocked = false;
		Open(InstigatorPawn);
		return;
	}

	if (bOpen)
	{
		bOpen = false;
		StartSwing();
		return;
	}

	Open(InstigatorPawn);
}

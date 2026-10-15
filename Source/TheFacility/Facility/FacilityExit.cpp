// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityExit.h"
#include "Facility/FacilityGameState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityExit::AFacilityExit()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// placeholder gate: 300 cm wide, 300 cm tall, across the actor's Y axis
	Gate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gate"));
	Gate->SetupAttachment(Root);
	Gate->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Gate->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Gate->SetStaticMesh(CubeMesh.Object);
		Gate->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
		Gate->SetRelativeScale3D(FVector(0.2f, 3.0f, 3.0f));
	}

	// the zone is only ever tested against player locations, so it needs no collision
	ExitZone = CreateDefaultSubobject<UBoxComponent>(TEXT("ExitZone"));
	ExitZone->SetupAttachment(Root);
	ExitZone->SetRelativeLocation(FVector(250.0f, 0.0f, 150.0f));
	ExitZone->SetBoxExtent(FVector(200.0f, 250.0f, 150.0f));
	ExitZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExitZone->SetCanEverAffectNavigation(false);
}

void AFacilityExit::BeginPlay()
{
	Super::BeginPlay();

	TryBindToGameState();
}

void AFacilityExit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindTimer);

	Super::EndPlay(EndPlayReason);
}

void AFacilityExit::TryBindToGameState()
{
	AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!GameState)
	{
		// on clients the game state can arrive after this actor begins play
		if (++BindAttempts > 50)
		{
			UE_LOG(LogTheFacility, Error, TEXT("%s: no Facility game state, the gate will never open. Check the level's game mode."), *GetName());
			return;
		}
		GetWorldTimerManager().SetTimer(BindTimer, this, &AFacilityExit::TryBindToGameState, 0.2f, false);
		return;
	}

	GameState->OnPowerRestored.AddUniqueDynamic(this, &AFacilityExit::HandlePowerRestored);
	if (GameState->IsPowerRestored())
	{
		HandlePowerRestored();
	}
}

void AFacilityExit::HandlePowerRestored()
{
	if (bOpen)
	{
		return;
	}

	bOpen = true;
	Gate->SetVisibility(false);
	Gate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogTheFacility, Log, TEXT("%s: gate open on the %s"), *GetName(), GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"));
}

bool AFacilityExit::IsInExitZone(const FVector& Location) const
{
	const FVector Local = ExitZone->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = ExitZone->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

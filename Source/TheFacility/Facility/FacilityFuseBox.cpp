// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityFuseBox.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityTenant.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityFuseBox::AFacilityFuseBox()
{
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	// placeholder: a flat box on the wall, 60 x 40 cm
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
		Mesh->SetRelativeScale3D(FVector(0.15f, 0.6f, 0.4f));
	}
}

void AFacilityFuseBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityFuseBox, FusesFitted);
}

bool AFacilityFuseBox::IsPowerRestored() const
{
	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	return GameState && GameState->IsPowerRestored();
}

bool AFacilityFuseBox::PawnHasFuse(const APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return Character && Character->HasKey(RequiredItemId);
}

bool AFacilityFuseBox::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return !IsPowerRestored() && PawnHasFuse(InstigatorPawn);
}

FText AFacilityFuseBox::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	if (IsPowerRestored())
	{
		return NSLOCTEXT("Facility", "FuseBoxOn", "Fuse box: power on");
	}

	const int32 Missing = FMath::Max(FusesRequired - FusesFitted, 1);
	if (PawnHasFuse(InstigatorPawn))
	{
		return Missing > 1
			? FText::Format(NSLOCTEXT("Facility", "FuseBoxFitOneOf", "Fit the fuse ({0} more after this)"), Missing - 1)
			: NSLOCTEXT("Facility", "FuseBoxFit", "Fit the fuse");
	}
	return Missing > 1
		? FText::Format(NSLOCTEXT("Facility", "FuseBoxEmptyMany", "Fuse box: {0} fuses missing"), Missing)
		: NSLOCTEXT("Facility", "FuseBoxEmpty", "Fuse box: one fuse missing");
}

void AFacilityFuseBox::Interact_Implementation(APawn* InstigatorPawn)
{
	AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!HasAuthority() || !Character || !GameState || GameState->IsPowerRestored() || !Character->RemoveKey(RequiredItemId))
	{
		return;
	}

	++FusesFitted;
	if (FusesFitted < FusesRequired)
	{
		UE_LOG(LogTheFacility, Log, TEXT("Fuse fitted by %s: %d of %d"), *GameState->MakeSignerName(Character->GetPlayerState()), FusesFitted, FusesRequired);
		AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation(), FitNoiseRadius, Character);
		return;
	}

	GameState->RestorePower(Character->GetPlayerState());

	// the whole building hums back to life, and anything listening knows where it started
	AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation(), PowerNoiseRadius, Character);
}

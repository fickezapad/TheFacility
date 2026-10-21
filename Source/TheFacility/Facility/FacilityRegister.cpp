// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityRegister.h"
#include "Facility/FacilityLook.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"

AFacilityRegister::AFacilityRegister()
{
	Box = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetStaticMesh(FacilityLook::Shape(TEXT("Cube")));
	Box->SetRelativeScale3D(FVector(0.7f, 0.5f, 0.1f));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Box->SetCanEverAffectNavigation(false);
	Box->SetHiddenInGame(true);
}

FText AFacilityRegister::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	return NSLOCTEXT("Facility", "RegisterRead", "The register: tonight's page");
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityPickup.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AFacilityPickup::AFacilityPickup()
{
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	// visible placeholder until real art exists
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
		Mesh->SetRelativeScale3D(FVector(0.3f));
	}
}

void AFacilityPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityPickup, bTaken);
}

bool AFacilityPickup::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return !bTaken;
}

FText AFacilityPickup::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	return FText::Format(NSLOCTEXT("Facility", "TakePrompt", "Take {0}"), DisplayName);
}

void AFacilityPickup::Interact_Implementation(APawn* InstigatorPawn)
{
	// requests are handled one at a time on the server, so a second taker sees bTaken already set
	if (!HasAuthority() || bTaken)
	{
		return;
	}

	bTaken = true;
	OnRep_Taken();

	if (AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn))
	{
		Character->GiveKey(GrantedKeyId);
	}

	if (IsOffering())
	{
		if (AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>())
		{
			GameState->RecordSignature(SignatureId, ZoneId, InstigatorPawn ? InstigatorPawn->GetPlayerState() : nullptr);
		}
	}
}

void AFacilityPickup::OnRep_Taken()
{
	if (bTaken)
	{
		Mesh->SetVisibility(false, true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BP_OnTaken();
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityPickup.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityLook.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AFacilityPickup::AFacilityPickup()
{
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
		Mesh->SetRelativeScale3D(FVector(0.3f));
	}
	// rendered by the look pieces instead; the sphere still blocks the interaction trace
	Mesh->SetHiddenInGame(true);

	Look = FacilityLook::AddPiece(this, TEXT("Look"), Mesh, NAME_None, TEXT("Sphere"), TEXT("MI_Char_Item"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.18f), true);
	LookDetail = FacilityLook::AddPiece(this, TEXT("LookDetail"), Mesh, NAME_None, TEXT("Cube"), TEXT("MI_Char_ItemBrass"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.01f), true);
	for (const TCHAR* ShapeName : { TEXT("Sphere"), TEXT("Cube"), TEXT("Cylinder") })
	{
		LookAssets.Add(ShapeName, FacilityLook::Shape(ShapeName));
	}
	for (const TCHAR* MaterialName : { TEXT("MI_Char_Item"), TEXT("MI_Char_ItemBrass"), TEXT("MI_Motel_Ledger"), TEXT("MI_Retro_Metal"), TEXT("MI_Retro_Glass") })
	{
		LookAssets.Add(MaterialName, FacilityLook::Material(MaterialName));
	}
}

void AFacilityPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void AFacilityPickup::OnRep_GrantedKeyId()
{
	ApplyLook();
}

void AFacilityPickup::ApplyLook()
{
	// positions are in the hidden sphere's space (scaled 0.3: 10 units = 3 cm); sizes are world scale
	const auto Dress = [this](UStaticMeshComponent* Piece, const TCHAR* ShapeName, const TCHAR* MaterialName, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
	{
		Piece->SetStaticMesh(Cast<UStaticMesh>(LookAssets.FindRef(ShapeName)));
		Piece->SetMaterial(0, Cast<UMaterialInterface>(LookAssets.FindRef(MaterialName)));
		Piece->SetRelativeLocationAndRotation(Location, Rotation);
		Piece->SetRelativeScale3D(Scale);
		Piece->SetVisibility(!bTaken);
	};

	static const FName FuseId(TEXT("Fuse"));
	static const FName MasterKeyId(TEXT("MasterKey"));
	if (GrantedKeyId == FuseId)
	{
		// a cartridge fuse lying on its side: a glass body between brass caps
		Dress(Look, TEXT("Cylinder"), TEXT("MI_Char_ItemBrass"), FVector::ZeroVector, FRotator(90.0f, 0.0f, 0.0f), FVector(0.07f, 0.07f, 0.24f));
		Dress(LookDetail, TEXT("Cylinder"), TEXT("MI_Retro_Glass"), FVector::ZeroVector, FRotator(90.0f, 0.0f, 0.0f), FVector(0.085f, 0.085f, 0.15f));
	}
	else if (GrantedKeyId == MasterKeyId)
	{
		// a motel key on its big red diamond fob
		Dress(Look, TEXT("Cube"), TEXT("MI_Char_ItemBrass"), FVector(25.0f, 0.0f, 0.0f), FRotator::ZeroRotator, FVector(0.14f, 0.03f, 0.012f));
		Dress(LookDetail, TEXT("Cube"), TEXT("MI_Motel_Ledger"), FVector(-15.0f, 0.0f, 0.0f), FRotator(0.0f, 45.0f, 0.0f), FVector(0.08f, 0.08f, 0.02f));
	}
	else if (GrantedKeyId == AFacilityCharacter::FlashCameraId)
	{
		// a boxy instant camera, lens forward
		Dress(Look, TEXT("Cube"), TEXT("MI_Retro_Metal"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.12f, 0.2f, 0.12f));
		Dress(LookDetail, TEXT("Cylinder"), TEXT("MI_Retro_Glass"), FVector(22.0f, 0.0f, 0.0f), FRotator(90.0f, 0.0f, 0.0f), FVector(0.07f, 0.07f, 0.05f));
	}
	else
	{
		Dress(Look, TEXT("Sphere"), TEXT("MI_Char_Item"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.18f));
		Dress(LookDetail, TEXT("Cube"), TEXT("MI_Char_ItemBrass"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.01f));
	}
}

void AFacilityPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityPickup, bTaken);
	DOREPLIFETIME_CONDITION(AFacilityPickup, DisplayName, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFacilityPickup, GrantedKeyId, COND_InitialOnly);
}

void AFacilityPickup::InitDropped(FName KeyId, const FText& Name, FName InSignatureId, FName InZoneId)
{
	if (!HasAuthority())
	{
		return;
	}

	GrantedKeyId = KeyId;
	ApplyLook();
	DisplayName = Name;
	SignatureId = InSignatureId;
	ZoneId = InZoneId;
	PlacementGroup = NAME_None;
}

bool AFacilityPickup::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	// hands are full: nobody carries two of the same thing
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return !bTaken && !(Character && !GrantedKeyId.IsNone() && Character->HasKey(GrantedKeyId));
}

FText AFacilityPickup::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	if (!bTaken && Character && !GrantedKeyId.IsNone() && Character->HasKey(GrantedKeyId))
	{
		return FText::Format(NSLOCTEXT("Facility", "AlreadyCarrying", "You already carry a {0}"), DisplayName);
	}
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
		// remember what was signed for it, so a dropped Offering is still one
		Character->GiveKey(GrantedKeyId, DisplayName, SignatureId, ZoneId);
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

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityLook.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

UStaticMesh* FacilityLook::Shape(const TCHAR* Name)
{
	ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	return Finder.Object;
}

UMaterialInterface* FacilityLook::Material(const TCHAR* Name)
{
	ConstructorHelpers::FObjectFinder<UMaterialInterface> Finder(*FString::Printf(TEXT("/Game/Facility/Materials/%s.%s"), Name, Name));
	return Finder.Object;
}

UStaticMeshComponent* FacilityLook::AddPiece(AActor* Owner, FName Name, USceneComponent* Parent, FName Socket, const TCHAR* ShapeName,
	const TCHAR* MaterialName, const FVector& Location, const FRotator& Rotation, const FVector& Scale, bool bAbsoluteScale)
{
	UStaticMeshComponent* Piece = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Piece->SetupAttachment(Parent, Socket);
	Piece->SetStaticMesh(Shape(ShapeName));
	if (MaterialName)
	{
		Piece->SetMaterial(0, Material(MaterialName));
	}
	Piece->SetRelativeLocationAndRotation(Location, Rotation);
	Piece->SetUsingAbsoluteScale(bAbsoluteScale);
	Piece->SetRelativeScale3D(Scale);
	Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Piece->SetCollisionResponseToAllChannels(ECR_Ignore);
	Piece->SetGenerateOverlapEvents(false);
	Piece->SetCanEverAffectNavigation(false);
	Piece->bReceivesDecals = false;
	return Piece;
}

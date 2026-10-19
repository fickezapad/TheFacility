// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityFuseBox.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityTenant.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
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
	Mesh->SetMaterial(0, FacilityLook::Material(TEXT("MI_Retro_Metal")));
	// a pull handle and a small red warning lamp on the front (+X) face, in the cube's own units
	FacilityLook::AddPiece(this, TEXT("Look_Handle"), Mesh, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Rust"), FVector(56.0f, 38.0f, 0.0f), FRotator::ZeroRotator, FVector(0.03f, 0.03f, 0.16f), true);
	FacilityLook::AddPiece(this, TEXT("Look_Lamp"), Mesh, NAME_None, TEXT("Sphere"), TEXT("MI_Retro_Neon"), FVector(52.0f, -38.0f, 36.0f), FRotator::ZeroRotator, FVector(0.035f), true);
	FacilityLook::AddPiece(this, TEXT("Look_Label"), Mesh, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Paper"), FVector(51.0f, -5.0f, 30.0f), FRotator::ZeroRotator, FVector(0.005f, 0.2f, 0.06f), true);

	static ConstructorHelpers::FObjectFinder<USoundBase> FitAsset(TEXT("/Game/Facility/Audio/S_FuseFit.S_FuseFit"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PowerAsset(TEXT("/Game/Facility/Audio/S_PowerOn.S_PowerOn"));
	FitSound = FitAsset.Object;
	PowerOnSound = PowerAsset.Object;
}

void AFacilityFuseBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityFuseBox, FusesFitted);
}

void AFacilityFuseBox::OnRep_FusesFitted()
{
	PlayFitSound();
}

void AFacilityFuseBox::PlayFitSound()
{
	FacilityAudio::PlayAt(this, FitSound, GetActorLocation(), 1500.0f);
	if (FusesFitted >= FusesRequired)
	{
		FacilityAudio::PlayAt(this, PowerOnSound, GetActorLocation(), 4000.0f, 1.2f);
	}
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
	PlayFitSound();
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

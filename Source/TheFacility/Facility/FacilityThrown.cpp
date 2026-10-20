// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityThrown.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityTenant.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

namespace
{
	FVector LastBreakLocation = FVector::ZeroVector;
	int32 BreakCount = 0;
	int32 BreaksSeen = 0;
}

FVector AFacilityThrown::GetLastBreakLocation() { return LastBreakLocation; }
int32 AFacilityThrown::GetBreakCount() { return BreakCount; }
int32 AFacilityThrown::GetBreaksSeen() { return BreaksSeen; }

AFacilityThrown::AFacilityThrown()
{
	bReplicates = true;
	SetReplicatingMovement(true);

	// a small ball that stops at walls, floors, furniture and doors; it flies through people
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(6.0f);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	Look = FacilityLook::AddPiece(this, TEXT("Look"), Collision, NAME_None, TEXT("Cylinder"), TEXT("MI_Retro_BottleGlass"), FVector::ZeroVector, FRotator(90.0f, 0.0f, 0.0f), FVector(0.075f, 0.075f, 0.2f), true);
	LookAssets.Add(TEXT("MI_Retro_BottleGlass"), FacilityLook::Material(TEXT("MI_Retro_BottleGlass")));
	LookAssets.Add(TEXT("MI_Retro_CanPaint"), FacilityLook::Material(TEXT("MI_Retro_CanPaint")));

	static ConstructorHelpers::FObjectFinder<USoundBase> BottleAsset(TEXT("/Game/Facility/Audio/S_BottleBreak.S_BottleBreak"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CanAsset(TEXT("/Game/Facility/Audio/S_CanClatter.S_CanClatter"));
	static ConstructorHelpers::FObjectFinder<USoundBase> WhooshAsset(TEXT("/Game/Facility/Audio/S_ThrowWhoosh.S_ThrowWhoosh"));
	LookAssets.Add(TEXT("Bottle"), BottleAsset.Object);
	LookAssets.Add(TEXT("Can"), CanAsset.Object);
	LookAssets.Add(TEXT("Whoosh"), WhooshAsset.Object);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bAutoActivate = false;
	Movement->bShouldBounce = false;
	Movement->ProjectileGravityScale = 1.0f;
	Movement->bRotationFollowsVelocity = true;

	InitialLifeSpan = 8.0f;
}

void AFacilityThrown::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AFacilityThrown, ItemId, COND_InitialOnly);
}

void AFacilityThrown::Launch(FName InItemId, AActor* InThrower, const FVector& Direction)
{
	if (!HasAuthority())
	{
		return;
	}

	ItemId = InItemId;
	Thrower = InThrower;
	ApplyLook();
	if (InThrower)
	{
		Collision->IgnoreActorWhenMoving(InThrower, true);
	}
	Movement->OnProjectileStop.AddDynamic(this, &AFacilityThrown::HandleStop);
	Movement->Velocity = Direction.GetSafeNormal() * Speed;
	Movement->Activate(true);
	FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Whoosh"))), GetActorLocation(), 800.0f, 0.5f);
}

void AFacilityThrown::OnRep_ItemId()
{
	ApplyLook();
	FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Whoosh"))), GetActorLocation(), 800.0f, 0.5f);
}

void AFacilityThrown::ApplyLook()
{
	const bool bCan = ItemId == AFacilityCharacter::CanId;
	Look->SetMaterial(0, Cast<UMaterialInterface>(LookAssets.FindRef(bCan ? TEXT("MI_Retro_CanPaint") : TEXT("MI_Retro_BottleGlass"))));
	Look->SetRelativeScale3D(bCan ? FVector(0.065f, 0.065f, 0.12f) : FVector(0.075f, 0.075f, 0.2f));
}

void AFacilityThrown::HandleStop(const FHitResult& Hit)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}
	bBroken = true;

	// the crash is what matters: loud, and where it landed, not where it was thrown from
	const FVector Where = Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetActorLocation();
	AFacilityTenant::ReportNoise(GetWorld(), Where, NoiseRadius, Thrower.Get());
	UE_LOG(LogTheFacility, Log, TEXT("%s breaks at %s, heard %.0f cm around"), *ItemId.ToString(), *Where.ToCompactString(), NoiseRadius);
	LastBreakLocation = Where;
	++BreakCount;
	Multicast_Break(Where);
	SetLifeSpan(0.5f);
}

void AFacilityThrown::Multicast_Break_Implementation(FVector_NetQuantize Location)
{
	++BreaksSeen;
	Look->SetVisibility(false);
	Movement->StopMovementImmediately();
	FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(ItemId == AFacilityCharacter::CanId ? TEXT("Can") : TEXT("Bottle"))), Location, NoiseRadius, 0.9f, FMath::FRandRange(0.94f, 1.06f));
}

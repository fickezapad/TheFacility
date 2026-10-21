// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityDoor.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityTenant.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

const FName AFacilityDoor::MasterKeyId(TEXT("MasterKey"));

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

	static ConstructorHelpers::FObjectFinder<USoundBase> CreakAsset(TEXT("/Game/Facility/Audio/S_DoorOpen.S_DoorOpen"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SlamAsset(TEXT("/Game/Facility/Audio/S_DoorSlam.S_DoorSlam"));
	CreakSound = CreakAsset.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> CloseAsset(TEXT("/Game/Facility/Audio/S_DoorClose.S_DoorClose"));
	CloseSound = CloseAsset.Object;
	SlamSound = SlamAsset.Object;

	// placeholder slab, 100cm wide and 210cm tall, swinging from its left edge
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		DoorMesh->SetStaticMesh(CubeMesh.Object);
		DoorMesh->SetRelativeLocation(FVector(0.0f, 50.0f, 105.0f));
		DoorMesh->SetRelativeScale3D(FVector(0.1f, 1.0f, 2.1f));
	}
	DoorMesh->SetMaterial(0, FacilityLook::Material(TEXT("MI_Retro_Door")));

	// brass knobs on both faces, near the free edge; they swing with the slab
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const float Out = Face == 0 ? 8.0f : -8.0f;
		FacilityLook::AddPiece(this, Face == 0 ? TEXT("Look_KnobFront") : TEXT("Look_KnobBack"), Hinge, NAME_None, TEXT("Sphere"), TEXT("MI_Retro_Brass"),
			FVector(Out, 88.0f, 100.0f), FRotator::ZeroRotator, FVector(0.065f), true);
		FacilityLook::AddPiece(this, Face == 0 ? TEXT("Look_PlateFront") : TEXT("Look_PlateBack"), Hinge, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Brass"),
			FVector(Out * 0.7f, 88.0f, 96.0f), FRotator::ZeroRotator, FVector(0.01f, 0.05f, 0.16f), true);
	}
}

void AFacilityDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityDoor, bOpen);
	DOREPLIFETIME(AFacilityDoor, bLocked);
	DOREPLIFETIME(AFacilityDoor, bSwingReversed);
	DOREPLIFETIME(AFacilityDoor, bBroken);
	DOREPLIFETIME(AFacilityDoor, bHeldShut);
}

void AFacilityDoor::LockForNight()
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	if (bOpen)
	{
		bOpen = false;
		OpenAlpha = 0.0f;
		Hinge->SetRelativeRotation(FRotator::ZeroRotator);
		UpdateNavigationBlocking();
	}
	bLocked = true;
	bHeldShut = false;
}

bool AFacilityDoor::ClerkLock()
{
	if (!HasAuthority() || bOpen || bLocked || bBroken || bHeldShut)
	{
		return false;
	}
	bLocked = true;
	bClerkLocked = true;
	return true;
}

void AFacilityDoor::ClerkUnlock()
{
	if (HasAuthority() && bClerkLocked)
	{
		bClerkLocked = false;
		if (!bBroken)
		{
			bLocked = false;
		}
	}
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
		if (!bOpen)
		{
			FacilityAudio::PlayAt(this, CloseSound, GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), 1500.0f, 0.6f, FMath::FRandRange(0.94f, 1.06f));
		}
		SetActorTickEnabled(false);
		UpdateNavigationBlocking();
	}
}

void AFacilityDoor::UpdateNavigationBlocking()
{
	// a closed door never cuts the doorway (AI opens doors itself), but a slab standing open blocks whatever it swung
	// into, a walkway or a room, so paths go round it instead of pushing into it. Only while it rests fully open, so a
	// swing doesn't rebuild navigation every frame. Navigation lives on the server
	const bool bBlocks = bOpen && OpenAlpha >= 1.0f;
	if (HasAuthority() && DoorMesh->CanEverAffectNavigation() != bBlocks)
	{
		DoorMesh->SetCanEverAffectNavigation(bBlocks);
	}
}

void AFacilityDoor::OnRep_Open()
{
	StartSwing();
}

void AFacilityDoor::OnRep_Slammed()
{
	// a door that was already broken or held when this machine joined doesn't slam again
	if ((bHeldShut || bBroken) && GetGameTimeSinceCreation() > 1.0f)
	{
		PlaySlam();
	}
}

void AFacilityDoor::PlaySlam()
{
	FacilityAudio::PlayAt(this, SlamSound, GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), 3000.0f);
}

void AFacilityDoor::StartSwing()
{
	// each machine animates and hears its own swing
	// opening: the latch and the groan now; closing: the latch catches when it gets there
	if (bOpen)
	{
		FacilityAudio::PlayAt(this, CreakSound, GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), 1500.0f, 0.6f, FMath::FRandRange(0.94f, 1.06f));
	}
	SetActorTickEnabled(true);
	// closing: the slab stops blocking at once
	UpdateNavigationBlocking();
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
	ReportDoorNoise(SwingNoiseRadius, Opener);
}

void AFacilityDoor::ReportDoorNoise(float Radius, const APawn* Source)
{
	AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), Radius, Source);
}

bool AFacilityDoor::CanToggleLock(const APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return Character && Character->IsStanding() && Character->HasKey(MasterKeyId) && !bOpen && !bBroken;
}

void AFacilityDoor::ToggleLock(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !CanToggleLock(InstigatorPawn))
	{
		return;
	}

	bLocked = !bLocked;
	bHeldShut = false;
	ReportDoorNoise(LockNoiseRadius, InstigatorPawn);
	UE_LOG(LogTheFacility, Log, TEXT("%s %s %s with the master key"), *GetNameSafe(InstigatorPawn), bLocked ? TEXT("locked") : TEXT("unlocked"), *GetName());
}

void AFacilityDoor::BreakOpen(APawn* Breaker)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	bBroken = true;
	PlaySlam();
	bLocked = false;
	bHeldShut = false;
	if (!bOpen)
	{
		Open(Breaker);
	}
	ReportDoorNoise(BreakNoiseRadius, Breaker);
}

void AFacilityDoor::HoldShut(float Duration)
{
	if (!HasAuthority() || bBroken || bLocked)
	{
		return;
	}

	if (bOpen)
	{
		bOpen = false;
		StartSwing();
	}
	bLocked = true;
	bHeldShut = true;
	PlaySlam();
	ReportDoorNoise(SlamNoiseRadius, nullptr);
	GetWorldTimerManager().SetTimer(HoldTimer, this, &AFacilityDoor::ReleaseHold, FMath::Max(Duration, 0.1f), false);
}

void AFacilityDoor::ReleaseHold()
{
	if (bHeldShut && bLocked && !bBroken)
	{
		bLocked = false;
		UE_LOG(LogTheFacility, Log, TEXT("The Facility lets go of %s"), *GetName());
	}
	bHeldShut = false;
}

void AFacilityDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HoldTimer);

	Super::EndPlay(EndPlayReason);
}

bool AFacilityDoor::PawnHasKey(const APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return Character && ((!RequiredKeyId.IsNone() && Character->HasKey(RequiredKeyId)) || Character->HasKey(MasterKeyId));
}

bool AFacilityDoor::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return !bBroken && (!bLocked || PawnHasKey(InstigatorPawn));
}

FText AFacilityDoor::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	if (bBroken)
	{
		return NSLOCTEXT("Facility", "DoorBroken", "Broken");
	}

	if (bLocked)
	{
		if (PawnHasKey(InstigatorPawn))
		{
			return NSLOCTEXT("Facility", "DoorUnlock", "Unlock");
		}
		return bHeldShut ? NSLOCTEXT("Facility", "DoorHeldShut", "Something holds it shut") : NSLOCTEXT("Facility", "DoorLocked", "Locked");
	}

	return bOpen ? NSLOCTEXT("Facility", "DoorClose", "Close") : NSLOCTEXT("Facility", "DoorOpen", "Open");
}

void AFacilityDoor::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority() || bBroken)
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
		bHeldShut = false;
		Open(InstigatorPawn);
		return;
	}

	if (bOpen)
	{
		bOpen = false;
		StartSwing();
		ReportDoorNoise(SwingNoiseRadius, InstigatorPawn);
		return;
	}

	Open(InstigatorPawn);
}

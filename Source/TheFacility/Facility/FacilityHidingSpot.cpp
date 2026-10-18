// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityHidingSpot.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDirector.h"
#include "Facility/FacilityTenant.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityHidingSpot::AFacilityHidingSpot()
{
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	// placeholder wardrobe: 110 cm wide, 60 deep, 210 tall, standing on the floor, its doors facing +X
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
	Mesh->SetRelativeScale3D(FVector(0.6f, 1.1f, 2.1f));

	static ConstructorHelpers::FObjectFinder<USoundBase> CreakAsset(TEXT("/Game/Facility/Audio/S_ClosetCreak.S_ClosetCreak"));
	CreakSound = CreakAsset.Object;
}

void AFacilityHidingSpot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityHidingSpot, Occupant);
	DOREPLIFETIME(AFacilityHidingSpot, ReplicatedEnteredTime);
}

void AFacilityHidingSpot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StayTimer);
	GetWorldTimerManager().ClearTimer(CrawlTimer);

	Super::EndPlay(EndPlayReason);
}

FVector AFacilityHidingSpot::GetSpotPoint(float InFront, float AboveFloor) const
{
	// the mesh is a centred cube: its front face is half its depth ahead of the actor, its floor half its height below
	// the mesh is the root, so its world scale is the whole story
	const FVector Scale = Mesh->GetComponentScale();
	const FVector Local(Scale.X * 50.0f + InFront, 0.0f, -Scale.Z * 50.0f + AboveFloor);
	return GetActorTransform().TransformPositionNoScale(Local);
}

FTransform AFacilityHidingSpot::GetEyeTransform() const
{
	// from a closet you peer through the doors at standing height; from under a bed, along the floor
	return Kind == EFacilityHidingKind::Closet
		? FTransform(GetActorRotation(), GetSpotPoint(5.0f, 160.0f))
		: FTransform(GetActorRotation(), GetSpotPoint(5.0f, 22.0f));
}

float AFacilityHidingSpot::GetTimeLeft() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!Occupant || !GameState)
	{
		return 0.0f;
	}
	return FMath::Max(MaxStayTime - static_cast<float>(GameState->GetServerWorldTimeSeconds() - ReplicatedEnteredTime), 0.0f);
}

bool AFacilityHidingSpot::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return Character && Character->IsStanding() && !Character->IsHiding() && !Occupant;
}

FText AFacilityHidingSpot::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	if (Occupant)
	{
		return NSLOCTEXT("Facility", "HidingTaken", "Someone is in there");
	}
	return Kind == EFacilityHidingKind::Closet ? NSLOCTEXT("Facility", "HideCloset", "Hide in the closet") : NSLOCTEXT("Facility", "HideBed", "Hide under the bed");
}

void AFacilityHidingSpot::Interact_Implementation(APawn* InstigatorPawn)
{
	if (AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn))
	{
		Enter(Character);
	}
}

bool AFacilityHidingSpot::Enter(AFacilityCharacter* Character)
{
	if (!HasAuthority() || !Character || !CanInteract_Implementation(Character))
	{
		return false;
	}

	Occupant = Character;
	EnteredTime = GetWorld()->GetTimeSeconds();
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	ReplicatedEnteredTime = GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : 0.0f;
	LeavingAt = 0.0;

	// the body goes where the eye is, so whatever is computed from the pawn (sounds, relevancy, the Director's aim) is here too
	// moved first, then hidden: a teleport can switch the movement mode to falling, and with collision off that never lands
	Character->TeleportTo(GetSpotPoint(-40.0f, 97.0f), GetActorRotation(), false, true);
	Character->SetHidingSpot(this);
	Creak(Character);

	UE_LOG(LogTheFacility, Log, TEXT("%s hides in %s"), Character->GetPlayerState() ? *Character->GetPlayerState()->GetPlayerName() : *Character->GetName(), *GetName());
	GetWorldTimerManager().SetTimer(StayTimer, this, &AFacilityHidingSpot::ForceOut, MaxStayTime, false);
	return true;
}

void AFacilityHidingSpot::RequestLeave(AFacilityCharacter* Character)
{
	if (!HasAuthority() || !Character || Character != Occupant || LeavingAt > 0.0)
	{
		return;
	}

	if (Kind == EFacilityHidingKind::UnderBed && CrawlOutTime > 0.0f)
	{
		LeavingAt = GetWorld()->GetTimeSeconds() + CrawlOutTime;
		GetWorldTimerManager().SetTimer(CrawlTimer, this, &AFacilityHidingSpot::FinishCrawl, CrawlOutTime, false);
		return;
	}

	Evict(TEXT("came out"));
}

void AFacilityHidingSpot::FinishCrawl()
{
	Evict(TEXT("crawled out"));
}

void AFacilityHidingSpot::Evict(const FString& Why)
{
	AFacilityCharacter* Character = Occupant;
	if (!HasAuthority() || !Character)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(StayTimer);
	GetWorldTimerManager().ClearTimer(CrawlTimer);
	Occupant = nullptr;
	LeavingAt = 0.0;

	Character->SetHidingSpot(nullptr);
	Character->TeleportTo(GetSpotPoint(70.0f, 100.0f), GetActorRotation(), false, false);
	Creak(Character);

	UE_LOG(LogTheFacility, Log, TEXT("%s leaves %s: %s"), Character->GetPlayerState() ? *Character->GetPlayerState()->GetPlayerName() : *Character->GetName(), *GetName(), *Why);
}

void AFacilityHidingSpot::ForceOut()
{
	if (!Occupant)
	{
		return;
	}

	// the Facility won't have anyone hide from it for long
	AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation(), ForcedNoiseRadius, nullptr);
	if (AFacilityDirector* Director = AFacilityDirector::Get(GetWorld()))
	{
		Director->AddAttention(ForcedOutAttention, FString::Printf(TEXT("%s hid too long"), *GetNameSafe(Occupant)));
	}
	Evict(TEXT("forced out by the Facility"));
}

void AFacilityHidingSpot::Creak(const AActor* Source)
{
	PlayCreak();
	if (Kind == EFacilityHidingKind::Closet && CreakNoiseRadius > 0.0f)
	{
		AFacilityTenant::ReportNoise(GetWorld(), GetSpotPoint(60.0f, 100.0f), CreakNoiseRadius, Source);
	}
}

void AFacilityHidingSpot::PlayCreak()
{
	if (Kind == EFacilityHidingKind::Closet)
	{
		FacilityAudio::PlayAt(this, CreakSound, GetSpotPoint(10.0f, 120.0f), 1200.0f, 0.9f, FMath::FRandRange(0.92f, 1.08f));
	}
}

void AFacilityHidingSpot::OnRep_Occupant()
{
	// the character applies its own hidden state; here only the door is heard
	PlayCreak();
}

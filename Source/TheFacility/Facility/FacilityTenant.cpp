// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityTenant.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDirector.h"
#include "Facility/FacilityHidingSpot.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityTenantAIController.h"
#include "Animation/AnimInstance.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityTenant::AFacilityTenant()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	AIControllerClass = AFacilityTenantAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// broad and stooped, so nobody mistakes it for a player or for The Marked
	GetCapsuleComponent()->InitCapsuleSize(45.0f, 95.0f);
	// interaction and camera traces pass through it
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> BodyAnim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (BodyMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(BodyMesh.Object);
	}
	if (BodyAnim.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(BodyAnim.Class);
	}
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -95.0f), FRotator(0.0f, -90.0f, 0.0f));
	// placeholder body: the mannequin squashed short and wide
	GetMesh()->SetRelativeScale3D(FVector(1.45f, 1.3f, 1.0f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 240.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 150.0f;

	// placeholder footsteps: heavier and lower than The Marked's, and they carry further
	FootstepAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("FootstepAudio"));
	FootstepAudio->SetupAttachment(GetCapsuleComponent());
	FootstepAudio->SetRelativeLocation(FVector(0.0f, 0.0f, -80.0f));
	FootstepAudio->bAutoActivate = false;
	FootstepAudio->VolumeMultiplier = 1.4f;
	FootstepAudio->bEnableLowPassFilter = true;
	FootstepAudio->LowPassFilterFrequency = 200.0f;
	FootstepAudio->bOverrideAttenuation = true;
	FootstepAudio->AttenuationOverrides.bAttenuate = true;
	FootstepAudio->AttenuationOverrides.bSpatialize = true;
	FootstepAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(200.0f);
	FootstepAudio->AttenuationOverrides.FalloffDistance = 2500.0f;

	CryAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("CryAudio"));
	CryAudio->SetupAttachment(GetCapsuleComponent());
	CryAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	CryAudio->bAutoActivate = false;
	CryAudio->VolumeMultiplier = 0.9f;
	CryAudio->bEnableLowPassFilter = true;
	CryAudio->LowPassFilterFrequency = 900.0f;
	CryAudio->bOverrideAttenuation = true;
	CryAudio->AttenuationOverrides.bAttenuate = true;
	CryAudio->AttenuationOverrides.bSpatialize = true;
	CryAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(200.0f);
	CryAudio->AttenuationOverrides.FalloffDistance = 3000.0f;

	// generated sounds (Saved/QA/make_sounds.py). Heard through walls, but muffled
	static ConstructorHelpers::FObjectFinder<USoundBase> StepAsset(TEXT("/Game/Facility/Audio/S_TenantStep.S_TenantStep"));
	static ConstructorHelpers::FObjectFinder<USoundBase> KnockAsset(TEXT("/Game/Facility/Audio/S_TenantKnock.S_TenantKnock"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CryAsset(TEXT("/Game/Facility/Audio/S_TenantCry.S_TenantCry"));
	StepSound = StepAsset.Object;
	KnockSound = KnockAsset.Object;
	CrySound = CryAsset.Object;
	FootstepAudio->SetSound(StepSound);
	CryAudio->SetSound(CrySound);
	for (UAudioComponent* Audio : { FootstepAudio.Get(), CryAudio.Get() })
	{
		Audio->bEnableLowPassFilter = false;
		Audio->AttenuationOverrides.bEnableOcclusion = true;
		Audio->AttenuationOverrides.OcclusionTraceChannel = ECC_Visibility;
		Audio->AttenuationOverrides.OcclusionLowPassFilterFrequency = 700.0f;
		Audio->AttenuationOverrides.OcclusionVolumeAttenuation = 0.5f;
	}
}

void AFacilityTenant::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityTenant, State);
}

void AFacilityTenant::BeginPlay()
{
	Super::BeginPlay();

	HomeTransform = GetActorTransform();
	ApplyPresence();

	if (!HasAuthority())
	{
		return;
	}

	AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!GameState)
	{
		UE_LOG(LogTheFacility, Error, TEXT("%s needs AFacilityGameState. Set the level's GameMode Override to a FacilityGameMode Blueprint. The Tenant stays dormant."), *GetName());
		return;
	}

	GameState->OnSignatureRecorded.AddUniqueDynamic(this, &AFacilityTenant::HandleSignatureRecorded);
	GameState->OnPowerRestored.AddUniqueDynamic(this, &AFacilityTenant::HandlePowerRestored);
}

void AFacilityTenant::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StateTimer);

	Super::EndPlay(EndPlayReason);
}

void AFacilityTenant::ReportNoise(UWorld* World, const FVector& Location, float Radius, const AActor* Source, bool bCountForDirector)
{
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f)
	{
		return;
	}

	// the Facility hears everything the Tenant could, and keeps count; its own pointing doesn't count
	if (AFacilityDirector* Director = AFacilityDirector::Get(World); Director && bCountForDirector && Source != Director)
	{
		Director->HearNoise(Radius);
	}

	for (TActorIterator<AFacilityTenant> It(World); It; ++It)
	{
		if (*It != Source)
		{
			It->HearNoise(Location, Radius, Source);
		}
	}
}

void AFacilityTenant::HearNoise(const FVector& Location, float Radius, const AActor* Source)
{
	if (!IsPresent() || IsSated())
	{
		return;
	}

	const float Distance = FVector::Dist(GetActorLocation(), Location);
	if (Distance > Radius)
	{
		return;
	}

	// louder = closer relative to how far the sound carries. A fresh, much quieter sound doesn't pull it off the one it follows
	const float Loudness = 1.0f - Distance / Radius;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bCurrentIsOld = !bHasNoise || Now - NoiseTime > NoiseMemory;
	if (!bCurrentIsOld && Loudness < NoiseLoudness * DistractionRatio)
	{
		return;
	}

	UE_LOG(LogTheFacility, Verbose, TEXT("The Tenant hears %s at %.0f cm (loudness %.2f)"), *GetNameSafe(Source), Distance, Loudness);
	bHasNoise = true;
	bNoiseIsNew = true;
	NoiseLocation = Location;
	NoiseLoudness = Loudness;
	NoiseTime = Now;
}

bool AFacilityTenant::TakeNewNoise(FVector& OutLocation)
{
	if (!bHasNoise || !bNoiseIsNew)
	{
		return false;
	}

	bNoiseIsNew = false;
	OutLocation = NoiseLocation;
	return true;
}

bool AFacilityTenant::IsSated() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < SatedUntil;
}

void AFacilityTenant::HandleSignatureRecorded(const FFacilitySignature& Signature)
{
	if (HasAuthority() && ArmedBySignatureIds.Contains(Signature.SignatureId))
	{
		Wake(ManifestDelay, FString::Printf(TEXT("%s signed %s"), *Signature.SignerName, *Signature.SignatureId.ToString()));
	}
}

void AFacilityTenant::HandlePowerRestored()
{
	if (HasAuthority() && bWakeOnPowerRestored)
	{
		Wake(PowerWakeDelay, TEXT("the power came back"));
	}
}

void AFacilityTenant::Wake(float Delay, const FString& Reason)
{
	if (bArmed || bShutDown)
	{
		return;
	}

	bArmed = true;
	UE_LOG(LogTheFacility, Log, TEXT("The Tenant wakes: %s. Comes out in %.1f s"), *Reason, Delay);
	SetTenantState(ETenantState::Manifesting);
	GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityTenant::Appear, FMath::Max(Delay, 0.01f), false);
}

void AFacilityTenant::HearVoiceFromHiding(AFacilityCharacter* Talker, AFacilityHidingSpot* Spot, float Range)
{
	if (!HasAuthority() || !Talker || !Spot || !IsPresent() || IsSated() || State == ETenantState::Breaking)
	{
		return;
	}

	if (FVector::Dist2D(GetActorLocation(), Spot->GetSpotPoint(60.0f, 100.0f)) > Range)
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Tenant hears a voice inside %s and pulls %s out"), *Spot->GetName(), *GetNameSafe(Talker));
	Spot->Evict(TEXT("heard talking by the Tenant"));
	CatchPlayer(Talker);
}

void AFacilityTenant::WakeNow(const FString& Reason)
{
	if (HasAuthority())
	{
		Wake(1.5f, Reason);
	}
}

void AFacilityTenant::Appear()
{
	if (bShutDown)
	{
		return;
	}

	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetTenantState(ETenantState::Wandering);
}

void AFacilityTenant::CatchPlayer(AFacilityCharacter* Victim)
{
	if (!HasAuthority() || !Victim || !Victim->IsStanding() || !IsPresent())
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Tenant caught %s"), Victim->GetPlayerState() ? *Victim->GetPlayerState()->GetPlayerName() : *Victim->GetName());
	Victim->SetDowned(true);

	// it got what it came for: deaf for a while, wandering back toward its room
	SatedUntil = GetWorld()->GetTimeSeconds() + SatedTime;
	ForgetNoise();
	SetTenantState(ETenantState::Wandering);
}

void AFacilityTenant::ShutDown()
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	bShutDown = true;
	GetWorldTimerManager().ClearTimer(StateTimer);
	ForgetNoise();
	SetTenantState(ETenantState::Dormant);
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTheFacility, Log, TEXT("The Tenant: the run is over, back in its room for good"));
}

void AFacilityTenant::SetTenantState(ETenantState NewState)
{
	if (!HasAuthority() || State == NewState)
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Tenant: %s -> %s"),
		*StaticEnum<ETenantState>()->GetNameStringByValue((int64)State),
		*StaticEnum<ETenantState>()->GetNameStringByValue((int64)NewState));

	State = NewState;
	ApplyPresence();
}

void AFacilityTenant::OnRep_State()
{
	ApplyPresence();
}

void AFacilityTenant::ApplyPresence()
{
	const bool bPresent = IsPresent();

	// it is the same for everyone, so replicated hiding is fine; set on clients too so a late state change shows at once
	SetActorHiddenInGame(!bPresent);
	SetActorEnableCollision(bPresent);

	if (HasAuthority())
	{
		if (bPresent && State != ETenantState::Listening && State != ETenantState::Breaking)
		{
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		else if (!bPresent)
		{
			GetCharacterMovement()->StopMovementImmediately();
			GetCharacterMovement()->DisableMovement();
		}
	}
}

void AFacilityTenant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateEvidence(DeltaSeconds);
	}
}

void AFacilityTenant::UpdateEvidence(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();

	if (FootstepAudio->IsPlaying() && Now >= StepStopTime)
	{
		FootstepAudio->Stop();
	}
	if (CryAudio->IsPlaying() && Now >= CryStopTime)
	{
		CryAudio->Stop();
	}

	// the cry: once, each time it starts running at a sound
	if (State != CosmeticState)
	{
		if (State == ETenantState::Rushing && CryAudio->Sound)
		{
			CryAudio->SetPitchMultiplier(FMath::FRandRange(0.92f, 1.08f));
			CryAudio->Play();
			CryStopTime = Now + 2.0f;
		}
		CosmeticState = State;
	}

	if (!IsPresent() || !FootstepAudio->Sound)
	{
		return;
	}

	// beating on a locked door: slow, heavy knocks
	if (State == ETenantState::Breaking)
	{
		KnockCooldown -= DeltaSeconds;
		if (KnockCooldown <= 0.0f)
		{
			KnockCooldown = FMath::FRandRange(0.45f, 0.75f);
			FootstepAudio->SetSound(KnockSound);
			FootstepAudio->SetPitchMultiplier(FMath::FRandRange(0.9f, 1.05f));
			FootstepAudio->Play();
			StepStopTime = Now + 0.5f;
		}
		return;
	}

	DistanceSinceStep += GetVelocity().Size2D() * DeltaSeconds;
	if (DistanceSinceStep >= StrideLength)
	{
		DistanceSinceStep = 0.0f;
		FootstepAudio->SetSound(StepSound);
		FootstepAudio->SetPitchMultiplier(FMath::FRandRange(0.85f, 1.05f));
		FootstepAudio->Play();
		StepStopTime = Now + 0.4f;
	}
}

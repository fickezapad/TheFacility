// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMarked.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityMarkedAIController.h"
#include "Animation/AnimInstance.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "TheFacility.h"

AFacilityMarked::AFacilityMarked()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	AIControllerClass = AFacilityMarkedAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// tall and thin, so nobody mistakes it for a player
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 115.0f);
	// interaction and camera traces pass through it, or the mark would be blocked by something they can't see
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
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -115.0f), FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetRelativeScale3D(FVector(0.95f, 0.95f, 1.25f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 300.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 220.0f;

	// placeholder footsteps: a short, muffled burst of engine white noise, positioned in 3D
	FootstepAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("FootstepAudio"));
	FootstepAudio->SetupAttachment(GetCapsuleComponent());
	FootstepAudio->SetRelativeLocation(FVector(0.0f, 0.0f, -100.0f));
	FootstepAudio->bAutoActivate = false;
	FootstepAudio->VolumeMultiplier = 0.8f;
	FootstepAudio->bEnableLowPassFilter = true;
	FootstepAudio->LowPassFilterFrequency = 350.0f;
	FootstepAudio->bOverrideAttenuation = true;
	FootstepAudio->AttenuationOverrides.bAttenuate = true;
	FootstepAudio->AttenuationOverrides.bSpatialize = true;
	FootstepAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(150.0f);
	FootstepAudio->AttenuationOverrides.FalloffDistance = 1800.0f;

	static ConstructorHelpers::FObjectFinder<USoundBase> StepSound(TEXT("/Engine/EngineSounds/WhiteNoise.WhiteNoise"));
	if (StepSound.Succeeded())
	{
		FootstepAudio->SetSound(StepSound.Object);
	}
}

void AFacilityMarked::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityMarked, Mark);
	DOREPLIFETIME(AFacilityMarked, State);
}

void AFacilityMarked::BeginPlay()
{
	Super::BeginPlay();

	HomeTransform = GetActorTransform();

	if (GetNetMode() != NM_DedicatedServer)
	{
		// lights it can disturb: anything movable or stationary that isn't a sun
		for (TObjectIterator<ULocalLightComponent> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld() && It->Mobility != EComponentMobility::Static)
			{
				FDisturbedLight& Entry = NearbyLights.AddDefaulted_GetRef();
				Entry.Light = *It;
				Entry.BaseIntensity = It->Intensity;
			}
		}
	}

	ApplyPresence();

	if (HasAuthority())
	{
		BindToLedger();
	}
}

void AFacilityMarked::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StateTimer);
	GetWorldTimerManager().ClearTimer(StunTimer);
	RestoreLights();

	Super::EndPlay(EndPlayReason);
}

void AFacilityMarked::BindToLedger()
{
	AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!GameState)
	{
		UE_LOG(LogTheFacility, Error, TEXT("%s needs AFacilityGameState. Set the level's GameMode Override to a FacilityGameMode Blueprint. The creature stays dormant."), *GetName());
		return;
	}

	GameState->OnSignatureRecorded.AddUniqueDynamic(this, &AFacilityMarked::HandleSignatureRecorded);
}

void AFacilityMarked::HandleSignatureRecorded(const FFacilitySignature& Signature)
{
	// fires on every machine, but only the server decides
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	if (HastenedBySignatureIds.Contains(Signature.SignatureId))
	{
		WithdrawScale *= 0.5f;
		UE_LOG(LogTheFacility, Log, TEXT("The Marked: %s signed %s, it will return faster (withdraw %.1f s)"), *Signature.SignerName, *Signature.SignatureId.ToString(), WithdrawTime * WithdrawScale);
	}

	if (Signature.SignatureId != ArmedBySignatureId || !IsValid(Signature.Signer))
	{
		return;
	}

	if (bArmed)
	{
		// signed again (a dropped Offering picked up): the bond passes on only if the old mark is out of the run
		const AFacilityCharacter* OldPawn = HasValidMark() ? Cast<AFacilityCharacter>(Mark->GetPawn()) : nullptr;
		if (OldPawn && !OldPawn->IsRetained())
		{
			return;
		}

		UE_LOG(LogTheFacility, Log, TEXT("The Marked: the bond passes to %s"), *Signature.SignerName);
		Mark = Signature.Signer;
		GetWorldTimerManager().ClearTimer(StateTimer);
		SetMarkedState(EMarkedState::Dormant);
		SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		Manifest();
		return;
	}

	bArmed = true;
	Mark = Signature.Signer;
	UE_LOG(LogTheFacility, Log, TEXT("The Marked armed by %s (signed %s). Appears in %.1f s"), *Signature.SignerName, *Signature.SignatureId.ToString(), ManifestDelay);

	Manifest();
}

bool AFacilityMarked::HasValidMark() const
{
	// a player who left has a PlayerState pending destruction; IsValid sees that at once, unlike a null check
	return IsValid(Mark);
}

void AFacilityMarked::Manifest()
{
	SetMarkedState(EMarkedState::Manifesting);
	GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityMarked::Appear, FMath::Max(ManifestDelay, 0.01f), false);
}

void AFacilityMarked::Appear()
{
	if (!HasValidMark())
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Marked: its mark left the game, staying dormant"));
		SetMarkedState(EMarkedState::Dormant);
		return;
	}

	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetMarkedState(EMarkedState::Hunting);
}

void AFacilityMarked::CatchMark()
{
	if (!HasAuthority())
	{
		return;
	}

	if (AFacilityCharacter* Victim = GetHuntablePawn())
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Marked caught %s"), *Mark->GetPlayerName());
		Victim->SetDowned(true);
	}

	// withdraw: gone for everyone, back home, and return once the mark has been up for a while
	GetWorldTimerManager().ClearTimer(StunTimer);
	SetMarkedState(EMarkedState::Dormant);
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityMarked::TryReturn, 2.0f, false);
}

void AFacilityMarked::TryReturn()
{
	if (bShutDown)
	{
		return;
	}

	if (!HasValidMark())
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Marked: its mark left the game, staying dormant"));
		return;
	}

	// wait until the mark is back on their feet. A retained mark keeps it waiting until someone signs for its Offering again
	if (!GetHuntablePawn())
	{
		GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityMarked::TryReturn, 2.0f, false);
		return;
	}

	// then give them time to get away before it starts coming back
	const float Withdraw = WithdrawTime * WithdrawScale;
	UE_LOG(LogTheFacility, Log, TEXT("The Marked: %s is back up, returning in %.1f s"), *Mark->GetPlayerName(), Withdraw + ManifestDelay);
	GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityMarked::Manifest, FMath::Max(Withdraw, 0.01f), false);
}

void AFacilityMarked::Stun(float Duration)
{
	if (!HasAuthority() || !IsPresent() || State == EMarkedState::Stunned)
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Marked is stunned for %.1f s"), Duration);
	SetMarkedState(EMarkedState::Stunned);
	GetWorldTimerManager().SetTimer(StunTimer, this, &AFacilityMarked::EndStun, FMath::Max(Duration, 0.01f), false);
}

void AFacilityMarked::EndStun()
{
	if (State == EMarkedState::Stunned)
	{
		// it lost track while blinded
		SetMarkedState(EMarkedState::Searching);
	}
}

void AFacilityMarked::Hasten(const FString& Why)
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	WithdrawScale *= 0.5f;
	UE_LOG(LogTheFacility, Log, TEXT("The Marked: %s, it will return faster (withdraw %.1f s)"), *Why, WithdrawTime * WithdrawScale);
}

void AFacilityMarked::ShutDown()
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}

	bShutDown = true;
	GetWorldTimerManager().ClearTimer(StateTimer);
	GetWorldTimerManager().ClearTimer(StunTimer);
	SetMarkedState(EMarkedState::Dormant);
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTheFacility, Log, TEXT("The Marked: the run is over, withdrawn for good"));
}

void AFacilityMarked::SetMarkedState(EMarkedState NewState)
{
	if (!HasAuthority() || State == NewState)
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("The Marked: %s -> %s"),
		*StaticEnum<EMarkedState>()->GetNameStringByValue((int64)State),
		*StaticEnum<EMarkedState>()->GetNameStringByValue((int64)NewState));

	State = NewState;
	ApplyPresence();
}

AFacilityCharacter* AFacilityMarked::GetHuntablePawn() const
{
	AFacilityCharacter* Pawn = HasValidMark() ? Cast<AFacilityCharacter>(Mark->GetPawn()) : nullptr;
	return (Pawn && Pawn->IsStanding()) ? Pawn : nullptr;
}

float AFacilityMarked::GetDreadAt(const FVector& Location) const
{
	if (!IsPresent() || DreadRadius <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Clamp(1.0f - FVector::Dist(Location, GetActorLocation()) / DreadRadius, 0.0f, 1.0f);
}

bool AFacilityMarked::IsLocalPlayerMarked() const
{
	if (!HasValidMark())
	{
		return false;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController() && PC->PlayerState == Mark)
		{
			// a retained mark watches the others now, and sees it the way they do
			const AFacilityCharacter* MarkPawn = Cast<AFacilityCharacter>(PC->GetPawn());
			return !(MarkPawn && MarkPawn->IsRetained());
		}
	}

	return false;
}

void AFacilityMarked::OnRep_Presence()
{
	ApplyPresence();
}

void AFacilityMarked::ApplyPresence()
{
	const bool bPresent = IsPresent();

	SetActorEnableCollision(bPresent);

	if (HasAuthority())
	{
		if (bPresent && State != EMarkedState::Stunned)
		{
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		else
		{
			GetCharacterMovement()->StopMovementImmediately();
			GetCharacterMovement()->DisableMovement();
		}
	}

	// per machine: hidden for the mark, shown to everyone else. Component visibility doesn't replicate, which is the point
	SetLocallyVisible(bPresent && !IsLocalPlayerMarked());

	if (State == EMarkedState::Dormant)
	{
		RestoreLights();
	}
}

void AFacilityMarked::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// the mark or the local player state can replicate after the creature does, so keep checking
	SetLocallyVisible(IsPresent() && !IsLocalPlayerMarked());

	UpdateEvidence(DeltaSeconds);
}

void AFacilityMarked::SetLocallyVisible(bool bVisible)
{
	if (bVisible == bLocallyVisible)
	{
		return;
	}

	bLocallyVisible = bVisible;
	GetMesh()->SetVisibility(bLocallyVisible, true);

	// logged per machine, so a two-player test can confirm from the log who saw it and who didn't
	UE_LOG(LogTheFacility, Log, TEXT("The Marked is %s on the %s (state %s, local player is the mark: %s)"),
		bLocallyVisible ? TEXT("VISIBLE") : TEXT("hidden"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
		*StaticEnum<EMarkedState>()->GetNameStringByValue((int64)State),
		IsLocalPlayerMarked() ? TEXT("yes") : TEXT("no"));
}

void AFacilityMarked::UpdateEvidence(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();

	if (FootstepAudio->IsPlaying() && Now >= StepStopTime)
	{
		FootstepAudio->Stop();
	}

	if (State == EMarkedState::Dormant)
	{
		return;
	}

	// footsteps, heard by everyone including the mark
	if (IsPresent() && FootstepAudio->Sound)
	{
		DistanceSinceStep += GetVelocity().Size2D() * DeltaSeconds;
		if (DistanceSinceStep >= StrideLength)
		{
			DistanceSinceStep = 0.0f;
			FootstepAudio->SetPitchMultiplier(FMath::FRandRange(0.55f, 0.85f));
			FootstepAudio->Play();
			StepStopTime = Now + 0.09f;
		}
	}

	// failing lights around it, seen by everyone including the mark
	LightFlickerCooldown -= DeltaSeconds;
	if (LightFlickerCooldown > 0.0f)
	{
		return;
	}
	LightFlickerCooldown = FMath::FRandRange(0.05f, 0.14f);

	const FVector Here = GetActorLocation();
	for (FDisturbedLight& Entry : NearbyLights)
	{
		ULightComponent* Light = Entry.Light.Get();
		if (!Light)
		{
			continue;
		}

		const bool bNear = FVector::DistSquared(Here, Light->GetComponentLocation()) <= FMath::Square(LightDisturbRadius);
		if (bNear)
		{
			// mostly dim, sometimes almost out
			const float Scale = FMath::FRand() < 0.3f ? FMath::FRandRange(0.0f, 0.15f) : FMath::FRandRange(0.35f, 0.9f);
			Light->SetIntensity(Entry.BaseIntensity * Scale);
			Entry.bDisturbed = true;
		}
		else if (Entry.bDisturbed)
		{
			Light->SetIntensity(Entry.BaseIntensity);
			Entry.bDisturbed = false;
		}
	}
}

void AFacilityMarked::RestoreLights()
{
	for (FDisturbedLight& Entry : NearbyLights)
	{
		if (Entry.bDisturbed)
		{
			if (ULightComponent* Light = Entry.Light.Get())
			{
				Light->SetIntensity(Entry.BaseIntensity);
			}
			Entry.bDisturbed = false;
		}
	}
}

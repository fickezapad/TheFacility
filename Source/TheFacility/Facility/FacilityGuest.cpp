// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGuest.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityShift.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Navigation/PathFollowingComponent.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityGuest::AFacilityGuest()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;

	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->InitCapsuleSize(36.0f, 108.0f);
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
	// too tall and too thin, under a stained motel sheet that hangs to the floor
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -108.0f), FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetRelativeScale3D(FVector(0.82f, 0.82f, 1.2f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetMaterial(0, FacilityLook::Material(TEXT("MI_Motel_Bed")));
	GetMesh()->SetMaterial(1, FacilityLook::Material(TEXT("MI_Motel_Bed")));
	const FName Head(TEXT("head"));
	const FName Chest(TEXT("spine_05"));
	FacilityLook::AddPiece(this, TEXT("Look_SheetHead"), GetMesh(), Head, TEXT("Sphere"), TEXT("MI_Motel_Bed"), FVector(10.0f, 2.0f, 0.0f), FRotator::ZeroRotator, FVector(0.34f, 0.32f, 0.3f));
	// two dark holes where eyes would be, and the room tag on its chest
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FacilityLook::AddPiece(this, Side == 0 ? TEXT("Look_EyeL") : TEXT("Look_EyeR"), GetMesh(), Head, TEXT("Sphere"), TEXT("MI_Char_Marked"),
			FVector(12.0f, 15.5f, Side == 0 ? -5.0f : 5.0f), FRotator::ZeroRotator, FVector(0.04f, 0.02f, 0.035f));
	}
	FacilityLook::AddPiece(this, TEXT("Look_KeyTag"), GetMesh(), Chest, TEXT("Cube"), TEXT("MI_Retro_Brass"), FVector(0.0f, 22.0f, 0.0f), FRotator(0.0f, 0.0f, 45.0f), FVector(0.06f, 0.012f, 0.06f));

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = Speed;
	GetCharacterMovement()->MaxAcceleration = 4000.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 8000.0f;

	static ConstructorHelpers::FObjectFinder<USoundBase> ScrapeAsset(TEXT("/Game/Facility/Audio/S_GuestScrape.S_GuestScrape"));
	ScrapeSound = ScrapeAsset.Object;
}

void AFacilityGuest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityGuest, State);
	DOREPLIFETIME(AFacilityGuest, bFrozen);
}

void AFacilityGuest::BeginPlay()
{
	Super::BeginPlay();

	HomeTransform = GetActorTransform();
	OnRep_State();
	if (HasAuthority())
	{
		BindToLedger();
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &AFacilityGuest::Think, ThinkInterval, true);
	}
}

void AFacilityGuest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	GetWorldTimerManager().ClearTimer(StateTimer);
	Super::EndPlay(EndPlayReason);
}

void AFacilityGuest::BindToLedger()
{
	if (AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>())
	{
		GameState->OnSignatureRecorded.AddUniqueDynamic(this, &AFacilityGuest::HandleSignatureRecorded);
	}
	else
	{
		FTimerHandle Retry;
		GetWorldTimerManager().SetTimer(Retry, this, &AFacilityGuest::BindToLedger, 0.25f, false);
	}
}

void AFacilityGuest::SetNightBinding(const TArray<FName>& SignatureIds, bool bAwake)
{
	if (!HasAuthority())
	{
		return;
	}
	bAwakeTonight = bAwake;
	ArmedBy = bAwake ? SignatureIds : TArray<FName>();
	UE_LOG(LogTheFacility, Log, TEXT("The Guest in Room 0: %s tonight, woken by [%s]"), bAwake ? TEXT("awake") : TEXT("asleep"),
		*FString::JoinBy(ArmedBy, TEXT(", "), [](FName Id) { return Id.ToString(); }));
}

void AFacilityGuest::HandleSignatureRecorded(const FFacilitySignature& Signature)
{
	if (HasAuthority() && ArmedBy.Contains(Signature.SignatureId))
	{
		Wake(FString::Printf(TEXT("%s signed %s"), *Signature.SignerName, *Signature.SignatureId.ToString()));
	}
}

void AFacilityGuest::Wake(const FString& Reason)
{
	if (!HasAuthority() || bArmed || bShutDown)
	{
		return;
	}
	bArmed = true;
	UE_LOG(LogTheFacility, Log, TEXT("The Guest in Room 0 wakes: %s"), *Reason);
	SetGuestState(EGuestState::Manifesting);
	GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityGuest::Appear, ManifestDelay, false);
}

void AFacilityGuest::Appear()
{
	if (bShutDown)
	{
		return;
	}
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetGuestState(EGuestState::Hunting);
}

void AFacilityGuest::ShutDown()
{
	if (!HasAuthority() || bShutDown)
	{
		return;
	}
	bShutDown = true;
	GetWorldTimerManager().ClearTimer(StateTimer);
	if (AController* Brain = GetController())
	{
		Brain->StopMovement();
	}
	bFrozen = false;
	OnRep_Frozen();
	SetGuestState(EGuestState::Dormant);
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTheFacility, Log, TEXT("The Guest in Room 0: the run is over, the room is quiet"));
}

void AFacilityGuest::SetGuestState(EGuestState NewState)
{
	if (!HasAuthority() || State == NewState)
	{
		return;
	}
	UE_LOG(LogTheFacility, Log, TEXT("The Guest in Room 0: %s -> %s"), *StaticEnum<EGuestState>()->GetNameStringByValue((int64)State), *StaticEnum<EGuestState>()->GetNameStringByValue((int64)NewState));
	State = NewState;
	StateTime = 0.0f;
	OnRep_State();
}

void AFacilityGuest::OnRep_State()
{
	const bool bOut = IsPresent();
	SetActorHiddenInGame(!bOut);
	SetActorEnableCollision(bOut);
	if (bOut)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	else
	{
		GetCharacterMovement()->DisableMovement();
	}
}

void AFacilityGuest::OnRep_Frozen()
{
	// lit, it stops mid-step on every machine, its pose held
	GetMesh()->bPauseAnims = bFrozen;
}

bool AFacilityGuest::IsLit() const
{
	const FVector Body = GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
	for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
	{
		const AFacilityCharacter* Player = *It;
		if (!Player->IsStanding() || Player->IsHiding() || !Player->IsFlashlightOn())
		{
			continue;
		}
		const FVector Eye = Player->GetPawnViewLocation();
		const FVector ToBody = Body - Eye;
		if (ToBody.Size() > LightRange
			|| FVector::DotProduct(Player->GetBaseAimRotation().Vector(), ToBody.GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(LightHalfAngle)))
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityGuestLight), false, Player);
		Params.AddIgnoredActor(this);
		if (!GetWorld()->LineTraceTestByChannel(Eye, Body, ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

void AFacilityGuest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// a dry scrape of bare feet under the sheet, only while it moves
	if (IsPresent() && !bFrozen && GetVelocity().SizeSquared2D() > FMath::Square(60.0f) && GetNetMode() != NM_DedicatedServer)
	{
		ScrapeTimer -= DeltaSeconds;
		if (ScrapeTimer <= 0.0f)
		{
			ScrapeTimer = 0.3f;
			FacilityAudio::PlayAt(this, ScrapeSound, GetActorLocation() - FVector(0.0f, 0.0f, 100.0f), 1500.0f, 0.55f, FMath::FRandRange(0.9f, 1.1f));
		}
	}
}

void AFacilityGuest::Think()
{
	StateTime += ThinkInterval;
	if (bShutDown)
	{
		return;
	}
	if (State == EGuestState::Resting && StateTime >= RestTime)
	{
		Appear();
		return;
	}
	if (State != EGuestState::Hunting)
	{
		return;
	}

	AAIController* Brain = Cast<AAIController>(GetController());
	const bool bLit = IsLit();
	if (bLit != bFrozen)
	{
		bFrozen = bLit;
		OnRep_Frozen();
		if (bLit && Brain)
		{
			Brain->StopMovement();
			GetCharacterMovement()->StopMovementImmediately();
		}
	}
	if (bLit)
	{
		return;
	}

	// unwatched: straight for the nearest player still standing
	AFacilityCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (It->IsStanding() && !It->IsHiding() && Distance < NearestDistance)
		{
			Nearest = *It;
			NearestDistance = Distance;
		}
	}
	if (!Nearest)
	{
		return;
	}

	if (NearestDistance <= TakeRange && !FacilityShift::AreCreaturesPassive())
	{
		UE_LOG(LogTheFacility, Log, TEXT("The Guest in Room 0 takes %s"), Nearest->GetPlayerState() ? *Nearest->GetPlayerState()->GetPlayerName() : *Nearest->GetName());
		Nearest->SetDowned(true);
		if (Brain)
		{
			Brain->StopMovement();
		}
		SetGuestState(EGuestState::Resting);
		SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	if (Brain && (Brain->GetMoveStatus() == EPathFollowingStatus::Idle || FMath::Fmod(StateTime, 0.5f) < ThinkInterval))
	{
		Brain->MoveToLocation(Nearest->GetActorLocation(), 60.0f, true, true, true, false, nullptr, true);
	}
}

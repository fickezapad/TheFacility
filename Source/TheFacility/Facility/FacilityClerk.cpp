// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityClerk.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityShift.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TargetPoint.h"
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

namespace
{
	const FName ClerkPostTag(TEXT("ClerkPost"));
}

AFacilityClerk::AFacilityClerk()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	// heard and seen everywhere in the motel, even while it waits out of sight
	bAlwaysRelevant = true;

	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->InitCapsuleSize(38.0f, 96.0f);
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
	// upright, narrow, a little too tall: a clerk who has stood behind the desk for forty years
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -96.0f), FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetRelativeScale3D(FVector(0.92f, 0.92f, 1.08f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetMaterial(0, FacilityLook::Material(TEXT("MI_Char_Jacket_2")));
	GetMesh()->SetMaterial(1, FacilityLook::Material(TEXT("MI_Char_GuestSkin")));

	// a waistcoat-and-tie look, a flat cap, a brass name badge, and a lantern held out in the right hand
	const FName Head(TEXT("head"));
	const FName Neck(TEXT("neck_01"));
	const FName Chest(TEXT("spine_05"));
	const FName Hand(TEXT("hand_r"));
	FacilityLook::AddPiece(this, TEXT("Look_Cap"), GetMesh(), Head, TEXT("Cylinder"), TEXT("MI_Char_Pants"), FVector(20.0f, 1.0f, 0.0f), FRotator(0.0f, 0.0f, 90.0f), FVector(0.24f, 0.24f, 0.07f));
	FacilityLook::AddPiece(this, TEXT("Look_CapPeak"), GetMesh(), Head, TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(15.0f, 13.0f, 0.0f), FRotator::ZeroRotator, FVector(0.02f, 0.12f, 0.2f));
	FacilityLook::AddPiece(this, TEXT("Look_Tie"), GetMesh(), Neck, TEXT("Cube"), TEXT("MI_Motel_Ledger"), FVector(-4.0f, 12.0f, 0.0f), FRotator::ZeroRotator, FVector(0.16f, 0.025f, 0.05f));
	FacilityLook::AddPiece(this, TEXT("Look_Badge"), GetMesh(), Chest, TEXT("Cube"), TEXT("MI_Retro_Brass"), FVector(4.0f, 15.0f, -9.0f), FRotator::ZeroRotator, FVector(0.03f, 0.01f, 0.06f));
	FacilityLook::AddPiece(this, TEXT("Look_LanternBody"), GetMesh(), Hand, TEXT("Cylinder"), TEXT("MI_Retro_WindowLit"), FVector(-12.0f, 0.0f, -14.0f), FRotator(0.0f, 0.0f, 90.0f), FVector(0.09f, 0.09f, 0.16f));
	FacilityLook::AddPiece(this, TEXT("Look_LanternTop"), GetMesh(), Hand, TEXT("Cone"), TEXT("MI_Retro_Metal"), FVector(-12.0f, 0.0f, -4.0f), FRotator(0.0f, 0.0f, -90.0f), FVector(0.11f, 0.11f, 0.06f));

	Lamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(GetCapsuleComponent());
	Lamp->SetRelativeLocation(FVector(40.0f, 25.0f, 10.0f));
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->SetIntensityUnits(ELightUnits::Candelas);
	Lamp->SetIntensity(9.0f);
	Lamp->SetAttenuationRadius(LampRadius + 120.0f);
	Lamp->SetLightColor(FLinearColor(1.0f, 0.72f, 0.4f));
	Lamp->SetCastShadows(true);
	Lamp->SetVisibility(false);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 200.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = PatrolSpeed;

	static ConstructorHelpers::FObjectFinder<USoundBase> BellAsset(TEXT("/Game/Facility/Audio/S_Bell.S_Bell"));
	static ConstructorHelpers::FObjectFinder<USoundBase> StepAsset(TEXT("/Game/Facility/Audio/S_ClerkStep.S_ClerkStep"));
	BellSound = BellAsset.Object;
	StepSound = StepAsset.Object;
}

void AFacilityClerk::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityClerk, State);
}

void AFacilityClerk::BeginPlay()
{
	Super::BeginPlay();

	HomeTransform = GetActorTransform();
	OnRep_State();
	if (!HasAuthority())
	{
		return;
	}

	// its rounds (target points tagged ClerkPost), and the reception bell (tagged ReceptionBell), else where it waits
	BellLocation = GetActorLocation();
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->Tags.Contains(ClerkPostTag))
		{
			Posts.Add(It->GetActorLocation());
		}
		else if (It->Tags.Contains(FName(TEXT("ReceptionBell"))))
		{
			BellLocation = It->GetActorLocation();
		}
	}
	Posts.Sort([](const FVector& A, const FVector& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	BindToLedger();
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AFacilityClerk::Think, ThinkInterval, true);
}

void AFacilityClerk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	GetWorldTimerManager().ClearTimer(StateTimer);
	Super::EndPlay(EndPlayReason);
}

void AFacilityClerk::BindToLedger()
{
	if (AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>())
	{
		GameState->OnSignatureRecorded.AddUniqueDynamic(this, &AFacilityClerk::HandleSignatureRecorded);
	}
	else
	{
		FTimerHandle Retry;
		GetWorldTimerManager().SetTimer(Retry, this, &AFacilityClerk::BindToLedger, 0.25f, false);
	}
}

void AFacilityClerk::SetNightBinding(const TArray<FName>& SignatureIds, bool bAwake)
{
	if (!HasAuthority())
	{
		return;
	}
	bAwakeTonight = bAwake;
	ArmedBy = bAwake ? SignatureIds : TArray<FName>();
	UE_LOG(LogTheFacility, Log, TEXT("The Night Clerk: %s tonight, woken by [%s]"), bAwake ? TEXT("awake") : TEXT("asleep"),
		*FString::JoinBy(ArmedBy, TEXT(", "), [](FName Id) { return Id.ToString(); }));
}

void AFacilityClerk::HandleSignatureRecorded(const FFacilitySignature& Signature)
{
	if (HasAuthority() && ArmedBy.Contains(Signature.SignatureId))
	{
		Wake(FString::Printf(TEXT("%s signed %s"), *Signature.SignerName, *Signature.SignatureId.ToString()));
	}
}

void AFacilityClerk::Wake(const FString& Reason)
{
	if (!HasAuthority() || bArmed || bShutDown)
	{
		return;
	}
	bArmed = true;
	UE_LOG(LogTheFacility, Log, TEXT("The Night Clerk wakes: %s. The bell rings"), *Reason);
	Multicast_RingBell(BellLocation);
	SetClerkState(EClerkState::Manifesting);
	GetWorldTimerManager().SetTimer(StateTimer, this, &AFacilityClerk::Appear, ManifestDelay, false);
}

void AFacilityClerk::Appear()
{
	if (bShutDown)
	{
		return;
	}
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	NextBell = FMath::FRandRange(25.0f, 40.0f);
	SetClerkState(EClerkState::Patrolling);
}

void AFacilityClerk::ShutDown()
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
	for (const TWeakObjectPtr<AFacilityDoor>& Door : LockedDoors)
	{
		if (Door.IsValid())
		{
			Door->ClerkUnlock();
		}
	}
	LockedDoors.Reset();
	SetClerkState(EClerkState::Dormant);
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTheFacility, Log, TEXT("The Night Clerk: the run is over, back behind the desk"));
}

void AFacilityClerk::SetClerkState(EClerkState NewState)
{
	if (!HasAuthority() || State == NewState)
	{
		return;
	}
	UE_LOG(LogTheFacility, Log, TEXT("The Night Clerk: %s -> %s"), *StaticEnum<EClerkState>()->GetNameStringByValue((int64)State), *StaticEnum<EClerkState>()->GetNameStringByValue((int64)NewState));
	State = NewState;
	StateTime = 0.0f;
	OnRep_State();
}

void AFacilityClerk::OnRep_State()
{
	// out of the night it isn't there at all; in it, its lamp is the first thing anyone sees
	const bool bPresent = IsPresent();
	SetActorHiddenInGame(!bPresent);
	SetActorEnableCollision(bPresent);
	Lamp->SetVisibility(bPresent);
	GetCharacterMovement()->MaxWalkSpeed = State == EClerkState::Approaching ? ApproachSpeed : PatrolSpeed;
	if (bPresent)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	else
	{
		GetCharacterMovement()->DisableMovement();
	}
}

void AFacilityClerk::Multicast_RingBell_Implementation(FVector_NetQuantize Where)
{
	// heard all over the motel, faintly through the walls
	FacilityAudio::PlayAt(this, BellSound, Where, 6000.0f, 0.8f, FMath::FRandRange(0.97f, 1.03f));
}

bool AFacilityClerk::IsInLamplight(const FVector& Location) const
{
	if (!IsPresent() || FVector::Dist(Lamp->GetComponentLocation(), Location) > LampRadius)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityClerkLamp), false, this);
	for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	return !GetWorld()->LineTraceTestByChannel(Lamp->GetComponentLocation(), Location, ECC_Visibility, Params);
}

float AFacilityClerk::GetStillTime(const AFacilityCharacter* Player) const
{
	const float* Found = StillFor.Find(Player);
	return Found ? *Found : 0.0f;
}

void AFacilityClerk::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// its steps: unhurried, everywhere the same
	if (IsPresent() && GetVelocity().SizeSquared2D() > FMath::Square(40.0f) && GetNetMode() != NM_DedicatedServer)
	{
		StepTimer -= DeltaSeconds;
		if (StepTimer <= 0.0f)
		{
			StepTimer = State == EClerkState::Approaching ? 0.45f : 0.6f;
			FacilityAudio::PlayAt(this, StepSound, GetActorLocation() - FVector(0.0f, 0.0f, 90.0f), 1400.0f, 0.6f, FMath::FRandRange(0.95f, 1.05f));
		}
	}
}

void AFacilityClerk::MoveTowards(const FVector& Location, float Acceptance)
{
	if (AAIController* Brain = Cast<AAIController>(GetController()))
	{
		Brain->MoveToLocation(Location, Acceptance, true, true, true, false, nullptr, true);
	}
}

void AFacilityClerk::Think()
{
	if (!IsPresent() || bShutDown)
	{
		return;
	}
	StateTime += ThinkInterval;
	UpdateDoors();

	// the bell, now and then, wherever it is
	NextBell -= ThinkInterval;
	if (NextBell <= 0.0f)
	{
		NextBell = FMath::FRandRange(25.0f, 40.0f);
		Multicast_RingBell(BellLocation);
	}

	if (State != EClerkState::Sated && TryTake())
	{
		return;
	}

	const AAIController* Brain = Cast<AAIController>(GetController());
	const bool bIdle = !Brain || Brain->GetMoveStatus() == EPathFollowingStatus::Idle;

	// a lit flashlight in sight draws it, unless it has just taken someone
	AFacilityCharacter* Lit = nullptr;
	float LitDistance = AttractRange;
	if (State != EClerkState::Sated)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityClerkSight), false, this);
		for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
			if (It->IsStanding() && !It->IsHiding() && It->IsFlashlightOn() && Distance < LitDistance)
			{
				FCollisionQueryParams Each = Params;
				Each.AddIgnoredActor(*It);
				if (!GetWorld()->LineTraceTestByChannel(GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), It->GetPawnViewLocation(), ECC_Visibility, Each))
				{
					Lit = *It;
					LitDistance = Distance;
				}
			}
		}
	}

	switch (State)
	{
	case EClerkState::Patrolling:
		if (Lit)
		{
			DrawnTo = Lit;
			SetClerkState(EClerkState::Approaching);
			MoveTowards(Lit->GetActorLocation(), 80.0f);
			break;
		}
		if (bIdle)
		{
			// a short look at each door before the next
			WaitAtPost -= ThinkInterval;
			if (WaitAtPost <= 0.0f && !Posts.IsEmpty())
			{
				int32 Next = FMath::RandRange(0, Posts.Num() - 1);
				if (Next == LastPost && Posts.Num() > 1)
				{
					Next = (Next + 1) % Posts.Num();
				}
				LastPost = Next;
				WaitAtPost = 2.5f;
				MoveTowards(Posts[Next], 60.0f);
			}
		}
		break;

	case EClerkState::Approaching:
	{
		AFacilityCharacter* Target = Lit ? Lit : DrawnTo.Get();
		// lost the light (it went dark, or it hid) for a while: back to its rounds
		if (!Lit && StateTime > 3.0f)
		{
			DrawnTo.Reset();
			SetClerkState(EClerkState::Patrolling);
			break;
		}
		if (Lit)
		{
			StateTime = 0.0f;
			DrawnTo = Lit;
		}
		if (Target && (bIdle || FMath::Fmod(StateTime, 0.5f) < ThinkInterval))
		{
			MoveTowards(Target->GetActorLocation(), 80.0f);
		}
		break;
	}

	case EClerkState::Sated:
		if (StateTime >= SatedTime)
		{
			SetClerkState(EClerkState::Patrolling);
		}
		else if (bIdle)
		{
			MoveTowards(HomeTransform.GetLocation(), 80.0f);
		}
		break;

	default:
		break;
	}
}

bool AFacilityClerk::TryTake()
{
	for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
	{
		AFacilityCharacter* Player = *It;
		const bool bStill = Player->IsStanding() && !Player->IsHiding() && Player->GetVelocity().Size2D() < StillSpeed;
		float& Still = StillFor.FindOrAdd(Player);
		if (bStill && IsInLamplight(Player->GetActorLocation()))
		{
			Still += ThinkInterval;
		}
		else
		{
			Still = FMath::Max(0.0f, Still - ThinkInterval * 2.0f);
		}

		if (Still >= StillTimeToTake && !FacilityShift::AreCreaturesPassive())
		{
			Still = 0.0f;
			UE_LOG(LogTheFacility, Log, TEXT("The Night Clerk takes %s, who stood still in its light"), Player->GetPlayerState() ? *Player->GetPlayerState()->GetPlayerName() : *Player->GetName());
			Player->SetDowned(true);
			Multicast_RingBell(BellLocation);
			DrawnTo.Reset();
			if (AController* Brain = GetController())
			{
				Brain->StopMovement();
			}
			SetClerkState(EClerkState::Sated);
			return true;
		}
	}
	return false;
}

void AFacilityClerk::UpdateDoors()
{
	const FVector Here = GetActorLocation();
	for (TActorIterator<AFacilityDoor> It(GetWorld()); It; ++It)
	{
		const bool bNear = FVector::Dist2D(It->GetActorLocation(), Here) <= DoorLockRange;
		if (bNear && It->ClerkLock())
		{
			LockedDoors.AddUnique(*It);
			UE_LOG(LogTheFacility, Verbose, TEXT("The Night Clerk passes %s: it locks"), *It->GetName());
		}
	}
	// past them again (a little further than it locks, so a door doesn't flicker)
	for (int32 Index = LockedDoors.Num() - 1; Index >= 0; --Index)
	{
		AFacilityDoor* Door = LockedDoors[Index].Get();
		if (!Door || FVector::Dist2D(Door->GetActorLocation(), Here) > DoorLockRange + 150.0f)
		{
			if (Door)
			{
				Door->ClerkUnlock();
			}
			LockedDoors.RemoveAt(Index);
		}
	}
}

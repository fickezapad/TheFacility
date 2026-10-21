// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityResident.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityContainer.h"
#include "Facility/FacilityHUD.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityShift.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

namespace
{
	constexpr int32 NumPieces = 8;
}

AFacilityResident::AFacilityResident()
{
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);

	// what is aimed at: a tall invisible box. Bodies pass through it and it never cuts the navmesh, so nothing gets
	// stuck on a figure that may not be there
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetStaticMesh(FacilityLook::Shape(TEXT("Cube")));
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	Body->SetRelativeScale3D(FVector(0.45f, 0.45f, 1.6f));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetCanEverAffectNavigation(false);
	Body->SetHiddenInGame(true);

	for (int32 Index = 0; Index < NumPieces; ++Index)
	{
		Pieces.Add(FacilityLook::AddPiece(this, *FString::Printf(TEXT("Look_%d"), Index), Pivot, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Curtain"),
			FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.01f), true));
	}
	for (const TCHAR* ShapeName : { TEXT("Sphere"), TEXT("Cube"), TEXT("Cylinder"), TEXT("Cone") })
	{
		LookAssets.Add(ShapeName, FacilityLook::Shape(ShapeName));
	}
	for (const TCHAR* MaterialName : { TEXT("MI_Retro_Curtain"), TEXT("MI_Char_GuestSkin"), TEXT("MI_Retro_Paper"), TEXT("MI_Char_Jacket_3"), TEXT("MI_Char_Pants"),
		TEXT("MI_Retro_WoodDark"), TEXT("MI_Retro_Paint"), TEXT("MI_Char_TenantSkin"), TEXT("MI_Motel_Bed") })
	{
		LookAssets.Add(MaterialName, FacilityLook::Material(MaterialName));
	}
	static ConstructorHelpers::FObjectFinder<USoundBase> GiggleAsset(TEXT("/Game/Facility/Audio/S_Giggle.S_Giggle"));
	static ConstructorHelpers::FObjectFinder<USoundBase> WhisperAsset(TEXT("/Game/Facility/Audio/S_ResidentWhisper.S_ResidentWhisper"));
	static ConstructorHelpers::FObjectFinder<USoundBase> StingerAsset(TEXT("/Game/Facility/Audio/S_Stinger.S_Stinger"));
	LookAssets.Add(TEXT("Giggle"), GiggleAsset.Object);
	LookAssets.Add(TEXT("Whisper"), WhisperAsset.Object);
	LookAssets.Add(TEXT("Stinger"), StingerAsset.Object);
}

void AFacilityResident::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityResident, bShown);
	DOREPLIFETIME(AFacilityResident, bVanished);
	DOREPLIFETIME(AFacilityResident, Reaction);
	DOREPLIFETIME(AFacilityResident, TurnYaw);
}

void AFacilityResident::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void AFacilityResident::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();
	OnRep_Shown();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &AFacilityResident::Think, 0.2f, true);
	}
}

void AFacilityResident::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	Super::EndPlay(EndPlayReason);
}

void AFacilityResident::ApplyLook()
{
	// positions in cm in the figure's frame: X is where it faces, Z up from the floor. Each piece: shape, material,
	// location, rotation, size in cm
	struct FPiece { const TCHAR* Shape; const TCHAR* Material; FVector Location; FRotator Rotation; FVector Size; };
	TArray<FPiece> Look;
	switch (Kind)
	{
	case EFacilityResidentKind::OldWoman:
		// stooped toward the wall, grey hair in a bun, a long dark dress and a shawl
		Look = {
			{ TEXT("Cone"), TEXT("MI_Retro_Curtain"), FVector(0, 0, 55), FRotator::ZeroRotator, FVector(56, 50, 110) },
			{ TEXT("Cylinder"), TEXT("MI_Retro_Curtain"), FVector(4, 0, 112), FRotator(-12, 0, 0), FVector(34, 30, 40) },
			{ TEXT("Cube"), TEXT("MI_Retro_WoodDark"), FVector(2, 0, 124), FRotator(-12, 0, 0), FVector(40, 46, 12) },
			{ TEXT("Sphere"), TEXT("MI_Char_GuestSkin"), FVector(14, 0, 140), FRotator::ZeroRotator, FVector(19, 17, 22) },
			{ TEXT("Sphere"), TEXT("MI_Retro_Paper"), FVector(6, 0, 147), FRotator::ZeroRotator, FVector(17, 18, 16) },
			{ TEXT("Sphere"), TEXT("MI_Retro_Paper"), FVector(-3, 0, 151), FRotator::ZeroRotator, FVector(10, 10, 10) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(10, -17, 95), FRotator(-8, 0, 0), FVector(7, 7, 38) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(10, 17, 95), FRotator(-8, 0, 0), FVector(7, 7, 38) } };
		break;
	case EFacilityResidentKind::BedSitter:
		// sitting on the edge of the bed, elbows on knees, head hanging
		Look = {
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(18, -10, 50), FRotator::ZeroRotator, FVector(44, 15, 14) },
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(18, 10, 50), FRotator::ZeroRotator, FVector(44, 15, 14) },
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(38, -10, 24), FRotator::ZeroRotator, FVector(13, 13, 48) },
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(38, 10, 24), FRotator::ZeroRotator, FVector(13, 13, 48) },
			{ TEXT("Cylinder"), TEXT("MI_Char_Jacket_3"), FVector(8, 0, 82), FRotator(-30, 0, 0), FVector(36, 30, 52) },
			{ TEXT("Sphere"), TEXT("MI_Char_GuestSkin"), FVector(30, 0, 108), FRotator::ZeroRotator, FVector(19, 18, 22) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(30, -16, 74), FRotator(-70, 0, 0), FVector(7, 7, 34) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(30, 16, 74), FRotator(-70, 0, 0), FVector(7, 7, 34) } };
		break;
	case EFacilityResidentKind::CarFigure:
		// crouched behind a parked car, only the head and shoulders over the hood, watching the motel
		Look = {
			{ TEXT("Cylinder"), TEXT("MI_Char_Pants"), FVector(0, 0, 60), FRotator::ZeroRotator, FVector(34, 30, 70) },
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(0, 0, 100), FRotator::ZeroRotator, FVector(26, 48, 16) },
			{ TEXT("Sphere"), TEXT("MI_Char_TenantSkin"), FVector(4, 0, 122), FRotator::ZeroRotator, FVector(19, 18, 23) },
			{ TEXT("Sphere"), TEXT("MI_Retro_WoodDark"), FVector(0, 0, 130), FRotator::ZeroRotator, FVector(20, 20, 12) },
			{ TEXT("Cylinder"), TEXT("MI_Char_Pants"), FVector(14, -20, 104), FRotator(-80, 0, 0), FVector(8, 8, 30) },
			{ TEXT("Cylinder"), TEXT("MI_Char_Pants"), FVector(14, 20, 104), FRotator(-80, 0, 0), FVector(8, 8, 30) },
			{ TEXT("Cube"), TEXT("MI_Char_TenantSkin"), FVector(30, -20, 106), FRotator::ZeroRotator, FVector(8, 6, 4) },
			{ TEXT("Cube"), TEXT("MI_Char_TenantSkin"), FVector(30, 20, 106), FRotator::ZeroRotator, FVector(8, 6, 4) } };
		break;
	case EFacilityResidentKind::PoolFigure:
		// thin, in a pale old bathing suit, long dark hair hanging over the face, standing in the deep end
		Look = {
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(0, -7, 42), FRotator::ZeroRotator, FVector(10, 10, 84) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(0, 7, 42), FRotator::ZeroRotator, FVector(10, 10, 84) },
			{ TEXT("Cylinder"), TEXT("MI_Motel_Bed"), FVector(0, 0, 112), FRotator::ZeroRotator, FVector(26, 30, 60) },
			{ TEXT("Sphere"), TEXT("MI_Char_GuestSkin"), FVector(0, 0, 158), FRotator::ZeroRotator, FVector(18, 17, 22) },
			{ TEXT("Cone"), TEXT("MI_Retro_WoodDark"), FVector(3, 0, 140), FRotator(180, 0, 0), FVector(26, 24, 56) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(0, -21, 108), FRotator::ZeroRotator, FVector(7, 7, 56) },
			{ TEXT("Cylinder"), TEXT("MI_Char_GuestSkin"), FVector(0, 21, 108), FRotator::ZeroRotator, FVector(7, 7, 56) },
			{ TEXT("Sphere"), TEXT("MI_Retro_WoodDark"), FVector(-4, 0, 166), FRotator::ZeroRotator, FVector(20, 20, 14) } };
		break;
	default:
		// a small figure in a pale yellow raincoat with the hood up, arms at its sides
		Look = {
			{ TEXT("Cone"), TEXT("MI_Retro_Paint"), FVector(0, 0, 45), FRotator::ZeroRotator, FVector(40, 36, 80) },
			{ TEXT("Cylinder"), TEXT("MI_Retro_Paint"), FVector(0, 0, 80), FRotator::ZeroRotator, FVector(24, 22, 26) },
			{ TEXT("Sphere"), TEXT("MI_Char_GuestSkin"), FVector(3, 0, 100), FRotator::ZeroRotator, FVector(16, 15, 18) },
			{ TEXT("Cone"), TEXT("MI_Retro_Paint"), FVector(-2, 0, 108), FRotator::ZeroRotator, FVector(26, 24, 26) },
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(0, -6, 5), FRotator::ZeroRotator, FVector(10, 8, 10) },
			{ TEXT("Cube"), TEXT("MI_Char_Pants"), FVector(0, 6, 5), FRotator::ZeroRotator, FVector(10, 8, 10) },
			{ TEXT("Cylinder"), TEXT("MI_Retro_Paint"), FVector(0, -16, 64), FRotator::ZeroRotator, FVector(7, 7, 30) },
			{ TEXT("Cylinder"), TEXT("MI_Retro_Paint"), FVector(0, 16, 64), FRotator::ZeroRotator, FVector(7, 7, 30) } };
		break;
	}

	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		UStaticMeshComponent* Piece = Pieces[Index];
		if (!Piece)
		{
			continue;
		}
		if (!Look.IsValidIndex(Index))
		{
			Piece->SetVisibility(false);
			continue;
		}
		const FPiece& P = Look[Index];
		Piece->SetStaticMesh(Cast<UStaticMesh>(LookAssets.FindRef(P.Shape)));
		Piece->SetMaterial(0, Cast<UMaterialInterface>(LookAssets.FindRef(P.Material)));
		Piece->SetRelativeLocationAndRotation(P.Location, P.Rotation);
		Piece->SetWorldScale3D(P.Size / 100.0f);
		Piece->SetVisibility(true);
	}
}

void AFacilityResident::SetShownTonight(bool bInScene, uint32 SeedHash)
{
	if (!HasAuthority())
	{
		return;
	}
	bShown = bInScene;
	bVanished = false;
	bTriggered = false;

	// the reaction, weighted: most are quiet or strange, a few whisper something useful, one in twenty jumps
	FRandomStream Stream(static_cast<int32>(SeedHash));
	const int32 Roll = Stream.RandRange(0, 99);
	Reaction = Roll < 15 ? EFacilityResidentReaction::Nothing : Roll < 35 ? EFacilityResidentReaction::Turn : Roll < 50 ? EFacilityResidentReaction::Giggle
		: Roll < 63 ? EFacilityResidentReaction::Whisper : Roll < 78 ? EFacilityResidentReaction::Vanish : Roll < 83 ? EFacilityResidentReaction::Jumpscare
		: EFacilityResidentReaction::Hint;
	OnRep_Shown();
}

void AFacilityResident::OnRep_Shown()
{
	const bool bVisible = bShown && !bVanished;
	SetActorHiddenInGame(!bVisible);
	Body->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AFacilityResident::OnRep_Turn()
{
	Pivot->SetRelativeRotation(FRotator(0.0f, TurnYaw, 0.0f));
}

bool AFacilityResident::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return IsShown() && !bTriggered;
}

FText AFacilityResident::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	return NSLOCTEXT("Facility", "ResidentLook", "Look closer");
}

void AFacilityResident::Interact_Implementation(APawn* InstigatorPawn)
{
	Trigger(InstigatorPawn);
}

void AFacilityResident::Think()
{
	if (!IsShown())
	{
		return;
	}

	if (!bTriggered)
	{
		for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsStanding() && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= TriggerRange)
			{
				Trigger(*It);
				return;
			}
		}
		return;
	}

	// gone the moment nobody is looking
	if (bVanishPending)
	{
		const FVector Here = GetActorLocation() + FVector(0.0f, 0.0f, 100.0f);
		for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
		{
			const FVector ToHere = Here - It->GetPawnViewLocation();
			if (It->IsStanding() && ToHere.Size() < 2500.0f
				&& FVector::DotProduct(It->GetBaseAimRotation().Vector(), ToHere.GetSafeNormal()) > FMath::Cos(FMath::DegreesToRadians(45.0f)))
			{
				return;
			}
		}
		bVanishPending = false;
		bVanished = true;
		OnRep_Shown();
		UE_LOG(LogTheFacility, Log, TEXT("%s is gone when nobody looks"), *GetName());
	}
}

void AFacilityResident::Trigger(APawn* Visitor)
{
	if (!HasAuthority() || !IsShown() || bTriggered)
	{
		return;
	}
	bTriggered = true;

	FString Words;
	switch (Reaction)
	{
	case EFacilityResidentReaction::Turn:
	case EFacilityResidentReaction::Jumpscare:
		if (Visitor)
		{
			// it turns its whole body to face whoever came
			const FVector Local = GetActorTransform().InverseTransformPosition(Visitor->GetActorLocation());
			TurnYaw = Local.Rotation().Yaw;
			OnRep_Turn();
		}
		break;
	case EFacilityResidentReaction::Whisper:
	{
		static const TCHAR* const Lines[] = {
			TEXT("Check-out was hours ago."),
			TEXT("Don't sign anything. Don't."),
			TEXT("You shouldn't be up this late."),
			TEXT("It hears you breathing, you know.") };
		Words = Lines[GetTypeHash(GetName()) % UE_ARRAY_COUNT(Lines)];
		break;
	}
	case EFacilityResidentReaction::Vanish:
		bVanishPending = true;
		Words = TEXT("Don't look away.");
		break;
	case EFacilityResidentReaction::Hint:
		Words = FindHint();
		break;
	default:
		break;
	}

	UE_LOG(LogTheFacility, Log, TEXT("%s (%s) reacts to %s: %s%s"), *GetName(), *StaticEnum<EFacilityResidentKind>()->GetNameStringByValue((int64)Kind),
		*GetNameSafe(Visitor), *StaticEnum<EFacilityResidentReaction>()->GetNameStringByValue((int64)Reaction), Words.IsEmpty() ? TEXT("") : *(TEXT(" \"") + Words + TEXT("\"")));
	Multicast_React(Reaction, Words);
}

FString AFacilityResident::FindHint() const
{
	// the nearest unsearched container hiding something the night needs, or else the nearest treasure lying out
	const AFacilityContainer* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AFacilityContainer> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (!It->IsSearched() && !It->GetContentKeyId().IsNone() && Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	if (Best)
	{
		const FString Area = Best->GetArea().ToString().Replace(TEXT("Room"), TEXT("room "));
		const FString What = Best->GetKind() == EFacilityContainerKind::Cart ? TEXT("the laundry cart") : Best->GetKind() == EFacilityContainerKind::Cabinet ? TEXT("the cabinet")
			: Best->GetKind() == EFacilityContainerKind::Desk ? TEXT("the desk") : TEXT("the dresser");
		return FString::Printf(TEXT("Look in %s. In the %s."), *What, *Area.ToLower());
	}
	for (TActorIterator<AFacilityPickup> It(GetWorld()); It; ++It)
	{
		if (FacilityShift::IsTreasure(It->GetGrantedKeyId()))
		{
			return FString::Printf(TEXT("Something pretty is waiting for you. Something %s."), *FacilityShift::NameOf(It->GetGrantedKeyId()).ToString().ToLower());
		}
	}
	return TEXT("Nothing left for you here.");
}

void AFacilityResident::Multicast_React_Implementation(EFacilityResidentReaction What, const FString& Words)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector Mouth = GetActorLocation() + FVector(0.0f, 0.0f, 130.0f);
	switch (What)
	{
	case EFacilityResidentReaction::Giggle:
		FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Giggle"))), Mouth, 1400.0f, 0.7f);
		break;
	case EFacilityResidentReaction::Whisper:
	case EFacilityResidentReaction::Vanish:
	case EFacilityResidentReaction::Hint:
		FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Whisper"))), Mouth, 900.0f, 0.8f);
		break;
	case EFacilityResidentReaction::Jumpscare:
	{
		// a shriek, and the figure lunges half a metre toward whoever came, then settles back
		FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Stinger"))), Mouth, 2000.0f, 1.0f);
		Pivot->SetRelativeLocation(FRotator(0.0f, TurnYaw, 0.0f).Vector() * 55.0f);
		TWeakObjectPtr<USceneComponent> WeakPivot = Pivot;
		FTimerHandle Back;
		GetWorldTimerManager().SetTimer(Back, FTimerDelegate::CreateLambda([WeakPivot]()
		{
			if (WeakPivot.IsValid())
			{
				WeakPivot->SetRelativeLocation(FVector::ZeroVector);
			}
		}), 0.6f, false);
		break;
	}
	default:
		break;
	}

	// the words reach whoever is close enough to hear them
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Listener = PC ? PC->GetPawn() : nullptr;
	if (!Words.IsEmpty() && Listener && FVector::Dist(Listener->GetActorLocation(), GetActorLocation()) <= 900.0f)
	{
		if (AFacilityHUD* HUD = Cast<AFacilityHUD>(PC->GetHUD()))
		{
			HUD->ShowLine(Words, 6.0f);
		}
	}
}

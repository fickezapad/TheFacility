// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityContainer.h"
#include "Facility/FacilityAudio.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityLook.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityTenant.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Facility/FacilityClue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityContainer::AFacilityContainer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetStaticMesh(FacilityLook::Shape(TEXT("Cube")));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	Drawer = FacilityLook::AddPiece(this, TEXT("Drawer"), RootComponent, NAME_None, TEXT("Cube"), nullptr, FVector::ZeroVector, FRotator::ZeroRotator, FVector::OneVector);
	Handle = FacilityLook::AddPiece(this, TEXT("Handle"), RootComponent, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Brass"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.02f, 0.12f, 0.025f));
	Trim = FacilityLook::AddPiece(this, TEXT("Trim"), RootComponent, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Paper"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.01f));
	Clue = CreateDefaultSubobject<UFacilityClueComponent>(TEXT("Clue"));
	Clue->SetupAttachment(RootComponent);

	for (const TCHAR* MaterialName : { TEXT("MI_Retro_WoodDark"), TEXT("MI_Motel_Wood"), TEXT("MI_Retro_Metal"), TEXT("MI_Retro_Rust"), TEXT("MI_Retro_Curtain"), TEXT("MI_Motel_Bed"), TEXT("MI_Retro_Paint"), TEXT("MI_Retro_Paper") })
	{
		LookAssets.Add(MaterialName, FacilityLook::Material(MaterialName));
	}
	static ConstructorHelpers::FObjectFinder<USoundBase> RummageAsset(TEXT("/Game/Facility/Audio/S_ClosetCreak.S_ClosetCreak"));
	LookAssets.Add(TEXT("Rummage"), RummageAsset.Object);
}

void AFacilityContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityContainer, bSearched);
	DOREPLIFETIME(AFacilityContainer, Searcher);
	DOREPLIFETIME(AFacilityContainer, SearchStartTime);
	DOREPLIFETIME(AFacilityContainer, ClueItem);
}

void AFacilityContainer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void AFacilityContainer::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();
	OnRep_ClueItem();
}

void AFacilityContainer::ApplyLook()
{
	const auto Material = [this](const TCHAR* Name) { return Cast<UMaterialInterface>(LookAssets.FindRef(Name)); };
	const FVector S = Size;
	const bool bCart = Kind == EFacilityContainerKind::Cart;
	const bool bMetal = Kind == EFacilityContainerKind::Cabinet;

	// the body stands on the floor; X is its front
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, S.Z * 0.5f));
	Body->SetRelativeScale3D(S / 100.0f);

	// searchable furniture reads apart from the rest: a painted front with a brass handle and a pale trim that glows a
	// little until it is searched (decor furniture is plain dark wood with no handle). In game each gets its own copies
	// of the front and trim materials, so the one aimed at can light up
	UMaterialInterface* FrontBase = Material(bCart ? TEXT("MI_Motel_Bed") : bMetal ? TEXT("MI_Retro_Rust") : TEXT("MI_Retro_Paint"));
	UMaterialInterface* TrimBase = Material(TEXT("MI_Retro_Paper"));
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (!DrawerMaterial && FrontBase)
		{
			DrawerMaterial = UMaterialInstanceDynamic::Create(FrontBase, this);
			FrontBase->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Tint")), FrontTint);
		}
		if (!TrimMaterial && TrimBase)
		{
			TrimMaterial = UMaterialInstanceDynamic::Create(TrimBase, this);
		}
	}
	Body->SetMaterial(0, Material(bCart ? TEXT("MI_Retro_Rust") : bMetal ? TEXT("MI_Retro_Metal") : TEXT("MI_Retro_WoodDark")));
	Drawer->SetMaterial(0, DrawerMaterial ? static_cast<UMaterialInterface*>(DrawerMaterial) : FrontBase);
	Trim->SetMaterial(0, TrimMaterial ? static_cast<UMaterialInterface*>(TrimMaterial) : TrimBase);

	if (bCart)
	{
		// a metal frame with a heap of laundry on top; searching tips the heap aside. The trim runs round the rim
		const FVector Heap(S.X * 0.85f, S.Y * 0.85f, 18.0f);
		Drawer->SetRelativeScale3D(Heap / 100.0f);
		Drawer->SetRelativeLocationAndRotation(
			FVector(0.0f, OpenAlpha * S.Y * 0.35f, S.Z + 9.0f - OpenAlpha * 4.0f),
			FRotator(0.0f, 0.0f, OpenAlpha * 18.0f));
		Handle->SetVisibility(false);
		Trim->SetRelativeScale3D(FVector(S.X + 2.0f, S.Y + 2.0f, 3.0f) / 100.0f);
		Trim->SetRelativeLocation(FVector(0.0f, 0.0f, S.Z - 1.0f));
		Clue->SetRelativeLocation(FVector(0.0f, 0.0f, S.Z + 10.0f));
		UpdateGlow();
		return;
	}

	// the top drawer: its front sits proud of the body, and it slides out half its depth when searched
	const float DrawerHeight = FMath::Min(S.Z * 0.3f, 26.0f);
	const FVector DrawerSize(S.X * 0.92f, S.Y * 0.84f, DrawerHeight);
	const float DrawerZ = S.Z - DrawerHeight * 0.5f - 6.0f;
	// closed, its front stands 1.5 cm proud of the body's front (it used to sit just inside it, out of sight)
	const float DrawerX = S.X * 0.5f - DrawerSize.X * 0.5f + 1.5f + OpenAlpha * S.X * 0.5f;
	Drawer->SetRelativeScale3D(DrawerSize / 100.0f);
	Drawer->SetRelativeLocationAndRotation(FVector(DrawerX, 0.0f, DrawerZ), FRotator::ZeroRotator);
	Handle->SetVisibility(true);
	Handle->SetRelativeLocation(FVector(DrawerX + DrawerSize.X * 0.5f + 1.0f, 0.0f, DrawerZ));
	// the trim: a strip along the top edge of the body's front, just proud of it
	Trim->SetRelativeScale3D(FVector(1.5f, S.Y * 0.96f, 2.5f) / 100.0f);
	Trim->SetRelativeLocation(FVector(S.X * 0.5f + 0.8f, 0.0f, S.Z - 2.0f));
	Clue->SetRelativeLocation(FVector(0.0f, 0.0f, S.Z - 10.0f));
	UpdateGlow();
}

void AFacilityContainer::UpdateGlow()
{
	// unsearched: the trim glows faintly; aimed at: front and trim brighten; searched: dark, the front dimmed
	if (TrimMaterial)
	{
		TrimMaterial->SetScalarParameterValue(TEXT("Emissive"), bSearched ? 0.0f : bHighlighted ? 1.4f : 0.35f);
		// searched, the pale strip goes dark too, so even under a flashlight it reads as done
		TrimMaterial->SetVectorParameterValue(TEXT("Tint"), bSearched ? FLinearColor(0.12f, 0.1f, 0.09f) : FLinearColor::White);
	}
	if (DrawerMaterial)
	{
		DrawerMaterial->SetScalarParameterValue(TEXT("Emissive"), bHighlighted && !bSearched ? 0.35f : 0.0f);
		DrawerMaterial->SetVectorParameterValue(TEXT("Tint"), bSearched ? FrontTint * 0.3f : FrontTint);
	}
}

void AFacilityContainer::SetLocalHighlight(bool bOn)
{
	if (bHighlighted != bOn)
	{
		bHighlighted = bOn;
		UpdateGlow();
	}
}

bool AFacilityContainer::IsGivingClue() const
{
	return Clue && Clue->IsActive();
}

void AFacilityContainer::OnRep_ClueItem()
{
	if (Clue)
	{
		Clue->SetItem(bSearched ? NAME_None : ClueItem);
	}
}

double AFacilityContainer::GetServerTime() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

FVector AFacilityContainer::GetFrontPoint(float InFront) const
{
	return GetActorLocation() + GetActorForwardVector() * (Size.X * 0.5f + InFront);
}

float AFacilityContainer::GetSearchProgressFor(const APawn* Pawn) const
{
	if (!Pawn || Searcher != Pawn || bSearched)
	{
		return -1.0f;
	}
	return FMath::Clamp(static_cast<float>((GetServerTime() - SearchStartTime) / SearchTime), 0.0f, 1.0f);
}

void AFacilityContainer::SetContent(FName KeyId, const FText& Name, FName SignatureId, FName ZoneId)
{
	if (!HasAuthority())
	{
		return;
	}
	ContentKeyId = KeyId;
	ContentName = Name;
	ContentSignatureId = SignatureId;
	ContentZoneId = ZoneId;

	// a fuse or the gate key gives itself away to someone close; everything else is silent
	ClueItem = (KeyId == FName(TEXT("Fuse")) || KeyId == AFacilityCharacter::GateKeyId) ? KeyId : NAME_None;
	OnRep_ClueItem();
}

bool AFacilityContainer::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return !bSearched && Character && Character->IsStanding() && (!Searcher || Searcher == InstigatorPawn);
}

FText AFacilityContainer::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	if (bSearched)
	{
		return FText::GetEmpty();
	}
	if (Searcher && Searcher == InstigatorPawn)
	{
		return NSLOCTEXT("Facility", "ContainerSearching", "Searching...");
	}
	if (Searcher)
	{
		return NSLOCTEXT("Facility", "ContainerBusy", "Someone is searching it");
	}

	switch (Kind)
	{
	case EFacilityContainerKind::Cart: return NSLOCTEXT("Facility", "SearchCart", "Search the laundry (hold)");
	case EFacilityContainerKind::Cabinet: return NSLOCTEXT("Facility", "SearchCabinet", "Search the cabinet (hold)");
	case EFacilityContainerKind::Desk: return NSLOCTEXT("Facility", "SearchDesk", "Search the desk (hold)");
	case EFacilityContainerKind::Nightstand: return NSLOCTEXT("Facility", "SearchNightstand", "Search the nightstand (hold)");
	default: return NSLOCTEXT("Facility", "SearchDresser", "Search the dresser (hold)");
	}
}

void AFacilityContainer::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !CanInteract_Implementation(InstigatorPawn) || Searcher == InstigatorPawn)
	{
		return;
	}

	Searcher = InstigatorPawn;
	SearchStartTime = GetServerTime();
	SetActorTickEnabled(true);

	// rummaging is quiet, but not silent
	OnRep_Searcher();
	AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation() + FVector(0.0f, 0.0f, Size.Z), SearchNoiseRadius, InstigatorPawn);
	UE_LOG(LogTheFacility, Verbose, TEXT("%s starts searching %s"), *GetNameSafe(InstigatorPawn), *GetName());
}

void AFacilityContainer::StopSearch(const APawn* Pawn)
{
	if (HasAuthority() && Searcher && Searcher == Pawn && !bSearched)
	{
		Searcher = nullptr;
	}
}

void AFacilityContainer::CompleteSearchNow(APawn* Pawn)
{
	if (HasAuthority() && !bSearched)
	{
		FinishSearch(Pawn);
	}
}

void AFacilityContainer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// the drawer slides on every machine
	const float Target = bSearched ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(OpenAlpha, Target))
	{
		OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, Target, DeltaSeconds, 3.0f);
		ApplyLook();
	}

	if (HasAuthority() && Searcher && !bSearched)
	{
		const AFacilityCharacter* Character = Cast<AFacilityCharacter>(Searcher);
		const bool bStillThere = Character && Character->IsStanding() && !Character->IsHiding()
			&& FVector::Dist2D(Character->GetActorLocation(), GetFrontPoint(0.0f)) <= MaxSearchDistance;
		if (!bStillThere)
		{
			Searcher = nullptr;
		}
		else if (GetServerTime() - SearchStartTime >= SearchTime)
		{
			FinishSearch(Searcher);
		}
	}

	if (!Searcher && FMath::IsNearlyEqual(OpenAlpha, Target))
	{
		SetActorTickEnabled(false);
	}
}

void AFacilityContainer::FinishSearch(APawn* Pawn)
{
	bSearched = true;
	Searcher = nullptr;
	OnRep_Searched();

	if (ContentKeyId.IsNone())
	{
		UE_LOG(LogTheFacility, Log, TEXT("%s searched %s: empty"), *GetNameSafe(Pawn), *GetName());
		return;
	}

	// what was inside lies in the open drawer (or on the laundry heap), to be taken like anything else
	const FVector Where = Kind == EFacilityContainerKind::Cart
		? GetActorLocation() + FVector(0.0f, 0.0f, Size.Z + 30.0f)
		: GetActorLocation() + GetActorForwardVector() * (Size.X * 0.5f + 4.0f) + FVector(0.0f, 0.0f, Size.Z + 4.0f);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AFacilityPickup* Found = GetWorld()->SpawnActor<AFacilityPickup>(AFacilityPickup::StaticClass(), Where, GetActorRotation(), SpawnParams))
	{
		Found->InitDropped(ContentKeyId, ContentName.IsEmpty() ? AFacilityCharacter::GetItemName(ContentKeyId) : ContentName, ContentSignatureId, ContentZoneId);
	}
	UE_LOG(LogTheFacility, Log, TEXT("%s searched %s: found %s"), *GetNameSafe(Pawn), *GetName(), *ContentKeyId.ToString());
	ContentKeyId = NAME_None;
	ClueItem = NAME_None;
	OnRep_ClueItem();
}

void AFacilityContainer::OnRep_Searcher()
{
	if (Searcher && GetNetMode() != NM_DedicatedServer)
	{
		FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Rummage"))), GetActorLocation() + FVector(0.0f, 0.0f, Size.Z), 900.0f, 0.45f, 1.35f);
	}
}

void AFacilityContainer::OnRep_Searched()
{
	SetActorTickEnabled(true);
	UpdateGlow();
	OnRep_ClueItem();
	if (GetNetMode() != NM_DedicatedServer && bSearched)
	{
		FacilityAudio::PlayAt(this, Cast<USoundBase>(LookAssets.FindRef(TEXT("Rummage"))), GetActorLocation() + FVector(0.0f, 0.0f, Size.Z), 900.0f, 0.5f, 1.6f);
	}
}

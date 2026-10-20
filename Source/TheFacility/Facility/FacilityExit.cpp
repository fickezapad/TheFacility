// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityExit.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityTenant.h"
#include "Facility/FacilityLook.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TheFacility.h"

AFacilityExit::AFacilityExit()
{
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// placeholder gate: 300 cm wide, 300 cm tall, across the actor's Y axis
	Gate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gate"));
	Gate->SetupAttachment(Root);
	Gate->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Gate->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Gate->SetStaticMesh(CubeMesh.Object);
		Gate->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
		Gate->SetRelativeScale3D(FVector(0.2f, 3.0f, 3.0f));
	}
	Gate->SetMaterial(0, FacilityLook::Material(TEXT("MI_Retro_Rust")));
	// the gate posts stay when the gate is gone; a padlock and chain hang on it until then
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FacilityLook::AddPiece(this, Side == 0 ? TEXT("Look_PostLeft") : TEXT("Look_PostRight"), Root, NAME_None, TEXT("Cylinder"), TEXT("MI_Retro_Metal"),
			FVector(0.0f, Side == 0 ? -158.0f : 158.0f, 165.0f), FRotator::ZeroRotator, FVector(0.16f, 0.16f, 3.3f));
	}
	ChainPieces.Add(FacilityLook::AddPiece(this, TEXT("Look_Chain"), Gate, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Metal"), FVector(-60.0f, 0.0f, -5.0f), FRotator::ZeroRotator, FVector(0.04f, 0.05f, 0.5f), true));
	ChainPieces.Add(FacilityLook::AddPiece(this, TEXT("Look_Padlock"), Gate, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Brass"), FVector(-70.0f, 0.0f, -14.0f), FRotator::ZeroRotator, FVector(0.04f, 0.08f, 0.1f), true));
	ChainPieces.Add(FacilityLook::AddPiece(this, TEXT("Look_ChainCross"), Gate, NAME_None, TEXT("Cube"), TEXT("MI_Retro_Metal"), FVector(-60.0f, 0.0f, -5.0f), FRotator(0.0f, 0.0f, 90.0f), FVector(0.04f, 0.05f, 0.5f), true));

	// the zone is only ever tested against player locations, so it needs no collision
	ExitZone = CreateDefaultSubobject<UBoxComponent>(TEXT("ExitZone"));
	ExitZone->SetupAttachment(Root);
	ExitZone->SetRelativeLocation(FVector(250.0f, 0.0f, 150.0f));
	ExitZone->SetBoxExtent(FVector(200.0f, 250.0f, 150.0f));
	ExitZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExitZone->SetCanEverAffectNavigation(false);
}

void AFacilityExit::BeginPlay()
{
	Super::BeginPlay();

	TryBindToGameState();
}

void AFacilityExit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindTimer);

	Super::EndPlay(EndPlayReason);
}

void AFacilityExit::TryBindToGameState()
{
	AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!GameState)
	{
		// on clients the game state can arrive after this actor begins play
		if (++BindAttempts > 50)
		{
			UE_LOG(LogTheFacility, Error, TEXT("%s: no Facility game state, the gate will never open. Check the level's game mode."), *GetName());
			return;
		}
		GetWorldTimerManager().SetTimer(BindTimer, this, &AFacilityExit::TryBindToGameState, 0.2f, false);
		return;
	}

	GameState->OnPowerRestored.AddUniqueDynamic(this, &AFacilityExit::HandlePowerRestored);
	if (GameState->IsPowerRestored())
	{
		HandlePowerRestored();
	}
}

void AFacilityExit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFacilityExit, bUnchained);
	DOREPLIFETIME(AFacilityExit, UnchainedBy);
}

void AFacilityExit::HandlePowerRestored()
{
	bPowerSeen = true;
	UpdateGate();
}

void AFacilityExit::OnRep_Unchained()
{
	for (UStaticMeshComponent* Piece : ChainPieces)
	{
		Piece->SetVisibility(!bUnchained);
	}
	UpdateGate();
}

void AFacilityExit::UpdateGate()
{
	if (bOpen || !bPowerSeen || IsChainedShut())
	{
		return;
	}

	bOpen = true;
	Gate->SetVisibility(false, true);
	Gate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogTheFacility, Log, TEXT("%s: gate open on the %s"), *GetName(), GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"));
}

bool AFacilityExit::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	return IsChainedShut() && Character && Character->IsStanding() && Character->HasKey(AFacilityCharacter::GateKeyId);
}

FText AFacilityExit::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	if (!IsChainedShut())
	{
		return bOpen ? FText::GetEmpty() : NSLOCTEXT("Facility", "GateNoPower", "The gate's motor is dead. Restore the power");
	}
	return CanInteract_Implementation(InstigatorPawn)
		? NSLOCTEXT("Facility", "GateUnchain", "Unlock the chain")
		: NSLOCTEXT("Facility", "GateChained", "Chained and padlocked. Find the gate key");
}

void AFacilityExit::Interact_Implementation(APawn* InstigatorPawn)
{
	AFacilityCharacter* Character = Cast<AFacilityCharacter>(InstigatorPawn);
	if (!HasAuthority() || !CanInteract_Implementation(InstigatorPawn) || !Character->RemoveKey(AFacilityCharacter::GateKeyId))
	{
		return;
	}

	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	UnchainedBy = GameState ? GameState->MakeSignerName(Character->GetPlayerState()) : Character->GetName();
	bUnchained = true;
	OnRep_Unchained();
	// the chain coming off rattles across the lot
	AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), UnchainNoiseRadius, Character);
	UE_LOG(LogTheFacility, Log, TEXT("%s unchains %s"), *UnchainedBy, *GetName());
}

bool AFacilityExit::IsInExitZone(const FVector& Location) const
{
	const FVector Local = ExitZone->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = ExitZone->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

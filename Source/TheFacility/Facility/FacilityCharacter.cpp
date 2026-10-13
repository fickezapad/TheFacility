// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityInteractionComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "TheFacility.h"

AFacilityCharacter::AFacilityCharacter()
{
	InteractionComponent = CreateDefaultSubobject<UFacilityInteractionComponent>(TEXT("Interaction"));

	// so teammates can aim at a downed player to help them up
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void AFacilityCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AFacilityCharacter, HeldKeys, COND_OwnerOnly);
	DOREPLIFETIME(AFacilityCharacter, bDowned);
}

void AFacilityCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// fall back to a code-made action and mapping so the feature works before any input assets exist
	if (!InteractAction)
	{
		InteractAction = NewObject<UInputAction>(this, TEXT("IA_Interact_Default"));
	}

	if (!InteractMappingContext)
	{
		InteractMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Interact_Default"));
		InteractMappingContext->MapKey(InteractAction, EKeys::E);
		InteractMappingContext->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
		bOwnsInteractMappingContext = true;
	}

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (!Subsystem->HasMappingContext(InteractMappingContext))
			{
				Subsystem->AddMappingContext(InteractMappingContext, 1);
				InteractMappingOwner = PC->GetLocalPlayer();
				UE_LOG(LogTheFacility, Log, TEXT("Interact mapping added for %s (%s)"), *GetName(), *GetNameSafe(InteractMappingContext));
			}
			else
			{
				UE_LOG(LogTheFacility, Log, TEXT("Interact mapping already present for %s, not added again"), *GetName());
			}
		}
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoInteract);
	}
}

void AFacilityCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// a pawn-made mapping dies with this pawn, so take it off the player or respawns would stack them.
	// A shared asset mapping is left alone; the next pawn finds it already present.
	if (bOwnsInteractMappingContext && InteractMappingOwner.IsValid())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InteractMappingOwner->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->RemoveMappingContext(InteractMappingContext);
			UE_LOG(LogTheFacility, Log, TEXT("Interact mapping removed for %s"), *GetName());
		}
	}

	Super::EndPlay(EndPlayReason);
}

FText AFacilityCharacter::GetInteractKeyText() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !InteractAction)
	{
		return FText::GetEmpty();
	}

	const UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Subsystem)
	{
		return FText::GetEmpty();
	}

	// prefer a keyboard or mouse key; gamepad-aware prompts are a later UI task
	for (const FKey& Key : Subsystem->QueryKeysMappedToAction(InteractAction))
	{
		if (!Key.IsGamepadKey())
		{
			return Key.GetDisplayName(false);
		}
	}

	return FText::GetEmpty();
}

void AFacilityCharacter::DoInteract()
{
	if (!bDowned)
	{
		InteractionComponent->TryInteract();
	}
}

void AFacilityCharacter::GiveKey(FName KeyId)
{
	if (HasAuthority() && !KeyId.IsNone())
	{
		HeldKeys.AddUnique(KeyId);
	}
}

void AFacilityCharacter::FacilityRespawn()
{
	Server_DebugRespawn();
}

void AFacilityCharacter::Server_DebugRespawn_Implementation()
{
#if !UE_BUILD_SHIPPING
	AController* OwningController = GetController();
	AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
	if (!OwningController || !GameMode)
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("Debug respawn: %s"), *GetNameSafe(OwningController));

	OwningController->UnPossess();
	Destroy();
	GameMode->RestartPlayer(OwningController);
#endif
}

void AFacilityCharacter::SetDowned(bool bNewDowned)
{
	if (!HasAuthority() || bDowned == bNewDowned)
	{
		return;
	}

	bDowned = bNewDowned;
	ApplyDowned();
	UE_LOG(LogTheFacility, Log, TEXT("%s is %s"), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName(), bDowned ? TEXT("down") : TEXT("back up"));

	GetWorldTimerManager().ClearTimer(RecoverTimer);
	if (bDowned)
	{
		GetWorldTimerManager().SetTimer(RecoverTimer, this, &AFacilityCharacter::RecoverIfAlone, FMath::Max(AloneRecoverTime, 0.01f), false);
	}
}

void AFacilityCharacter::RecoverIfAlone()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AFacilityCharacter* Other = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
		if (Other && Other != this && !Other->IsDowned())
		{
			// someone could help; leave it to them
			return;
		}
	}

	SetDowned(false);
}

void AFacilityCharacter::OnRep_Downed()
{
	ApplyDowned();
}

void AFacilityCharacter::ApplyDowned()
{
	if (bDowned)
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
}

bool AFacilityCharacter::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Helper = Cast<AFacilityCharacter>(InstigatorPawn);
	return bDowned && Helper && Helper != this && !Helper->IsDowned();
}

FText AFacilityCharacter::GetInteractPrompt_Implementation(APawn* InstigatorPawn) const
{
	return CanInteract_Implementation(InstigatorPawn) ? NSLOCTEXT("Facility", "HelpUp", "Help up") : FText::GetEmpty();
}

void AFacilityCharacter::Interact_Implementation(APawn* InstigatorPawn)
{
	if (HasAuthority() && CanInteract_Implementation(InstigatorPawn))
	{
		SetDowned(false);
	}
}

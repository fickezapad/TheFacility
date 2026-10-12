// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityInteractionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "TheFacility.h"

AFacilityCharacter::AFacilityCharacter()
{
	InteractionComponent = CreateDefaultSubobject<UFacilityInteractionComponent>(TEXT("Interaction"));
}

void AFacilityCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AFacilityCharacter, HeldKeys, COND_OwnerOnly);
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
	InteractionComponent->TryInteract();
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

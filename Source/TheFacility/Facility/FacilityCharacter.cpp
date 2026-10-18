// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityGameMode.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityHidingSpot.h"
#include "Facility/FacilityInteractionComponent.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityMovementComponent.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityTenant.h"
#include "Facility/FacilityVoiceComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "TheFacility.h"

const FName AFacilityCharacter::FlashCameraId(TEXT("FlashCamera"));

#if !UE_BUILD_SHIPPING
/** QA: BugItGo needs cheats, which clients don't have. This moves the local player's own character on any machine */
static FAutoConsoleCommandWithWorldAndArgs FacilityGotoCommand(
	TEXT("facility.Goto"),
	TEXT("QA: moves your own Facility character. facility.Goto X Y Z [Yaw] [Pitch]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const ULocalPlayer* LocalPlayer = World && Args.Num() >= 3 ? World->GetFirstLocalPlayerFromController() : nullptr;
		APlayerController* PlayerController = LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr;
		AFacilityCharacter* Character = PlayerController ? Cast<AFacilityCharacter>(PlayerController->GetPawn()) : nullptr;
		if (!Character)
		{
			UE_LOG(LogTheFacility, Warning, TEXT("facility.Goto X Y Z [Yaw] [Pitch]: needs a location and a Facility character to move"));
			return;
		}

		const FVector Location(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
		const FRotator View(Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.0f, Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.0f, 0.0f);
		// the view is the client's to set; the server moves the body
		PlayerController->SetControlRotation(View);
		Character->Server_DebugGoto(Location, View.Yaw);
	}));
#endif

AFacilityCharacter::AFacilityCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UFacilityMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	InteractionComponent = CreateDefaultSubobject<UFacilityInteractionComponent>(TEXT("Interaction"));
	Voice = CreateDefaultSubobject<UFacilityVoiceComponent>(TEXT("Voice"));

	// so teammates can aim at a downed player to help them up
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// a narrow, short-range beam: enough to search a room, not enough to make it feel safe
	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(GetCapsuleComponent());
	Flashlight->SetRelativeLocation(FVector(20.0f, 12.0f, 50.0f));
	Flashlight->SetMobility(EComponentMobility::Movable);
	Flashlight->IntensityUnits = ELightUnits::Candelas;
	Flashlight->Intensity = 35.0f;
	Flashlight->AttenuationRadius = 1600.0f;
	Flashlight->InnerConeAngle = 12.0f;
	Flashlight->OuterConeAngle = 28.0f;

	DarkAdaptLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("DarkAdaptLight"));
	DarkAdaptLight->SetupAttachment(GetCapsuleComponent());
	DarkAdaptLight->SetMobility(EComponentMobility::Movable);
	DarkAdaptLight->IntensityUnits = ELightUnits::Candelas;
	DarkAdaptLight->Intensity = 20.0f;
	DarkAdaptLight->AttenuationRadius = 900.0f;
	DarkAdaptLight->SetCastShadows(false);
	DarkAdaptLight->SetVisibility(false);
}

void AFacilityCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AFacilityCharacter, HeldKeys, COND_OwnerOnly);
	DOREPLIFETIME(AFacilityCharacter, bDowned);
	DOREPLIFETIME(AFacilityCharacter, bRetained);
	DOREPLIFETIME(AFacilityCharacter, HidingSpot);
	DOREPLIFETIME(AFacilityCharacter, DownedDeadline);
	DOREPLIFETIME(AFacilityCharacter, bDownedRecoversAlone);
	// the owner switches its own light straight away and must not be overwritten by a late echo
	DOREPLIFETIME_CONDITION(AFacilityCharacter, bFlashlightOn, COND_SkipOwner);
}

UFacilityMovementComponent* AFacilityCharacter::GetFacilityMovement() const
{
	return Cast<UFacilityMovementComponent>(GetCharacterMovement());
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

	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		if (Subsystem)
		{
			InteractMappingOwner = PC->GetLocalPlayer();
			if (!Subsystem->HasMappingContext(InteractMappingContext))
			{
				Subsystem->AddMappingContext(InteractMappingContext, 1);
				UE_LOG(LogTheFacility, Log, TEXT("Interact mapping added for %s (%s)"), *GetName(), *GetNameSafe(InteractMappingContext));
			}
			else
			{
				UE_LOG(LogTheFacility, Log, TEXT("Interact mapping already present for %s, not added again"), *GetName());
			}
		}
	}

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (EnhancedInputComponent)
	{
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoInteract);
	}

	SetupRunInput(EnhancedInputComponent, Subsystem);

	// the local player's voice: open mic or not, gain and gate, and the server learns who can't be heard
	if (UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>())
	{
		GameInstance->ApplyVoiceSettings(Cast<APlayerController>(GetController()));
	}
}

void AFacilityCharacter::SetupRunInput(UEnhancedInputComponent* EnhancedInputComponent, UEnhancedInputLocalPlayerSubsystem* Subsystem)
{
	SprintAction = NewObject<UInputAction>(this, TEXT("IA_Sprint_Default"));
	SneakAction = NewObject<UInputAction>(this, TEXT("IA_Sneak_Default"));
	TalkAction = NewObject<UInputAction>(this, TEXT("IA_Talk_Default"));
	FlashlightAction = NewObject<UInputAction>(this, TEXT("IA_Flashlight_Default"));
	UseItemAction = NewObject<UInputAction>(this, TEXT("IA_UseItem_Default"));
	RestartAction = NewObject<UInputAction>(this, TEXT("IA_Restart_Default"));

	RunMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Run_Default"));
	RunMappingContext->MapKey(SprintAction, EKeys::LeftShift);
	RunMappingContext->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
	RunMappingContext->MapKey(SneakAction, EKeys::LeftControl);
	RunMappingContext->MapKey(SneakAction, EKeys::C);
	RunMappingContext->MapKey(SneakAction, EKeys::Gamepad_FaceButton_Right);
	RunMappingContext->MapKey(TalkAction, EKeys::V);
	RunMappingContext->MapKey(TalkAction, EKeys::Gamepad_LeftShoulder);
	RunMappingContext->MapKey(FlashlightAction, EKeys::F);
	RunMappingContext->MapKey(FlashlightAction, EKeys::Gamepad_DPad_Up);
	RunMappingContext->MapKey(UseItemAction, EKeys::LeftMouseButton);
	RunMappingContext->MapKey(UseItemAction, EKeys::Gamepad_RightTrigger);
	RunMappingContext->MapKey(RestartAction, EKeys::R);

	if (Subsystem)
	{
		Subsystem->AddMappingContext(RunMappingContext, 1);
	}

	if (EnhancedInputComponent)
	{
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoSprintStart);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFacilityCharacter::DoSprintEnd);
		EnhancedInputComponent->BindAction(SneakAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoSneakStart);
		EnhancedInputComponent->BindAction(SneakAction, ETriggerEvent::Completed, this, &AFacilityCharacter::DoSneakEnd);
		EnhancedInputComponent->BindAction(TalkAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoTalkStart);
		EnhancedInputComponent->BindAction(TalkAction, ETriggerEvent::Completed, this, &AFacilityCharacter::DoTalkEnd);
		EnhancedInputComponent->BindAction(FlashlightAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoToggleFlashlight);
		EnhancedInputComponent->BindAction(UseItemAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoUseItem);
		EnhancedInputComponent->BindAction(RestartAction, ETriggerEvent::Started, this, &AFacilityCharacter::DoRestart);
	}
}

void AFacilityCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DownedTimer);

	// pawn-made mappings die with this pawn, so take them off the player or respawns would stack them.
	// A shared asset mapping is left alone; the next pawn finds it already present.
	if (InteractMappingOwner.IsValid())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InteractMappingOwner->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (bOwnsInteractMappingContext)
			{
				Subsystem->RemoveMappingContext(InteractMappingContext);
				UE_LOG(LogTheFacility, Log, TEXT("Interact mapping removed for %s"), *GetName());
			}
			if (RunMappingContext)
			{
				Subsystem->RemoveMappingContext(RunMappingContext);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFacilityCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// hidden bodies stay exactly where the spot put them, on every machine, whatever a teleport or a correction did
	if (IsHiding() && GetCharacterMovement()->MovementMode != MOVE_None)
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}

	if (HasAuthority())
	{
		UpdateFootstepNoise(DeltaSeconds);
	}

	if (GetNetMode() == NM_DedicatedServer || !Flashlight->IsVisible())
	{
		return;
	}

	// point the beam where this player looks. On other machines the aim comes from the replicated view pitch
	const FRotator Aim = GetBaseAimRotation();
	Flashlight->SetWorldLocationAndRotation(GetPawnViewLocation() + Aim.RotateVector(FVector(15.0f, 12.0f, -12.0f)), Aim);
}

void AFacilityCharacter::UpdateFootstepNoise(float DeltaSeconds)
{
	FootstepNoiseCooldown -= DeltaSeconds;
	const UFacilityMovementComponent* Movement = GetFacilityMovement();
	if (FootstepNoiseCooldown > 0.0f || !Movement || !IsStanding() || !Movement->IsMovingOnGround())
	{
		return;
	}

	// the server runs every player's movement, so it knows who is running without asking the client
	if (Movement->IsSprinting())
	{
		AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation(), SprintNoiseRadius, this);
		FootstepNoiseCooldown = 0.4f;
	}
	else if (GetVelocity().SizeSquared2D() > FMath::Square(QuietSpeed))
	{
		AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation(), WalkNoiseRadius, this);
		FootstepNoiseCooldown = 0.5f;
	}
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
	// from inside a closet or under a bed, interact means coming out
	if (IsHiding())
	{
		Server_LeaveHiding();
		return;
	}

	if (IsStanding())
	{
		InteractionComponent->TryInteract();
	}
}

void AFacilityCharacter::DoSprintStart()
{
	if (UFacilityMovementComponent* Movement = GetFacilityMovement())
	{
		Movement->SetWantsToSprint(true);
	}
}

void AFacilityCharacter::DoSprintEnd()
{
	if (UFacilityMovementComponent* Movement = GetFacilityMovement())
	{
		Movement->SetWantsToSprint(false);
	}
}

void AFacilityCharacter::DoSneakStart()
{
	if (UFacilityMovementComponent* Movement = GetFacilityMovement())
	{
		Movement->SetWantsToSneak(true);
	}
}

void AFacilityCharacter::DoSneakEnd()
{
	if (UFacilityMovementComponent* Movement = GetFacilityMovement())
	{
		Movement->SetWantsToSneak(false);
	}
}

void AFacilityCharacter::DoTalkStart()
{
	if (UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>())
	{
		GameInstance->SetTalkKeyHeld(Cast<APlayerController>(GetController()), true);
	}
}

void AFacilityCharacter::DoTalkEnd()
{
	if (UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>())
	{
		GameInstance->SetTalkKeyHeld(Cast<APlayerController>(GetController()), false);
	}
}

void AFacilityCharacter::DoToggleFlashlight()
{
	if (bRetained)
	{
		return;
	}

	// switch at once on this machine, then tell the server so everyone else sees it
	bFlashlightOn = !bFlashlightOn;
	OnRep_Flashlight();
	Server_SetFlashlight(bFlashlightOn);
}

void AFacilityCharacter::Server_SetFlashlight_Implementation(bool bOn)
{
	bFlashlightOn = bOn;
	OnRep_Flashlight();
}

void AFacilityCharacter::OnRep_Flashlight()
{
	Flashlight->SetVisibility(bFlashlightOn && !bRetained && !IsHiding());
}

void AFacilityCharacter::DoUseItem()
{
	if (bRetained)
	{
		Server_CycleSpectate();
		return;
	}

	if (!IsStanding())
	{
		return;
	}

	// the master key turns in the door you're looking at; otherwise the flash goes off
	if (AFacilityDoor* Door = GetFocusedLockableDoor())
	{
		Server_ToggleDoorLock(Door);
		return;
	}

	if (HasKey(FlashCameraId))
	{
		Server_UseFlash();
	}
}

AFacilityDoor* AFacilityCharacter::GetFocusedLockableDoor() const
{
	AFacilityDoor* Door = Cast<AFacilityDoor>(InteractionComponent->GetFocusedActor());
	return (Door && Door->CanToggleLock(this)) ? Door : nullptr;
}

void AFacilityCharacter::Server_ToggleDoorLock_Implementation(AFacilityDoor* Door)
{
	if (!IsValid(Door) || !Door->CanToggleLock(this))
	{
		return;
	}

	// same reach as interacting, measured to the door's nearest edge, with the same latency allowance
	const FBox Bounds = Door->GetComponentsBoundingBox();
	if (Bounds.IsValid && Bounds.ComputeSquaredDistanceToPoint(GetPawnViewLocation()) > FMath::Square(300.0f))
	{
		UE_LOG(LogTheFacility, Log, TEXT("Lock toggle rejected, out of reach: %s -> %s"), *GetName(), *Door->GetName());
		return;
	}

	Door->ToggleLock(this);
}

void AFacilityCharacter::Server_UseFlash_Implementation()
{
	if (!IsStanding() || !RemoveKey(FlashCameraId))
	{
		return;
	}

	const FVector Eye = GetPawnViewLocation();
	const FVector Direction = GetBaseAimRotation().Vector();
	Multicast_FlashEffect(Eye + Direction * 30.0f);

	// it can't stun what can't see, but the Tenant hears the shutter and the whine
	AFacilityTenant::ReportNoise(GetWorld(), Eye, FlashNoiseRadius, this);

	// players never block the flash; walls and doors do
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityFlash), false, this);
	for (TActorIterator<AFacilityCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	int32 NumStunned = 0;
	for (TActorIterator<AFacilityMarked> It(GetWorld()); It; ++It)
	{
		AFacilityMarked* Creature = *It;
		if (!Creature->IsPresent())
		{
			continue;
		}

		const FVector ToCreature = Creature->GetActorLocation() - Eye;
		if (ToCreature.SizeSquared() > FMath::Square(FlashRange)
			|| FVector::DotProduct(Direction, ToCreature.GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(FlashHalfAngle)))
		{
			continue;
		}

		FCollisionQueryParams CreatureParams = Params;
		CreatureParams.AddIgnoredActor(Creature);
		if (GetWorld()->LineTraceTestByChannel(Eye, Creature->GetActorLocation(), ECC_Visibility, CreatureParams))
		{
			continue;
		}

		Creature->Stun(FlashStunTime);
		++NumStunned;
	}

	UE_LOG(LogTheFacility, Log, TEXT("%s used the flash camera: %s"), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName(),
		NumStunned > 0 ? TEXT("The Marked is stunned") : TEXT("nothing caught in the flash"));
}

void AFacilityCharacter::Multicast_FlashEffect_Implementation(FVector_NetQuantize Location)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (IsLocallyControlled())
	{
		LastFlashTime = GetWorld()->GetTimeSeconds();
	}

	// a short, harsh burst of light where the camera went off
	UPointLightComponent* Burst = NewObject<UPointLightComponent>(this);
	Burst->SetMobility(EComponentMobility::Movable);
	Burst->SetIntensityUnits(ELightUnits::Candelas);
	Burst->SetIntensity(3000.0f);
	Burst->SetAttenuationRadius(1500.0f);
	Burst->SetCastShadows(false);
	Burst->RegisterComponent();
	Burst->SetWorldLocation(Location);

	TWeakObjectPtr<UPointLightComponent> WeakBurst = Burst;
	FTimerHandle BurstTimer;
	GetWorldTimerManager().SetTimer(BurstTimer, FTimerDelegate::CreateWeakLambda(this, [WeakBurst]()
	{
		if (WeakBurst.IsValid())
		{
			WeakBurst->DestroyComponent();
		}
	}), 0.12f, false);
}

void AFacilityCharacter::DoRestart()
{
	// only meaningful once the run is over; the server checks that it came from the host
	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (GameState && !GameState->IsRunInProgress())
	{
		Server_Restart();
	}
}

void AFacilityCharacter::Server_Restart_Implementation()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		UE_LOG(LogTheFacility, Log, TEXT("Restart ignored: only the host can start a new run"));
		return;
	}

	if (AFacilityGameMode* GameMode = GetWorld()->GetAuthGameMode<AFacilityGameMode>())
	{
		GameMode->RestartRun();
	}
}

void AFacilityCharacter::GiveKey(FName KeyId, const FText& DisplayName, FName SignatureId, FName ZoneId)
{
	if (!HasAuthority() || KeyId.IsNone())
	{
		return;
	}

	HeldKeys.AddUnique(KeyId);

	FFacilityHeldItemInfo& Info = HeldItemInfo.FindOrAdd(KeyId);
	Info.DisplayName = DisplayName.IsEmpty() ? GetItemName(KeyId) : DisplayName;
	Info.SignatureId = SignatureId;
	Info.ZoneId = ZoneId;

	UpdateFuseCarrier();
}

bool AFacilityCharacter::RemoveKey(FName KeyId)
{
	if (!HasAuthority())
	{
		return false;
	}

	HeldItemInfo.Remove(KeyId);
	const bool bRemoved = HeldKeys.Remove(KeyId) > 0;
	UpdateFuseCarrier();
	return bRemoved;
}

void AFacilityCharacter::UpdateFuseCarrier()
{
	AFacilityGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AFacilityGameState>() : nullptr;
	APlayerState* Mine = GetPlayerState();
	if (!HasAuthority() || !GameState || !Mine)
	{
		return;
	}

	if (HeldKeys.Contains(FName(TEXT("Fuse"))))
	{
		GameState->SetFuseCarrier(Mine);
	}
	else if (GameState->GetFuseCarrier() == Mine)
	{
		GameState->SetFuseCarrier(nullptr);
	}
}

FText AFacilityCharacter::GetItemName(FName KeyId)
{
	if (KeyId == FlashCameraId)
	{
		return NSLOCTEXT("Facility", "ItemFlashCamera", "Flash camera");
	}
	if (KeyId == TEXT("Fuse"))
	{
		return NSLOCTEXT("Facility", "ItemFuse", "Fuse");
	}
	if (KeyId == AFacilityDoor::MasterKeyId)
	{
		return NSLOCTEXT("Facility", "ItemMasterKey", "Master key");
	}
	return FText::FromName(KeyId);
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

void AFacilityCharacter::Server_DebugGoto_Implementation(FVector_NetQuantize Location, float Yaw)
{
#if !UE_BUILD_SHIPPING
	if (!IsStanding())
	{
		return;
	}

	UE_LOG(LogTheFacility, Log, TEXT("Debug goto: %s to %s"), *GetName(), *FVector(Location).ToCompactString());
	TeleportTo(Location, FRotator(0.0f, Yaw, 0.0f));
#endif
}

void AFacilityCharacter::SetDowned(bool bNewDowned)
{
	if (!HasAuthority() || bRetained || bDowned == bNewDowned)
	{
		return;
	}

	if (bNewDowned && HidingSpot)
	{
		HidingSpot->Evict(TEXT("downed"));
	}

	bDowned = bNewDowned;
	DownedTime = 0.0f;
	DownedDeadline = 0.0;
	bDownedRecoversAlone = false;
	ApplyLifeState();
	UE_LOG(LogTheFacility, Log, TEXT("%s is %s"), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName(), bDowned ? TEXT("down") : TEXT("back up"));

	GetWorldTimerManager().ClearTimer(DownedTimer);
	if (bDowned)
	{
		AFacilityTenant::ReportNoise(GetWorld(), GetActorLocation(), DownedNoiseRadius, this);
		// re-evaluated the whole time they are down, so a helper leaving or arriving is always noticed
		GetWorldTimerManager().SetTimer(DownedTimer, this, &AFacilityCharacter::TickDowned, DownedTickInterval, true);
		TickDowned();
	}
}

void AFacilityCharacter::TickDowned()
{
	if (!bDowned)
	{
		GetWorldTimerManager().ClearTimer(DownedTimer);
		return;
	}

	// a finished run freezes everyone as they are
	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (GameState && !GameState->IsRunInProgress())
	{
		DownedDeadline = 0.0;
		return;
	}

	const bool bAlone = !IsAnyoneElseStanding();
	if (bAlone && bUsedAloneRecovery)
	{
		// nobody can come, and the Facility has already let this one go once
		Retain();
		return;
	}

	const float Limit = bAlone ? AloneRecoverTime : BleedOutTime;
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	DownedDeadline = Now + FMath::Max(Limit - DownedTime, 0.0f);
	bDownedRecoversAlone = bAlone;

	if (DownedTime >= Limit)
	{
		if (bAlone)
		{
			bUsedAloneRecovery = true;
			UE_LOG(LogTheFacility, Log, TEXT("%s gets up alone (once per run)"), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName());
			SetDowned(false);
		}
		else
		{
			Retain();
		}
		return;
	}

	DownedTime += DownedTickInterval;
}

bool AFacilityCharacter::IsAnyoneElseStanding() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AFacilityCharacter* Other = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
		if (Other && Other != this && Other->IsStanding())
		{
			return true;
		}
	}

	return false;
}

float AFacilityCharacter::GetDownedTimeLeft(bool& bOutRecoversAlone) const
{
	bOutRecoversAlone = bDownedRecoversAlone;
	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!bDowned || DownedDeadline <= 0.0 || !GameState)
	{
		return 0.0f;
	}

	return static_cast<float>(FMath::Max(DownedDeadline - GameState->GetServerWorldTimeSeconds(), 0.0));
}

void AFacilityCharacter::Retain()
{
	if (!HasAuthority() || bRetained)
	{
		return;
	}

	if (HidingSpot)
	{
		HidingSpot->Evict(TEXT("retained"));
	}

	GetWorldTimerManager().ClearTimer(DownedTimer);
	bDowned = false;
	bRetained = true;
	DownedDeadline = 0.0;
	UE_LOG(LogTheFacility, Log, TEXT("%s is retained by the Facility"), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : *GetName());

	DropHeldItems();
	ApplyLifeState();
	SpectateNext();

	// the taken talk only among themselves: the server stops passing their voice to anyone still in the run
	if (const APlayerState* Mine = GetPlayerState(); Mine && Mine->GetUniqueId().IsValid())
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Listener = It->Get();
			const AFacilityCharacter* ListenerPawn = Listener ? Cast<AFacilityCharacter>(Listener->GetPawn()) : nullptr;
			if (Listener && Listener != GetController() && ListenerPawn && !ListenerPawn->IsRetained())
			{
				Listener->GameplayMutePlayer(Mine->GetUniqueId());
			}
		}
	}

	// anyone who was watching this player moves on to someone still here
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AFacilityCharacter* Watcher = PC ? Cast<AFacilityCharacter>(PC->GetPawn()) : nullptr;
		if (Watcher && Watcher != this && Watcher->IsRetained() && PC->GetViewTarget() == this)
		{
			Watcher->SpectateNext();
		}
	}
}

void AFacilityCharacter::DropHeldItems()
{
	if (HeldKeys.IsEmpty())
	{
		return;
	}

	const FVector Feet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 25.0f);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < HeldKeys.Num(); ++Index)
	{
		const FName KeyId = HeldKeys[Index];
		const FFacilityHeldItemInfo Info = HeldItemInfo.FindRef(KeyId);
		const float Angle = Index * 2.1f;
		const FVector Location = Feet + FVector(FMath::Cos(Angle) * 40.0f, FMath::Sin(Angle) * 40.0f, 0.0f);

		if (AFacilityPickup* Dropped = GetWorld()->SpawnActor<AFacilityPickup>(AFacilityPickup::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
		{
			Dropped->InitDropped(KeyId, Info.DisplayName.IsEmpty() ? GetItemName(KeyId) : Info.DisplayName, Info.SignatureId, Info.ZoneId);
			UE_LOG(LogTheFacility, Log, TEXT("Dropped %s%s"), *KeyId.ToString(), Info.SignatureId.IsNone() ? TEXT("") : TEXT(" (still an Offering)"));
		}
	}

	HeldKeys.Reset();
	HeldItemInfo.Reset();
	UpdateFuseCarrier();
}

void AFacilityCharacter::SpectateNext()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!HasAuthority() || !PC)
	{
		return;
	}

	TArray<AFacilityCharacter*> Candidates;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFacilityCharacter* Other = It->Get() ? Cast<AFacilityCharacter>(It->Get()->GetPawn()) : nullptr;
		if (Other && Other != this && !Other->IsRetained())
		{
			Candidates.Add(Other);
		}
	}

	if (Candidates.IsEmpty())
	{
		PC->SetViewTargetWithBlend(this);
		return;
	}

	const AActor* Current = PC->GetViewTarget();
	const int32 CurrentIndex = Candidates.IndexOfByPredicate([Current](const AFacilityCharacter* Candidate) { return Candidate == Current; });
	PC->SetViewTargetWithBlend(Candidates[(CurrentIndex + 1) % Candidates.Num()], 0.4f);
}

void AFacilityCharacter::SetHidingSpot(AFacilityHidingSpot* Spot)
{
	if (!HasAuthority() || HidingSpot == Spot)
	{
		return;
	}

	HidingSpot = Spot;
	ApplyLifeState();
}

void AFacilityCharacter::Server_LeaveHiding_Implementation()
{
	if (HidingSpot)
	{
		HidingSpot->RequestLeave(this);
	}
}

void AFacilityCharacter::Server_CycleSpectate_Implementation()
{
	if (bRetained)
	{
		SpectateNext();
	}
}

void AFacilityCharacter::OnRep_LifeState()
{
	ApplyLifeState();
}

void AFacilityCharacter::ApplyLifeState()
{
	if (bDowned || bRetained || IsHiding())
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	// a retained player is gone from the world for everyone; their camera follows a teammate
	SetActorHiddenInGame(bRetained);
	// hidden in a closet or under a bed: the body is out of sight for everyone (meshes only: hiding the whole actor would also
	// put out the dark-adapt light), and out of the way like a retained player
	TInlineComponentArray<UMeshComponent*> Meshes(this);
	for (UMeshComponent* MeshComponent : Meshes)
	{
		MeshComponent->SetHiddenInGame(IsHiding());
	}
	SetActorEnableCollision(!bRetained && !IsHiding());

	// only the one hiding sees by it: every other machine leaves it off
	const bool bAdapt = IsHiding() && IsLocallyControlled();
	DarkAdaptLight->SetVisibility(bAdapt);
	if (bAdapt)
	{
		DarkAdaptLight->SetWorldLocation(HidingSpot->GetEyeTransform().GetLocation() + HidingSpot->GetActorForwardVector() * 120.0f);
	}
	Flashlight->SetVisibility(bFlashlightOn && !bRetained && !IsHiding());

	ApplyDownedPose();
}

void AFacilityCharacter::ApplyDownedPose()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return;
	}

	// the owner never sees this body, and their first person camera hangs off it
	const bool bShow = bDowned && !bRetained && !IsLocallyControlled();
	if (bShow == bShowingDownedPose)
	{
		return;
	}

	if (!bStandingMeshCaptured)
	{
		StandingMeshTransform = Body->GetRelativeTransform();
		bStandingMeshCaptured = true;
	}

	bShowingDownedPose = bShow;
	if (!bShow)
	{
		Body->SetRelativeTransform(StandingMeshTransform);
		CacheInitialMeshOffset(StandingMeshTransform.GetLocation(), StandingMeshTransform.Rotator());
		return;
	}

	// tip the body over sideways, then slide it so it lies across the capsule, just above the floor
	const FQuat Lying = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(90.0f)) * StandingMeshTransform.GetRotation();
	const FVector BodyUp = Lying.RotateVector(FVector::UpVector);
	const FVector Location = StandingMeshTransform.GetLocation() + FVector(0.0f, 0.0f, 12.0f) - BodyUp * 85.0f;
	Body->SetRelativeLocationAndRotation(Location, Lying);

	// network smoothing puts the mesh back at its cached offset every frame on other machines, so move that too
	CacheInitialMeshOffset(Location, Lying.Rotator());
}

void AFacilityCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);

	// hidden: the view is from inside the spot, looking out the way it faces, with a limited turn
	if (HidingSpot)
	{
		const FTransform Eye = HidingSpot->GetEyeTransform();
		const FRotator Facing = Eye.Rotator();
		const FRotator Look = (GetController() ? GetController()->GetControlRotation() : GetBaseAimRotation());
		const float Yaw = FMath::ClampAngle(FRotator::NormalizeAxis(Look.Yaw - Facing.Yaw), -70.0f, 70.0f);
		const float Pitch = FMath::ClampAngle(FRotator::NormalizeAxis(Look.Pitch), -40.0f, 30.0f);
		OutResult.Location = Eye.GetLocation();
		OutResult.Rotation = FRotator(Pitch, Facing.Yaw + Yaw, 0.0f);
		return;
	}

	if (bShowingDownedPose)
	{
		const float FloorEyeHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 30.0f;
		OutResult.Location = GetActorLocation() - FVector(0.0f, 0.0f, FloorEyeHeight);
		OutResult.Rotation = FRotator(FMath::Clamp(GetBaseAimRotation().Pitch, -30.0f, 40.0f), GetActorRotation().Yaw, 0.0f);
	}
}

bool AFacilityCharacter::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	const AFacilityCharacter* Helper = Cast<AFacilityCharacter>(InstigatorPawn);
	return bDowned && !bRetained && Helper && Helper != this && Helper->IsStanding();
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

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TheFacilityCharacter.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityCharacter.generated.h"

class AFacilityDoor;
class AFacilityHidingSpot;
class UFacilityInteractionComponent;
class UFacilityVoiceComponent;
class UFacilityMovementComponent;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;
class USpotLightComponent;

/** Where an item a player carries came from, so it can be dropped again as the same thing. Server only */
struct FFacilityHeldItemInfo
{
	FText DisplayName;
	FName SignatureId;
	FName ZoneId;
};

/**
 *  First person character for Facility runs.
 *  Adds interaction, server-owned held items, a predicted sprint, a flashlight, the flash camera,
 *  and the downed -> retained lifecycle on top of the template character.
 *
 *  Downed: can't move or interact. A standing teammate helps them up. Left down too long, they are retained:
 *  the Facility keeps them, their body is gone, they drop what they carried, and they watch the others.
 *  With nobody standing who could help, a downed player gets up alone once per run; the second time they are retained.
 */
UCLASS(abstract)
class THEFACILITY_API AFacilityCharacter : public ATheFacilityCharacter, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UFacilityInteractionComponent> InteractionComponent;

	/** Follows where the player looks, on every machine, so teammates see each other's beams */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USpotLightComponent> Flashlight;

	/** Hidden, the eyes adjust: a faint light around the spot, switched on only on the hiding player's own machine */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<class UPointLightComponent> DarkAdaptLight;

	/** This player's voice: where others hear it from, and what the Tenant hears */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<class UFacilityVoiceComponent> Voice;

public:

	/** Item id of the flash camera */
	static const FName FlashCameraId;

protected:

	/** Interact input action. If left empty, a default one bound to E and gamepad face button left is created at runtime */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> InteractAction;

	/** Mapping context holding InteractAction. If left empty, one is created at runtime */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> InteractMappingContext;

	/** Sprint, sneak, flashlight, use item and restart. Created at runtime until input assets exist */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RunMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SneakAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TalkAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FlashlightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> UseItemAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RestartAction;

	/** Items this player carries (keys, the fuse, the flash camera). Changed only on the server, replicated to the owner */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Facility")
	TArray<FName> HeldKeys;

	/** Server only: what each held item was, for dropping it */
	TMap<FName, FFacilityHeldItemInfo> HeldItemInfo;

	/** Down: can't move or interact until a teammate helps them up. Server owned */
	UPROPERTY(ReplicatedUsing=OnRep_LifeState, BlueprintReadOnly, Category="Facility")
	bool bDowned = false;

	/** The closet or bed this player hides in, if any. Server owned */
	UPROPERTY(ReplicatedUsing=OnRep_LifeState, BlueprintReadOnly, Category="Facility")
	TObjectPtr<AFacilityHidingSpot> HidingSpot;

	/** Taken by the Facility for the rest of the run. Server owned */
	UPROPERTY(ReplicatedUsing=OnRep_LifeState, BlueprintReadOnly, Category="Facility")
	bool bRetained = false;

	/** Server time when the current downed state resolves (retained, or getting up alone). 0 when not down */
	UPROPERTY(Replicated)
	double DownedDeadline = 0.0;

	/** True if DownedDeadline means getting up alone, false if it means being retained */
	UPROPERTY(Replicated)
	bool bDownedRecoversAlone = false;

	/** A downed player is retained if no teammate helps them up in this time */
	UPROPERTY(EditAnywhere, Category="Facility", meta=(ClampMin=0, Units="s"))
	float BleedOutTime = 45.0f;

	/** With nobody standing to help, a downed player gets up alone after this long, once per run */
	UPROPERTY(EditAnywhere, Category="Facility", meta=(ClampMin=0, Units="s"))
	float AloneRecoverTime = 20.0f;

	/** Server: time spent down so far */
	float DownedTime = 0.0f;

	/** Server: the one get-up-alone of this run has been used */
	bool bUsedAloneRecovery = false;

	FTimerHandle DownedTimer;

	static constexpr float DownedTickInterval = 0.25f;

	UPROPERTY(ReplicatedUsing=OnRep_Flashlight, BlueprintReadOnly, Category="Facility")
	bool bFlashlightOn = true;

	/** The flash camera stuns The Marked if it is within this range and inside the cone */
	UPROPERTY(EditAnywhere, Category="Facility|Flash", meta=(ClampMin=0, Units="cm"))
	float FlashRange = 1100.0f;

	UPROPERTY(EditAnywhere, Category="Facility|Flash", meta=(ClampMin=0, ClampMax=90, Units="deg"))
	float FlashHalfAngle = 25.0f;

	UPROPERTY(EditAnywhere, Category="Facility|Flash", meta=(ClampMin=0, Units="s"))
	float FlashStunTime = 5.0f;

	/** Local: world time of this player's last flash, for the screen effect */
	float LastFlashTime = -100.0f;

	/** How far a camera flash carries, for the Tenant */
	UPROPERTY(EditAnywhere, Category="Facility|Noise", meta=(ClampMin=0, Units="cm"))
	float FlashNoiseRadius = 1500.0f;

	/** How far sprinting footsteps carry */
	UPROPERTY(EditAnywhere, Category="Facility|Noise", meta=(ClampMin=0, Units="cm"))
	float SprintNoiseRadius = 1500.0f;

	/** How far walking footsteps carry. Keeping still makes no sound at all */
	UPROPERTY(EditAnywhere, Category="Facility|Noise", meta=(ClampMin=0, Units="cm"))
	float WalkNoiseRadius = 300.0f;

	/** Below this speed walking is quiet */
	UPROPERTY(EditAnywhere, Category="Facility|Noise", meta=(ClampMin=0, Units="cm/s"))
	float QuietSpeed = 200.0f;

	/** How far going down carries: a cry, a fall */
	UPROPERTY(EditAnywhere, Category="Facility|Noise", meta=(ClampMin=0, Units="cm"))
	float DownedNoiseRadius = 1200.0f;

	/** Server: time until the next footstep sound is reported */
	float FootstepNoiseCooldown = 0.0f;

	/** True if this pawn created InteractMappingContext itself, so it must also remove it */
	bool bOwnsInteractMappingContext = false;

	/** The body mesh as authored, captured before it is first laid down */
	FTransform StandingMeshTransform;

	bool bStandingMeshCaptured = false;

	/** True while the body is shown lying down on this machine */
	bool bShowingDownedPose = false;

	/** Local player the mappings were added to, so they can be removed when this pawn goes away */
	TWeakObjectPtr<ULocalPlayer> InteractMappingOwner;

	/** The motel guest's look, one colour per player (picked by player id, so every machine agrees): jacket and cap */
	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInterface>> JacketMaterials;

	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInterface>> CapMaterials;

	UPROPERTY()
	TObjectPtr<class UMaterialInterface> PantsMaterial;

	/** Cap crown and brim, coloured with the jacket */
	UPROPERTY()
	TArray<TObjectPtr<class UStaticMeshComponent>> CapPieces;

	/** Colour index shown on this machine; none until the player state has arrived */
	int32 AppliedLookIndex = INDEX_NONE;

	/** The player's brightness setting has been put on this character's camera, on this machine */
	bool bBrightnessApplied = false;

	/** Dresses the body (and the owner's arms) in this player's colours, once the player id is known */
	void UpdateLook();

public:

	AFacilityCharacter(const FObjectInitializer& ObjectInitializer);

	UFacilityInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFacilityVoiceComponent* GetVoice() const { return Voice; }

	UFacilityMovementComponent* GetFacilityMovement() const;

	UFUNCTION(BlueprintPure, Category="Facility")
	bool HasKey(FName KeyId) const { return HeldKeys.Contains(KeyId); }

	UFUNCTION(BlueprintPure, Category="Facility")
	const TArray<FName>& GetHeldKeys() const { return HeldKeys; }

	/** Server only. SignatureId is set when the item is an Offering */
	void GiveKey(FName KeyId, const FText& DisplayName = FText::GetEmpty(), FName SignatureId = NAME_None, FName ZoneId = NAME_None);

	/** Server only. Returns true if the item was held */
	bool RemoveKey(FName KeyId);

	/** Readable name for a held item id */
	static FText GetItemName(FName KeyId);

	UFUNCTION(BlueprintPure, Category="Facility")
	bool IsDowned() const { return bDowned; }

	UFUNCTION(BlueprintPure, Category="Facility")
	bool IsRetained() const { return bRetained; }

	/** Neither downed nor retained: can move, interact, help and be hunted */
	UFUNCTION(BlueprintPure, Category="Facility")
	bool IsStanding() const { return !bDowned && !bRetained; }

	/** Seconds until the current downed state resolves, and how. Any machine */
	float GetDownedTimeLeft(bool& bOutRecoversAlone) const;

	/** Server only */
	void SetDowned(bool bNewDowned);

	/** Server only. The Facility keeps this player for the rest of the run */
	void Retain();

	/** Hidden in a closet or under a bed: can't move, can't be seen, makes no sound */
	UFUNCTION(BlueprintPure, Category="Facility")
	bool IsHiding() const { return HidingSpot != nullptr; }

	AFacilityHidingSpot* GetHidingSpot() const { return HidingSpot; }

	/** Server only. Called by the hiding spot as the player goes in (Spot) or comes out (null) */
	void SetHidingSpot(AFacilityHidingSpot* Spot);

	bool IsFlashlightOn() const { return bFlashlightOn; }

	/** World time of this player's last flash on this machine */
	float GetLastFlashTime() const { return LastFlashTime; }

	// IFacilityInteractable: a downed player can be helped up by a standing one
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	/** Display name of the keyboard key bound to Interact for the local player, e.g. "E". Empty if unknown */
	FText GetInteractKeyText() const;

	/** Closed door under the crosshair that this player can lock or unlock with the master key, or null. Owning client */
	AFacilityDoor* GetFocusedLockableDoor() const;

	/** Debug: destroys this pawn and spawns a fresh one for the same player. Console: FacilityRespawn */
	UFUNCTION(Exec)
	void FacilityRespawn();

	/** Server: points a retained player's camera at the next teammate still in the run */
	void SpectateNext();

	/** QA, not in shipping builds: moves this character. Console: facility.Goto */
	UFUNCTION(Server, Reliable)
	void Server_DebugGoto(FVector_NetQuantize Location, float Yaw);

	virtual void Tick(float DeltaSeconds) override;

	/** Someone watching a downed player sees from the floor, not from the laid-down body's tilted head */
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

protected:

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Look input, scaled by the player's sensitivity setting */
	virtual void DoAim(float Yaw, float Pitch) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Handles interact inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoInteract();

	void DoSprintStart();
	void DoSprintEnd();
	void DoSneakStart();
	void DoSneakEnd();
	void DoTalkStart();
	void DoTalkEnd();
	void DoToggleFlashlight();
	void DoUseItem();
	void DoRestart();

	/** Creates the code-made run input actions and adds their mapping for the local player */
	void SetupRunInput(class UEnhancedInputComponent* EnhancedInputComponent, class UEnhancedInputLocalPlayerSubsystem* Subsystem);

	UFUNCTION(Server, Reliable)
	void Server_DebugRespawn();

	UFUNCTION(Server, Reliable)
	void Server_Restart();

	UFUNCTION(Server, Reliable)
	void Server_SetFlashlight(bool bOn);

	UFUNCTION(Server, Reliable)
	void Server_UseFlash();

	/** Locks or unlocks a door with the master key. The server checks reach and that the door is closed */
	UFUNCTION(Server, Reliable)
	void Server_ToggleDoorLock(AFacilityDoor* Door);

	/** Server: reports this player's footsteps to the Tenant */
	void UpdateFootstepNoise(float DeltaSeconds);

	/** Asks the hiding spot to let this player out */
	UFUNCTION(Server, Reliable)
	void Server_LeaveHiding();

	/** Retained players cycle which teammate they watch */
	UFUNCTION(Server, Reliable)
	void Server_CycleSpectate();

	/** Cosmetic, everyone: the burst of light from a flash camera */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_FlashEffect(FVector_NetQuantize Location);

	UFUNCTION()
	void OnRep_LifeState();

	UFUNCTION()
	void OnRep_Flashlight();

	/** Applies downed/retained to movement, collision, light and visibility on this machine */
	void ApplyLifeState();

	/** Server: runs while downed. Decides between help, getting up alone and being retained */
	void TickDowned();

	/** Server: true if another player is standing and could come and help */
	bool IsAnyoneElseStanding() const;

	/** Server: drops everything this player carries where they are */
	void DropHeldItems();

	/** Server: tells the game state whether this player now carries the fuse */
	void UpdateFuseCarrier();

	/** Lays the body down for everyone else while downed (there is no downed animation yet) */
	void ApplyDownedPose();
};

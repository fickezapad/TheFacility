// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TheFacilityCharacter.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityCharacter.generated.h"

class UFacilityInteractionComponent;
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

	/** Sprint, flashlight, use item and restart. Created at runtime until input assets exist */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RunMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;

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

	/** True if this pawn created InteractMappingContext itself, so it must also remove it */
	bool bOwnsInteractMappingContext = false;

	/** Local player the mappings were added to, so they can be removed when this pawn goes away */
	TWeakObjectPtr<ULocalPlayer> InteractMappingOwner;

public:

	AFacilityCharacter(const FObjectInitializer& ObjectInitializer);

	UFacilityInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

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

	bool IsFlashlightOn() const { return bFlashlightOn; }

	/** World time of this player's last flash on this machine */
	float GetLastFlashTime() const { return LastFlashTime; }

	// IFacilityInteractable: a downed player can be helped up by a standing one
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	/** Display name of the keyboard key bound to Interact for the local player, e.g. "E". Empty if unknown */
	FText GetInteractKeyText() const;

	/** Debug: destroys this pawn and spawns a fresh one for the same player. Console: FacilityRespawn */
	UFUNCTION(Exec)
	void FacilityRespawn();

	/** Server: points a retained player's camera at the next teammate still in the run */
	void SpectateNext();

	/** QA, not in shipping builds: moves this character. Console: facility.Goto */
	UFUNCTION(Server, Reliable)
	void Server_DebugGoto(FVector_NetQuantize Location, float Yaw);

	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Handles interact inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoInteract();

	void DoSprintStart();
	void DoSprintEnd();
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
};

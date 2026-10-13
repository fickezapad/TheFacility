// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TheFacilityCharacter.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityCharacter.generated.h"

class UFacilityInteractionComponent;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;

/**
 *  First person character for Facility runs.
 *  Adds interaction, a server-owned set of held keys and a downed state on top of the template character.
 *  A downed player can't move or interact; a teammate helps them up by interacting with them.
 */
UCLASS(abstract)
class THEFACILITY_API AFacilityCharacter : public ATheFacilityCharacter, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UFacilityInteractionComponent> InteractionComponent;

protected:

	/** Interact input action. If left empty, a default one bound to E and gamepad face button left is created at runtime */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> InteractAction;

	/** Mapping context holding InteractAction. If left empty, one is created at runtime */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> InteractMappingContext;

	/** Keys this player carries. Changed only on the server, replicated to the owner */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Facility")
	TArray<FName> HeldKeys;

	/** Down: can't move or interact until a teammate helps them up. Server owned */
	UPROPERTY(ReplicatedUsing=OnRep_Downed, BlueprintReadOnly, Category="Facility")
	bool bDowned = false;

	/** With nobody standing to help, a downed player gets up by themselves after this long */
	UPROPERTY(EditAnywhere, Category="Facility", meta=(ClampMin=0, Units="s"))
	float AloneRecoverTime = 20.0f;

	FTimerHandle RecoverTimer;

	/** True if this pawn created InteractMappingContext itself, so it must also remove it */
	bool bOwnsInteractMappingContext = false;

	/** Local player the interact mapping was added to, so it can be removed when this pawn goes away */
	TWeakObjectPtr<ULocalPlayer> InteractMappingOwner;

public:

	AFacilityCharacter();

	UFacilityInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFUNCTION(BlueprintPure, Category="Facility")
	bool HasKey(FName KeyId) const { return HeldKeys.Contains(KeyId); }

	UFUNCTION(BlueprintPure, Category="Facility")
	const TArray<FName>& GetHeldKeys() const { return HeldKeys; }

	/** Server only */
	void GiveKey(FName KeyId);

	UFUNCTION(BlueprintPure, Category="Facility")
	bool IsDowned() const { return bDowned; }

	/** Server only */
	void SetDowned(bool bNewDowned);

	// IFacilityInteractable: a downed player can be helped up by a standing one
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	/** Display name of the keyboard key bound to Interact for the local player, e.g. "E". Empty if unknown */
	FText GetInteractKeyText() const;

	/** Debug: destroys this pawn and spawns a fresh one for the same player. Console: FacilityRespawn */
	UFUNCTION(Exec)
	void FacilityRespawn();

protected:

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Handles interact inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoInteract();

	UFUNCTION(Server, Reliable)
	void Server_DebugRespawn();

	UFUNCTION()
	void OnRep_Downed();

	/** Applies the downed state to movement on this machine */
	void ApplyDowned();

	/** Server: gets up alone if nobody is standing to help */
	void RecoverIfAlone();
};

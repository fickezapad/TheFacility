// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityInteractable.h"
#include "FacilityExit.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 *  The way out: a gate that stays shut until the power is back, and the zone behind it.
 *  A chained gate (bChained) also needs the gate key: a player carrying it unchains the gate (Interact), on the server.
 *  The gate opens on every machine once both are done: power from the game state, the chain from this actor.
 *  The run is won when every player still standing is inside the zone (decided by the game mode, on the server).
 */
UCLASS()
class THEFACILITY_API AFacilityExit : public AActor, public IFacilityInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> Root;

	/** Blocks the way until the power is back */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Gate;

	/** Players standing in here when the gate is open have escaped */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBoxComponent> ExitZone;

public:

	AFacilityExit();

	/** True if the gate is open on this machine */
	bool IsOpen() const { return bOpen; }

	/** True while the chain is still on: a chained gate whose key hasn't been used */
	bool IsChainedShut() const { return bChained && !bUnchained; }

	/** Who took the chain off, for the end of the night */
	const FString& GetUnchainedBy() const { return UnchainedBy; }

	// IFacilityInteractable: the chain
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;
	virtual FText GetInteractPrompt_Implementation(APawn* InstigatorPawn) const override;
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	/** True if the location is inside the exit zone */
	bool IsInExitZone(const FVector& Location) const;

protected:

	/** The gate is chained and padlocked: power alone doesn't open it, the gate key has to come off it too */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Exit")
	bool bChained = false;

	/** How far unchaining it carries */
	UPROPERTY(EditAnywhere, Category="Exit", meta=(ClampMin=0, Units="cm"))
	float UnchainNoiseRadius = 1500.0f;

	UPROPERTY(ReplicatedUsing=OnRep_Unchained, BlueprintReadOnly, Category="Exit")
	bool bUnchained = false;

	UPROPERTY(Replicated)
	FString UnchainedBy;

	/** The chain and padlock, gone once unchained */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ChainPieces;

	bool bOpen = false;

	bool bPowerSeen = false;

	UFUNCTION()
	void OnRep_Unchained();

	/** Opens the gate on this machine once the power and the chain both allow it */
	void UpdateGate();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FTimerHandle BindTimer;

	int32 BindAttempts = 0;

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Binds to the game state, retrying until it has replicated to this machine */
	void TryBindToGameState();

	UFUNCTION()
	void HandlePowerRestored();
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityExit.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 *  The way out: a gate that stays shut until the power is back, and the zone behind it.
 *  The run is won when every player still standing is inside the zone (decided by the game mode, on the server).
 *  The gate opens on every machine from the replicated power state, so it needs no replication of its own.
 */
UCLASS()
class THEFACILITY_API AFacilityExit : public AActor
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

	/** True if the location is inside the exit zone */
	bool IsInExitZone(const FVector& Location) const;

protected:

	bool bOpen = false;

	FTimerHandle BindTimer;

	int32 BindAttempts = 0;

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Binds to the game state, retrying until it has replicated to this machine */
	void TryBindToGameState();

	UFUNCTION()
	void HandlePowerRestored();
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityThrown.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class USoundBase;

/**
 *  A bottle or a can in flight. Thrown by a player (LMB while carrying one), it flies, hits something and breaks,
 *  and the crash is a loud noise there that the Tenant goes to investigate: a way to pull it somewhere else.
 *
 *  Server-authoritative: the server spawns it, moves it and decides where it lands and what hears it. It replicates
 *  to everyone so all see it fly; every machine plays the crash itself.
 */
UCLASS()
class THEFACILITY_API AFacilityThrown : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Look;

	UPROPERTY(VisibleAnywhere, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY()
	TMap<FName, TObjectPtr<UObject>> LookAssets;

protected:

	/** Bottle or Can */
	UPROPERTY(ReplicatedUsing=OnRep_ItemId)
	FName ItemId;

	/** How far the crash carries, for the Tenant */
	UPROPERTY(EditAnywhere, Category="Thrown", meta=(Units="cm"))
	float NoiseRadius = 1500.0f;

	/** Launch speed */
	UPROPERTY(EditAnywhere, Category="Thrown", meta=(Units="cm/s"))
	float Speed = 1300.0f;

	bool bBroken = false;

	/** Who threw it, so it never hits them */
	TWeakObjectPtr<AActor> Thrower;

public:

	AFacilityThrown();

	/** Server only, right after spawning: what it is, who threw it and which way */
	void Launch(FName InItemId, AActor* InThrower, const FVector& Direction);

	FName GetItemId() const { return ItemId; }

	/** QA, server: where the last throw broke, and how many have broken in this process */
	static FVector GetLastBreakLocation();
	static int32 GetBreakCount();

	/** QA, any machine: crashes this machine has played */
	static int32 GetBreaksSeen();

protected:

	UFUNCTION()
	void OnRep_ItemId();

	UFUNCTION()
	void HandleStop(const FHitResult& Hit);

	/** Every machine: the crash where it landed */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Break(FVector_NetQuantize Location);

	void ApplyLook();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

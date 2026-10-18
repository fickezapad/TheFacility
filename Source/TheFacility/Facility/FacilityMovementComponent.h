// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FacilityMovementComponent.generated.h"

/**
 *  Character movement for Facility players: walk, a quiet sneak, a short stamina-limited sprint, and a slow exhausted walk.
 *  Sprint and sneak intent travel inside the movement prediction (a compressed flag on every saved move), so the server
 *  runs the same simulation as the owning client. Nothing the client says can make it faster than the server allows,
 *  and a sprinting client doesn't rubber-band the way a locally set MaxWalkSpeed does.
 */
UCLASS()
class THEFACILITY_API UFacilityMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Facility;

public:

	UFacilityMovementComponent();

	/** Owning client input. Takes effect through the predicted move on both client and server */
	void SetWantsToSprint(bool bWants) { bWantsToSprint = bWants; }

	/** Owning client input: a slow walk that the Tenant can't hear. Predicted like the sprint */
	void SetWantsToSneak(bool bWants) { bWantsToSneak = bWants; }

	/** Normal walking speed. The Marked paces its chase against this, not against a sprint */
	float GetWalkSpeed() const { return WalkSpeed; }

	/** True while actually running faster than a walk */
	UFUNCTION(BlueprintPure, Category="Facility|Sprint")
	bool IsSprinting() const;

	/** Holding the sneak walk and not sprinting */
	UFUNCTION(BlueprintPure, Category="Facility|Sprint")
	bool IsSneaking() const;

	/** Out of breath: slow until stamina recovers past RecoverThreshold */
	UFUNCTION(BlueprintPure, Category="Facility|Sprint")
	bool IsExhausted() const { return bExhausted; }

	/** 0..1, as this machine simulates it. Used by the owner's HUD */
	UFUNCTION(BlueprintPure, Category="Facility|Sprint")
	float GetStaminaFraction() const;

	virtual float GetMaxSpeed() const override;

	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:

	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0, Units="cm/s"))
	float WalkSpeed = 320.0f;

	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0, Units="cm/s"))
	float SprintSpeed = 540.0f;

	/** Speed while sneaking. Below the Tenant's hearing threshold for footsteps (QuietSpeed on the character) */
	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0, Units="cm/s"))
	float SneakSpeed = 150.0f;

	/** Speed while out of breath */
	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0, Units="cm/s"))
	float ExhaustedSpeed = 220.0f;

	/** How long a full bar of stamina lasts while sprinting */
	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0.1, Units="s"))
	float StaminaSeconds = 5.0f;

	/** Time to refill from empty to full when not sprinting */
	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0.1, Units="s"))
	float StaminaRefillSeconds = 7.0f;

	/** After running dry, the share of stamina needed before sprinting works again */
	UPROPERTY(EditAnywhere, Category="Facility|Sprint", meta=(ClampMin=0, ClampMax=1))
	float RecoverThreshold = 0.4f;

	virtual void BeginPlay() override;

	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

	bool bWantsToSprint = false;

	bool bWantsToSneak = false;

	float Stamina = 0.0f;

	bool bExhausted = false;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FacilityInteractionComponent.generated.h"

/**
 *  Lets the owning pawn use IFacilityInteractable actors.
 *  The owning client traces to find a focus target and shows a prompt.
 *  Interacting sends a server RPC; the server re-validates range and line of sight before anything happens.
 */
UCLASS(ClassGroup=(Facility), meta=(BlueprintSpawnableComponent))
class THEFACILITY_API UFacilityInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFacilityInteractionComponent();

	/** Called from input on the owning client. Asks the server to use the focused actor */
	UFUNCTION(BlueprintCallable, Category="Interaction")
	void TryInteract();

	/** Actor currently under the crosshair. Only valid on the owning client */
	UFUNCTION(BlueprintPure, Category="Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	/** Prompt for the focused actor. Only valid on the owning client */
	UFUNCTION(BlueprintPure, Category="Interaction")
	FText GetFocusedPrompt() const;

protected:

	/** How far the player can reach, in cm */
	UPROPERTY(EditAnywhere, Category="Interaction", meta=(ClampMin=0, Units="cm"))
	float InteractRange = 250.0f;

	/** Extra distance the server accepts on top of InteractRange, to absorb movement during latency and the camera sitting slightly off the pawn eye point */
	UPROPERTY(EditAnywhere, Category="Interaction", meta=(ClampMin=0, Units="cm"))
	float ServerRangeTolerance = 50.0f;

	/** Trace channel used to find interactables */
	UPROPERTY(EditAnywhere, Category="Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Server, Reliable)
	void Server_Interact(AActor* Target);

	/** Server side check that the pawn can actually reach and see the target */
	bool ValidateTarget(AActor* Target) const;

	/** True if a trace from ViewLocation to Point reaches Target without anything else in the way */
	bool HasLineOfSight(const AActor* Target, const FVector& ViewLocation, const FVector& Point) const;

	/** Finds the interactable under the crosshair, or null */
	AActor* TraceForInteractable() const;

	bool GetOwnerView(FVector& OutLocation, FRotator& OutRotation) const;

	TWeakObjectPtr<AActor> FocusedActor;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FacilityInteractable.generated.h"

class APawn;

UINTERFACE(MinimalAPI, Blueprintable)
class UFacilityInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 *  Anything a player can use with the Interact input.
 *  CanInteract and GetInteractPrompt may run on any machine.
 *  Interact only ever runs on the server, after the interaction component has validated range and line of sight.
 */
class THEFACILITY_API IFacilityInteractable
{
	GENERATED_BODY()

public:

	/** Returns true if the given pawn may use this right now */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction")
	bool CanInteract(APawn* InstigatorPawn) const;

	/** Text shown to the local player while looking at this */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction")
	FText GetInteractPrompt(APawn* InstigatorPawn) const;

	/** Server only. Performs the interaction */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction")
	void Interact(APawn* InstigatorPawn);
};

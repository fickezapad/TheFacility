// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "FacilityMenuGameMode.generated.h"

/** Game mode for the main menu map: no pawn, just the menu */
UCLASS()
class THEFACILITY_API AFacilityMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AFacilityMenuGameMode();
};

/** Shows the main menu for the local player, looking through the menu map's camera */
UCLASS()
class THEFACILITY_API AFacilityMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

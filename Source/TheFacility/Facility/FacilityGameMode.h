// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TheFacilityGameMode.h"
#include "FacilityGameMode.generated.h"

/**
 *  Game mode for Facility runs.
 *  Uses the Facility game state and debug HUD; pawn and controller are set in the Blueprint subclass.
 */
UCLASS()
class THEFACILITY_API AFacilityGameMode : public ATheFacilityGameMode
{
	GENERATED_BODY()

public:

	AFacilityGameMode();
};

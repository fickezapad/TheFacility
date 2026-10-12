// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGameMode.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityHUD.h"

AFacilityGameMode::AFacilityGameMode()
{
	GameStateClass = AFacilityGameState::StaticClass();
	HUDClass = AFacilityHUD::StaticClass();
}

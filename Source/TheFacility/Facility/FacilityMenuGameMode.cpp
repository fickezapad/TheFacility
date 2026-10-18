// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMenuGameMode.h"
#include "Facility/FacilityGameInstance.h"
#include "TheFacility.h"

AFacilityMenuGameMode::AFacilityMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AFacilityMenuPlayerController::StaticClass();
}

void AFacilityMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	if (UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>())
	{
		GameInstance->ShowMainMenu(this);
	}
	else
	{
		UE_LOG(LogTheFacility, Error, TEXT("The main menu needs UFacilityGameInstance. Set GameInstanceClass in DefaultEngine.ini"));
	}
}

void AFacilityMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>())
	{
		GameInstance->HideMainMenu();
	}

	Super::EndPlay(EndPlayReason);
}

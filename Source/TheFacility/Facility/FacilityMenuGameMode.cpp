// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMenuGameMode.h"
#include "Facility/FacilityGameInstance.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
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

	// the menu map is the motel at night, seen from the lot through its camera
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		SetViewTarget(*It);
		break;
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

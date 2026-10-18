// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityMenuWidgets.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TheFacility.h"

/** Sees Escape and the gamepad's start button before anything else, so the menu opens whatever the pawn is doing */
class FFacilityMenuKeyProcessor : public IInputProcessor
{
public:

	explicit FFacilityMenuKeyProcessor(UFacilityGameInstance* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		const FKey Key = InKeyEvent.GetKey();
		if (InKeyEvent.IsRepeat() || (Key != EKeys::Escape && Key != EKeys::Gamepad_Special_Right))
		{
			return false;
		}

		UFacilityGameInstance* GameInstance = Owner.Get();
		return GameInstance && GameInstance->HandleMenuKey();
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("FacilityMenuKey"); }

private:

	TWeakObjectPtr<UFacilityGameInstance> Owner;
};

void UFacilityGameInstance::Init()
{
	Super::Init();

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UFacilityGameInstance::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UFacilityGameInstance::HandleTravelFailure);
	}

	if (FSlateApplication::IsInitialized())
	{
		MenuKeyProcessor = MakeShared<FFacilityMenuKeyProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(MenuKeyProcessor);
	}
}

void UFacilityGameInstance::Shutdown()
{
	HidePauseMenu();
	HideMainMenu();

	if (MenuKeyProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(MenuKeyProcessor);
	}
	MenuKeyProcessor.Reset();

	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	Super::Shutdown();
}

FString UFacilityGameInstance::SanitizePlayerName(const FString& Name)
{
	FString Clean;
	for (const TCHAR Char : Name.TrimStartAndEnd())
	{
		Clean.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('-') || Char == TEXT('_') ? Char : TEXT('_'));
	}
	return Clean.Left(20);
}

FString UFacilityGameInstance::GetPlayerName() const
{
	if (!LastPlayerName.IsEmpty())
	{
		return LastPlayerName;
	}

	const FString UserName = SanitizePlayerName(FPlatformProcess::UserName());
	return UserName.IsEmpty() ? TEXT("Guest") : UserName;
}

void UFacilityGameInstance::RememberPlayer(const FString& PlayerName, const FString& Address)
{
	LastPlayerName = SanitizePlayerName(PlayerName);
	if (!Address.IsEmpty())
	{
		LastAddress = Address.TrimStartAndEnd();
	}
	SaveConfig();
}

void UFacilityGameInstance::HostRun(const FString& PlayerName)
{
	RememberPlayer(PlayerName, FString());
	HideMainMenu();

	UE_LOG(LogTheFacility, Log, TEXT("Hosting a run on %s as %s"), *RunMap, *GetPlayerName());
	UGameplayStatics::OpenLevel(GetWorld(), FName(*RunMap), true, FString::Printf(TEXT("listen?PlayerName=%s"), *GetPlayerName()));
}

void UFacilityGameInstance::JoinRun(const FString& Address, const FString& PlayerName)
{
	const FString Target = Address.TrimStartAndEnd();
	APlayerController* PlayerController = GetFirstLocalPlayerController();
	if (Target.IsEmpty() || !PlayerController)
	{
		return;
	}

	RememberPlayer(PlayerName, Target);
	HideMainMenu();

	UE_LOG(LogTheFacility, Log, TEXT("Joining a run at %s as %s"), *Target, *GetPlayerName());
	PlayerController->ClientTravel(FString::Printf(TEXT("%s?PlayerName=%s"), *Target, *GetPlayerName()), TRAVEL_Absolute);
}

void UFacilityGameInstance::LeaveRun()
{
	HidePauseMenu();

	UE_LOG(LogTheFacility, Log, TEXT("Leaving the run%s"), IsHosting() ? TEXT(", which ends it for everyone") : TEXT(""));
	// closing a listen server drops every client, and they come back to their own menu with a message
	UGameplayStatics::OpenLevel(GetWorld(), FName(*MenuMap), true);
}

void UFacilityGameInstance::QuitToDesktop()
{
	HidePauseMenu();
	HideMainMenu();
	UKismetSystemLibrary::QuitGame(GetWorld(), GetFirstLocalPlayerController(), EQuitPreference::Quit, false);
}

bool UFacilityGameInstance::IsHosting() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_ListenServer;
}

bool UFacilityGameInstance::HandleMenuKey()
{
	const UWorld* World = GetWorld();
	// in the editor, Escape belongs to Play In Editor (it stops the session); the main menu has its own buttons
	if (!World || World->IsPlayInEditor() || MainMenu.IsValid())
	{
		return false;
	}

	if (PauseMenu.IsValid())
	{
		HidePauseMenu();
		return true;
	}

	APlayerController* PlayerController = GetFirstLocalPlayerController();
	if (!PlayerController || !PlayerController->GetPawn())
	{
		return false;
	}

	TogglePauseMenu(PlayerController);
	return true;
}

FString UFacilityGameInstance::ConsumeMenuMessage()
{
	FString Message = MoveTemp(MenuMessage);
	MenuMessage.Reset();
	return Message;
}

void UFacilityGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// only the game's own connection matters; the engine takes this machine back to the menu map by itself
	if (NetDriver && NetDriver->NetDriverName != NAME_GameNetDriver && NetDriver->NetDriverName != NAME_PendingNetDriver)
	{
		return;
	}

	UE_LOG(LogTheFacility, Warning, TEXT("Network failure (%s): %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::ConnectionTimeout:
		MenuMessage = TEXT("Lost the connection to the host. The run is over for you.");
		break;
	case ENetworkFailure::PendingConnectionFailure:
		MenuMessage = FString::Printf(TEXT("Couldn't join: %s"), ErrorString.IsEmpty() ? TEXT("the host refused the connection.") : *ErrorString);
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		MenuMessage = TEXT("Couldn't join: you and the host are running different versions.");
		break;
	default:
		MenuMessage = FString::Printf(TEXT("Disconnected: %s"), ErrorString.IsEmpty() ? ENetworkFailure::ToString(FailureType) : *ErrorString);
		break;
	}
}

void UFacilityGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogTheFacility, Warning, TEXT("Travel failure (%s): %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	MenuMessage = FailureType == ETravelFailure::PendingNetGameCreateFailure || FailureType == ETravelFailure::ServerTravelFailure
		? FString::Printf(TEXT("Couldn't reach the host at %s."), *LastAddress)
		: FString::Printf(TEXT("Couldn't open the run: %s"), ErrorString.IsEmpty() ? ETravelFailure::ToString(FailureType) : *ErrorString);
}

void UFacilityGameInstance::AddMenuWidget(APlayerController* PlayerController, TSharedRef<SWidget> Widget, int32 ZOrder)
{
	UGameViewportClient* Viewport = GetGameViewportClient();
	if (!Viewport || !PlayerController)
	{
		UE_LOG(LogTheFacility, Warning, TEXT("Menu not shown: %s"), !Viewport ? TEXT("no game viewport") : TEXT("no player controller"));
		return;
	}

	Viewport->AddViewportWidgetContent(Widget, ZOrder);
	MenuOwner = PlayerController;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
}

FString UFacilityGameInstance::DescribeMenus() const
{
	const auto Describe = [](const TSharedPtr<SWidget>& Widget)
	{
		if (!Widget.IsValid())
		{
			return FString(TEXT("none"));
		}
		const FGeometry& Geometry = Widget->GetTickSpaceGeometry();
		return FString::Printf(TEXT("size %s at %s, %s"), *Geometry.GetLocalSize().ToString(), *Geometry.GetAbsolutePosition().ToString(),
			*Widget->GetVisibility().ToString());
	};
	return FString::Printf(TEXT("main menu: %s; in-run menu: %s"), *Describe(MainMenu), *Describe(PauseMenu));
}

void UFacilityGameInstance::RemoveMenuWidget(TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid())
	{
		return;
	}

	if (UGameViewportClient* Viewport = GetGameViewportClient())
	{
		Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
}

void UFacilityGameInstance::ShowMainMenu(APlayerController* PlayerController)
{
	if (MainMenu.IsValid() || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	MainMenu = SNew(SFacilityMainMenu)
		.GameInstance(this)
		.PlayerName(GetPlayerName())
		.Address(LastAddress)
		.Message(ConsumeMenuMessage());
	AddMenuWidget(PlayerController, MainMenu.ToSharedRef(), 10);
}

void UFacilityGameInstance::HideMainMenu()
{
	RemoveMenuWidget(MainMenu);
}

void UFacilityGameInstance::TogglePauseMenu(APlayerController* PlayerController)
{
	if (PauseMenu.IsValid())
	{
		HidePauseMenu();
		return;
	}

	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	PauseMenu = SNew(SFacilityPauseMenu)
		.GameInstance(this)
		.bHosting(IsHosting());
	AddMenuWidget(PlayerController, PauseMenu.ToSharedRef(), 20);
}

void UFacilityGameInstance::HidePauseMenu()
{
	if (!PauseMenu.IsValid())
	{
		return;
	}

	RemoveMenuWidget(PauseMenu);
	if (APlayerController* PlayerController = MenuOwner.Get())
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->SetShowMouseCursor(false);
	}
}

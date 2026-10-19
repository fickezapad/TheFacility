// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityMenuWidgets.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityVoiceComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/VoiceCapture.h"
#include "VoiceModule.h"
#include "Interfaces/VoiceInterface.h"
#include "OnlineSubsystemUtils.h"
#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
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

	ApplySoundSettings();
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
	ProbeMicrophone();
	AddMenuWidget(PlayerController, MainMenu.ToSharedRef(), 10);
	SetMeterActive(true);
}

void UFacilityGameInstance::HideMainMenu()
{
	SetMeterActive(false);
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

void UFacilityGameInstance::ShowMenuSettings(bool bShow)
{
	if (PauseMenu.IsValid())
	{
		StaticCastSharedPtr<SFacilityPauseMenu>(PauseMenu)->ShowSettings(bShow);
	}
	else if (MainMenu.IsValid())
	{
		StaticCastSharedPtr<SFacilityMainMenu>(MainMenu)->ShowSettings(bShow);
	}
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

void UFacilityGameInstance::SetVoiceMode(uint8 Mode)
{
	VoiceMode = FMath::Min<uint8>(Mode, 2);
	SaveConfig();
}

void UFacilityGameInstance::SetMicSensitivity(float Gain)
{
	MicSensitivity = FMath::Clamp(Gain, 0.25f, 4.0f);
	SaveConfig();
}

void UFacilityGameInstance::SetNoiseGate(float Threshold)
{
	NoiseGate = FMath::Clamp(Threshold, 0.0f, 0.3f);
	SaveConfig();
}

void UFacilityGameInstance::SetLookSensitivity(float Multiplier)
{
	LookSensitivity = FMath::Clamp(Multiplier, 0.2f, 3.0f);
	SaveConfig();
}

void UFacilityGameInstance::SetMasterVolume(float Volume)
{
	MasterVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	ApplySoundSettings();
	SaveConfig();
}

void UFacilityGameInstance::SetVoiceVolume(float Volume)
{
	VoiceVolume = FMath::Clamp(Volume, 0.0f, 2.0f);
	ApplySoundSettings();
	SaveConfig();
}

void UFacilityGameInstance::SetBrightness(float Stops)
{
	Brightness = FMath::Clamp(Stops, -1.5f, 2.0f);
	ApplyBrightnessToAll();
	SaveConfig();
}

void UFacilityGameInstance::ApplySoundSettings()
{
	if (GEngine)
	{
		if (FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice())
		{
			AudioDevice->SetTransientPrimaryVolume(MasterVolume);
		}
	}

	// remote voices play through the engine's VoiP sound class (Config/DefaultEngine.ini: SC_Voice)
	const FSoftObjectPath VoiceClassPath = GetDefault<UAudioSettings>()->VoiPSoundClass;
	if (USoundClass* VoiceClass = VoiceClassPath.IsValid() ? Cast<USoundClass>(VoiceClassPath.TryLoad()) : nullptr)
	{
		VoiceClass->Properties.Volume = VoiceVolume;
	}
}

void UFacilityGameInstance::ApplyBrightness(AFacilityCharacter* Character) const
{
	UCameraComponent* Camera = Character ? Character->GetFirstPersonCameraComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!Camera || !World)
	{
		return;
	}

	// the camera's settings win over the level's, so start from the level's own exposure bias
	float LevelBias = 0.0f;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->bUnbound && It->bEnabled && It->Settings.bOverride_AutoExposureBias)
		{
			LevelBias = It->Settings.AutoExposureBias;
			break;
		}
	}
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = LevelBias + Brightness;
	Camera->PostProcessBlendWeight = 1.0f;
}

void UFacilityGameInstance::ApplyBrightnessToAll() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AFacilityCharacter> It(World); It; ++It)
		{
			ApplyBrightness(*It);
		}
	}
}

void UFacilityGameInstance::ApplyVoiceSettings(APlayerController* PlayerController)
{
	ProbeMicrophone();
	ApplySoundSettings();

	// the engine's own capture reads these each time it gathers microphone audio
	if (IConsoleVariable* Gain = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicInputGain")))
	{
		Gain->Set(MicSensitivity, ECVF_SetByGameSetting);
	}
	if (IConsoleVariable* Gate = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicNoiseGateThreshold")))
	{
		Gate->Set(NoiseGate, ECVF_SetByGameSetting);
	}

	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	// joining by IP there is no online session to register talkers, so the local player is registered here
	if (bMicrophoneFound)
	{
		IOnlineVoicePtr Voice = Online::GetVoiceInterface(GetWorld());
		if (Voice.IsValid())
		{
			Voice->RegisterLocalTalker(0);
		}
	}

	if (VoiceMode == static_cast<uint8>(EFacilityVoiceMode::OpenMic) && bMicrophoneFound)
	{
		PlayerController->StartTalking();
	}
	else
	{
		PlayerController->StopTalking();
	}

	if (const AFacilityCharacter* Character = Cast<AFacilityCharacter>(PlayerController->GetPawn()))
	{
		if (UFacilityVoiceComponent* Voice = Character->GetVoice())
		{
			Voice->Server_SetVoiceState(static_cast<EFacilityVoiceMode>(VoiceMode), bMicrophoneFound);
		}
	}
	UE_LOG(LogTheFacility, Log, TEXT("Voice: %s, gain %.2f, gate %.2f, microphone %s"),
		*StaticEnum<EFacilityVoiceMode>()->GetNameStringByValue(VoiceMode), MicSensitivity, NoiseGate, bMicrophoneFound ? TEXT("found") : TEXT("not found"));
}

void UFacilityGameInstance::SetTalkKeyHeld(APlayerController* PlayerController, bool bHeld)
{
	if (!PlayerController || VoiceMode != static_cast<uint8>(EFacilityVoiceMode::PushToTalk) || !bMicrophoneFound)
	{
		return;
	}

	if (bHeld)
	{
		PlayerController->StartTalking();
	}
	else
	{
		PlayerController->StopTalking();
	}
}

void UFacilityGameInstance::ProbeMicrophone()
{
	if (bMicrophoneProbed || IsDedicatedServerInstance())
	{
		return;
	}

	// is there a microphone at all? Opening a capture once answers it. Not in Init: the Voice module isn't up yet there
	bMicrophoneProbed = true;
	FModuleManager::Get().LoadModule(TEXT("Voice"));
	const bool bWasActive = MeterCapture.IsValid();
	SetMeterActive(true);
	if (!bWasActive)
	{
		SetMeterActive(false);
	}
	UE_LOG(LogTheFacility, Log, TEXT("Voice: microphone %s"), bMicrophoneFound ? TEXT("found") : TEXT("not found: voice off, breathing on"));
}

void UFacilityGameInstance::SetMeterActive(bool bActive)
{
	if (!bActive)
	{
		if (MeterCapture.IsValid())
		{
			MeterCapture->Shutdown();
			MeterCapture.Reset();
		}
		return;
	}

	if (!MeterCapture.IsValid() && FVoiceModule::IsAvailable())
	{
		MeterCapture = FVoiceModule::Get().CreateVoiceCapture(FString());
		const bool bCreated = MeterCapture.IsValid();
		if (MeterCapture.IsValid() && !MeterCapture->Start())
		{
			MeterCapture.Reset();
		}
		bMicrophoneFound = MeterCapture.IsValid();
		UE_LOG(LogTheFacility, Verbose, TEXT("Voice: capture %s (voice module %s)"), bCreated ? (bMicrophoneFound ? TEXT("started") : TEXT("created but would not start")) : TEXT("not created"),
			FVoiceModule::Get().IsVoiceEnabled() ? TEXT("enabled") : TEXT("disabled"));
	}
}

float UFacilityGameInstance::GetMicLevel()
{
	if (!MeterCapture.IsValid())
	{
		return 0.0f;
	}

	// the amplitude follows what has been read, so drain what the device has gathered
	uint32 Available = 0;
	if (MeterCapture->GetCaptureState(Available) == EVoiceCaptureState::Ok && Available > 0)
	{
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(Available);
		uint32 Read = 0;
		MeterCapture->GetVoiceData(Buffer.GetData(), Available, Read);
	}
	return FMath::Clamp(MeterCapture->GetCurrentAmplitude(), 0.0f, 1.0f);
}

FString UFacilityGameInstance::ReadMicSamples(TArray<int16>& OutSamples)
{
	OutSamples.Reset();
	SetMeterActive(true);
	if (!MeterCapture.IsValid())
	{
		return TEXT("NoCapture");
	}

	uint32 Available = 0;
	const EVoiceCaptureState::Type State = MeterCapture->GetCaptureState(Available);
	if (State == EVoiceCaptureState::Ok && Available > 0)
	{
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(Available);
		uint32 Read = 0;
		MeterCapture->GetVoiceData(Buffer.GetData(), Available, Read);
		OutSamples.Append(reinterpret_cast<const int16*>(Buffer.GetData()), Read / sizeof(int16));
	}
	return EVoiceCaptureState::ToString(State);
}

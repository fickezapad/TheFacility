// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "Facility/FacilityShift.h"
#include "FacilityGameInstance.generated.h"

class APlayerController;
class SWidget;
class UNetDriver;

/**
 *  Session flow outside the editor: hosting a run, joining one by address, leaving, and the menus around that.
 *  Lives for the whole process, so it is also where a dropped connection is turned into a message for the main menu.
 *  No online subsystem yet: hosting is a listen server, joining is by IP (LAN, or a forwarded port).
 */
UCLASS(config=Game)
class THEFACILITY_API UFacilityGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:

	virtual void Init() override;

	virtual void Shutdown() override;

	/** Opens the run map as a listen server with this player as host */
	void HostRun(const FString& PlayerName);

	/** Connects to a host. Address is an IP or host name, with an optional :port */
	void JoinRun(const FString& Address, const FString& PlayerName);

	/** Back to the main menu. The host leaving ends the run for everyone */
	void LeaveRun();

	void QuitToDesktop();

	FString GetPlayerName() const;

	const FString& GetLastAddress() const { return LastAddress; }

	/** A message for the main menu (why the last run ended early), cleared once read */
	FString ConsumeMenuMessage();

	/** Main menu, on the menu map. Takes UI input */
	void ShowMainMenu(APlayerController* PlayerController);

	void HideMainMenu();

	/** In-run menu over the game. The night doesn't stop while it is open */
	void TogglePauseMenu(APlayerController* PlayerController);

	void HidePauseMenu();

	bool IsPauseMenuOpen() const { return PauseMenu.IsValid(); }

	/** Turns whichever menu is open to its settings page, or back */
	void ShowMenuSettings(bool bShow);

	/** QA: where the menus are on screen, if anywhere */
	FString DescribeMenus() const;

	/** Voice: how this player uses their microphone, and how it is tuned. Saved in the user's Game.ini */
	uint8 GetVoiceModeIndex() const { return VoiceMode; }
	float GetMicSensitivity() const { return MicSensitivity; }
	float GetNoiseGate() const { return NoiseGate; }
	bool HasMicrophone() const { return bMicrophoneFound; }

	void SetVoiceMode(uint8 Mode);
	void SetMicSensitivity(float Gain);
	void SetNoiseGate(float Threshold);

	/** Applies the voice settings for the local player: engine gain and gate, open mic or not, and tells the server */
	void ApplyVoiceSettings(APlayerController* PlayerController);

	/** Look, sound and picture. Saved in the user's Game.ini */
	float GetLookSensitivity() const { return LookSensitivity; }
	float GetMasterVolume() const { return MasterVolume; }
	float GetVoiceVolume() const { return VoiceVolume; }
	/** Footsteps, doors, creatures, everything that isn't a voice */
	float GetEffectsVolume() const { return EffectsVolume; }
	/** The retro picture: 0 off, 1 low (fine dither), 2 full (coarse, the PS1 look) */
	int32 GetRetroLevel() const { return RetroLevel; }
	/** Exposure offset in stops, added to whatever the level sets */
	float GetBrightness() const { return Brightness; }

	void SetLookSensitivity(float Multiplier);
	void SetMasterVolume(float Volume);
	void SetVoiceVolume(float Volume);
	void SetEffectsVolume(float Volume);
	void SetRetroLevel(int32 Level);
	void SetBrightness(float Stops);

	/** Master volume on the audio device, voice volume on the voice sound class, effects on SC_SFX */
	void ApplySoundSettings();

	/** The retro pass on this world's post-process volumes, at the chosen level */
	void ApplyRetro(UWorld* World);

	/** Brightness on a character's camera, the view this machine sees through when it looks from that character */
	void ApplyBrightness(class AFacilityCharacter* Character) const;

	/** Brightness on every character's camera in the world (the local player may be watching any of them) */
	void ApplyBrightnessToAll() const;

	/** Push-to-talk key held or released */
	void SetTalkKeyHeld(APlayerController* PlayerController, bool bHeld);

	/** The microphone level right now, 0..1, for the menu meter. Only while the main menu is open */
	float GetMicLevel();

	/** QA: opens the menu's capture if needed and reads raw samples gathered since the last call. Returns the capture state name */
	FString ReadMicSamples(TArray<int16>& OutSamples);

	/** True on the machine hosting the current run */
	bool IsHosting() const;

	/** The shift, kept across the map reloads between its nights (server). BeginShift starts a new one at night 1 */
	void BeginShift(int32 Seed) { ShiftSeed = Seed; ShiftNight = 1; ShiftRecords.Reset(); bContinueShift = false; }
	/** The next night of this shift is about to load */
	void ContinueShift() { bContinueShift = true; ++ShiftNight; }
	/** True once, on the load that continues a shift */
	bool ConsumeContinueShift() { const bool bWas = bContinueShift; bContinueShift = false; return bWas; }
	int32 GetShiftSeed() const { return ShiftSeed; }
	int32 GetShiftNight() const { return ShiftNight; }
	TArray<FFacilityNightRecord>& GetShiftRecords() { return ShiftRecords; }

	/** The seed the next night starts with, handed over across the map reload. 0 = none */
	void SetNextRunSeed(int32 Seed) { NextRunSeed = Seed; }
	int32 ConsumeNextRunSeed() { const int32 Seed = NextRunSeed; NextRunSeed = 0; return Seed; }

	/** First-night hints: which ones this machine has shown already. Saved in the user's Game.ini */
	bool HasSeenHint(int32 Hint) const { return (SeenHints & (1u << Hint)) != 0; }
	void MarkHintSeen(int32 Hint);
	void ResetHints();

	/** Escape or the gamepad's start button, seen before the game gets it. Returns true if a menu took it */
	bool HandleMenuKey();

protected:

	/** The map a hosted run starts on */
	UPROPERTY(config)
	FString RunMap = TEXT("/Game/Facility/Lvl_Test_Run");

	/** The map with the main menu */
	UPROPERTY(config)
	FString MenuMap = TEXT("/Game/Facility/Lvl_MainMenu");

	/** Remembered between sessions, in the user's Game.ini */
	UPROPERTY(config)
	FString LastPlayerName;

	UPROPERTY(config)
	FString LastAddress = TEXT("127.0.0.1");

	/** 0 open mic, 1 push-to-talk, 2 off (EFacilityVoiceMode) */
	UPROPERTY(config)
	uint8 VoiceMode = 0;

	/** Microphone input gain */
	UPROPERTY(config)
	float MicSensitivity = 1.0f;

	/** Below this level the microphone sends nothing */
	UPROPERTY(config)
	float NoiseGate = 0.04f;

	/** Multiplies mouse and stick look */
	UPROPERTY(config)
	float LookSensitivity = 1.0f;

	UPROPERTY(config)
	float MasterVolume = 1.0f;

	/** Teammates' voices */
	UPROPERTY(config)
	float VoiceVolume = 1.0f;

	/** Exposure offset in stops */
	UPROPERTY(config)
	float Brightness = 0.0f;

	/** Everything but voices. Below full by default: the night is meant to be heard, not to be loud */
	UPROPERTY(config)
	float EffectsVolume = 0.6f;

	/** 0 off, 1 low, 2 full. Low by default */
	UPROPERTY(config)
	int32 RetroLevel = 1;

	FDelegateHandle PostLoadMapHandle;

	void HandlePostLoadMap(UWorld* World) { ApplyRetro(World); }

	/** Bit per first-night hint already shown on this machine */
	UPROPERTY(config)
	uint32 SeenHints = 0;

	int32 NextRunSeed = 0;

	int32 ShiftSeed = 0;
	int32 ShiftNight = 1;
	bool bContinueShift = false;
	TArray<FFacilityNightRecord> ShiftRecords;

	/** Whether a capture device could be opened at all */
	bool bMicrophoneFound = false;

	/** The menu's own capture, for the level meter */
	TSharedPtr<class IVoiceCapture> MeterCapture;

	/** Opens (or closes) the menu's capture */
	void SetMeterActive(bool bActive);

	/** Finds out once whether there is a microphone */
	void ProbeMicrophone();

	bool bMicrophoneProbed = false;

	FString MenuMessage;

	TSharedPtr<SWidget> MainMenu;

	TSharedPtr<SWidget> PauseMenu;

	TWeakObjectPtr<APlayerController> MenuOwner;

	/** Catches the menu key anywhere in a run, whatever the pawn is doing (down, retained, spectating) */
	TSharedPtr<class IInputProcessor> MenuKeyProcessor;

	FDelegateHandle NetworkFailureHandle;

	FDelegateHandle TravelFailureHandle;

	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	/** Letters, digits, - and _ only, at most 20: it travels inside a URL */
	static FString SanitizePlayerName(const FString& Name);

	void RememberPlayer(const FString& PlayerName, const FString& Address);

	/** Puts a widget over the game and points input at it */
	void AddMenuWidget(APlayerController* PlayerController, TSharedRef<SWidget> Widget, int32 ZOrder);

	void RemoveMenuWidget(TSharedPtr<SWidget>& Widget);
};

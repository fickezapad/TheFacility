// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UFacilityGameInstance;
class SEditableTextBox;
class SWidgetSwitcher;

/**
 *  Settings, shared by the main menu and the in-run menu: look sensitivity, brightness, master and voice volume,
 *  and the microphone (mode, sensitivity, noise gate, and a live meter where the menu can measure it).
 *  Every change applies at once and is saved in the user's Game.ini.
 */
class SFacilitySettingsPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFacilitySettingsPanel) : _bShowMicMeter(true) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UFacilityGameInstance>, GameInstance)
		/** The meter needs the menu's own capture, which only the main menu opens */
		SLATE_ARGUMENT(bool, bShowMicMeter)
		SLATE_EVENT(FOnClicked, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/**
 *  Title screen and main menu, over the motel at night: the name, a few lines on what this is, then name, host a run,
 *  join one by address, settings, quit. Built in Slate so it needs no assets.
 *  Shows why the last run ended early, if it did (a dropped connection, a host that left).
 */
class SFacilityMainMenu : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFacilityMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UFacilityGameInstance>, GameInstance)
		SLATE_ARGUMENT(FString, PlayerName)
		SLATE_ARGUMENT(FString, Address)
		SLATE_ARGUMENT(FString, Message)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Swaps the menu column for the settings page, or back */
	void ShowSettings(bool bShow);

private:

	TWeakObjectPtr<UFacilityGameInstance> GameInstance;

	TSharedPtr<SEditableTextBox> NameBox;

	TSharedPtr<SEditableTextBox> AddressBox;

	TSharedPtr<SWidgetSwitcher> Pages;

	FReply OnHost();

	FReply OnJoin();

	FReply OnQuit();
};

/**
 *  In-run menu over the game: resume, settings, leave the run, quit. The run keeps going behind it.
 *  For the host, leaving ends the run for everyone.
 */
class SFacilityPauseMenu : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFacilityPauseMenu) : _bHosting(false) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UFacilityGameInstance>, GameInstance)
		SLATE_ARGUMENT(bool, bHosting)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Escape (or the gamepad's start or back) closes it again */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Swaps the menu column for the settings page, or back */
	void ShowSettings(bool bShow);

private:

	TWeakObjectPtr<UFacilityGameInstance> GameInstance;

	TSharedPtr<SWidgetSwitcher> Pages;
};

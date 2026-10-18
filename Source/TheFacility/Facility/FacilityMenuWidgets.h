// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UFacilityGameInstance;
class SEditableTextBox;

/**
 *  Main menu: name, host a run, join one by address, quit. Built in Slate so it needs no assets.
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

private:

	TWeakObjectPtr<UFacilityGameInstance> GameInstance;

	TSharedPtr<SEditableTextBox> NameBox;

	TSharedPtr<SEditableTextBox> AddressBox;

	/** Voice mode, sensitivity, noise gate and a live microphone meter */
	TSharedRef<SWidget> MakeVoiceSection();

	FReply OnHost();

	FReply OnJoin();

	FReply OnQuit();
};

/**
 *  In-run menu over the game: resume, leave the run, quit. The run keeps going behind it.
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

private:

	TWeakObjectPtr<UFacilityGameInstance> GameInstance;
};

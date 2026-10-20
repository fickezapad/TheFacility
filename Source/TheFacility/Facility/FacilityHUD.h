// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FacilityHUD.generated.h"

/**
 *  Prototype HUD drawn with the canvas, per viewport.
 *  Shows a crosshair, the interaction prompt, the objective, carried items, stamina, the downed and retained states,
 *  the end-of-run screen and (with facility.QA.HUD 1) a debug panel with the Facility ledger.
 *  Each PIE window draws from its own world, so it shows what that machine actually received.
 */
UCLASS()
class THEFACILITY_API AFacilityHUD : public AHUD
{
	GENERATED_BODY()

protected:

	/** How long the Facility's whisper stays on screen */
	static constexpr float WhisperShowTime = 6.0f;

	virtual void DrawHUD() override;

	void DrawLine(const FString& Text, float X, float& Y, const FLinearColor& Color);

	/** Draws text centred on X */
	void DrawCentered(const FString& Text, float CenterX, float Y, const FLinearColor& Color, class UFont* Font, float Scale = 1.0f);

	/** Outcome, time and the Facility's ledger, once the run is over */
	void DrawRunOver(const class AFacilityGameState* GameState);

	/** What the crew has to do next, while the run is on */
	void DrawObjective(const class AFacilityGameState* GameState, const class AFacilityCharacter* Character);

	/** Tab: the night's whole list, ticked off as it goes */
	void DrawObjectivesPanel(const class AFacilityGameState* GameState, const class AFacilityCharacter* Character);

	/** A bar under the crosshair while the local player holds a search */
	void DrawSearchProgress(const class AFacilityCharacter* Character);

	/** First-night hints: one short line at a time, each shown once per machine */
	void UpdateHints(const class AFacilityGameState* GameState, const class AFacilityCharacter* Character);
	void DrawHint();

	/** Text with a drop shadow, left-aligned */
	void DrawShadowed(const FString& Text, float X, float Y, const FLinearColor& Color, class UFont* Font, float Scale = 1.0f);

	enum EHint : int32 { HintSneak, HintSprint, HintHide, HintSearch, HintOffering, HintVoice, HintObjectives, HintCount };

	/** Queued hints, oldest first; the first one is on screen */
	TArray<int32> HintQueue;

	/** Local time the current hint went up */
	float HintShownAt = -1.0f;

	static constexpr float HintTime = 6.5f;

	/** Hints this HUD has queued already this session, so a trigger fires only once */
	uint32 HintsQueued = 0;

	void QueueHint(int32 Hint);

	/** The container lit up because the local player aims at it */
	TWeakObjectPtr<class AFacilityContainer> HighlightedContainer;
};

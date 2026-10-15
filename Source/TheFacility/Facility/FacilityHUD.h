// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FacilityHUD.generated.h"

/**
 *  Prototype HUD drawn with the canvas, per viewport.
 *  Shows a crosshair, the interaction prompt, the objective, carried items, stamina, the downed and retained states,
 *  the end-of-run screen and (with facility.ShowLedger 1) a debug panel with the Facility ledger.
 *  Each PIE window draws from its own world, so it shows what that machine actually received.
 */
UCLASS()
class THEFACILITY_API AFacilityHUD : public AHUD
{
	GENERATED_BODY()

protected:

	virtual void DrawHUD() override;

	void DrawLine(const FString& Text, float X, float& Y, const FLinearColor& Color);

	/** Draws text centred on X */
	void DrawCentered(const FString& Text, float CenterX, float Y, const FLinearColor& Color, class UFont* Font, float Scale = 1.0f);

	/** Outcome, time and the Facility's ledger, once the run is over */
	void DrawRunOver(const class AFacilityGameState* GameState);

	/** What the crew has to do next, while the run is on */
	void DrawObjective(const class AFacilityGameState* GameState, const class AFacilityCharacter* Character);
};

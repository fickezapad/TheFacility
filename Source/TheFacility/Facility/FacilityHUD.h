// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FacilityHUD.generated.h"

/**
 *  Prototype HUD drawn with the canvas, per viewport.
 *  Shows a crosshair, the interaction prompt, held keys and (with facility.ShowLedger 1) the Facility ledger.
 *  Each PIE window draws from its own world, so it shows what that machine actually received.
 */
UCLASS()
class THEFACILITY_API AFacilityHUD : public AHUD
{
	GENERATED_BODY()

protected:

	virtual void DrawHUD() override;

	void DrawLine(const FString& Text, float X, float& Y, const FLinearColor& Color);
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityHUD.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityInteractionComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarFacilityShowLedger(
	TEXT("facility.ShowLedger"),
	1,
	TEXT("Draw the Facility ledger on the HUD. 0 = off, 1 = on"));

void AFacilityHUD::DrawLine(const FString& Text, float X, float& Y, const FLinearColor& Color)
{
	DrawText(Text, Color, X, Y, GEngine->GetSmallFont());
	Y += 16.0f;
}

void AFacilityHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;

	// crosshair dot
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f), CenterX - 2.0f, CenterY - 2.0f, 4.0f, 4.0f);

	const AFacilityCharacter* Character = Cast<AFacilityCharacter>(GetOwningPawn());

	// interaction prompt
	if (Character && Character->GetInteractionComponent())
	{
		const FText Prompt = Character->GetInteractionComponent()->GetFocusedPrompt();
		if (!Prompt.IsEmpty())
		{
			const FText KeyText = Character->GetInteractKeyText();
			const FString PromptText = KeyText.IsEmpty()
				? Prompt.ToString()
				: FString::Printf(TEXT("[%s] %s"), *KeyText.ToString(), *Prompt.ToString());
			float TextWidth, TextHeight;
			GetTextSize(PromptText, TextWidth, TextHeight, GEngine->GetMediumFont());
			DrawText(PromptText, FLinearColor::White, CenterX - TextWidth * 0.5f, CenterY + 40.0f, GEngine->GetMediumFont());
		}
	}

	if (CVarFacilityShowLedger.GetValueOnGameThread() == 0)
	{
		return;
	}

	// debug panel: which machine this is, what it holds, and the ledger as this machine sees it
	float Y = 20.0f;
	const TCHAR* NetLabel = TEXT("Standalone");
	switch (GetNetMode())
	{
	case NM_ListenServer: NetLabel = TEXT("Host (listen server)"); break;
	case NM_Client: NetLabel = TEXT("Client"); break;
	default: break;
	}
	DrawLine(FString::Printf(TEXT("THE FACILITY  |  %s"), NetLabel), 20.0f, Y, FLinearColor::Yellow);

	if (Character)
	{
		FString Keys;
		for (const FName& Key : Character->GetHeldKeys())
		{
			Keys += Key.ToString() + TEXT(" ");
		}
		DrawLine(FString::Printf(TEXT("Keys: %s"), Keys.IsEmpty() ? TEXT("none") : *Keys), 20.0f, Y, FLinearColor::White);
	}

	const AFacilityGameState* GameState = GetWorld()->GetGameState<AFacilityGameState>();
	if (!GameState)
	{
		DrawLine(TEXT("Ledger: no Facility game state (check the level's game mode)"), 20.0f, Y, FLinearColor::Red);
		return;
	}

	DrawLine(FString::Printf(TEXT("Ledger (%d)"), GameState->GetLedger().Num()), 20.0f, Y, FLinearColor::White);
	for (const FFacilitySignature& Entry : GameState->GetLedger())
	{
		const bool bLocalSigner = Entry.Signer && Entry.Signer == GetOwningPlayerController()->PlayerState;
		const FString Signer = bLocalSigner ? Entry.SignerName + TEXT(" (you)") : Entry.SignerName;
		DrawLine(FString::Printf(TEXT("  %6.1fs  %s signed %s in %s"), Entry.ServerTime, *Signer, *Entry.SignatureId.ToString(), *Entry.ZoneId.ToString()), 20.0f, Y, FLinearColor(1.0f, 0.4f, 0.4f));
	}
}

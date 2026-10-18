// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityHUD.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityExit.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityInteractionComponent.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityMovementComponent.h"
#include "Facility/FacilityTenant.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"

static TAutoConsoleVariable<int32> CVarFacilityShowLedger(
	TEXT("facility.ShowLedger"),
	1,
	TEXT("Draw the Facility ledger on the HUD. 0 = off, 1 = on"));

void AFacilityHUD::DrawLine(const FString& Text, float X, float& Y, const FLinearColor& Color)
{
	DrawText(Text, Color, X, Y, GEngine->GetSmallFont());
	Y += 16.0f;
}

void AFacilityHUD::DrawCentered(const FString& Text, float CenterX, float Y, const FLinearColor& Color, UFont* Font, float Scale)
{
	float TextWidth, TextHeight;
	GetTextSize(Text, TextWidth, TextHeight, Font, Scale);
	// a dark copy underneath keeps it readable over a surface the flashlight has washed out
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.8f), CenterX - TextWidth * 0.5f + 1.5f, Y + 1.5f, Font, Scale);
	DrawText(Text, Color, CenterX - TextWidth * 0.5f, Y, Font, Scale);
}

void AFacilityHUD::DrawObjective(const AFacilityGameState* GameState, const AFacilityCharacter* Character)
{
	// only maps with a way out have a night to survive
	TActorIterator<AFacilityExit> ExitIt(GetWorld());
	if (!ExitIt)
	{
		return;
	}

	FString Objective;
	if (GameState->IsPowerRestored())
	{
		Objective = TEXT("The power is back and the gate is open. Get out. Everyone still standing has to be through the gate.");
	}
	else if (Character && Character->HasKey(TEXT("Fuse")))
	{
		Objective = TEXT("Fit the fuse in the fuse box in the lobby.");
	}
	else if (const APlayerState* Carrier = GameState->GetFuseCarrier(); IsValid(Carrier))
	{
		Objective = FString::Printf(TEXT("%s has the fuse. Get them to the fuse box in the lobby."), *GameState->MakeSignerName(Carrier));
	}
	else
	{
		Objective = TEXT("Restore the power. The fuse box in the lobby is missing a fuse.");
	}

	DrawCentered(Objective, Canvas->ClipX * 0.5f, 18.0f, FLinearColor(0.75f, 0.75f, 0.7f, 0.8f), GEngine->GetSmallFont());
}

void AFacilityHUD::DrawRunOver(const AFacilityGameState* GameState)
{
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);

	const float CenterX = Canvas->ClipX * 0.5f;
	float Y = Canvas->ClipY * 0.22f;

	FString Title;
	FLinearColor TitleColor = FLinearColor(0.9f, 0.85f, 0.7f);
	if (GameState->GetRunState() == EFacilityRunState::Retained)
	{
		Title = TEXT("THE FACILITY KEPT YOU");
		TitleColor = FLinearColor(0.9f, 0.2f, 0.2f);
	}
	else if (GameState->GetLeftBehindNames().Num() > 0)
	{
		Title = FString::Printf(TEXT("YOU GOT OUT. %d LEFT BEHIND"), GameState->GetLeftBehindNames().Num());
	}
	else
	{
		Title = TEXT("YOU GOT OUT");
	}

	DrawCentered(Title, CenterX, Y, TitleColor, GEngine->GetLargeFont(), 1.5f);
	Y += 70.0f;

	const float RunTime = GameState->GetRunTime();
	DrawCentered(FString::Printf(TEXT("%d:%02d    night %d"), FMath::FloorToInt(RunTime / 60.0f), FMath::FloorToInt(RunTime) % 60, GameState->GetRunSeed()),
		CenterX, Y, FLinearColor(0.6f, 0.6f, 0.6f), GEngine->GetMediumFont());
	Y += 40.0f;

	if (GameState->GetEscapedNames().Num() > 0)
	{
		DrawCentered(TEXT("Out: ") + FString::Join(GameState->GetEscapedNames(), TEXT(", ")), CenterX, Y, FLinearColor::White, GEngine->GetMediumFont());
		Y += 26.0f;
	}
	if (GameState->GetLeftBehindNames().Num() > 0)
	{
		DrawCentered(TEXT("Kept: ") + FString::Join(GameState->GetLeftBehindNames(), TEXT(", ")), CenterX, Y, FLinearColor(1.0f, 0.45f, 0.45f), GEngine->GetMediumFont());
		Y += 26.0f;
	}
	Y += 20.0f;

	// the debrief is the Facility's own paperwork about you
	DrawCentered(TEXT("THE LEDGER"), CenterX, Y, FLinearColor(0.8f, 0.6f, 0.6f), GEngine->GetMediumFont());
	Y += 26.0f;
	if (GameState->GetLedger().IsEmpty())
	{
		DrawCentered(TEXT("Nothing was signed."), CenterX, Y, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont());
		Y += 18.0f;
	}
	for (const FFacilitySignature& Entry : GameState->GetLedger())
	{
		const int32 Seconds = FMath::Max(FMath::FloorToInt(static_cast<float>(Entry.ServerTime - GameState->GetRunStartTime())), 0);
		DrawCentered(FString::Printf(TEXT("%d:%02d   %s signed for the %s"), Seconds / 60, Seconds % 60,
			*Entry.SignerName, *Entry.SignatureId.ToString()), CenterX, Y, FLinearColor(0.85f, 0.75f, 0.75f), GEngine->GetSmallFont());
		Y += 18.0f;
	}
	if (!GameState->GetPowerRestoredBy().IsEmpty())
	{
		DrawCentered(FString::Printf(TEXT("Power restored by %s"), *GameState->GetPowerRestoredBy()), CenterX, Y, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont());
		Y += 18.0f;
	}

	DrawCentered(GetNetMode() == NM_Client ? TEXT("Waiting for the host to start another night") : TEXT("Press R for another night"),
		CenterX, Canvas->ClipY * 0.85f, FLinearColor(0.6f, 0.6f, 0.6f), GEngine->GetMediumFont());
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
	const AFacilityGameState* RunGameState = GetWorld()->GetGameState<AFacilityGameState>();

	if (RunGameState && !RunGameState->IsRunInProgress())
	{
		DrawRunOver(RunGameState);
		return;
	}

	// the flash camera going off in your own hands
	if (Character)
	{
		const float SinceFlash = GetWorld()->GetTimeSeconds() - Character->GetLastFlashTime();
		if (SinceFlash < 0.4f)
		{
			DrawRect(FLinearColor(1.0f, 1.0f, 0.95f, 0.85f * (1.0f - SinceFlash / 0.4f)), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
		}
	}

	AFacilityMarked* Marked = nullptr;
	for (TActorIterator<AFacilityMarked> It(GetWorld()); It; ++It)
	{
		Marked = *It;
		break;
	}

	// the mark can't see the creature, but feels it: a red pulse at the screen edges that grows as it gets closer
	if (Character && Marked && Marked->IsLocalPlayerMarked())
	{
		const float Dread = Marked->GetDreadAt(Character->GetActorLocation());
		if (Dread > 0.0f)
		{
			const float Time = GetWorld()->GetTimeSeconds();
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * (3.0f + 7.0f * Dread));
			const FLinearColor Edge(0.5f, 0.0f, 0.0f, Dread * (0.25f + 0.35f * Pulse));
			const float Thickness = 30.0f + 90.0f * Dread;
			DrawRect(Edge, 0.0f, 0.0f, Canvas->ClipX, Thickness);
			DrawRect(Edge, 0.0f, Canvas->ClipY - Thickness, Canvas->ClipX, Thickness);
			DrawRect(Edge, 0.0f, Thickness, Thickness, Canvas->ClipY - 2.0f * Thickness);
			DrawRect(Edge, Canvas->ClipX - Thickness, Thickness, Thickness, Canvas->ClipY - 2.0f * Thickness);
		}
	}

	if (Character && Character->IsDowned())
	{
		bool bRecoversAlone = false;
		const int32 SecondsLeft = FMath::CeilToInt(Character->GetDownedTimeLeft(bRecoversAlone));
		const FString DownText = bRecoversAlone
			? FString::Printf(TEXT("You are down. Nobody can reach you. The Facility lets you go, once. %d"), SecondsLeft)
			: FString::Printf(TEXT("You are down. A teammate has %d s to help you up."), SecondsLeft);
		DrawCentered(DownText, CenterX, CenterY - 60.0f, FLinearColor(1.0f, 0.3f, 0.3f), GEngine->GetMediumFont());
	}

	if (Character && Character->IsRetained())
	{
		const APawn* Watched = Cast<APawn>(GetOwningPlayerController()->GetViewTarget());
		const FString WatchedName = (Watched && Watched != Character && Watched->GetPlayerState()) ? Watched->GetPlayerState()->GetPlayerName() : TEXT("nobody");
		DrawCentered(TEXT("The Facility has kept you."), CenterX, Canvas->ClipY * 0.75f, FLinearColor(1.0f, 0.3f, 0.3f), GEngine->GetMediumFont());
		DrawCentered(FString::Printf(TEXT("Watching %s.  [LMB] next"), *WatchedName), CenterX, Canvas->ClipY * 0.75f + 26.0f, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont());
	}

	if (RunGameState && Character && !Character->IsRetained())
	{
		DrawObjective(RunGameState, Character);
	}

	// stamina: only while it isn't full
	if (const UFacilityMovementComponent* Movement = Character ? Character->GetFacilityMovement() : nullptr)
	{
		const float Stamina = Movement->GetStaminaFraction();
		if (Stamina < 0.999f && Character->IsStanding())
		{
			const float Width = 220.0f;
			const FLinearColor BarColor = Movement->IsExhausted() ? FLinearColor(0.7f, 0.2f, 0.2f, 0.7f) : FLinearColor(0.8f, 0.8f, 0.75f, 0.5f);
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f), CenterX - Width * 0.5f, Canvas->ClipY - 60.0f, Width, 5.0f);
			DrawRect(BarColor, CenterX - Width * 0.5f, Canvas->ClipY - 60.0f, Width * Stamina, 5.0f);
		}

		if (Movement->IsSneaking() && Character->IsStanding())
		{
			DrawCentered(TEXT("sneaking"), CenterX, Canvas->ClipY - 80.0f, FLinearColor(0.6f, 0.65f, 0.7f, 0.6f), GEngine->GetSmallFont());
		}
	}

	// what this player carries
	if (Character && Character->IsStanding() && Character->GetHeldKeys().Num() > 0)
	{
		FString Items;
		for (const FName& Key : Character->GetHeldKeys())
		{
			Items += (Items.IsEmpty() ? TEXT("") : TEXT(",  ")) + AFacilityCharacter::GetItemName(Key).ToString();
			if (Key == AFacilityCharacter::FlashCameraId)
			{
				Items += TEXT(" [LMB]");
			}
			else if (Key == AFacilityDoor::MasterKeyId)
			{
				Items += TEXT(" [LMB on a closed door]");
			}
		}
		DrawText(TEXT("Carrying: ") + Items, FLinearColor(0.85f, 0.85f, 0.8f, 0.85f), 20.0f, Canvas->ClipY - 40.0f, GEngine->GetSmallFont());
	}

	// interaction prompt
	if (Character && Character->IsStanding() && Character->GetInteractionComponent())
	{
		const FText Prompt = Character->GetInteractionComponent()->GetFocusedPrompt();
		if (!Prompt.IsEmpty())
		{
			const FText KeyText = Character->GetInteractKeyText();
			const FString PromptText = KeyText.IsEmpty()
				? Prompt.ToString()
				: FString::Printf(TEXT("[%s] %s"), *KeyText.ToString(), *Prompt.ToString());
			DrawCentered(PromptText, CenterX, CenterY + 40.0f, FLinearColor::White, GEngine->GetMediumFont());
		}

		if (const AFacilityDoor* Door = Character->GetFocusedLockableDoor())
		{
			DrawCentered(Door->IsLocked() ? TEXT("[LMB] Unlock with the master key") : TEXT("[LMB] Lock with the master key"),
				CenterX, CenterY + 66.0f, FLinearColor(0.85f, 0.8f, 0.6f), GEngine->GetSmallFont());
		}
	}

	if (CVarFacilityShowLedger.GetValueOnGameThread() == 0)
	{
		return;
	}

	// debug panel: which machine this is, what it holds, and the ledger as this machine sees it
	// below the objective line
	float Y = 40.0f;
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

	// maps without a way out have no power to restore, so don't claim it is off
	const bool bHasNight = static_cast<bool>(TActorIterator<AFacilityExit>(GetWorld()));
	DrawLine(FString::Printf(TEXT("Run: %s  seed %d  %.0f s%s"),
		*StaticEnum<EFacilityRunState>()->GetNameStringByValue((int64)GameState->GetRunState()), GameState->GetRunSeed(), GameState->GetRunTime(),
		!bHasNight ? TEXT("") : GameState->IsPowerRestored() ? TEXT("  power on") : TEXT("  power off")), 20.0f, Y, FLinearColor(0.6f, 0.9f, 0.6f));

	if (Marked)
	{
		const APlayerState* MarkState = Marked->GetMark();
		DrawLine(FString::Printf(TEXT("Marked: %s  mark: %s%s"),
			*StaticEnum<EMarkedState>()->GetNameStringByValue((int64)Marked->GetMarkedState()),
			IsValid(MarkState) ? *MarkState->GetPlayerName() : TEXT("none"),
			Marked->IsLocalPlayerMarked() ? TEXT(" (you)") : TEXT("")), 20.0f, Y, FLinearColor(0.8f, 0.6f, 1.0f));
	}

	for (TActorIterator<AFacilityTenant> It(GetWorld()); It; ++It)
	{
		DrawLine(FString::Printf(TEXT("Tenant: %s"), *StaticEnum<ETenantState>()->GetNameStringByValue((int64)It->GetTenantState())), 20.0f, Y, FLinearColor(1.0f, 0.75f, 0.4f));
	}

	DrawLine(FString::Printf(TEXT("Ledger (%d)"), GameState->GetLedger().Num()), 20.0f, Y, FLinearColor::White);
	for (const FFacilitySignature& Entry : GameState->GetLedger())
	{
		const bool bLocalSigner = Entry.Signer && Entry.Signer == GetOwningPlayerController()->PlayerState;
		const FString Signer = bLocalSigner ? Entry.SignerName + TEXT(" (you)") : Entry.SignerName;
		DrawLine(FString::Printf(TEXT("  %6.1fs  %s signed %s in %s"), Entry.ServerTime, *Signer, *Entry.SignatureId.ToString(), *Entry.ZoneId.ToString()), 20.0f, Y, FLinearColor(1.0f, 0.4f, 0.4f));
	}
}

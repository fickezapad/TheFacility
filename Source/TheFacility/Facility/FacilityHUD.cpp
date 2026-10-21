// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityHUD.h"
#include "Facility/FacilityCharacter.h"
#include "Facility/FacilityContainer.h"
#include "Facility/FacilityDirector.h"
#include "Facility/FacilityDoor.h"
#include "Facility/FacilityExit.h"
#include "Facility/FacilityFuseBox.h"
#include "Facility/FacilityGameInstance.h"
#include "Facility/FacilityGameState.h"
#include "Facility/FacilityHidingSpot.h"
#include "Facility/FacilityInteractionComponent.h"
#include "Facility/FacilityMarked.h"
#include "Facility/FacilityMovementComponent.h"
#include "Facility/FacilityPickup.h"
#include "Facility/FacilityTenant.h"
#include "Facility/FacilityNight.h"
#include "Facility/FacilityRegister.h"
#include "Facility/FacilityShift.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"

static TAutoConsoleVariable<int32> CVarFacilityShowLedger(
	TEXT("facility.QA.HUD"),
	0,
	TEXT("QA: draw the debug panel on the HUD (machine, keys, run, creatures, the Facility ledger). 0 = off, 1 = on"));

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

	const AFacilityExit* Exit = *ExitIt;

	// how many fuses the box still needs, for maps where it takes more than one
	int32 Missing = 1;
	int32 Required = 1;
	FString Where = TEXT("in the lobby");
	if (TActorIterator<AFacilityFuseBox> BoxIt(GetWorld()); BoxIt)
	{
		Required = BoxIt->GetFusesRequired();
		Missing = FMath::Max(Required - BoxIt->GetFusesFitted(), 0);
		if (!BoxIt->GetWhereLabel().IsEmpty())
		{
			Where = BoxIt->GetWhereLabel();
		}
	}
	const FString Progress = Required > 1 ? FString::Printf(TEXT(" (%d of %d fitted)"), Required - Missing, Required) : FString();
	const bool bChained = Exit->IsChainedShut();
	const APlayerState* KeyCarrier = GameState->GetGateKeyCarrier();

	FString Objective;
	if (GameState->IsPowerRestored() && !bChained)
	{
		Objective = TEXT("The gate is open. Get out. Everyone still standing has to be through the gate.");
	}
	else if (Character && Character->HasKey(TEXT("Fuse")))
	{
		Objective = FString::Printf(TEXT("Fit the fuse in the fuse box %s."), *Where) + Progress;
	}
	else if (Character && Character->HasKey(AFacilityCharacter::GateKeyId))
	{
		Objective = TEXT("You have the gate key. Unlock the chain on the gate by the road.");
	}
	else if (GameState->IsPowerRestored())
	{
		Objective = IsValid(KeyCarrier)
			? FString::Printf(TEXT("The power is back, but the gate is chained. %s has the key."), *GameState->MakeSignerName(KeyCarrier))
			: TEXT("The power is back, but the gate is chained. Find the gate key.");
	}
	else if (const APlayerState* Carrier = GameState->GetFuseCarrier(); IsValid(Carrier))
	{
		Objective = FString::Printf(TEXT("%s has a fuse. Get them to the fuse box %s."), *GameState->MakeSignerName(Carrier), *Where) + Progress;
	}
	else
	{
		const FString Fuses = Missing > 1
			? FString::Printf(TEXT("Restore the power: the fuse box %s is missing %d fuses."), *Where, Missing)
			: FString::Printf(TEXT("Restore the power: the fuse box %s is missing a fuse."), *Where) + Progress;
		Objective = bChained ? Fuses + TEXT(" The gate is chained, too.") : Fuses;
	}

	Objective = FString::Printf(TEXT("Night %d of %d.  "), GameState->GetNight(), FacilityShift::NightsPerShift) + Objective;
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
	Y += 10.0f;
	if (!GameState->GetPowerRestoredBy().IsEmpty())
	{
		DrawCentered(FString::Printf(TEXT("Power restored by %s"), *GameState->GetPowerRestoredBy()), CenterX, Y, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont());
		Y += 18.0f;
	}
	for (TActorIterator<AFacilityExit> It(GetWorld()); It; ++It)
	{
		if (!It->GetUnchainedBy().IsEmpty())
		{
			DrawCentered(FString::Printf(TEXT("Gate unchained by %s"), *It->GetUnchainedBy()), CenterX, Y, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont());
			Y += 18.0f;
		}
	}
	if (AFacilityDirector::Get(GetWorld()) || GameState->GetPeakAttention() > 0)
	{
		DrawCentered(FString::Printf(TEXT("The Facility's attention peaked at %d (tier %d)"), GameState->GetPeakAttention(), GameState->GetPeakTier()),
			CenterX, Y, FLinearColor(0.85f, 0.45f, 0.42f), GEngine->GetSmallFont());
		Y += 18.0f;
	}

	// the night's score and what was taken, then the shift so far (or, once it is over, the whole shift)
	const TArray<FFacilityNightRecord>& Records = GameState->GetShiftRecords();
	if (!Records.IsEmpty())
	{
		const FFacilityNightRecord& Last = Records.Last();
		Y += 10.0f;
		DrawCentered(FString::Printf(TEXT("NIGHT %d OF %d:  %d POINTS"), Last.Night, FacilityShift::NightsPerShift, Last.Score), CenterX, Y, FLinearColor(0.95f, 0.85f, 0.55f), GEngine->GetMediumFont());
		Y += 24.0f;
		for (const FString& Offering : Last.Offerings)
		{
			DrawCentered(Offering, CenterX, Y, FLinearColor(0.8f, 0.75f, 0.62f), GEngine->GetSmallFont());
			Y += 16.0f;
		}
		if (Last.Offerings.IsEmpty())
		{
			DrawCentered(TEXT("Nothing taken from the motel tonight"), CenterX, Y, FLinearColor(0.6f, 0.6f, 0.6f), GEngine->GetSmallFont());
			Y += 16.0f;
		}
		if (!Last.Creatures.IsEmpty())
		{
			DrawCentered(TEXT("Awake tonight: ") + FString::Join(Last.Creatures, TEXT(", ")), CenterX, Y, FLinearColor(0.7f, 0.5f, 0.5f), GEngine->GetSmallFont());
			Y += 16.0f;
		}
		if (GameState->IsShiftOver())
		{
			Y += 10.0f;
			int32 Total = 0;
			for (const FFacilityNightRecord& Record : Records)
			{
				Total += Record.Score;
				DrawCentered(FString::Printf(TEXT("Night %d   %s   %d:%02d   %d"), Record.Night, Record.bEscaped ? TEXT("got out") : TEXT("all taken"),
					FMath::FloorToInt(Record.Seconds / 60.0f), FMath::FloorToInt(Record.Seconds) % 60, Record.Score), CenterX, Y, FLinearColor(0.75f, 0.75f, 0.72f), GEngine->GetSmallFont());
				Y += 16.0f;
			}
			DrawCentered(FString::Printf(TEXT("THE SHIFT:  %d POINTS"), Total), CenterX, Y + 6.0f, FLinearColor(0.95f, 0.85f, 0.55f), GEngine->GetMediumFont());
		}
	}

	FString Footer;
	if (GameState->GetNextNightTime() > 0.0)
	{
		const int32 Left = FMath::Max(0, FMath::CeilToInt(GameState->GetNextNightTime() - GameState->GetServerWorldTimeSeconds()));
		Footer = FString::Printf(TEXT("Night %d begins in %d"), GameState->GetNight() + 1, Left);
		if (GetNetMode() != NM_Client)
		{
			Footer += TEXT("          R   a new shift          T   this shift again");
		}
	}
	else
	{
		Footer = GetNetMode() == NM_Client ? TEXT("The shift is over. Waiting for the host")
			: FString::Printf(TEXT("R   a new shift          T   shift %d again"), GameState->GetShiftSeed());
	}
	DrawCentered(Footer, CenterX, Canvas->ClipY * 0.9f, FLinearColor(0.6f, 0.6f, 0.6f), GEngine->GetMediumFont());
}

void AFacilityHUD::ShowLine(const FString& Text, float Seconds)
{
	ShownLines.Add({ Text, static_cast<float>(GetWorld()->GetRealTimeSeconds() + Seconds) });
}

void AFacilityHUD::DrawLines()
{
	const float Now = GetWorld()->GetRealTimeSeconds();
	ShownLines.RemoveAll([Now](const FShownLine& Line) { return Line.Until < Now; });
	float Y = Canvas->ClipY * 0.74f;
	for (const FShownLine& Line : ShownLines)
	{
		const float Alpha = FMath::Clamp(Line.Until - Now, 0.0f, 1.0f);
		DrawCentered(TEXT("\"") + Line.Text + TEXT("\""), Canvas->ClipX * 0.5f, Y, FLinearColor(0.78f, 0.82f, 0.86f, Alpha), GEngine->GetMediumFont());
		Y += 26.0f;
	}
}

void AFacilityHUD::DrawRegister(const AFacilityGameState* GameState)
{
	// tonight's page, worked out here from the night's seed like the server did
	const int32 Key = GameState->GetRunSeed() * 10 + GameState->GetNight();
	if (Key != RegisterKey)
	{
		RegisterKey = Key;
		RegisterPage.Reset();
		const FacilityNight::FSite Site = FacilityNight::GatherSite(GetWorld());
		const FacilityNight::FPlan Plan = FacilityNight::Plan(GameState->GetRunSeed(), Site, GameState->GetNight());
		for (const FName Offering : Plan.GetOfferings())
		{
			const int32* Variant = Plan.HintVariant.Find(Offering);
			RegisterPage.Add(FString::Printf(TEXT("%s, %d:  %s"), *FacilityShift::NameOf(Offering).ToString().ToUpper(), FacilityShift::ValueOf(Offering),
				*FacilityShift::HintFor(Plan.BoundTo(Offering), Variant ? *Variant : 0)));
		}
	}

	const float Width = FMath::Min(Canvas->ClipX - 80.0f, 760.0f);
	const float X = (Canvas->ClipX - Width) * 0.5f;
	float Y = Canvas->ClipY * 0.2f;
	const float Height = 70.0f + RegisterPage.Num() * 24.0f;
	DrawRect(FLinearColor(0.16f, 0.13f, 0.09f, 0.92f), X, Y, Width, Height);
	DrawRect(FLinearColor(0.45f, 0.06f, 0.05f, 0.95f), X, Y, Width, 4.0f);
	DrawCentered(FString::Printf(TEXT("THE REGISTER.  NIGHT %d"), GameState->GetNight()), Canvas->ClipX * 0.5f, Y + 12.0f, FLinearColor(0.92f, 0.86f, 0.7f), GEngine->GetMediumFont());
	Y += 44.0f;
	for (const FString& Line : RegisterPage)
	{
		DrawText(Line, FLinearColor(0.85f, 0.8f, 0.68f), X + 20.0f, Y, GEngine->GetSmallFont());
		Y += 24.0f;
	}
	DrawCentered(TEXT("Taken, each pays the crew if its taker gets out. Each wakes something."), Canvas->ClipX * 0.5f, Y + 4.0f, FLinearColor(0.6f, 0.55f, 0.48f), GEngine->GetSmallFont());
}

void AFacilityHUD::DrawShadowed(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale)
{
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.8f), X + 1.5f, Y + 1.5f, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AFacilityHUD::DrawObjectivesPanel(const AFacilityGameState* GameState, const AFacilityCharacter* Character)
{
	TActorIterator<AFacilityExit> ExitIt(GetWorld());
	if (!ExitIt)
	{
		return;
	}
	const AFacilityExit* Exit = *ExitIt;

	int32 Required = 1;
	int32 Fitted = 0;
	FString Where;
	if (TActorIterator<AFacilityFuseBox> BoxIt(GetWorld()); BoxIt)
	{
		Required = BoxIt->GetFusesRequired();
		Fitted = FMath::Min(BoxIt->GetFusesFitted(), Required);
		Where = BoxIt->GetWhereLabel();
	}

	int32 Searched = 0;
	int32 Containers = 0;
	for (TActorIterator<AFacilityContainer> It(GetWorld()); It; ++It)
	{
		++Containers;
		Searched += It->IsSearched() ? 1 : 0;
	}
	TSet<FName> LockedRooms;
	for (TActorIterator<AFacilityDoor> It(GetWorld()); It; ++It)
	{
		if (It->IsLocked() && !It->IsHeldShut() && !It->GetLockGroup().IsNone())
		{
			LockedRooms.Add(It->GetLockGroup());
		}
	}

	const bool bPower = GameState->IsPowerRestored();
	const APlayerState* KeyCarrier = GameState->GetGateKeyCarrier();
	FString KeyState = TEXT("not found yet");
	if (!Exit->IsChainedShut())
	{
		KeyState = TEXT("used, the chain is off");
	}
	else if (Character->HasKey(AFacilityCharacter::GateKeyId))
	{
		KeyState = TEXT("you have it");
	}
	else if (IsValid(KeyCarrier))
	{
		KeyState = FString::Printf(TEXT("%s has it"), *GameState->MakeSignerName(KeyCarrier));
	}

	struct FLine { bool bDone; FString Text; };
	TArray<FLine> Lines;
	Lines.Add({ bPower, FString::Printf(TEXT("Fuses fitted: %d of %d%s"), Fitted, Required, Where.IsEmpty() ? TEXT("") : *(TEXT("   (fuse box ") + Where + TEXT(")"))) });
	Lines.Add({ bPower, TEXT("Power restored") });
	if (Containers > 0 || !Exit->IsChainedShut())
	{
		Lines.Add({ !Exit->IsChainedShut(), TEXT("Gate key: ") + KeyState });
	}
	Lines.Add({ Exit->IsOpen(), TEXT("The gate open") });
	Lines.Add({ false, TEXT("Everyone still standing through the gate") });

	const float X = 40.0f;
	float Y = 64.0f;
	const float Width = 430.0f;
	const float Height = 40.0f + Lines.Num() * 22.0f + (Containers > 0 ? 50.0f : 0.0f);
	DrawRect(FLinearColor(0.01f, 0.01f, 0.012f, 0.72f), X - 16.0f, Y - 14.0f, Width, Height);
	DrawRect(FLinearColor(0.55f, 0.08f, 0.06f, 0.9f), X - 16.0f, Y - 14.0f, 3.0f, Height);
	DrawShadowed(FString::Printf(TEXT("THE NIGHT   %d:%02d"), FMath::FloorToInt(GameState->GetRunTime() / 60.0f), FMath::FloorToInt(GameState->GetRunTime()) % 60),
		X, Y, FLinearColor(0.82f, 0.80f, 0.74f), GEngine->GetMediumFont());
	Y += 34.0f;
	for (const FLine& Line : Lines)
	{
		DrawShadowed(Line.bDone ? TEXT("[x]") : TEXT("[ ]"), X, Y, Line.bDone ? FLinearColor(0.55f, 0.75f, 0.5f) : FLinearColor(0.6f, 0.58f, 0.54f), GEngine->GetSmallFont());
		DrawShadowed(Line.Text, X + 32.0f, Y, Line.bDone ? FLinearColor(0.55f, 0.55f, 0.52f) : FLinearColor(0.88f, 0.84f, 0.72f), GEngine->GetSmallFont());
		Y += 22.0f;
	}
	if (Containers > 0)
	{
		Y += 8.0f;
		DrawShadowed(FString::Printf(TEXT("Searched %d of %d drawers, cabinets and carts"), Searched, Containers), X, Y, FLinearColor(0.6f, 0.58f, 0.54f), GEngine->GetSmallFont());
		Y += 18.0f;
		TArray<FString> Names;
		for (const FName Room : LockedRooms)
		{
			Names.Add(Room.ToString().Replace(TEXT("Room"), TEXT("Room ")));
		}
		Names.Sort();
		DrawShadowed(Names.IsEmpty() ? FString(TEXT("No rooms locked")) : TEXT("Locked: ") + FString::Join(Names, TEXT(", ")) + TEXT("   (the master key opens them)"),
			X, Y, FLinearColor(0.6f, 0.58f, 0.54f), GEngine->GetSmallFont());
	}
}

void AFacilityHUD::DrawSearchProgress(const AFacilityCharacter* Character)
{
	const UFacilityInteractionComponent* Interaction = Character->GetInteractionComponent();
	const AFacilityContainer* Container = Interaction ? Cast<AFacilityContainer>(Interaction->GetFocusedActor()) : nullptr;
	const float Progress = Container ? Container->GetSearchProgressFor(Character) : -1.0f;
	if (Progress < 0.0f)
	{
		return;
	}

	const float Width = 160.0f;
	const float X = Canvas->ClipX * 0.5f - Width * 0.5f;
	const float Y = Canvas->ClipY * 0.5f + 70.0f;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X - 2.0f, Y - 2.0f, Width + 4.0f, 8.0f);
	DrawRect(FLinearColor(0.82f, 0.80f, 0.74f, 0.85f), X, Y, Width * Progress, 4.0f);
}

void AFacilityHUD::QueueHint(int32 Hint)
{
	const UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>();
	if (!GameInstance || GameInstance->HasSeenHint(Hint) || (HintsQueued & (1u << Hint)) != 0)
	{
		return;
	}
	HintsQueued |= 1u << Hint;
	HintQueue.Add(Hint);
}

void AFacilityHUD::UpdateHints(const AFacilityGameState* GameState, const AFacilityCharacter* Character)
{
	// only on a night with a way out, while it lasts
	if (!GameState || !GameState->IsRunInProgress() || !TActorIterator<AFacilityExit>(GetWorld()) || !Character->IsStanding())
	{
		return;
	}

	const float RunTime = GameState->GetRunTime();
	if (RunTime > 3.0f)
	{
		QueueHint(HintSneak);
	}
	if (RunTime > 14.0f)
	{
		QueueHint(HintObjectives);
	}
	if (const UFacilityMovementComponent* Movement = Character->GetFacilityMovement(); Movement && Movement->IsSprinting())
	{
		QueueHint(HintSprint);
	}

	const UFacilityInteractionComponent* Interaction = Character->GetInteractionComponent();
	const AActor* Focused = Interaction ? Interaction->GetFocusedActor() : nullptr;
	if (Cast<AFacilityHidingSpot>(Focused))
	{
		QueueHint(HintHide);
	}
	if (Cast<AFacilityContainer>(Focused))
	{
		QueueHint(HintSearch);
	}
	if (const AFacilityPickup* Pickup = Cast<AFacilityPickup>(Focused); Pickup && Pickup->IsOffering())
	{
		QueueHint(HintOffering);
	}
	for (TActorIterator<AFacilityTenant> It(GetWorld()); It; ++It)
	{
		if (It->GetTenantState() != ETenantState::Dormant)
		{
			QueueHint(HintVoice);
		}
	}
}

void AFacilityHUD::DrawHint()
{
	if (HintQueue.IsEmpty())
	{
		return;
	}

	const float Now = GetWorld()->GetRealTimeSeconds();
	if (HintShownAt < 0.0f)
	{
		HintShownAt = Now;
		if (UFacilityGameInstance* GameInstance = GetGameInstance<UFacilityGameInstance>())
		{
			GameInstance->MarkHintSeen(HintQueue[0]);
		}
	}

	const float Age = Now - HintShownAt;
	if (Age > HintTime)
	{
		HintQueue.RemoveAt(0);
		HintShownAt = -1.0f;
		return;
	}

	static const TCHAR* const Texts[HintCount] = {
		TEXT("Hold Ctrl to sneak. Footsteps carry here."),
		TEXT("Running is loud. Things in this motel listen."),
		TEXT("Closets and beds hide you, but not for long."),
		TEXT("Hold E to search. Rummaging makes a little noise."),
		TEXT("Taking this signs the register. The Facility notices what you take."),
		TEXT("Something is awake. Your voice carries: it hears you talk."),
		TEXT("Hold Tab to see what is left to do."),
	};
	const int32 Hint = HintQueue[0];
	const float Alpha = FMath::Clamp(FMath::Min(Age / 0.5f, (HintTime - Age) / 1.2f), 0.0f, 1.0f);
	const FString Text = Texts[FMath::Clamp(Hint, 0, HintCount - 1)];

	float TextWidth = 0.0f, TextHeight = 0.0f;
	GetTextSize(Text, TextWidth, TextHeight, GEngine->GetMediumFont());
	const float CenterX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.66f;
	DrawRect(FLinearColor(0.01f, 0.01f, 0.012f, 0.6f * Alpha), CenterX - TextWidth * 0.5f - 16.0f, Y - 6.0f, TextWidth + 32.0f, TextHeight + 12.0f);
	DrawRect(FLinearColor(0.55f, 0.08f, 0.06f, 0.9f * Alpha), CenterX - TextWidth * 0.5f - 16.0f, Y - 6.0f, 3.0f, TextHeight + 12.0f);
	DrawCentered(Text, CenterX, Y, FLinearColor(0.88f, 0.84f, 0.72f, Alpha), GEngine->GetMediumFont());
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
		if (Character->IsShowingObjectives())
		{
			DrawObjectivesPanel(RunGameState, Character);
		}
	}

	// what can be searched lights up while it is aimed at
	AFacilityContainer* Aimed = nullptr;
	if (Character && Character->IsLocallyControlled() && Character->GetInteractionComponent())
	{
		Aimed = Cast<AFacilityContainer>(Character->GetInteractionComponent()->GetFocusedActor());
	}
	if (HighlightedContainer.Get() != Aimed)
	{
		if (AFacilityContainer* Previous = HighlightedContainer.Get())
		{
			Previous->SetLocalHighlight(false);
		}
		if (Aimed)
		{
			Aimed->SetLocalHighlight(true);
		}
		HighlightedContainer = Aimed;
	}

	if (Character && Character->IsLocallyControlled())
	{
		UpdateHints(RunGameState, Character);
		DrawSearchProgress(Character);
	}
	DrawHint();
	DrawLines();

	// aiming at the register shows tonight's page
	if (Character && Character->IsLocallyControlled() && RunGameState && Character->GetInteractionComponent()
		&& Cast<AFacilityRegister>(Character->GetInteractionComponent()->GetFocusedActor()))
	{
		DrawRegister(RunGameState);
	}

	// the Facility's whisper: everyone sees it, it fades over a few seconds
	if (const AFacilityDirector* Director = AFacilityDirector::Get(GetWorld()))
	{
		float Age = 0.0f;
		const FString& Whisper = Director->GetWhisper(Age);
		if (!Whisper.IsEmpty() && Age < WhisperShowTime)
		{
			const float Alpha = FMath::Clamp(FMath::Min(Age / 0.6f, (WhisperShowTime - Age) / 1.5f), 0.0f, 1.0f);
			DrawCentered(Whisper, CenterX, Canvas->ClipY * 0.3f, FLinearColor(0.85f, 0.3f, 0.28f, Alpha), GEngine->GetMediumFont());
		}
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

	// what this player carries: one line per item, bottom left, with how to use it
	if (Character && Character->IsStanding() && Character->GetHeldKeys().Num() > 0)
	{
		float ItemY = Canvas->ClipY - 32.0f - 18.0f * (Character->GetHeldKeys().Num() - 1);
		for (const FName& Key : Character->GetHeldKeys())
		{
			const FString Name = AFacilityCharacter::GetItemName(Key).ToString().ToUpper();
			const TCHAR* Use = Key == AFacilityCharacter::FlashCameraId ? TEXT("LMB  flash")
				: Key == AFacilityDoor::MasterKeyId ? TEXT("LMB on a closed door  lock / unlock")
				: Key == AFacilityCharacter::GateKeyId ? TEXT("E on the chained gate")
				: AFacilityCharacter::IsThrowable(Key) ? TEXT("LMB  throw: the crash draws it away")
				: FacilityShift::IsTreasure(Key) ? TEXT("worth points if you get out with it")
				: TEXT("");
			DrawText(Name, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), 25.5f, ItemY + 1.5f, GEngine->GetSmallFont());
			DrawText(Name, FLinearColor(0.88f, 0.84f, 0.72f, 0.9f), 24.0f, ItemY, GEngine->GetSmallFont());
			if (*Use)
			{
				float NameWidth = 0.0f, NameHeight = 0.0f;
				GetTextSize(Name, NameWidth, NameHeight, GEngine->GetSmallFont());
				DrawText(Use, FLinearColor(0.55f, 0.53f, 0.48f, 0.8f), 24.0f + NameWidth + 14.0f, ItemY, GEngine->GetSmallFont());
			}
			ItemY += 18.0f;
		}
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

		if (const AFacilityHidingSpot* Spot = Character->GetHidingSpot())
		{
			const FText KeyText = Character->GetInteractKeyText();
			DrawCentered(FString::Printf(TEXT("Hidden %s.  [%s] come out%s"), Spot->GetKind() == EFacilityHidingKind::Closet ? TEXT("in the closet") : TEXT("under the bed"),
				KeyText.IsEmpty() ? TEXT("E") : *KeyText.ToString(), Spot->GetKind() == EFacilityHidingKind::UnderBed ? TEXT(" (slowly)") : TEXT("")),
				CenterX, Canvas->ClipY * 0.8f, FLinearColor(0.75f, 0.75f, 0.7f), GEngine->GetMediumFont());
			const float Left = Spot->GetTimeLeft();
			if (Left < 15.0f)
			{
				DrawCentered(TEXT("Something is coming to open it."), CenterX, Canvas->ClipY * 0.8f + 28.0f, FLinearColor(0.85f, 0.3f, 0.28f, 0.5f + 0.5f * FMath::Abs(FMath::Sin(GetWorld()->GetTimeSeconds() * 3.0f))), GEngine->GetSmallFont());
			}
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

	if (const AFacilityDirector* Director = AFacilityDirector::Get(GetWorld()))
	{
		DrawLine(FString::Printf(TEXT("Facility: attention %d, tier %d"), Director->GetAttention(), Director->GetTier()), 20.0f, Y, FLinearColor(0.9f, 0.5f, 0.5f));
	}

	DrawLine(FString::Printf(TEXT("Ledger (%d)"), GameState->GetLedger().Num()), 20.0f, Y, FLinearColor::White);
	for (const FFacilitySignature& Entry : GameState->GetLedger())
	{
		const bool bLocalSigner = Entry.Signer && Entry.Signer == GetOwningPlayerController()->PlayerState;
		const FString Signer = bLocalSigner ? Entry.SignerName + TEXT(" (you)") : Entry.SignerName;
		DrawLine(FString::Printf(TEXT("  %6.1fs  %s signed %s in %s"), Entry.ServerTime, *Signer, *Entry.SignatureId.ToString(), *Entry.ZoneId.ToString()), 20.0f, Y, FLinearColor(1.0f, 0.4f, 0.4f));
	}
}

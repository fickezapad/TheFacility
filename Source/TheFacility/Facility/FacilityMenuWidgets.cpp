// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMenuWidgets.h"
#include "Facility/FacilityGameInstance.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FacilityMenu"

namespace FacilityMenuStyle
{
	static const FLinearColor Paper(0.82f, 0.80f, 0.74f);
	static const FLinearColor Dim(0.45f, 0.44f, 0.42f);
	static const FLinearColor Warning(0.85f, 0.45f, 0.38f);
	/** The ledger's red ink */
	static const FLinearColor Ink(0.55f, 0.08f, 0.06f);

	static FSlateFontInfo Font(int32 Size, const FName Weight = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	static TSharedRef<SWidget> Label(const FText& Text, int32 Size = 12, const FLinearColor& Color = Dim)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color);
	}

	static TSharedRef<SWidget> Button(const FText& Text, FOnClicked OnClicked, float Height = 42.0f)
	{
		return SNew(SBox)
			.HeightOverride(Height)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ButtonColorAndOpacity(FLinearColor(0.12f, 0.12f, 0.11f, 0.92f))
				.OnClicked(OnClicked)
				[
					SNew(STextBlock).Text(Text).Font(Font(13, TEXT("Bold"))).ColorAndOpacity(Paper)
				]
			];
	}

	/** A short red rule under a heading */
	static TSharedRef<SWidget> Rule(float Width)
	{
		return SNew(SBox).WidthOverride(Width).HeightOverride(2.0f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Ink)
			];
	}

	/** Dark backing over the whole screen with a fixed-width column in the middle */
	static TSharedRef<SWidget> Screen(float Opacity, TSharedRef<SWidget> Column)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.01f, 0.01f, 0.012f, Opacity))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(440.0f)[Column]
			];
	}
}

using namespace FacilityMenuStyle;

// ---------------------------------------------------------------- settings

void SFacilitySettingsPanel::Construct(const FArguments& InArgs)
{
	const TWeakObjectPtr<UFacilityGameInstance> Weak = InArgs._GameInstance;

	// label, slider over 0..1, and the value as the player reads it
	const auto Slider = [](const FText& Name, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText()> Shown)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.40f).VAlign(VAlign_Center)
			[
				Label(Name, 11, Paper)
			]
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Get]() { return Get(); })
				.OnValueChanged_Lambda([Set](float Value) { Set(Value); })
			]
			+ SHorizontalBox::Slot().FillWidth(0.15f).VAlign(VAlign_Center).HAlign(HAlign_Right)
			[
				SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text_Lambda([Shown]() { return Shown(); })
			];
	};
	const auto Percent = [](float Value) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f))); };

	// one button per voice mode; the chosen one is lit
	const auto ModeButton = [Weak](const FText& Text, uint8 Mode)
	{
		return SNew(SBox).HeightOverride(32.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ButtonColorAndOpacity_Lambda([Weak, Mode]()
				{
					const bool bChosen = Weak.IsValid() && Weak->GetVoiceModeIndex() == Mode;
					return bChosen ? FLinearColor(0.35f, 0.32f, 0.26f) : FLinearColor(0.12f, 0.12f, 0.11f);
				})
				.OnClicked_Lambda([Weak, Mode]()
				{
					if (UFacilityGameInstance* Instance = Weak.Get())
					{
						Instance->SetVoiceMode(Mode);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(Text).Font(Font(10, TEXT("Bold")))
					.ColorAndOpacity_Lambda([Weak, Mode]()
					{
						const bool bChosen = Weak.IsValid() && Weak->GetVoiceModeIndex() == Mode;
						return FSlateColor(bChosen ? Warning : Dim);
					})
				]
			];
	};

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("SettingsTitle", "SETTINGS")).Font(Font(22, TEXT("Bold"))).ColorAndOpacity(Paper)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 18)
		[
			Rule(80.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Slider(LOCTEXT("LookSensitivity", "Mouse sensitivity"),
				[Weak]() { return Weak.IsValid() ? (Weak->GetLookSensitivity() - 0.2f) / 2.8f : 0.29f; },
				[Weak](float Value) { if (Weak.IsValid()) { Weak->SetLookSensitivity(0.2f + Value * 2.8f); } },
				[Weak]() { return FText::FromString(FString::Printf(TEXT("x%.2f"), Weak.IsValid() ? Weak->GetLookSensitivity() : 1.0f)); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Slider(LOCTEXT("Brightness", "Brightness"),
				[Weak]() { return Weak.IsValid() ? (Weak->GetBrightness() + 1.5f) / 3.5f : 0.43f; },
				[Weak](float Value) { if (Weak.IsValid()) { Weak->SetBrightness(Value * 3.5f - 1.5f); } },
				[Weak]() { return FText::FromString(FString::Printf(TEXT("%+.1f"), Weak.IsValid() ? Weak->GetBrightness() : 0.0f)); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Slider(LOCTEXT("MasterVolume", "Master volume"),
				[Weak]() { return Weak.IsValid() ? Weak->GetMasterVolume() : 1.0f; },
				[Weak](float Value) { if (Weak.IsValid()) { Weak->SetMasterVolume(Value); } },
				[Weak, Percent]() { return Percent(Weak.IsValid() ? Weak->GetMasterVolume() : 1.0f); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
		[
			Slider(LOCTEXT("VoiceVolume", "Voice volume"),
				[Weak]() { return Weak.IsValid() ? Weak->GetVoiceVolume() / 2.0f : 0.5f; },
				[Weak](float Value) { if (Weak.IsValid()) { Weak->SetVoiceVolume(Value * 2.0f); } },
				[Weak, Percent]() { return Percent(Weak.IsValid() ? Weak->GetVoiceVolume() : 1.0f); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			Label(LOCTEXT("VoiceLabel", "Your voice. The Tenant hears it too."), 11)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)[ModeButton(LOCTEXT("OpenMic", "OPEN MIC"), 0)]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 2, 0)[ModeButton(LOCTEXT("PushToTalk", "PUSH TO TALK (V)"), 1)]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0, 0, 0)[ModeButton(LOCTEXT("VoiceOff", "OFF"), 2)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Slider(LOCTEXT("MicSensitivity", "Mic sensitivity"),
				[Weak]() { return Weak.IsValid() ? (Weak->GetMicSensitivity() - 0.25f) / 3.75f : 0.2f; },
				[Weak](float Value) { if (Weak.IsValid()) { Weak->SetMicSensitivity(0.25f + Value * 3.75f); } },
				[Weak]() { return FText::FromString(FString::Printf(TEXT("x%.2f"), Weak.IsValid() ? Weak->GetMicSensitivity() : 1.0f)); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Slider(LOCTEXT("NoiseGate", "Noise gate"),
				[Weak]() { return Weak.IsValid() ? Weak->GetNoiseGate() / 0.3f : 0.13f; },
				[Weak](float Value) { if (Weak.IsValid()) { Weak->SetNoiseGate(Value * 0.3f); } },
				[Weak, Percent]() { return Percent(Weak.IsValid() ? Weak->GetNoiseGate() / 0.3f : 0.13f); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
		[
			SNew(SHorizontalBox)
			.Visibility(InArgs._bShowMicMeter ? EVisibility::Visible : EVisibility::Collapsed)
			+ SHorizontalBox::Slot().FillWidth(0.40f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(Font(11))
				.ColorAndOpacity(Paper)
				.Text_Lambda([Weak]()
				{
					return Weak.IsValid() && !Weak->HasMicrophone() ? LOCTEXT("NoMic", "No microphone") : LOCTEXT("MicLevel", "Microphone");
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.60f).VAlign(VAlign_Center)
			[
				SNew(SBox).HeightOverride(10.0f)
				[
					SNew(SProgressBar)
					.FillColorAndOpacity_Lambda([Weak]()
					{
						// red while the noise gate holds it back, paper once it would be sent
						const bool bOpen = Weak.IsValid() && Weak->GetMicLevel() >= Weak->GetNoiseGate();
						return bOpen ? Paper : FLinearColor(0.6f, 0.25f, 0.22f);
					})
					.Percent_Lambda([Weak]() { return Weak.IsValid() ? TOptional<float>(Weak->GetMicLevel()) : TOptional<float>(0.0f); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Button(LOCTEXT("Back", "BACK"), InArgs._OnBack)
		];

	ChildSlot[Column];
}

// ---------------------------------------------------------------- main menu

void SFacilityMainMenu::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;

	const FText Intro = LOCTEXT("Intro",
		"A roadside motel at the edge of the forest, shut for years. The power is out, the gate is chained, "
		"and the guest in Room 7 never checked out.\n\n"
		"Find the fuses. Get the lights back on. Get everyone through the gate.\n"
		"Take what you need, but the register remembers who took what.");
	const FText Controls = LOCTEXT("Controls",
		"WASD move   Shift sprint   Ctrl sneak   E interact   F flashlight   LMB use item   V push-to-talk   Esc menu");

	TSharedRef<SVerticalBox> MainPage = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "THE FACILITY"))
			.Font([]() { FSlateFontInfo Logo = Font(36, TEXT("Bold")); Logo.LetterSpacing = 120; return Logo; }())
			.ColorAndOpacity(Paper)
			.ShadowOffset(FVector2D(2.0f, 2.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 0, 6)
		[
			Rule(140.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 18)
		[
			SNew(STextBlock).Text(LOCTEXT("Subtitle", "Everything you take is written down.")).Font(Font(12, TEXT("Italic"))).ColorAndOpacity(Warning)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 24)
		[
			SNew(STextBlock).Text(Intro).Font(Font(11)).ColorAndOpacity(FLinearColor(0.62f, 0.60f, 0.56f)).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			Label(LOCTEXT("NameLabel", "Your name"), 10)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.Text(FText::FromString(InArgs._PlayerName))
			.Font(Font(13))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
		[
			Button(LOCTEXT("Host", "HOST A NIGHT"), FOnClicked::CreateSP(this, &SFacilityMainMenu::OnHost), 46.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			Label(LOCTEXT("AddressLabel", "Or join a host by address (IP, or IP:port)"), 10)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SAssignNew(AddressBox, SEditableTextBox)
				.Text(FText::FromString(InArgs._Address))
				.Font(Font(13))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
				{
					if (Commit == ETextCommit::OnEnter)
					{
						OnJoin();
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(120.0f)
				[
					Button(LOCTEXT("Join", "JOIN"), FOnClicked::CreateSP(this, &SFacilityMainMenu::OnJoin))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				Button(LOCTEXT("Settings", "SETTINGS"), FOnClicked::CreateLambda([this]() { ShowSettings(true); return FReply::Handled(); }))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0, 0, 0)
			[
				Button(LOCTEXT("Quit", "QUIT"), FOnClicked::CreateSP(this, &SFacilityMainMenu::OnQuit))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(InArgs._Message))
			.Visibility(InArgs._Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			.Font(Font(11))
			.ColorAndOpacity(Warning)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
		[
			SNew(STextBlock).Text(Controls).Font(Font(9)).ColorAndOpacity(Dim).AutoWrapText(true)
		];

	// the menu sits in a dark band down the left; the motel at night shows through on the right
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.008f, 0.008f, 0.01f, 0.82f))
				.Padding(FMargin(56.0f, 36.0f))
				.VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(440.0f)
					[
						SAssignNew(Pages, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()[MainPage]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SFacilitySettingsPanel)
							.GameInstance(GameInstance)
							.bShowMicMeter(true)
							.OnBack(FOnClicked::CreateLambda([this]() { ShowSettings(false); return FReply::Handled(); }))
						]
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(24.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("DemoTag", "DEMO")).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.5f, 0.48f, 0.44f, 0.6f))
		]
	];
}

void SFacilityMainMenu::ShowSettings(bool bShow)
{
	if (Pages.IsValid())
	{
		Pages->SetActiveWidgetIndex(bShow ? 1 : 0);
	}
}

FReply SFacilityMainMenu::OnHost()
{
	if (UFacilityGameInstance* Instance = GameInstance.Get())
	{
		Instance->HostRun(NameBox->GetText().ToString());
	}
	return FReply::Handled();
}

FReply SFacilityMainMenu::OnJoin()
{
	if (UFacilityGameInstance* Instance = GameInstance.Get())
	{
		Instance->JoinRun(AddressBox->GetText().ToString(), NameBox->GetText().ToString());
	}
	return FReply::Handled();
}

FReply SFacilityMainMenu::OnQuit()
{
	if (UFacilityGameInstance* Instance = GameInstance.Get())
	{
		Instance->QuitToDesktop();
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------- in-run menu

void SFacilityPauseMenu::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	const TWeakObjectPtr<UFacilityGameInstance> Weak = GameInstance;

	TSharedRef<SVerticalBox> MainPage = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("PauseTitle", "THE NIGHT GOES ON")).Font(Font(22, TEXT("Bold"))).ColorAndOpacity(Paper)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 6)
		[
			Rule(80.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 16)
		[
			Label(LOCTEXT("PauseNote", "Nothing stops while this is open."), 11)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
		[
			// the controls card
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.85f))
			.Padding(FMargin(14.0f, 10.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0)
				[
					SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Paper)
					.Text(LOCTEXT("ControlKeys", "WASD\nShift\nCtrl\nE\nhold E\nF\nLMB\nV\nTab\nEsc"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim)
					.Text(LOCTEXT("ControlUses", "move\nsprint (loud)\nsneak (quiet)\nopen, take, hide, help up\nsearch drawers, cabinets, carts\nflashlight\nuse the item you carry\npush to talk\nwhat is left to do tonight\nthis menu"))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			Button(LOCTEXT("Resume", "RESUME"), FOnClicked::CreateLambda([Weak]()
			{
				if (UFacilityGameInstance* Instance = Weak.Get())
				{
					Instance->HidePauseMenu();
				}
				return FReply::Handled();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			Button(LOCTEXT("PauseSettings", "SETTINGS"), FOnClicked::CreateLambda([this]() { ShowSettings(true); return FReply::Handled(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			Button(InArgs._bHosting ? LOCTEXT("EndRun", "END THE NIGHT FOR EVERYONE") : LOCTEXT("LeaveRun", "LEAVE THE NIGHT"), FOnClicked::CreateLambda([Weak]()
			{
				if (UFacilityGameInstance* Instance = Weak.Get())
				{
					Instance->LeaveRun();
				}
				return FReply::Handled();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Button(LOCTEXT("QuitDesktop", "QUIT TO DESKTOP"), FOnClicked::CreateLambda([Weak]()
			{
				if (UFacilityGameInstance* Instance = Weak.Get())
				{
					Instance->QuitToDesktop();
				}
				return FReply::Handled();
			}))
		];

	ChildSlot
	[
		Screen(0.75f,
			SAssignNew(Pages, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()[MainPage]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SFacilitySettingsPanel)
				.GameInstance(GameInstance)
				.bShowMicMeter(false)
				.OnBack(FOnClicked::CreateLambda([this]() { ShowSettings(false); return FReply::Handled(); }))
			])
	];
}

void SFacilityPauseMenu::ShowSettings(bool bShow)
{
	if (Pages.IsValid())
	{
		Pages->SetActiveWidgetIndex(bShow ? 1 : 0);
	}
}

FReply SFacilityPauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (UFacilityGameInstance* Instance = GameInstance.Get())
		{
			Instance->HidePauseMenu();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE

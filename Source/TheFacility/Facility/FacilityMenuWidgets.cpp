// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facility/FacilityMenuWidgets.h"
#include "Facility/FacilityGameInstance.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FacilityMenu"

namespace FacilityMenuStyle
{
	static const FLinearColor Paper(0.82f, 0.80f, 0.74f);
	static const FLinearColor Dim(0.45f, 0.44f, 0.42f);
	static const FLinearColor Warning(0.85f, 0.45f, 0.38f);

	static FSlateFontInfo Font(int32 Size, const FName Weight = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	static TSharedRef<SWidget> Label(const FText& Text, int32 Size = 12, const FLinearColor& Color = Dim)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color);
	}

	static TSharedRef<SWidget> Button(const FText& Text, FOnClicked OnClicked)
	{
		return SNew(SBox)
			.HeightOverride(44.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ButtonColorAndOpacity(FLinearColor(0.12f, 0.12f, 0.11f))
				.OnClicked(OnClicked)
				[
					SNew(STextBlock).Text(Text).Font(Font(14, TEXT("Bold"))).ColorAndOpacity(Paper)
				]
			];
	}

	/** Dark full-screen backing with a fixed-width column in the middle */
	static TSharedRef<SWidget> Screen(float Opacity, TSharedRef<SWidget> Column)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.01f, 0.01f, 0.012f, Opacity))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(420.0f)[Column]
			];
	}
}

using namespace FacilityMenuStyle;

void SFacilityMainMenu::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;

	const FText Controls = LOCTEXT("Controls",
		"WASD move   Shift sprint   Ctrl sneak   E interact   F flashlight   LMB use item   Esc menu");

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Text(LOCTEXT("Title", "THE FACILITY")).Font(Font(40, TEXT("Bold"))).ColorAndOpacity(Paper)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 28)
		[
			Label(LOCTEXT("Subtitle", "Everything you take is written down."), 12)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			Label(LOCTEXT("NameLabel", "Your name"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.Text(FText::FromString(InArgs._PlayerName))
			.Font(Font(14))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
		[
			Button(LOCTEXT("Host", "HOST A RUN"), FOnClicked::CreateSP(this, &SFacilityMainMenu::OnHost))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			Label(LOCTEXT("AddressLabel", "Host address (IP, or IP:port)"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SAssignNew(AddressBox, SEditableTextBox)
			.Text(FText::FromString(InArgs._Address))
			.Font(Font(14))
			.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter)
				{
					OnJoin();
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
		[
			Button(LOCTEXT("Join", "JOIN"), FOnClicked::CreateSP(this, &SFacilityMainMenu::OnJoin))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
		[
			Button(LOCTEXT("Quit", "QUIT"), FOnClicked::CreateSP(this, &SFacilityMainMenu::OnQuit))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 18)
		[
			SNew(STextBlock)
			.Text(FText::FromString(InArgs._Message))
			.Visibility(InArgs._Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			.Font(Font(12))
			.ColorAndOpacity(Warning)
			.AutoWrapText(true)
			.Justification(ETextJustify::Center)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Text(Controls).Font(Font(9)).ColorAndOpacity(Dim).AutoWrapText(true).Justification(ETextJustify::Center)
		];

	ChildSlot[Screen(1.0f, Column)];
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

void SFacilityPauseMenu::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	const TWeakObjectPtr<UFacilityGameInstance> Weak = GameInstance;

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("PauseTitle", "THE NIGHT GOES ON")).Font(Font(22, TEXT("Bold"))).ColorAndOpacity(Paper)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 24)
		[
			Label(LOCTEXT("PauseNote", "Nothing stops while this is open."), 11)
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
			Button(InArgs._bHosting ? LOCTEXT("EndRun", "END THE RUN FOR EVERYONE") : LOCTEXT("LeaveRun", "LEAVE THE RUN"), FOnClicked::CreateLambda([Weak]()
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

	ChildSlot[Screen(0.75f, Column)];
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

// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SlashMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/SlashPlayerController.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SlashMenu"

namespace
{
	const FLinearColor ButtonIdle(0.08f, 0.08f, 0.1f, 0.85f);
	const FLinearColor ButtonFocused(0.75f, 0.18f, 0.12f, 1.f);

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FName Weight, const FLinearColor& Color)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(Text);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(Weight, Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetJustification(ETextJustify::Center);
		return Block;
	}
}

void USlashMenuWidget::Configure(ESlashMenuKind InKind)
{
	Kind = InKind;
	Buttons.Reset();
	SetIsFocusable(true);

	FText Title;
	FText Subtitle;
	switch (Kind)
	{
	case ESlashMenuKind::Title:
		Title = LOCTEXT("TitleTitle", "SLASH");
		Subtitle = LOCTEXT("TitleSubtitle", "The dead are rising. Survive every wave.");
		break;
	case ESlashMenuKind::Pause:
		Title = LOCTEXT("PauseTitle", "PAUSED");
		break;
	case ESlashMenuKind::Victory:
		Title = LOCTEXT("VictoryTitle", "VICTORY");
		Subtitle = LOCTEXT("VictorySubtitle", "The dead stay down. For now.");
		break;
	case ESlashMenuKind::Defeat:
		Title = LOCTEXT("DefeatTitle", "YOU DIED");
		Subtitle = LOCTEXT("DefeatSubtitle", "You join the horde.");
		break;
	}

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;

	// dim the world behind the menu
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, Kind == ESlashMenuKind::Pause ? 0.55f : 0.7f));
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column))
	{
		ColumnSlot->SetHorizontalAlignment(HAlign_Center);
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}

	const FLinearColor TitleColor = Kind == ESlashMenuKind::Defeat ? FLinearColor(0.85f, 0.12f, 0.1f) : FLinearColor::White;
	Column->AddChildToVerticalBox(MakeText(WidgetTree, Title, 72, "Bold", TitleColor))->SetHorizontalAlignment(HAlign_Center);
	if (!Subtitle.IsEmpty())
	{
		Column->AddChildToVerticalBox(MakeText(WidgetTree, Subtitle, 20, "Regular", FLinearColor(0.8f, 0.8f, 0.8f)))->SetHorizontalAlignment(HAlign_Center);
	}

	USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
	Gap->SetSize(FVector2D(1.f, 36.f));
	Column->AddChildToVerticalBox(Gap);

	switch (Kind)
	{
	case ESlashMenuKind::Title:
		AddButton(Column, LOCTEXT("Start", "Start"), ESlashMenuAction::Start);
		break;
	case ESlashMenuKind::Pause:
		AddButton(Column, LOCTEXT("Resume", "Resume"), ESlashMenuAction::Resume);
		AddButton(Column, LOCTEXT("Restart", "Restart"), ESlashMenuAction::Restart);
		break;
	case ESlashMenuKind::Victory:
		AddButton(Column, LOCTEXT("PlayAgain", "Play Again"), ESlashMenuAction::Restart);
		break;
	case ESlashMenuKind::Defeat:
		AddButton(Column, LOCTEXT("Retry", "Retry"), ESlashMenuAction::Restart);
		break;
	}
	AddButton(Column, LOCTEXT("Quit", "Quit"), ESlashMenuAction::Quit);

	USpacer* HintGap = WidgetTree->ConstructWidget<USpacer>();
	HintGap->SetSize(FVector2D(1.f, 24.f));
	Column->AddChildToVerticalBox(HintGap);

	const FText Hint = Kind == ESlashMenuKind::Title
		? LOCTEXT("TitleHint", "Move: WASD / Left Stick    Attack: LMB / X (hold for aerial)    Heavy: RMB / Y\nSprint: Shift / LB    Jump: Space / A    Pause: Esc / Start")
		: LOCTEXT("MenuHint", "Enter / A to select");
	Column->AddChildToVerticalBox(MakeText(WidgetTree, Hint, 13, "Regular", FLinearColor(0.6f, 0.6f, 0.6f)))->SetHorizontalAlignment(HAlign_Center);
}

void USlashMenuWidget::AddButton(UVerticalBox* Box, const FText& Label, ESlashMenuAction Action)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(ButtonIdle);

	UTextBlock* Text = MakeText(WidgetTree, Label, 26, "Bold", FLinearColor::White);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		TextSlot->SetPadding(FMargin(0.f, 8.f));
	}

	USizeBox* Sized = WidgetTree->ConstructWidget<USizeBox>();
	Sized->SetWidthOverride(320.f);
	Sized->AddChild(Button);
	if (UVerticalBoxSlot* ButtonSlot = Box->AddChildToVerticalBox(Sized))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetPadding(FMargin(0.f, 6.f));
	}

	switch (Action)
	{
	case ESlashMenuAction::Start:
		Button->OnClicked.AddDynamic(this, &USlashMenuWidget::OnStartClicked);
		break;
	case ESlashMenuAction::Resume:
		Button->OnClicked.AddDynamic(this, &USlashMenuWidget::OnResumeClicked);
		break;
	case ESlashMenuAction::Restart:
		Button->OnClicked.AddDynamic(this, &USlashMenuWidget::OnRestartClicked);
		break;
	case ESlashMenuAction::Quit:
		Button->OnClicked.AddDynamic(this, &USlashMenuWidget::OnQuitClicked);
		break;
	}

	Buttons.Add(Button);
}

void USlashMenuWidget::FocusFirstButton()
{
	if (!Buttons.IsEmpty())
	{
		Buttons[0]->SetKeyboardFocus();
		if (APlayerController* PC = GetOwningPlayer())
		{
			Buttons[0]->SetUserFocus(PC);
		}
	}
}

void USlashMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// buttons have no focus look by default
	bool bAnyFocused = false;
	for (UButton* Button : Buttons)
	{
		const bool bFocused = Button->HasKeyboardFocus() || Button->IsHovered();
		bAnyFocused |= bFocused;
		Button->SetBackgroundColor(bFocused ? ButtonFocused : ButtonIdle);
	}

	// take focus back after a backdrop click
	if (!bAnyFocused && !Buttons.IsEmpty() && HasAnyUserFocus() == false)
	{
		FocusFirstButton();
	}
}

FReply USlashMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bBack = Key == EKeys::Escape || Key == EKeys::P || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right;
	if (bBack && Kind == ESlashMenuKind::Pause)
	{
		Fire(ESlashMenuAction::Resume);
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USlashMenuWidget::Fire(ESlashMenuAction Action)
{
	if (ASlashPlayerController* PC = Cast<ASlashPlayerController>(GetOwningPlayer()))
	{
		PC->HandleMenuAction(Action);
	}
}

void USlashMenuWidget::OnStartClicked() { Fire(ESlashMenuAction::Start); }
void USlashMenuWidget::OnResumeClicked() { Fire(ESlashMenuAction::Resume); }
void USlashMenuWidget::OnRestartClicked() { Fire(ESlashMenuAction::Restart); }
void USlashMenuWidget::OnQuitClicked() { Fire(ESlashMenuAction::Quit); }

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SlashHUDWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Character/SlashCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Camera/PlayerCameraManager.h"
#include "Enemy/Enemy.h"
#include "Enemy/EnemyBrainComponent.h"
#include "Enemy/SlashEncounterDirector.h"
#include "Enemy/TargetableComponent.h"
#include "Styling/CoreStyle.h"
#include "Targeting/SlashTargetingComponent.h"

#define LOCTEXT_NAMESPACE "SlashHUD"

namespace
{
	UTextBlock* MakeHUDText(UWidgetTree* Tree, int32 Size, ETextJustify::Type Justify)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Block->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Block->SetShadowOffset(FVector2D(2.f, 2.f));
		Block->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		Block->SetJustification(Justify);
		return Block;
	}
}

void USlashHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);

	Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;

	// health top left
	UTextBlock* HealthLabel = MakeHUDText(WidgetTree, 14, ETextJustify::Left);
	HealthLabel->SetText(LOCTEXT("Health", "HEALTH"));
	if (UCanvasPanelSlot* LabelSlot = Canvas->AddChildToCanvas(HealthLabel))
	{
		LabelSlot->SetPosition(FVector2D(40.f, 32.f));
		LabelSlot->SetAutoSize(true);
	}

	HealthBar = WidgetTree->ConstructWidget<UProgressBar>();
	HealthBar->SetPercent(1.f);
	HealthBar->SetFillColorAndOpacity(FLinearColor(0.75f, 0.1f, 0.08f));
	if (UCanvasPanelSlot* BarSlot = Canvas->AddChildToCanvas(HealthBar))
	{
		BarSlot->SetPosition(FVector2D(40.f, 54.f));
		BarSlot->SetSize(FVector2D(320.f, 18.f));
	}

	// wave progress top right
	WaveText = MakeHUDText(WidgetTree, 20, ETextJustify::Right);
	if (UCanvasPanelSlot* WaveSlot = Canvas->AddChildToCanvas(WaveText))
	{
		WaveSlot->SetAnchors(FAnchors(1.f, 0.f));
		WaveSlot->SetAlignment(FVector2D(1.f, 0.f));
		WaveSlot->SetPosition(FVector2D(-40.f, 32.f));
		WaveSlot->SetAutoSize(true);
	}

	// centre banner for wave text
	BannerText = MakeHUDText(WidgetTree, 48, ETextJustify::Center);
	BannerText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* BannerSlot = Canvas->AddChildToCanvas(BannerText))
	{
		BannerSlot->SetAnchors(FAnchors(0.5f, 0.3f));
		BannerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		BannerSlot->SetAutoSize(true);
	}

	// threat marker pool
	for (int32 i = 0; i < 8; ++i)
	{
		// dark outline so the marker shows against light and dark backgrounds
		UBorder* Indicator = WidgetTree->ConstructWidget<UBorder>();
		Indicator->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.7f));
		Indicator->SetPadding(FMargin(3.f));
		Indicator->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Indicator->SetVisibility(ESlateVisibility::Collapsed);

		UBorder* Fill = WidgetTree->ConstructWidget<UBorder>();
		Indicator->SetContent(Fill);

		if (UCanvasPanelSlot* IndicatorSlot = Canvas->AddChildToCanvas(Indicator))
		{
			IndicatorSlot->SetSize(FVector2D(16.f, 40.f));
			IndicatorSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		}
		ThreatIndicators.Add(Indicator);
		ThreatIndicatorFills.Add(Fill);
	}

	// diamond over the locked target
	LockOnMarker = WidgetTree->ConstructWidget<UBorder>();
	LockOnMarker->SetBrushColor(FLinearColor(1.f, 0.85f, 0.2f, 0.9f));
	LockOnMarker->SetRenderTransformAngle(45.f);
	LockOnMarker->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* MarkerSlot = Canvas->AddChildToCanvas(LockOnMarker))
	{
		MarkerSlot->SetSize(FVector2D(12.f, 12.f));
		MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	}
}

void USlashHUDWidget::HandleMatchStateChanged(ESlashMatchState NewState)
{
	const ASlashGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASlashGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	if (NewState == ESlashMatchState::Playing && GameMode->GetCurrentWave() != LastAnnouncedWave)
	{
		LastAnnouncedWave = GameMode->GetCurrentWave();
		ShowBanner(FText::Format(LOCTEXT("WaveBanner", "WAVE {0}"), LastAnnouncedWave), 2.f);
	}
	else if (NewState == ESlashMatchState::Intermission)
	{
		ShowBanner(LOCTEXT("WaveCleared", "WAVE CLEARED"), GameMode->TimeBetweenWaves);
	}
}

void USlashHUDWidget::ShowBanner(const FText& Text, float Duration)
{
	BannerText->SetText(Text);
	BannerText->SetVisibility(ESlateVisibility::HitTestInvisible);
	BannerText->SetRenderOpacity(1.f);
	BannerTimeRemaining = Duration;
}

void USlashHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const ASlashCharacter* Player = Cast<ASlashCharacter>(GetOwningPlayerPawn());
	const UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
	const float TargetHealth = Health ? Health->GetHealthPercent() : 0.f;

	// ease the bar down after a hit
	DisplayedHealth = FMath::FInterpTo(DisplayedHealth, TargetHealth, InDeltaTime, 10.f);
	HealthBar->SetPercent(DisplayedHealth);

	if (const ASlashGameMode* GameMode = GetWorld()->GetAuthGameMode<ASlashGameMode>())
	{
		WaveText->SetText(FText::Format(LOCTEXT("WaveStatus", "WAVE {0}/{1}    ENEMIES {2}"),
			GameMode->GetCurrentWave(), GameMode->GetNumWaves(), GameMode->GetEnemiesRemaining()));
	}

	if (BannerTimeRemaining > 0.f)
	{
		BannerTimeRemaining -= InDeltaTime;
		BannerText->SetRenderOpacity(FMath::Clamp(BannerTimeRemaining / 0.5f, 0.f, 1.f));
		if (BannerTimeRemaining <= 0.f)
		{
			BannerText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	UpdateLockOnMarker();
	UpdateThreatIndicators(MyGeometry.GetLocalSize());
}

void USlashHUDWidget::UpdateThreatIndicators(const FVector2D& ScreenSize)
{
	int32 Used = 0;

	const APlayerController* PC = GetOwningPlayer();
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	const ASlashCharacter* Player = Cast<ASlashCharacter>(GetOwningPlayerPawn());
	const USlashEncounterDirector* Director = GetWorld()->GetSubsystem<USlashEncounterDirector>();

	if (Camera && Player && !Player->IsDead() && Director)
	{
		const FVector CameraLocation = Camera->GetCameraLocation();
		const float CameraYaw = Camera->GetCameraRotation().Yaw;
		const FVector2D Center = ScreenSize * 0.5f;
		const FVector2D Radii(ScreenSize.X * 0.4f, ScreenSize.Y * 0.4f);

		TArray<AEnemy*> Enemies;
		Director->GetEnemies(Enemies);
		for (const AEnemy* Enemy : Enemies)
		{
			const UEnemyBrainComponent* Brain = Enemy->GetBrain();
			if (!Brain || Brain->IsOnScreen() || !ThreatIndicators.IsValidIndex(Used)
				|| FVector::Dist2D(Enemy->GetActorLocation(), Player->GetActorLocation()) > ThreatIndicatorRange)
			{
				continue;
			}

			// angle from the camera view, 0 ahead and 180 behind
			const FVector ToEnemy = Enemy->GetActorLocation() - CameraLocation;
			const float RelDegrees = FRotator::NormalizeAxis(ToEnemy.Rotation().Yaw - CameraYaw);
			const float Rel = FMath::DegreesToRadians(RelDegrees);

			float Threat = 0.f;
			FLinearColor Color(1.f, 1.f, 1.f, 0.85f);
			if (Brain->IsThreatening())
			{
				Threat = Brain->GetState() == EEnemyBrainState::Approach ? 0.35f : FMath::Lerp(0.5f, 1.f, Brain->GetWindupAlpha());
				Color = FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.6f, 0.1f, 0.9f), FLinearColor(1.f, 0.05f, 0.05f, 1.f), Threat);
			}

			ThreatIndicatorFills[Used]->SetBrushColor(Color);
			UBorder* Indicator = ThreatIndicators[Used++];
			Indicator->SetVisibility(ESlateVisibility::HitTestInvisible);
			Indicator->SetRenderTransformAngle(RelDegrees);
			Indicator->SetRenderScale(FVector2D(1.f + 0.6f * Threat));
			if (UCanvasPanelSlot* IndicatorSlot = Cast<UCanvasPanelSlot>(Indicator->Slot))
			{
				IndicatorSlot->SetPosition(Center + FVector2D(FMath::Sin(Rel) * Radii.X, -FMath::Cos(Rel) * Radii.Y));
			}
		}
	}

	for (int32 i = Used; i < ThreatIndicators.Num(); ++i)
	{
		ThreatIndicators[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USlashHUDWidget::UpdateLockOnMarker()
{
	const ASlashCharacter* Player = Cast<ASlashCharacter>(GetOwningPlayerPawn());
	const USlashTargetingComponent* Targeting = Player ? Player->GetTargetingComponent() : nullptr;
	const UTargetableComponent* Target = Targeting ? Targeting->GetPersistentTarget() : nullptr;

	FVector2D ScreenPosition;
	const bool bOnScreen = Target && !Player->IsDead()
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Target->GetTargetPoint() + FVector(0.f, 0.f, 70.f), ScreenPosition, false);

	if (!bOnScreen)
	{
		LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	LockOnMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(LockOnMarker->Slot))
	{
		MarkerSlot->SetPosition(ScreenPosition);
	}
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/SlashGameMode.h"
#include "SlashHUDWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;

// in game hud built in c++ and polled each tick
UCLASS()
class SLASH_API USlashHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void HandleMatchStateChanged(ESlashMatchState NewState);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void ShowBanner(const FText& Text, float Duration);
	void UpdateLockOnMarker();

	// off screen threat markers pointing at enemies the camera can't see
	void UpdateThreatIndicators(const FVector2D& ScreenSize);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WaveText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BannerText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> LockOnMarker;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ThreatIndicators;

	// fill inside each marker
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ThreatIndicatorFills;

	// no marker for enemies further away than this
	float ThreatIndicatorRange = 1800.f;

	float BannerTimeRemaining = 0.f;
	float DisplayedHealth = 1.f;
	int32 LastAnnouncedWave = 0;
};

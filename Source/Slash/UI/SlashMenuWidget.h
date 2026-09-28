// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SlashMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

UENUM()
enum class ESlashMenuKind : uint8
{
	Title,
	Pause,
	Victory,
	Defeat
};

UENUM()
enum class ESlashMenuAction : uint8
{
	Start,
	Resume,
	Restart,
	Quit
};

// every full screen menu in one c++ widget
UCLASS()
class SLASH_API USlashMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(ESlashMenuKind InKind);
	void FocusFirstButton();
	ESlashMenuKind GetKind() const { return Kind; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void AddButton(UVerticalBox* Box, const FText& Label, ESlashMenuAction Action);
	void Fire(ESlashMenuAction Action);

	// click delegates have no payload so one handler per action
	UFUNCTION()
	void OnStartClicked();
	UFUNCTION()
	void OnResumeClicked();
	UFUNCTION()
	void OnRestartClicked();
	UFUNCTION()
	void OnQuitClicked();

	ESlashMenuKind Kind = ESlashMenuKind::Title;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> Buttons;
};

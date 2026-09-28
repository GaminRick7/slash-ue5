// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Framework/SlashGameMode.h"
#include "UI/SlashMenuWidget.h"
#include "SlashPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class USlashHUDWidget;

// owns the hud and menus, input mode, cursor and pause
UCLASS()
class SLASH_API ASlashPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASlashPlayerController();

	void HandleMatchStateChanged(ESlashMatchState NewState);

	// called by menu buttons
	void HandleMenuAction(ESlashMenuAction Action);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void TogglePause();
	void ShowMenu(ESlashMenuKind Kind);
	void CloseMenu();

	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> GamepadMappingContext;

	// gamepad jump and pause
	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> GamepadExtrasMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> KeyboardMouseMappingContext;

	// dodge on Left Ctrl and gamepad B
	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DodgeMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(Transient)
	TObjectPtr<USlashHUDWidget> HUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<USlashMenuWidget> MenuWidget;
};

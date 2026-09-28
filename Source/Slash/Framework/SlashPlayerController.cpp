// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlashPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SlashHUDWidget.h"
#include "UObject/ConstructorHelpers.h"

ASlashPlayerController::ASlashPlayerController()
{
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> GamepadExtras(TEXT("/Game/Input/IMC_GamepadExtras.IMC_GamepadExtras"));
	if (GamepadExtras.Succeeded())
	{
		GamepadExtrasMappingContext = GamepadExtras.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> KeyboardMouse(TEXT("/Game/Input/IMC_KeyboardMouse.IMC_KeyboardMouse"));
	if (KeyboardMouse.Succeeded())
	{
		KeyboardMouseMappingContext = KeyboardMouse.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> Dodge(TEXT("/Game/Input/IMC_Dodge.IMC_Dodge"));
	if (Dodge.Succeeded())
	{
		DodgeMappingContext = Dodge.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> Pause(TEXT("/Game/Input/IA_Pause.IA_Pause"));
	if (Pause.Succeeded())
	{
		PauseAction = Pause.Object;
	}
}

void ASlashPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* Context : { GamepadMappingContext.Get(), GamepadExtrasMappingContext.Get(), KeyboardMouseMappingContext.Get(), DodgeMappingContext.Get() })
		{
			if (Context)
			{
				Subsystem->AddMappingContext(Context, 0);
			}
		}
	}

	HUDWidget = CreateWidget<USlashHUDWidget>(this, USlashHUDWidget::StaticClass());
	if (HUDWidget)
	{
		HUDWidget->AddToViewport(0);
	}

	// the game mode may have set its state before this controller began play
	if (const ASlashGameMode* GameMode = GetWorld()->GetAuthGameMode<ASlashGameMode>())
	{
		HandleMatchStateChanged(GameMode->GetMatchState());
	}
}

void ASlashPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent); EnhancedInput && PauseAction)
	{
		EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &ASlashPlayerController::TogglePause);
	}
}

void ASlashPlayerController::HandleMatchStateChanged(ESlashMatchState NewState)
{
	if (!IsLocalController())
	{
		return;
	}

	switch (NewState)
	{
	case ESlashMatchState::Title:
		ShowMenu(ESlashMenuKind::Title);
		break;
	case ESlashMatchState::Victory:
		ShowMenu(ESlashMenuKind::Victory);
		break;
	case ESlashMatchState::Defeat:
		ShowMenu(ESlashMenuKind::Defeat);
		break;
	case ESlashMatchState::Playing:
	case ESlashMatchState::Intermission:
		if (MenuWidget && MenuWidget->GetKind() != ESlashMenuKind::Pause)
		{
			CloseMenu();
		}
		break;
	}

	if (HUDWidget)
	{
		HUDWidget->HandleMatchStateChanged(NewState);
	}
}

void ASlashPlayerController::TogglePause()
{
	const ASlashGameMode* GameMode = GetWorld()->GetAuthGameMode<ASlashGameMode>();
	const ESlashMatchState State = GameMode ? GameMode->GetMatchState() : ESlashMatchState::Playing;
	if (State != ESlashMatchState::Playing && State != ESlashMatchState::Intermission)
	{
		return;
	}

	if (MenuWidget && MenuWidget->GetKind() == ESlashMenuKind::Pause)
	{
		CloseMenu();
	}
	else if (!MenuWidget)
	{
		ShowMenu(ESlashMenuKind::Pause);
	}
}

void ASlashPlayerController::ShowMenu(ESlashMenuKind Kind)
{
	CloseMenu();

	MenuWidget = CreateWidget<USlashMenuWidget>(this, USlashMenuWidget::StaticClass());
	if (!MenuWidget)
	{
		return;
	}
	MenuWidget->Configure(Kind);
	MenuWidget->AddToViewport(10);

	if (HUDWidget)
	{
		HUDWidget->SetVisibility(Kind == ESlashMenuKind::Title ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	MenuWidget->FocusFirstButton();

	SetPause(true);
}

void ASlashPlayerController::CloseMenu()
{
	if (!MenuWidget)
	{
		return;
	}

	MenuWidget->RemoveFromParent();
	MenuWidget = nullptr;

	if (HUDWidget)
	{
		HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	SetPause(false);
}

void ASlashPlayerController::HandleMenuAction(ESlashMenuAction Action)
{
	ASlashGameMode* GameMode = GetWorld()->GetAuthGameMode<ASlashGameMode>();

	switch (Action)
	{
	case ESlashMenuAction::Start:
		CloseMenu();
		if (GameMode)
		{
			GameMode->StartMatch();
		}
		break;
	case ESlashMenuAction::Resume:
		CloseMenu();
		break;
	case ESlashMenuAction::Restart:
		if (GameMode)
		{
			GameMode->RestartMatch();
		}
		break;
	case ESlashMenuAction::Quit:
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		break;
	}
}

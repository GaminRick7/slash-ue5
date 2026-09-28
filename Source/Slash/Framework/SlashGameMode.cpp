// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlashGameMode.h"
#include "Character/SlashCharacter.h"
#include "Enemy/Enemy.h"
#include "Enemy/SlashEncounterDirector.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "SlashPlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* SkipTitleOption = TEXT("SkipTitle");
}

#if !UE_BUILD_SHIPPING
// dev shortcut for testing wave flow and the victory screen
static FAutoConsoleCommandWithWorld KillAllEnemiesCommand(
	TEXT("slash.KillAllEnemies"),
	TEXT("Deals lethal damage to every living enemy, as if the player had struck them."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		TArray<AEnemy*> Enemies;
		for (TActorIterator<AEnemy> It(World); It; ++It)
		{
			Enemies.Add(*It);
		}
		for (AEnemy* Enemy : Enemies)
		{
			UGameplayStatics::ApplyDamage(Enemy, 100000.f, Player ? Player->GetController() : nullptr, Player, nullptr);
		}
	}),
	ECVF_Cheat);
#endif

ASlashGameMode::ASlashGameMode()
{
	DefaultPawnClass = ASlashCharacter::StaticClass();
	PlayerControllerClass = ASlashPlayerController::StaticClass();

	WaveSizes = { 4, 5, 6 };

	static ConstructorHelpers::FClassFinder<AEnemy> EnemyBlueprint(TEXT("/Game/Blueprints/BP_Enemy"));
	EnemyClass = EnemyBlueprint.Succeeded() ? EnemyBlueprint.Class : TSubclassOf<AEnemy>(AEnemy::StaticClass());
}

void ASlashGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	bSkipTitle = UGameplayStatics::HasOption(Options, SkipTitleOption);
}

void ASlashGameMode::StartPlay()
{
	Super::StartPlay();

	// tagged spawn points in the alleys and yards
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(EnemySpawnTag))
		{
			SpawnPoints.Add(It->GetActorTransform());
		}
	}

	// fallback to where placed enemies started
	if (SpawnPoints.IsEmpty())
	{
		for (TActorIterator<AEnemy> It(GetWorld()); It; ++It)
		{
			SpawnPoints.Add(It->GetActorTransform());
		}
	}

	// fallback to a ring around the player start
	if (SpawnPoints.IsEmpty())
	{
		const AActor* Start = FindPlayerStart(nullptr);
		const FVector Center = Start ? Start->GetActorLocation() : FVector::ZeroVector;
		for (int32 i = 0; i < 6; ++i)
		{
			const float Angle = UE_TWO_PI * i / 6.f;
			SpawnPoints.Add(FTransform(Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 900.f));
		}
		SpawnWave(0);
	}

	CurrentWave = 1;

	if (bSkipTitle)
	{
		StartMatch();
	}
	else
	{
		SetMatchState(ESlashMatchState::Title);
	}
}

void ASlashGameMode::StartMatch()
{
	if (MatchState == ESlashMatchState::Title || bSkipTitle)
	{
		SetMatchState(ESlashMatchState::Playing);
	}
}

int32 ASlashGameMode::GetEnemiesRemaining() const
{
	const USlashEncounterDirector* Director = GetWorld()->GetSubsystem<USlashEncounterDirector>();
	return Director ? Director->GetAliveEnemyCount() : 0;
}

void ASlashGameMode::SetMatchState(ESlashMatchState NewState)
{
	MatchState = NewState;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ASlashPlayerController* PC = Cast<ASlashPlayerController>(It->Get()))
		{
			PC->HandleMatchStateChanged(NewState);
		}
	}
}

void ASlashGameMode::NotifyEnemyKilled(AEnemy* Enemy)
{
	// director already removed this enemy
	if (MatchState == ESlashMatchState::Playing)
	{
		CheckWaveCleared();
	}
}

void ASlashGameMode::CheckWaveCleared()
{
	if (GetEnemiesRemaining() > 0)
	{
		return;
	}

	if (CurrentWave >= WaveSizes.Num())
	{
		MatchState = ESlashMatchState::Victory;
		GetWorldTimerManager().SetTimer(EndScreenTimerHandle,
			FTimerDelegate::CreateUObject(this, &ASlashGameMode::SetMatchState, ESlashMatchState::Victory), EndScreenDelay, false);
		return;
	}

	SetMatchState(ESlashMatchState::Intermission);
	GetWorldTimerManager().SetTimer(WaveTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (MatchState != ESlashMatchState::Intermission)
		{
			return;
		}
		++CurrentWave;
		SpawnWave(CurrentWave - 1);
		SetMatchState(ESlashMatchState::Playing);
	}), TimeBetweenWaves, false);
}

void ASlashGameMode::SpawnWave(int32 WaveIndex)
{
	if (!EnemyClass || SpawnPoints.IsEmpty() || !WaveSizes.IsValidIndex(WaveIndex))
	{
		return;
	}

	// spawn out of view and away from the player
	TArray<FTransform> Ordered = SpawnPoints;
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (Player && PC->PlayerCameraManager)
	{
		const FVector PlayerLocation = Player->GetActorLocation();
		const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
		const FVector CameraForward = PC->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D();
		const float CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(PC->PlayerCameraManager->GetFOVAngle() * 0.5f));

		auto Score = [&](const FTransform& Point)
		{
			const FVector Location = Point.GetLocation();
			const float Distance = FVector::Dist2D(Location, PlayerLocation);
			const bool bVisible = FVector::DotProduct(CameraForward, (Location - CameraLocation).GetSafeNormal2D()) >= CosHalfFOV;
			// lower is better, too close worst then visible then far
			return (Distance < MinSpawnDistance ? 100000.f : 0.f) + (bVisible ? 10000.f : 0.f) + Distance;
		};
		Ordered.Sort([&](const FTransform& A, const FTransform& B) { return Score(A) < Score(B); });
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// reuse points with an offset
	for (int32 i = 0; i < WaveSizes[WaveIndex]; ++i)
	{
		FTransform Spawn = Ordered[i % Ordered.Num()];
		const int32 Lap = i / Ordered.Num();
		if (Lap > 0)
		{
			const float Angle = Lap * 2.4f;
			Spawn.AddToTranslation(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 120.f * Lap);
		}
		if (AEnemy* Zombie = GetWorld()->SpawnActor<AEnemy>(EnemyClass, Spawn, Params))
		{
			Zombie->PlaySpawnRise();
		}
	}
}

void ASlashGameMode::NotifyPlayerDied()
{
	if (MatchState == ESlashMatchState::Defeat || MatchState == ESlashMatchState::Victory)
	{
		return;
	}
	MatchState = ESlashMatchState::Defeat;
	GetWorldTimerManager().ClearTimer(WaveTimerHandle);
	GetWorldTimerManager().SetTimer(EndScreenTimerHandle,
		FTimerDelegate::CreateUObject(this, &ASlashGameMode::SetMatchState, ESlashMatchState::Defeat), EndScreenDelay, false);
}

void ASlashGameMode::RestartMatch()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, SkipTitleOption);
}

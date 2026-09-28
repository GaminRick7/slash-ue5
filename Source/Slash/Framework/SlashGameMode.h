// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SlashGameMode.generated.h"

class AEnemy;

UENUM(BlueprintType)
enum class ESlashMatchState : uint8
{
	Title,		// title screen with the world paused
	Playing,
	Intermission,	// between waves
	Victory,
	Defeat
};

// wave based match flow, screens handled by the player controller
UCLASS()
class SLASH_API ASlashGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASlashGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

	void StartMatch();
	void NotifyEnemyKilled(AEnemy* Enemy);
	void NotifyPlayerDied();

	// reload without the title screen for retry
	void RestartMatch();

	ESlashMatchState GetMatchState() const { return MatchState; }
	int32 GetCurrentWave() const { return CurrentWave; }
	int32 GetNumWaves() const { return WaveSizes.Num(); }
	int32 GetEnemiesRemaining() const;

	// enemies per wave, entry 0 ignored for the placed enemies
	UPROPERTY(EditDefaultsOnly, Category = "Waves")
	TArray<int32> WaveSizes;

	UPROPERTY(EditDefaultsOnly, Category = "Waves")
	TSubclassOf<AEnemy> EnemyClass;

	UPROPERTY(EditDefaultsOnly, Category = "Waves")
	float TimeBetweenWaves = 3.f;

	// actors with this tag are used as wave spawn points
	UPROPERTY(EditDefaultsOnly, Category = "Waves")
	FName EnemySpawnTag = TEXT("EnemySpawn");

	// spawn points closer than this are used only as a last resort
	UPROPERTY(EditDefaultsOnly, Category = "Waves")
	float MinSpawnDistance = 900.f;

	// delay before the end screen appears
	UPROPERTY(EditDefaultsOnly, Category = "Waves")
	float EndScreenDelay = 2.f;

private:
	void SetMatchState(ESlashMatchState NewState);
	void SpawnWave(int32 WaveIndex);
	void CheckWaveCleared();

	ESlashMatchState MatchState = ESlashMatchState::Title;
	int32 CurrentWave = 0;
	bool bSkipTitle = false;

	// tagged spawn points or wave 1 positions
	TArray<FTransform> SpawnPoints;

	FTimerHandle WaveTimerHandle;
	FTimerHandle EndScreenTimerHandle;
};

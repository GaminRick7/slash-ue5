// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SlashEncounterDirector.generated.h"

class AEnemy;
class APawn;
class APlayerController;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnEncounterEnemyCountChanged, int32 /*AliveCount*/);

// places enemies on an arc facing the camera and decides who attacks, see Docs/EnemyPositioning_Plan.md
UCLASS()
class SLASH_API USlashEncounterDirector : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	void RegisterEnemy(AEnemy* Enemy);

	// called on death
	void UnregisterEnemy(AEnemy* Enemy);

	// called when an enemy is done with its token
	void ReleaseToken(AEnemy* Enemy);

	int32 GetAliveEnemyCount() const;

	// all living enemies for the hud
	void GetEnemies(TArray<AEnemy*>& OutEnemies) const;

	FOnEncounterEnemyCountChanged OnEnemyCountChanged;

	// tuning

	// arc half width for one enemy plus PerEnemyArcDegrees each
	float MinArcHalfDegrees = 35.f;
	float PerEnemyArcDegrees = 22.f;

	// the arc starts following the camera once it's this far off
	float AnchorDeadZoneDegrees = 40.f;
	// and turns at most this fast
	float AnchorTurnRateDegrees = 70.f;

	int32 MaxSimultaneousAttackers = 1;
	float TokenCooldown = 1.2f;
	float TokenGrantInterval = 0.6f;

	// how long no on-screen enemy can attack before an off-screen one may
	float OffscreenAttackDelay = 3.f;

	// fraction of the camera's horizontal FOV that counts as on-screen
	float OnScreenFOVFraction = 0.85f;

	float DirectorTickRate = 10.f;

private:
	void TickDirector();
	void PruneRegistry();
	void UpdateCameraAnchor(const APlayerController* PC, float DeltaSeconds);
	void UpdateOnScreen(const APlayerController* PC);
	void AssignSlots(const APawn* Player);
	void GrantTokens(const APawn* Player);
	void DebugDraw(const APawn* Player) const;

	TArray<TWeakObjectPtr<AEnemy>> Enemies;
	TArray<TWeakObjectPtr<AEnemy>> TokenHolders;
	TMap<TWeakObjectPtr<AEnemy>, double> LastTokenReleaseTime;

	// last side for enemies directly behind the player
	TMap<TWeakObjectPtr<AEnemy>, bool> BehindSide;

	double LastGrantTime = -1000.0;
	double LastOnScreenCandidateTime = 0.0;

	// world yaw (radians) the arc faces
	float AnchorYaw = 0.f;
	bool bAnchorInitialized = false;
	bool bAnchorCatchingUp = false;

	FTimerHandle DirectorTimerHandle;
};

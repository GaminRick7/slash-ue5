// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/SlashEncounterDirector.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Enemy/Enemy.h"
#include "Enemy/EnemyBrainComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

static TAutoConsoleVariable<int32> CVarEncounterDebug(
	TEXT("slash.Encounter.Debug"),
	0,
	TEXT("Draw the enemy arc: its facing, each enemy's goal, who is on-screen, and who holds an attack token."),
	ECVF_Cheat);

namespace
{
	float WrapAngle(float Radians)
	{
		return FMath::UnwindRadians(Radians);
	}
}

bool USlashEncounterDirector::ShouldCreateSubsystem(UObject* Outer) const
{
	// game and PIE worlds only
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void USlashEncounterDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	InWorld.GetTimerManager().SetTimer(DirectorTimerHandle, this, &USlashEncounterDirector::TickDirector,
		1.f / FMath::Max(DirectorTickRate, 1.f), true);
}

void USlashEncounterDirector::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DirectorTimerHandle);
	}
	Super::Deinitialize();
}

void USlashEncounterDirector::RegisterEnemy(AEnemy* Enemy)
{
	if (Enemy && !Enemies.Contains(Enemy))
	{
		Enemies.Add(Enemy);
		OnEnemyCountChanged.Broadcast(GetAliveEnemyCount());
	}
}

void USlashEncounterDirector::UnregisterEnemy(AEnemy* Enemy)
{
	ReleaseToken(Enemy);
	BehindSide.Remove(Enemy);
	if (Enemies.Remove(Enemy) > 0)
	{
		OnEnemyCountChanged.Broadcast(GetAliveEnemyCount());
	}
}

void USlashEncounterDirector::ReleaseToken(AEnemy* Enemy)
{
	if (TokenHolders.Remove(Enemy) > 0)
	{
		LastTokenReleaseTime.Add(Enemy, GetWorld()->GetTimeSeconds());
	}
	if (UEnemyBrainComponent* Brain = Enemy ? Enemy->GetBrain() : nullptr)
	{
		Brain->SetHasAttackToken(false);
	}
}

int32 USlashEncounterDirector::GetAliveEnemyCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		Count += Enemy.IsValid() ? 1 : 0;
	}
	return Count;
}

void USlashEncounterDirector::GetEnemies(TArray<AEnemy*>& OutEnemies) const
{
	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		if (AEnemy* Pinned = Enemy.Get())
		{
			OutEnemies.Add(Pinned);
		}
	}
}

void USlashEncounterDirector::PruneRegistry()
{
	const int32 Before = Enemies.Num();
	Enemies.RemoveAll([](const TWeakObjectPtr<AEnemy>& Enemy) { return !Enemy.IsValid(); });
	TokenHolders.RemoveAll([](const TWeakObjectPtr<AEnemy>& Enemy) { return !Enemy.IsValid(); });
	if (Enemies.Num() != Before)
	{
		OnEnemyCountChanged.Broadcast(Enemies.Num());
	}
}

void USlashEncounterDirector::TickDirector()
{
	PruneRegistry();

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	const bool bPlayerActive = Player && !Player->IsHidden() && Player->CanBeDamaged();

	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		if (UEnemyBrainComponent* Brain = Enemy->GetBrain())
		{
			Brain->SetTargetPawn(bPlayerActive ? Player : nullptr);
		}
	}

	if (!bPlayerActive)
	{
		// no player so take back all tokens
		for (const TWeakObjectPtr<AEnemy>& Holder : TArray<TWeakObjectPtr<AEnemy>>(TokenHolders))
		{
			ReleaseToken(Holder.Get());
		}
		return;
	}

	UpdateCameraAnchor(PC, 1.f / FMath::Max(DirectorTickRate, 1.f));
	UpdateOnScreen(PC);
	AssignSlots(Player);
	GrantTokens(Player);

	if (CVarEncounterDebug.GetValueOnGameThread() > 0)
	{
		DebugDraw(Player);
	}
}

void USlashEncounterDirector::UpdateCameraAnchor(const APlayerController* PC, float DeltaSeconds)
{
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	if (!Camera)
	{
		return;
	}

	const float CameraYaw = FMath::DegreesToRadians(Camera->GetCameraRotation().Yaw);
	if (!bAnchorInitialized)
	{
		AnchorYaw = CameraYaw;
		bAnchorInitialized = true;
		return;
	}

	// keep turning until caught up once past the dead zone
	const float Delta = WrapAngle(CameraYaw - AnchorYaw);
	if (FMath::Abs(Delta) > FMath::DegreesToRadians(AnchorDeadZoneDegrees))
	{
		bAnchorCatchingUp = true;
	}
	if (bAnchorCatchingUp)
	{
		const float MaxStep = FMath::DegreesToRadians(AnchorTurnRateDegrees) * DeltaSeconds;
		AnchorYaw = WrapAngle(AnchorYaw + FMath::Clamp(Delta, -MaxStep, MaxStep));
		if (FMath::Abs(Delta) <= MaxStep)
		{
			bAnchorCatchingUp = false;
		}
	}
}

void USlashEncounterDirector::UpdateOnScreen(const APlayerController* PC)
{
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	if (!Camera)
	{
		return;
	}

	const FVector CameraLocation = Camera->GetCameraLocation();
	const FVector CameraForward = Camera->GetCameraRotation().Vector().GetSafeNormal2D();
	const float HalfFOV = FMath::DegreesToRadians(Camera->GetFOVAngle() * 0.5f * OnScreenFOVFraction);
	const float CosHalfFOV = FMath::Cos(HalfFOV);

	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		if (UEnemyBrainComponent* Brain = Enemy->GetBrain())
		{
			const FVector ToEnemy = (Enemy->GetActorLocation() - CameraLocation).GetSafeNormal2D();
			Brain->SetOnScreen(FVector::DotProduct(CameraForward, ToEnemy) >= CosHalfFOV);
		}
	}
}

void USlashEncounterDirector::AssignSlots(const APawn* Player)
{
	const FVector PlayerLocation = Player->GetActorLocation();

	struct FEnemyAngle
	{
		AEnemy* Enemy;
		float RelAngle;	// angle around the player relative to the arc centre
	};
	TArray<FEnemyAngle> Sorted;
	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		const FVector Offset = Enemy->GetActorLocation() - PlayerLocation;
		float Rel = WrapAngle(FMath::Atan2(Offset.Y, Offset.X) - AnchorYaw);

		// keep an enemy directly behind on its last side so slots don't swap
		const float SeamBand = FMath::DegreesToRadians(160.f);
		bool* Side = BehindSide.Find(Enemy);
		if (FMath::Abs(Rel) > SeamBand && Side)
		{
			Rel = *Side ? FMath::Abs(Rel) : -FMath::Abs(Rel);
		}
		else
		{
			BehindSide.Add(Enemy, Rel >= 0.f);
		}

		Sorted.Add({ Enemy.Get(), Rel });
	}
	if (Sorted.IsEmpty())
	{
		return;
	}
	Sorted.Sort([](const FEnemyAngle& A, const FEnemyAngle& B) { return A.RelAngle < B.RelAngle; });

	// spread slots evenly and fill in angle order so paths don't cross
	const int32 M = Sorted.Num();
	const float HalfArc = FMath::Min(FMath::DegreesToRadians(MinArcHalfDegrees + PerEnemyArcDegrees * (M - 1)), UE_PI);
	const bool bFullRing = HalfArc >= UE_PI - KINDA_SMALL_NUMBER;

	for (int32 i = 0; i < M; ++i)
	{
		// full ring or arc including both ends
		float Rel = 0.f;
		if (M > 1)
		{
			Rel = bFullRing
				? -UE_PI + UE_TWO_PI * (i + 0.5f) / M
				: -HalfArc + 2.f * HalfArc * i / (M - 1);
		}

		// small fixed radius jitter so the arc looks less regular
		const float RadiusScale = 0.9f + 0.2f * FMath::Frac(FMath::Sin((i + 1) * 12.9898f) * 43758.5453f);

		if (UEnemyBrainComponent* Brain = Sorted[i].Enemy->GetBrain())
		{
			Brain->SetSlot(WrapAngle(AnchorYaw + Rel), RadiusScale);
		}
	}
}

void USlashEncounterDirector::GrantTokens(const APawn* Player)
{
	const double Now = GetWorld()->GetTimeSeconds();

	// checked even when no token is free
	bool bAnyOnScreenCandidate = false;
	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		const UEnemyBrainComponent* Brain = Enemy->GetBrain();
		bAnyOnScreenCandidate |= Brain && Brain->IsOnScreen() && (Brain->CanAcceptToken() || TokenHolders.Contains(Enemy));
	}
	if (bAnyOnScreenCandidate)
	{
		LastOnScreenCandidateTime = Now;
	}

	if (TokenHolders.Num() >= MaxSimultaneousAttackers || Now - LastGrantTime < TokenGrantInterval)
	{
		return;
	}

	const bool bAllowOffscreen = Now - LastOnScreenCandidateTime >= OffscreenAttackDelay;

	// pick the closest ready enemy that's roughly facing the player
	AEnemy* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		UEnemyBrainComponent* Brain = Enemy->GetBrain();
		if (!Brain || !Brain->CanAcceptToken() || (!Brain->IsOnScreen() && !bAllowOffscreen))
		{
			continue;
		}

		if (const double* Released = LastTokenReleaseTime.Find(Enemy); Released && Now - *Released < TokenCooldown)
		{
			continue;
		}

		const FVector ToPlayer = Player->GetActorLocation() - Enemy->GetActorLocation();
		const float Facing = FVector::DotProduct(Enemy->GetActorForwardVector(), ToPlayer.GetSafeNormal2D());
		const float Score = ToPlayer.Size2D() * (1.5f - 0.5f * Facing);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Enemy.Get();
		}
	}

	if (Best)
	{
		TokenHolders.Add(Best);
		LastGrantTime = Now;
		Best->GetBrain()->SetHasAttackToken(true);
	}
}

void USlashEncounterDirector::DebugDraw(const APawn* Player) const
{
	const UWorld* World = GetWorld();
	const FVector Center = Player->GetActorLocation();
	const float Duration = 1.f / FMath::Max(DirectorTickRate, 1.f);

	DrawDebugLine(World, Center, Center + FVector(FMath::Cos(AnchorYaw), FMath::Sin(AnchorYaw), 0.f) * 500.f,
		FColor::Yellow, false, Duration, 0, 3.f);

	for (const TWeakObjectPtr<AEnemy>& Enemy : Enemies)
	{
		const UEnemyBrainComponent* Brain = Enemy->GetBrain();
		if (!Brain)
		{
			continue;
		}
		const FColor Color = TokenHolders.Contains(Enemy) ? FColor::Red : (Brain->IsOnScreen() ? FColor::Green : FColor::Blue);
		DrawDebugSphere(World, Brain->GetMoveGoal(), 25.f, 8, Color, false, Duration);
		DrawDebugLine(World, Enemy->GetActorLocation(), Brain->GetMoveGoal(), Color, false, Duration);
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/EnemyBrainComponent.h"
#include "Enemy/Enemy.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/SlashEncounterDirector.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

UEnemyBrainComponent::UEnemyBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UEnemyBrainComponent::BeginPlay()
{
	Super::BeginPlay();
	Enemy = Cast<AEnemy>(GetOwner());
}

AEnemyAIController* UEnemyBrainComponent::GetAIController() const
{
	return Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
}

void UEnemyBrainComponent::SetTargetPawn(APawn* NewTarget)
{
	if (State == EEnemyBrainState::Dead || TargetPawn.Get() == NewTarget)
	{
		return;
	}

	TargetPawn = NewTarget;
	if (AEnemyAIController* AI = GetAIController())
	{
		// focus on the player so the enemy keeps facing them while strafing
		if (NewTarget)
		{
			AI->SetFocus(NewTarget);
		}
		else
		{
			AI->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	if (!NewTarget)
	{
		EnterState(EEnemyBrainState::Idle);
	}
	else if (State == EEnemyBrainState::Idle)
	{
		EnterState(EEnemyBrainState::Circle);
	}
}

void UEnemyBrainComponent::SetSlot(float AngleRadians, float RadiusScale)
{
	SlotAngle = AngleRadians;
	SlotRadiusScale = RadiusScale;
	bHasSlot = true;
}

void UEnemyBrainComponent::SetHasAttackToken(bool bInHasToken)
{
	const bool bGranted = bInHasToken && !bHasToken;
	bHasToken = bInHasToken;
	if (bGranted && State == EEnemyBrainState::Circle)
	{
		EnterState(EEnemyBrainState::Approach);
	}
}

bool UEnemyBrainComponent::CanAcceptToken() const
{
	return State == EEnemyBrainState::Circle && !bHasToken && TargetPawn.IsValid();
}

bool UEnemyBrainComponent::IsThreatening() const
{
	return State == EEnemyBrainState::Approach || State == EEnemyBrainState::Windup
		|| State == EEnemyBrainState::Leap || State == EEnemyBrainState::Attack;
}

float UEnemyBrainComponent::GetWindupAlpha() const
{
	if (State == EEnemyBrainState::Attack)
	{
		return 1.f;
	}
	return State == EEnemyBrainState::Windup && CurrentWindupTime > 0.f ? FMath::Clamp(StateTime / CurrentWindupTime, 0.f, 1.f) : 0.f;
}

void UEnemyBrainComponent::NotifyStaggered()
{
	if (State == EEnemyBrainState::Dead)
	{
		return;
	}
	// give up the token when staggered
	GiveUpToken();
	EnterState(EEnemyBrainState::Stagger);
}

void UEnemyBrainComponent::NotifyAttackFinished()
{
	if (State == EEnemyBrainState::Attack)
	{
		GiveUpToken();
		EnterState(EEnemyBrainState::Recover);
	}
}

void UEnemyBrainComponent::NotifyLanded()
{
	if (State != EEnemyBrainState::Leap)
	{
		return;
	}

	// landing was the hit so stay in Attack until the clip ends
	if (bHasToken && Enemy && Enemy->IsAttacking())
	{
		EnterState(EEnemyBrainState::Attack);
	}
	else
	{
		GiveUpToken();
		EnterState(EEnemyBrainState::Recover);
	}
}

void UEnemyBrainComponent::NotifyRising(float Duration)
{
	GiveUpToken();
	RiseDuration = Duration;
	EnterState(EEnemyBrainState::Rising);
}

void UEnemyBrainComponent::NotifyDied()
{
	GiveUpToken();
	EnterState(EEnemyBrainState::Dead);
	SetComponentTickEnabled(false);
	if (AEnemyAIController* AI = GetAIController())
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void UEnemyBrainComponent::GiveUpToken()
{
	if (!bHasToken)
	{
		return;
	}
	bHasToken = false;
	if (USlashEncounterDirector* Director = GetWorld()->GetSubsystem<USlashEncounterDirector>())
	{
		Director->ReleaseToken(Enemy);
	}
}

void UEnemyBrainComponent::EnterState(EEnemyBrainState NewState)
{
	if (State == EEnemyBrainState::Dead)
	{
		return;
	}

	State = NewState;
	StateTime = 0.f;

	switch (NewState)
	{
	case EEnemyBrainState::Approach:
		bWantsLeap = FMath::FRand() < LeapChance
			&& GetWorld()->GetTimeSeconds() - LastLeapTime >= LeapCooldown;
		break;
	case EEnemyBrainState::Leap:
		StopMoving();
		LastLeapTime = GetWorld()->GetTimeSeconds();
		if (Enemy && TargetPawn.IsValid())
		{
			// land just short of the player's centre
			const FVector ToTarget = TargetPawn->GetActorLocation() - Enemy->GetActorLocation();
			Enemy->LeapTo(TargetPawn->GetActorLocation() - ToTarget.GetSafeNormal2D() * LeapLandDistance);
		}
		break;
	case EEnemyBrainState::Windup:
		CurrentWindupTime = bOnScreen ? WindupTime : OffscreenWindupTime;
		StopMoving();
		break;
	case EEnemyBrainState::Idle:
	case EEnemyBrainState::Stagger:
	case EEnemyBrainState::Rising:
	case EEnemyBrainState::Dead:
		StopMoving();
		break;
	case EEnemyBrainState::Attack:
		StopMoving();
		// pounce already attacking
		if (Enemy && !Enemy->IsAttacking())
		{
			Enemy->StartAttack();
		}
		break;
	default:
		break;
	}
}

void UEnemyBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Enemy)
	{
		return;
	}

	StateTime += DeltaTime;
	RepathCooldown -= DeltaTime;

	// eye glow threat from wind up progress
	const float Threat = State == EEnemyBrainState::Leap || State == EEnemyBrainState::Attack ? 1.f : GetWindupAlpha();
	Enemy->UpdateEyeGlow(DeltaTime, Threat);

	// lost the player
	if (!TargetPawn.IsValid() && State != EEnemyBrainState::Idle && State != EEnemyBrainState::Dead)
	{
		GiveUpToken();
		EnterState(EEnemyBrainState::Idle);
		return;
	}

	switch (State)
	{
	case EEnemyBrainState::Circle:
		TickCircle(DeltaTime);
		break;
	case EEnemyBrainState::Approach:
		TickApproach(DeltaTime);
		break;
	case EEnemyBrainState::Windup:
		TickWindup(DeltaTime);
		break;
	case EEnemyBrainState::Recover:
		TickRecover(DeltaTime);
		break;
	case EEnemyBrainState::Leap:
		TickLeap(DeltaTime);
		break;
	case EEnemyBrainState::Stagger:
		if (StateTime >= StaggerTime)
		{
			EnterState(EEnemyBrainState::Circle);
		}
		break;
	case EEnemyBrainState::Rising:
		if (StateTime >= RiseDuration)
		{
			EnterState(EEnemyBrainState::Circle);
		}
		break;
	case EEnemyBrainState::Attack:
		// timeout in case the attack never started
		if (StateTime > 4.f)
		{
			NotifyAttackFinished();
		}
		break;
	default:
		break;
	}
}

FVector UEnemyBrainComponent::ComputeSlotGoal(float Radius) const
{
	const FVector Center = TargetPawn->GetActorLocation();
	return Center + FVector(FMath::Cos(SlotAngle), FMath::Sin(SlotAngle), 0.f) * Radius;
}

FVector UEnemyBrainComponent::ComputeOrbitGoal(float Radius) const
{
	const FVector Center = TargetPawn->GetActorLocation();
	const FVector Offset = Enemy->GetActorLocation() - Center;
	const float CurrentAngle = FMath::Atan2(Offset.Y, Offset.X);
	const float Delta = FMath::UnwindRadians(SlotAngle - CurrentAngle);
	const float MaxStep = FMath::DegreesToRadians(MaxOrbitStepDegrees);

	// close to the slot so go straight there
	if (FMath::Abs(Delta) <= MaxStep)
	{
		return ComputeSlotGoal(Radius);
	}

	// otherwise step around the player toward it
	const float StepAngle = CurrentAngle + FMath::Sign(Delta) * MaxStep;
	return Center + FVector(FMath::Cos(StepAngle), FMath::Sin(StepAngle), 0.f) * Radius;
}

void UEnemyBrainComponent::TickCircle(float DeltaTime)
{
	if (!bHasSlot)
	{
		StopMoving();
		return;
	}
	const float Radius = StandoffRadius * SlotRadiusScale * (bOnScreen ? 1.f : OffscreenStandoffScale);
	SteerTo(ComputeOrbitGoal(Radius), CircleSpeed, 50.f);
}

void UEnemyBrainComponent::TickApproach(float DeltaTime)
{
	if (!bHasToken || StateTime > ApproachTimeout)
	{
		GiveUpToken();
		EnterState(EEnemyBrainState::Recover);
		return;
	}

	const FVector ToTarget = TargetPawn->GetActorLocation() - Enemy->GetActorLocation();
	const float Distance = ToTarget.Size2D();
	if (Distance <= AttackRange)
	{
		EnterState(EEnemyBrainState::Windup);
		return;
	}

	if (bWantsLeap && bOnScreen && Distance >= LeapMinDistance && Distance <= LeapMaxDistance
		&& Enemy->GetCharacterMovement()->IsMovingOnGround())
	{
		EnterState(EEnemyBrainState::Leap);
		return;
	}

	// aim just inside attack range
	const FVector Goal = TargetPawn->GetActorLocation() - ToTarget.GetSafeNormal2D() * (AttackRange * 0.8f);
	SteerTo(Goal, ApproachSpeed, 10.f);
}

void UEnemyBrainComponent::TickWindup(float DeltaTime)
{
	if (!bHasToken)
	{
		EnterState(EEnemyBrainState::Recover);
		return;
	}

	// player backed off so chase again
	const float Distance = FVector::Dist2D(TargetPawn->GetActorLocation(), Enemy->GetActorLocation());
	if (Distance > AttackRange * 1.4f)
	{
		EnterState(EEnemyBrainState::Approach);
		return;
	}

	if (StateTime >= CurrentWindupTime)
	{
		EnterState(EEnemyBrainState::Attack);
	}
}

void UEnemyBrainComponent::TickLeap(float DeltaTime)
{
	// timeout in case the leap never left the ground
	if (StateTime > 2.f)
	{
		GiveUpToken();
		EnterState(EEnemyBrainState::Recover);
	}
}

void UEnemyBrainComponent::TickRecover(float DeltaTime)
{
	// move back toward the slot
	if (bHasSlot)
	{
		SteerTo(ComputeOrbitGoal(StandoffRadius * SlotRadiusScale * 1.1f), CircleSpeed, 50.f);
	}
	if (StateTime >= RecoverTime)
	{
		EnterState(EEnemyBrainState::Circle);
	}
}

void UEnemyBrainComponent::SteerTo(const FVector& Goal, float Speed, float AcceptRadius)
{
	MoveGoal = Goal;
	Enemy->GetCharacterMovement()->MaxWalkSpeed = Speed;

	const FVector ToGoal = Goal - Enemy->GetActorLocation();
	const float Distance = ToGoal.Size2D();

	AEnemyAIController* AI = GetAIController();
	if (AI && Distance > PathfindDistance)
	{
		// don't repath every frame
		if (!bIsPathing || RepathCooldown <= 0.f)
		{
			AI->MoveToLocation(Goal, AcceptRadius, /*bStopOnOverlap=*/false);
			bIsPathing = true;
			RepathCooldown = 0.5f;
		}
		return;
	}

	if (bIsPathing)
	{
		StopMoving();
	}

	if (Distance > AcceptRadius)
	{
		// slow down only near the goal
		const float Scale = FMath::Clamp(Distance / 120.f, 0.4f, 1.f);
		Enemy->AddMovementInput(ToGoal.GetSafeNormal2D(), Scale);
	}
}

void UEnemyBrainComponent::StopMoving()
{
	bIsPathing = false;
	if (AEnemyAIController* AI = GetAIController())
	{
		AI->StopMovement();
	}
}

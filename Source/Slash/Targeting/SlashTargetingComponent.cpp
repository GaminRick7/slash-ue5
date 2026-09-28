// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlashTargetingComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Enemy/TargetableComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarTargetingDebug(
	TEXT("slash.Targeting.Debug"),
	0,
	TEXT("Draw a box on every lock-on candidate: green for the current target, red for the rest."),
	ECVF_Cheat);

USlashTargetingComponent::USlashTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USlashTargetingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateTargeting(DeltaTime);
}

void USlashTargetingComponent::UpdateTargeting(float DeltaSeconds)
{
	const TArray<UTargetableComponent*> Candidates = GatherCandidates();
	RefreshPersistentTarget(Candidates);
	if (CVarTargetingDebug.GetValueOnGameThread() > 0)
	{
		DebugDrawAllCandidates(Candidates);
	}
}

TArray<UTargetableComponent*> USlashTargetingComponent::GatherCandidates() const
{
	TArray<UTargetableComponent*> Candidates;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return Candidates;
	}

	FCollisionObjectQueryParams ObjectParams(COLLISION_TARGETABLE);
	FCollisionShape Sphere = FCollisionShape::MakeSphere(TargetQueryRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Owner->GetActorLocation(), FQuat::Identity, ObjectParams, Sphere, QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		UTargetableComponent* Targetable = Cast<UTargetableComponent>(Overlap.GetComponent());
		if (Targetable && Targetable->IsTargetable())
		{
			Candidates.Add(Targetable);
		}
	}

	return Candidates;
}

TOptional<float> USlashTargetingComponent::ScoreCandidate(const UTargetableComponent* Candidate, const FVector& Origin, const FVector& IntentDir, const UTargetableComponent* Incumbent, float Clamp) const
{
	if (!Candidate || !Candidate->IsTargetable())
	{
		return {};
	}

	const FVector TargetPoint = Candidate->GetTargetPoint();
	const float Dist = FVector::Dist(Origin, TargetPoint);

	const float EffectiveMaxDistance = Clamp > 0.f ? FMath::Min(Clamp, Weights.MaxDistance) : Weights.MaxDistance;
	if (Dist > EffectiveMaxDistance)
	{
		return {};
	}

	const FVector DirToTarget = (TargetPoint - Origin).GetSafeNormal();
	const float Dot = FVector::DotProduct(IntentDir, DirToTarget);
	if (Dot < Weights.MinDot)
	{
		return {};
	}

	const float AngleAlpha = FMath::GetMappedRangeValueClamped(FVector2D(Weights.MinDot, 1.f), FVector2D(0.f, 1.f), Dot);
	const float DistanceAlpha = 1.f - FMath::Clamp(Dist / Weights.MaxDistance, 0.f, 1.f);

	float Score = Weights.AngleWeight * AngleAlpha + Weights.DistanceWeight * DistanceAlpha + Candidate->TargetPriorityBias;
	if (Candidate == Incumbent)
	{
		Score += Weights.StickinessBonus;
	}

	return Score;
}

UTargetableComponent* USlashTargetingComponent::SelectBest(const TArray<UTargetableComponent*>& Candidates) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	const FVector Origin = Owner->GetActorLocation();

	// camera direction, who the player is looking at
	FVector IntentDir = Owner->GetActorForwardVector();
	if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		if (const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				IntentDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
			}
		}
	}

	UTargetableComponent* Best = nullptr;
	float BestScore = 0.f;

	for (UTargetableComponent* Candidate : Candidates)
	{
		const TOptional<float> Score = ScoreCandidate(Candidate, Origin, IntentDir, PersistentTarget, TargetQueryRadius);
		if (Score.IsSet() && (!Best || Score.GetValue() > BestScore))
		{
			Best = Candidate;
			BestScore = Score.GetValue();
		}
	}

	return Best;
}

void USlashTargetingComponent::RefreshPersistentTarget(const TArray<UTargetableComponent*>& Candidates)
{
	PersistentTarget = SelectBest(Candidates);
}

void USlashTargetingComponent::DebugDrawAllCandidates(const TArray<UTargetableComponent*>& Candidates) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const UTargetableComponent* Candidate : Candidates)
	{
		const FColor BoxColor = (Candidate == PersistentTarget) ? FColor::Green : FColor::Red;
		DrawDebugBox(World, Candidate->GetTargetPoint(), FVector(30.f), BoxColor, false, -1.f, 0, 2.f);
	}
}

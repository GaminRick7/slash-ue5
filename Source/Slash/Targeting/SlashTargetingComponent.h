// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SlashTargetingComponent.generated.h"

class UTargetableComponent;

USTRUCT(BlueprintType)
struct FSlashTargetWeights
{
	GENERATED_BODY()

	// lock on range, keep under TargetQueryRadius
	UPROPERTY(EditAnywhere, Category = "Targeting")
	float MaxDistance = 500.f;

	// cone half angle as a dot product, 0.5 is 60 deg
	UPROPERTY(EditAnywhere, Category = "Targeting")
	float MinDot = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Targeting")
	float DistanceWeight = 0.35f;

	// above distance weight so facing matters more
	UPROPERTY(EditAnywhere, Category = "Targeting")
	float AngleWeight = 0.65f;

	UPROPERTY(EditAnywhere, Category = "Targeting")
	float StickinessBonus = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Targeting")
	bool bRequireLineOfSight = true;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API USlashTargetingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USlashTargetingComponent();

	void UpdateTargeting(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Targeting")
	UTargetableComponent* GetPersistentTarget() const { return PersistentTarget; }

	UPROPERTY(EditDefaultsOnly, Category = "Targeting")
	float TargetQueryRadius = 2000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Targeting")
	FSlashTargetWeights Weights;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TArray<UTargetableComponent*> GatherCandidates() const;
	TOptional<float> ScoreCandidate(const UTargetableComponent* Candidate, const FVector& Origin, const FVector& IntentDir, const UTargetableComponent* Incumbent, float Clamp) const;
	UTargetableComponent* SelectBest(const TArray<UTargetableComponent*>& Candidates) const;
	void RefreshPersistentTarget(const TArray<UTargetableComponent*>& Candidates);
	void DebugDrawAllCandidates(const TArray<UTargetableComponent*>& Candidates) const;

	UPROPERTY(Transient)
	TObjectPtr<UTargetableComponent> PersistentTarget;
};

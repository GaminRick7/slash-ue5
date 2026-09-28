// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyBrainComponent.generated.h"

class AEnemy;
class AEnemyAIController;
class APawn;

UENUM(BlueprintType)
enum class EEnemyBrainState : uint8
{
	Idle,		// no player
	Circle,		// holding its slot around the player
	Approach,	// has a token and is closing in
	Windup,		// telegraphing the attack
	Leap,		// pouncing, landing is the hit
	Attack,		// attack clip playing
	Recover,	// backing off after attacking
	Stagger,	// flinching
	Rising,		// climbing out of the ground after spawning
	Dead
};

// per enemy state machine driven by the director's slot and token
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API UEnemyBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyBrainComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// set by the director
	void SetTargetPawn(APawn* NewTarget);
	void SetSlot(float AngleRadians, float RadiusScale);
	void SetHasAttackToken(bool bHasToken);
	void SetOnScreen(bool bInOnScreen) { bOnScreen = bInOnScreen; }

	// only enemies circling in their slot can be given a token
	bool CanAcceptToken() const;

	// set by AEnemy
	void NotifyStaggered();
	void NotifyAttackFinished();
	void NotifyDied();

	// landed after a pounce
	void NotifyLanded();

	// wait for the spawn clip
	void NotifyRising(float Duration);

	EEnemyBrainState GetState() const { return State; }
	bool IsOnScreen() const { return bOnScreen; }

	// approaching, winding up, leaping or attacking
	bool IsThreatening() const;

	// 0..1 progress through the wind-up
	float GetWindupAlpha() const;
	FVector GetMoveGoal() const { return MoveGoal; }

	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float StandoffRadius = 400.f;

	// distance at which an approaching enemy stops and winds up
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float AttackRange = 130.f;

	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float CircleSpeed = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float ApproachSpeed = 330.f;

	// wind-up time before an attack
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float WindupTime = 0.35f;

	// longer wind up when off screen
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float OffscreenWindupTime = 1.0f;

	// off-screen enemies stand this much further out
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float OffscreenStandoffScale = 1.25f;

	// max step around the player so enemies go around instead of through
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float MaxOrbitStepDegrees = 35.f;

	// pounce range, on screen only
	UPROPERTY(EditDefaultsOnly, Category = "Brain|Leap")
	float LeapMinDistance = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Brain|Leap")
	float LeapMaxDistance = 650.f;

	// pounce chance per approach
	UPROPERTY(EditDefaultsOnly, Category = "Brain|Leap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeapChance = 0.6f;

	// how far short of the player's centre the pounce lands
	UPROPERTY(EditDefaultsOnly, Category = "Brain|Leap")
	float LeapLandDistance = 55.f;

	// minimum time between pounces for one enemy
	UPROPERTY(EditDefaultsOnly, Category = "Brain|Leap")
	float LeapCooldown = 5.f;

	// give up the approach after this long
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float ApproachTimeout = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float RecoverTime = 1.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float StaggerTime = 0.6f;

	// pathfind beyond this, steer directly inside it
	UPROPERTY(EditDefaultsOnly, Category = "Brain")
	float PathfindDistance = 700.f;

protected:
	virtual void BeginPlay() override;

private:
	void EnterState(EEnemyBrainState NewState);
	void TickCircle(float DeltaTime);
	void TickApproach(float DeltaTime);
	void TickWindup(float DeltaTime);
	void TickRecover(float DeltaTime);
	void TickLeap(float DeltaTime);

	// steer or pathfind to a point
	void SteerTo(const FVector& Goal, float Speed, float AcceptRadius);
	void StopMoving();
	void GiveUpToken();
	AEnemyAIController* GetAIController() const;
	FVector ComputeSlotGoal(float Radius) const;

	// next point on the way round to the slot
	FVector ComputeOrbitGoal(float Radius) const;

	UPROPERTY(Transient)
	TObjectPtr<AEnemy> Enemy;

	TWeakObjectPtr<APawn> TargetPawn;
	EEnemyBrainState State = EEnemyBrainState::Idle;
	float StateTime = 0.f;

	float SlotAngle = 0.f;
	float SlotRadiusScale = 1.f;
	bool bHasSlot = false;
	bool bHasToken = false;
	bool bIsPathing = false;
	bool bOnScreen = true;

	// wind-up time chosen when the wind-up starts
	float CurrentWindupTime = 0.f;
	float RepathCooldown = 0.f;
	float RiseDuration = 0.f;

	// rolled on entering Approach
	bool bWantsLeap = false;
	double LastLeapTime = -1000.0;
	FVector MoveGoal = FVector::ZeroVector;
};

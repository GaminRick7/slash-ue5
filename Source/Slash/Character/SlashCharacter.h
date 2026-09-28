// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/CharacterOutfit.h"
#include "SlashCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UPointLightComponent;
class UMeleeWeaponComponent;
class UInputAction;
class UComboComponent;
class USlashCameraRigComponent;
class USlashTargetingComponent;
class UHealthComponent;
class UAnimSequenceBase;
class UAnimMontage;
class UCurveFloat;
struct FInputActionValue;
struct FInputActionInstance;

UCLASS()
class SLASH_API ASlashCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASlashCharacter();

	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UHealthComponent* GetHealthComponent() const { return HealthComponent; }
	USlashTargetingComponent* GetTargetingComponent() const { return TargetingComponent; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDead() const { return bIsDead; }

	// briefly slows only this character when a hit lands
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ApplyHitStop(float Duration, float TimeDilation);

	// read by the AnimBP

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetSprintAlpha() const { return SprintAlpha; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInCombo() const { return bIsInCombo; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDodging() const { return bIsDodging; }

	// roll or buffer the press
	void TryDodge();

	// smoothed turn rate around -1..1 for a lean additive
	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetLeanAmount() const { return LeanAmount; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual bool CanJumpWhileFalling() const override;
	virtual void CheckJumpInput(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USlashCameraRigComponent> CameraRig;

	// camera light for the hero channel only, no shadows
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UPointLightComponent> HeroLight;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	// optional, no jumping until set
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackHoldAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackHeavyAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> DodgeAction;

	// sword that draws the blade and detects its own hits, needs RightHandSocket on the mesh
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UMeleeWeaponComponent> SwordComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UComboComponent> ComboComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	TObjectPtr<USlashTargetingComponent> TargetingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UHealthComponent> HealthComponent;

	// no damage for this long after a hit so crowds can't juggle
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Damage Taken")
	float HitInvulnerabilityTime = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Damage Taken")
	float HitKnockbackSpeed = 380.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Damage Taken")
	TObjectPtr<UAnimSequenceBase> HitReactionAnim;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Damage Taken")
	TObjectPtr<UAnimSequenceBase> DeathAnim;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Damage Taken")
	FName ReactionSlot = TEXT("DefaultSlot");

	// clothing worn over the hidden base mesh
	UPROPERTY(EditDefaultsOnly, Category = "Appearance")
	FCharacterOutfit Outfit;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float StrafeSpeedMultiplier = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float WalkSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeed = 800.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeedInterpSpeed = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintForwardDotThreshold = 0.7f;

	// turn rate standing still, lowered with speed
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float TurnInterpSpeed = 14.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintTurnInterpSpeed = 6.f;

	// turn rate during attacks
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float CombatTurnInterpSpeed = 3.5f;

	// stick deflection needed to walk out of an attack's recovery
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float RecoveryCancelInputThreshold = 0.3f;

	// blend time when walking out of an attack back into locomotion
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float ComboCancelBlendTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float LeanInterpSpeed = 8.f;

	// yaw rate (deg/s) that counts as a full lean
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float LeanYawRateScale = 180.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Jump")
	float CoyoteTime = 0.12f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Jump")
	float JumpBufferTime = 0.15f;

	// fraction of vertical speed kept when jump is released early
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Jump")
	float JumpCutMultiplier = 0.45f;

	// gravity multiplier while falling
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Jump")
	float FallGravityScale = 1.9f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Landing")
	float LandShakeMinSpeed = 400.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Landing")
	float LandShakeMaxSpeed = 1400.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Landing")
	float LandMaxPitchDip = 55.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Combat")
	float AttackFOVPunch = -3.5f;

	// placeholder until damage moves onto the combo node
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackDamage = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float HitStopDuration = 0.09f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float HitStopTimeDilation = 0.15f;

	// sets the leap duration, the leap always ends at its target
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackLeapSpeed = 900.f;

	// no leap if already this close to the target
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackLeapMinDistance = 90.f;

	// land this far short of the target instead of on top of it
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackLeapStopDistance = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackLeapMinDuration = 0.08f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackLeapMaxDuration = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	TObjectPtr<UAnimSequenceBase> DodgeAnim;

	// roll clip speed
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeAnimPlayRate = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeDistance = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeDuration = 0.6f;

	// invulnerable window during the roll
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeInvulnerableStart = 0.04f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeInvulnerableEnd = 0.42f;

	// attack press after this ends the roll early
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeAttackCancelTime = 0.4f;

	// how long an early dodge press is held
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeBufferTime = 0.2f;

	// speed kept after the roll
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Dodge")
	float DodgeExitSpeed = 150.f;

	bool bWantsToSprint = false;
	bool bIsSprinting = false;
	float SprintAlpha = 0.f;

	bool bIsInCombo = false;
	bool bRootMotionMovementActive = false;

	void SetRootMotionMovementActive(bool bActive);

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint(const FInputActionValue& Value);
	void StopSprint(const FInputActionValue& Value);
	void StartJump(const FInputActionValue& Value);
	void StopJump(const FInputActionValue& Value);
	void RequestAttack(const FInputActionInstance& Instance);
	void RequestDodge(const FInputActionValue& Value);

	UFUNCTION()
	void HandleComboNodeEntered(FName NodeId, bool bIsComboStart);

	UFUNCTION()
	void HandleAttackWindowOpened(FName NodeId);

	UFUNCTION()
	void HandleAttackWindowClosed(FName NodeId);

	// blade contact, drives all impact feedback
	UFUNCTION()
	void HandleWeaponHit(AActor* HitActor, const FHitResult& Hit, int32 HitIndex);

	UFUNCTION()
	void HandleComboEnded(FName NodeId, bool bWasInterrupted);

	UFUNCTION()
	void HandleDeath(AActor* Killer);

private:
	void UpdateFacing(float DeltaSeconds);
	void UpdateSprintState(float DeltaSeconds);
	void UpdateAirState(float DeltaSeconds);
	void EndHitStop();
	bool LeapTowardTarget();
	void UpdateLeap(float DeltaSeconds);
	void EndLeap();

	bool CanDodge() const;
	void StartDodge();
	void UpdateDodge(float DeltaSeconds);

	// bInterrupted when ended early by a hit, attack or death
	void EndDodge(bool bInterrupted);
	bool IsDodgeInvulnerable() const;

	bool bIsDodging = false;
	float DodgeElapsed = 0.f;
	float DodgeBufferRemaining = 0.f;
	FVector DodgeDirection = FVector::ForwardVector;
	uint16 DodgeRootMotionID = 0;
	TWeakObjectPtr<UAnimMontage> DodgeMontage;

	// attack pressed mid roll, fired when allowed
	const UInputAction* QueuedAttackAction = nullptr;

	// roll speed curve built in BeginPlay
	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> DodgeSpeedCurve;

	// curve average so distance stays the same
	float DodgeCurveMean = 1.f;

	// last move direction, kept since attacks block movement but still steer
	FVector CachedMoveDirection = FVector::ZeroVector;
	uint64 MoveInputFrame = 0;

	// leap moves by hand with movement off so the landing is exact
	bool bIsLeaping = false;
	FVector LeapStartLocation = FVector::ZeroVector;
	FVector LeapEndLocation = FVector::ZeroVector;
	// yaw and position share one alpha, both ends fixed at leap start
	float LeapStartYaw = 0.f;
	float LeapTargetYaw = 0.f;
	float LeapElapsed = 0.f;
	float LeapDuration = 0.f;

	float TimeSinceLeftGround = 0.f;
	float JumpBufferRemaining = 0.f;
	float PeakFallSpeed = 0.f;
	float BaseGravityScale = 1.f;
	float PreviousYaw = 0.f;
	float LeanAmount = 0.f;

	FTimerHandle HitStopTimerHandle;

	bool bIsDead = false;
	double LastDamageTakenTime = -1000.0;

public:
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

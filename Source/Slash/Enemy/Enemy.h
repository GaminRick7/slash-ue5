// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/CharacterOutfit.h"
#include "Enemy.generated.h"

class UAnimSequenceBase;
class UTargetableComponent;
class UHealthComponent;
class UMaterialInterface;
class UAnimMontage;
class UEnemyBrainComponent;
class UMaterialInstanceDynamic;

// body part hit, from the enemy's point of view
UENUM(BlueprintType)
enum class EEnemyHitZone : uint8
{
	Head,
	ShoulderLeft,
	ShoulderRight,
	Chest,
	Stomach
};

// one monster type on UAL1_Standard_Skeleton with its own bone lengths
USTRUCT(BlueprintType)
struct FMonsterVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Monster")
	TObjectPtr<USkeletalMesh> Body;

	// extra meshes following the body pose
	UPROPERTY(EditAnywhere, Category = "Monster")
	FCharacterOutfit Gear;

	// colour skins, one picked per enemy
	UPROPERTY(EditAnywhere, Category = "Monster")
	TArray<TObjectPtr<UMaterialInterface>> Skins;

	UPROPERTY(EditAnywhere, Category = "Monster")
	float Scale = 1.f;

	// capsule is the hurtbox so sized per monster
	UPROPERTY(EditAnywhere, Category = "Monster")
	float CapsuleHalfHeight = 88.f;

	UPROPERTY(EditAnywhere, Category = "Monster")
	float CapsuleRadius = 34.f;

	// relative chance of picking this monster
	UPROPERTY(EditAnywhere, Category = "Monster", meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

// claw or bite that lands partway through the clip
USTRUCT(BlueprintType)
struct FZombieAttack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Attack")
	TObjectPtr<UAnimSequenceBase> Anim;

	UPROPERTY(EditAnywhere, Category = "Attack")
	float Damage = 10.f;

	// hit time as a fraction of the clip
	UPROPERTY(EditAnywhere, Category = "Attack", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HitFraction = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Attack")
	float PlayRate = 1.f;

	// distance in front of the enemy to the centre of the hit sphere
	UPROPERTY(EditAnywhere, Category = "Attack")
	float Reach = 100.f;

	UPROPERTY(EditAnywhere, Category = "Attack")
	float Radius = 60.f;

	// small push forward on the swing
	UPROPERTY(EditAnywhere, Category = "Attack")
	float LungeSpeed = 220.f;
};

// a bestiary monster that attacks with claws and teeth
UCLASS()
class SLASH_API AEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemy();

	// hit reactions here since it's the only place with both hit location and post hit health
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UEnemyBrainComponent* GetBrain() const { return BrainComponent; }
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	// called after the wind up, random attack
	void StartAttack();

	// plays the climb-out-of-the-ground clip for wave spawns
	void PlaySpawnRise();

	// pounce to Destination, landing is the hit
	void LeapTo(const FVector& Destination);

	// true while an attack or pounce clip is playing
	bool IsAttacking() const { return ActiveAttackMontage.IsValid(); }

	virtual void Landed(const FHitResult& Hit) override;

	// 0..1 threat from the brain drives the eye glow
	void UpdateEyeGlow(float DeltaTime, float Threat);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	TObjectPtr<UTargetableComponent> TargetableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UEnemyBrainComponent> BrainComponent;

	// scratch and bite
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
	TArray<FZombieAttack> Attacks;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
	FName AttackSlot = TEXT("DefaultSlot");

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UAnimSequenceBase> SpawnAnim;

	// pounce speed which sets the flight time
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapSpeed = 1300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapApexHeight = 70.f;

	// limits on flight time
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapMinFlightTime = 0.28f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapMaxFlightTime = 0.5f;

	// claw clip timed to connect on landing
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	TObjectPtr<UAnimSequenceBase> LeapAnim;

	// time in LeapAnim where the claws connect
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapAnimContactTime = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapDamage = 14.f;

	// landing hit sphere
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapReach = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Leap")
	float LeapRadius = 90.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UAnimSequenceBase> DeathAnim;

	// material parameter for the emissive (eye) strength
	UPROPERTY(EditDefaultsOnly, Category = "Appearance|Eyes")
	FName EyeGlowParameter = TEXT("EmissiveStrength");

	// eye glow at rest
	UPROPERTY(EditDefaultsOnly, Category = "Appearance|Eyes")
	float EyeGlowIdle = 8.f;

	// eye glow when attacking
	UPROPERTY(EditDefaultsOnly, Category = "Appearance|Eyes")
	float EyeGlowThreat = 45.f;

	// how quickly the glow follows the threat level
	UPROPERTY(EditDefaultsOnly, Category = "Appearance|Eyes")
	float EyeGlowResponse = 14.f;

	// monster types this enemy can be
	UPROPERTY(EditDefaultsOnly, Category = "Appearance")
	TArray<FMonsterVariant> Variants;

	// Variants index or -1 to pick from the actor name
	UPROPERTY(EditAnywhere, Category = "Appearance")
	int32 VariantIndex = INDEX_NONE;

	// seconds before the body is destroyed
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float CorpseLifeSpan = 5.f;

	UFUNCTION()
	void HandleDeath(AActor* Killer);

	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// knockback away from the attacker (horizontal only)
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	float KnockbackSpeed = 450.f;

	// upward part of the knockback
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	float KnockbackUpSpeed = 180.f;

	// hit flash overlay, unlit emissive, optional
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	TObjectPtr<UMaterialInterface> HitFlashMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	float HitFlashDuration = 0.12f;

	// flinch clip per body zone
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	TMap<EEnemyHitZone, TObjectPtr<UAnimSequenceBase>> HitReactionAnims;

	// AnimBP slot the flinch plays in
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	FName HitReactionSlot = TEXT("DefaultSlot");

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	float HitReactionBlendIn = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	float HitReactionBlendOut = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction")
	float HitReactionPlayRate = 1.4f;

	// zone boundaries as a fraction of capsule height from the feet
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeadZoneHeight = 0.85f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChestZoneHeight = 0.65f;

	// off centre distance for a shoulder hit as a fraction of radius
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShoulderLateralFraction = 0.4f;

private:
	// picks monster and skin, false if none usable
	bool PickVariant(int32& OutVariant, UMaterialInterface*& OutSkin) const;

	// sets up body, gear and capsule
	void ApplyVariant();

	// replaces the default monster material with Skin on the body and gear
	void ApplySkin(const FMonsterVariant& Variant, UMaterialInterface* Skin);

	void SetEyeGlow(float Glow);

	// dynamic materials for the eye glow
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> EyeGlowMaterials;

	float EyeGlow = 0.f;

	void EndHitFlash();

	// sets the overlay on every skeletal mesh (body and gear)
	void SetBodyOverlayMaterial(UMaterialInterface* Material);

	// maps a hit location to a body zone
	EEnemyHitZone ClassifyHitZone(const FVector& ImpactPoint) const;

	void PlayHitReaction(EEnemyHitZone Zone);

	FTimerHandle HitFlashTimerHandle;

	void LandAttack();

	// damages living non-enemy pawns in front, returns the count
	int32 ApplyStrike(float Reach, float Radius, float Damage);

	// restores normal gravity after a pounce
	void EndLeap();

	bool bLeapStrikePending = false;
	bool bLeaping = false;
	float PreLeapGravityScale = 1.f;

	// stops an attack without telling the brain it finished
	void CancelAttack();

	int32 ActiveAttackIndex = INDEX_NONE;
	TWeakObjectPtr<UAnimMontage> ActiveAttackMontage;
	FTimerHandle AttackHitTimerHandle;
};

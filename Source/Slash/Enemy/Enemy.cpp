// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy.h"
#include "Character/HeroLighting.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "AIController.h"
#include "Combat/CombatAnimation.h"
#include "Animation/AnimMontage.h"
#include "Combat/HealthComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBrainComponent.h"
#include "Enemy/SlashEncounterDirector.h"
#include "Framework/SlashGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"
#include "TimerManager.h"
#include "Enemy/TargetableComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// FObjectFinder only works inside the constructor
	void AddHitReactionAnim(TMap<EEnemyHitZone, TObjectPtr<UAnimSequenceBase>>& Anims, EEnemyHitZone Zone, const TCHAR* AssetPath)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Finder(AssetPath);
		if (Finder.Succeeded())
		{
			Anims.Add(Zone, Finder.Object);
		}
	}

	// same for a monster body and its gear
	void AddMonsterVariant(TArray<FMonsterVariant>& Variants, const TCHAR* BodyPath, std::initializer_list<const TCHAR*> GearPaths,
		std::initializer_list<const TCHAR*> SkinPaths, float Scale, float CapsuleHalfHeight, float CapsuleRadius, float Weight)
	{
		ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyFinder(BodyPath);
		if (!BodyFinder.Succeeded())
		{
			return;
		}
		FMonsterVariant& Variant = Variants.AddDefaulted_GetRef();
		Variant.Body = BodyFinder.Object;
		Variant.Scale = Scale;
		Variant.CapsuleHalfHeight = CapsuleHalfHeight;
		Variant.CapsuleRadius = CapsuleRadius;
		Variant.Weight = Weight;
		Variant.Gear.bHideBaseMesh = false;
		for (const TCHAR* GearPath : GearPaths)
		{
			ConstructorHelpers::FObjectFinder<USkeletalMesh> GearFinder(GearPath);
			if (GearFinder.Succeeded())
			{
				Variant.Gear.Parts.Add(GearFinder.Object);
			}
		}
		for (const TCHAR* SkinPath : SkinPaths)
		{
			ConstructorHelpers::FObjectFinder<UMaterialInterface> SkinFinder(SkinPath);
			if (SkinFinder.Succeeded())
			{
				Variant.Skins.Add(SkinFinder.Object);
			}
		}
	}

	void AddZombieAttack(TArray<FZombieAttack>& Attacks, const TCHAR* AssetPath, float Damage, float HitFraction)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Finder(AssetPath);
		if (Finder.Succeeded())
		{
			FZombieAttack& Attack = Attacks.AddDefaulted_GetRef();
			Attack.Anim = Finder.Object;
			Attack.Damage = Damage;
			Attack.HitFraction = HitFraction;
		}
	}
}

AEnemy::AEnemy()
{
	PrimaryActorTick.bCanEverTick = false;

	// possessed whether placed in the level or spawned
	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// face the focus instead of the move direction
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 150.f;

	// director handles spacing, RVO stops overlap
	GetCharacterMovement()->bUseRVOAvoidance = true;
	GetCharacterMovement()->AvoidanceConsiderationRadius = 150.f;

	// on the capsule so the lock-on point is at the body's centre
	TargetableComponent = CreateDefaultSubobject<UTargetableComponent>(TEXT("TargetableComponent"));
	TargetableComponent->SetupAttachment(GetCapsuleComponent());

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	// feet on the capsule floor facing +x
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));

	// fallback body replaced in OnConstruction
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> EnemyMeshAsset(TEXT("/Game/Animations/UAL2/SkeletalMeshes/UAL2.UAL2"));
	if (EnemyMeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(EnemyMeshAsset.Object);
	}

	AddMonsterVariant(Variants, TEXT("/Game/Characters/Monsters/Imp/Imp/SkeletalMeshes/Imp_Body.Imp_Body"),
		{ TEXT("/Game/Characters/Monsters/Imp/Imp/SkeletalMeshes/Imp_Shorts.Imp_Shorts"),
		  TEXT("/Game/Characters/Monsters/Imp/Imp/SkeletalMeshes/Imp_Chains.Imp_Chains"),
		  TEXT("/Game/Characters/Monsters/Imp/Imp/SkeletalMeshes/Imp_SpikedCollar.Imp_SpikedCollar"),
		  TEXT("/Game/Characters/Monsters/Imp/Imp/SkeletalMeshes/Imp_Mace.Imp_Mace") },
		{ TEXT("/Game/Characters/Monsters/Imp/Imp/Materials/MI_Imp.MI_Imp"),
		  TEXT("/Game/Characters/Monsters/Imp/Imp/Materials/MI_Imp_Green.MI_Imp_Green"),
		  TEXT("/Game/Characters/Monsters/Imp/Imp/Materials/MI_Imp_Blue.MI_Imp_Blue") },
		1.f, 84.f, 34.f, 1.5f);
	// puglin is 93 cm so scale it up a bit
	AddMonsterVariant(Variants, TEXT("/Game/Characters/Monsters/Puglin/Puglin/SkeletalMeshes/Puglin_Body.Puglin_Body"),
		{ TEXT("/Game/Characters/Monsters/Puglin/Puglin/SkeletalMeshes/Puglin_Tusks.Puglin_Tusks"),
		  TEXT("/Game/Characters/Monsters/Puglin/Puglin/SkeletalMeshes/Puglin_Stick.Puglin_Stick") },
		{ TEXT("/Game/Characters/Monsters/Puglin/Puglin/Materials/MI_Puglin.MI_Puglin"),
		  TEXT("/Game/Characters/Monsters/Puglin/Puglin/Materials/MI_Puglin_Red.MI_Puglin_Red"),
		  TEXT("/Game/Characters/Monsters/Puglin/Puglin/Materials/MI_Puglin_Teal.MI_Puglin_Teal") },
		1.2f, 56.f, 36.f, 1.f);

	BrainComponent = CreateDefaultSubobject<UEnemyBrainComponent>(TEXT("BrainComponent"));

	// hit points are where the claw crosses chest height and where the head snaps forward
	AddZombieAttack(Attacks, TEXT("/Game/Animations/UAL2/SkeletalMeshes/UAL2Zombie_Scratch.UAL2Zombie_Scratch"), 10.f, 0.34f);
	AddZombieAttack(Attacks, TEXT("/Game/Animations/UAL2/SkeletalMeshes/UAL2Zombie_Bite.UAL2Zombie_Bite"), 16.f, 0.4f);

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> LeapAnimAsset(TEXT("/Game/Animations/UAL2/SkeletalMeshes/UAL2Zombie_Scratch.UAL2Zombie_Scratch"));
	if (LeapAnimAsset.Succeeded())
	{
		LeapAnim = LeapAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> SpawnAnimAsset(TEXT("/Game/Animations/UAL2/SkeletalMeshes/UAL2Zombie_Spawn.UAL2Zombie_Spawn"));
	if (SpawnAnimAsset.Succeeded())
	{
		SpawnAnim = SpawnAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> DeathAnimAsset(TEXT("/Game/Animations/UAL1/SkeletalMeshes/UAL1Death01.UAL1Death01"));
	if (DeathAnimAsset.Succeeded())
	{
		DeathAnim = DeathAnimAsset.Object;
	}

	// ABP_Zombie uses the UAL2 zombie clips
	static ConstructorHelpers::FClassFinder<UAnimInstance> ZombieAnimBP(TEXT("/Game/Characters/Zombie/ABP_Zombie"));
	if (ZombieAnimBP.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(ZombieAnimBP.Class);
	}

	// from UAL1 because UAL1_Standard only has chest and head hit clips
	AddHitReactionAnim(HitReactionAnims, EEnemyHitZone::Head, TEXT("/Game/Animations/UAL1/SkeletalMeshes/UAL1Hit_Head.UAL1Hit_Head"));
	AddHitReactionAnim(HitReactionAnims, EEnemyHitZone::ShoulderLeft, TEXT("/Game/Animations/UAL1/SkeletalMeshes/UAL1Hit_Shoulder_L.UAL1Hit_Shoulder_L"));
	AddHitReactionAnim(HitReactionAnims, EEnemyHitZone::ShoulderRight, TEXT("/Game/Animations/UAL1/SkeletalMeshes/UAL1Hit_Shoulder_R.UAL1Hit_Shoulder_R"));
	AddHitReactionAnim(HitReactionAnims, EEnemyHitZone::Chest, TEXT("/Game/Animations/UAL1/SkeletalMeshes/UAL1Hit_Chest.UAL1Hit_Chest"));
	AddHitReactionAnim(HitReactionAnims, EEnemyHitZone::Stomach, TEXT("/Game/Animations/UAL1/SkeletalMeshes/UAL1Hit_Stomach.UAL1Hit_Stomach"));
}

float AEnemy::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// deduct first, HandleDeath has already run if this killed it
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// no flinch on a killing blow
	if (ActualDamage <= 0.f || !HealthComponent || !HealthComponent->IsAlive())
	{
		return ActualDamage;
	}

	// push straight away from the attacker
	FVector Away = DamageCauser ? GetActorLocation() - DamageCauser->GetActorLocation() : -GetActorForwardVector();
	Away.Z = 0.f;
	if (!Away.Normalize())
	{
		// attacker is exactly on top of us
		Away = -GetActorForwardVector();
	}

	// override velocity so combo hits don't add up
	LaunchCharacter(Away * KnockbackSpeed + FVector::UpVector * KnockbackUpSpeed, true, true);

	// only point damage has a location, default to chest
	EEnemyHitZone Zone = EEnemyHitZone::Chest;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
		Zone = ClassifyHitZone(PointDamage.HitInfo.ImpactPoint);
	}

	// cancel the attack first so the brain is told in the same frame
	CancelAttack();
	if (BrainComponent)
	{
		BrainComponent->NotifyStaggered();
	}

	PlayHitReaction(Zone);

	if (HitFlashMaterial)
	{
		SetBodyOverlayMaterial(HitFlashMaterial);

		// another hit before the timer fires just restarts it
		GetWorldTimerManager().SetTimer(HitFlashTimerHandle, this, &AEnemy::EndHitFlash, HitFlashDuration, false);
	}

	return ActualDamage;
}

void AEnemy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyVariant();
	SlashHeroLighting::AddToHeroChannel(this);
}

bool AEnemy::PickVariant(int32& OutVariant, UMaterialInterface*& OutSkin) const
{
	float TotalWeight = 0.f;
	for (const FMonsterVariant& Variant : Variants)
	{
		TotalWeight += Variant.Body ? FMath::Max(Variant.Weight, 0.f) : 0.f;
	}
	if (TotalWeight <= 0.f)
	{
		return false;
	}

	// name crc seed so the pick is stable and spawns in a row don't all match
	const FRandomStream Stream(static_cast<int32>(FCrc::StrCrc32(*GetName())));
	OutVariant = INDEX_NONE;
	float Roll = Stream.FRand() * TotalWeight;
	for (int32 Index = 0; Index < Variants.Num(); ++Index)
	{
		const FMonsterVariant& Variant = Variants[Index];
		if (!Variant.Body || Variant.Weight <= 0.f)
		{
			continue;
		}
		OutVariant = Index;
		Roll -= Variant.Weight;
		if (Roll <= 0.f)
		{
			break;
		}
	}
	if (Variants.IsValidIndex(VariantIndex) && Variants[VariantIndex].Body)
	{
		OutVariant = VariantIndex;
	}

	const FMonsterVariant& Variant = Variants[OutVariant];
	OutSkin = Variant.Skins.IsEmpty() ? nullptr : Variant.Skins[Stream.RandRange(0, Variant.Skins.Num() - 1)].Get();
	return true;
}

void AEnemy::ApplyVariant()
{
	int32 Chosen = INDEX_NONE;
	UMaterialInterface* Skin = nullptr;
	if (!PickVariant(Chosen, Skin))
	{
		return;
	}
	const FMonsterVariant& Variant = Variants[Chosen];

	GetCapsuleComponent()->SetCapsuleSize(Variant.CapsuleRadius, Variant.CapsuleHalfHeight);

	// clear any material overrides left by an older version of this class
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetSkeletalMeshAsset(Variant.Body);
	Body->EmptyOverrideMaterials();
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Variant.CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
	Body->SetRelativeScale3D(FVector(Variant.Scale));

	Variant.Gear.Apply(this, Body);
	ApplySkin(Variant, Skin);
}

void AEnemy::ApplySkin(const FMonsterVariant& Variant, UMaterialInterface* Skin)
{
	// default material from the asset
	const TArray<FSkeletalMaterial>& BodyMaterials = Variant.Body->GetMaterials();
	UMaterialInterface* BodyMaterial = BodyMaterials.IsEmpty() ? nullptr : BodyMaterials[0].MaterialInterface.Get();
	if (!Skin || !BodyMaterial)
	{
		return;
	}

	// gear shares the material so skin every mesh
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(this);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		for (int32 SlotIndex = 0; SlotIndex < Mesh->GetNumMaterials(); ++SlotIndex)
		{
			if (Mesh->GetMaterial(SlotIndex) == BodyMaterial)
			{
				Mesh->SetMaterial(SlotIndex, Skin);
			}
		}
	}
}

void AEnemy::UpdateEyeGlow(float DeltaTime, float Threat)
{
	const float Target = FMath::Lerp(EyeGlowIdle, EyeGlowThreat, FMath::Clamp(Threat, 0.f, 1.f));
	SetEyeGlow(FMath::FInterpTo(EyeGlow, Target, DeltaTime, EyeGlowResponse));
}

void AEnemy::SetEyeGlow(float Glow)
{
	// skip the parameter write if the value hasn't changed
	if (FMath::IsNearlyEqual(Glow, EyeGlow, 0.01f))
	{
		return;
	}
	EyeGlow = Glow;
	for (UMaterialInstanceDynamic* Material : EyeGlowMaterials)
	{
		Material->SetScalarParameterValue(EyeGlowParameter, EyeGlow);
	}
}

void AEnemy::BeginPlay()
{
	Super::BeginPlay();

	// placed actors may predate the hero channel
	SlashHeroLighting::AddToHeroChannel(this);

	// placed enemies may predate the skins
	int32 Chosen = INDEX_NONE;
	UMaterialInterface* Skin = nullptr;
	if (PickVariant(Chosen, Skin) && GetMesh()->GetSkeletalMeshAsset() == Variants[Chosen].Body)
	{
		ApplySkin(Variants[Chosen], Skin);
	}

	// one dynamic material per material
	TMap<UMaterialInterface*, UMaterialInstanceDynamic*> Dynamic;
	TInlineComponentArray<USkeletalMeshComponent*> BodyMeshes(this);
	for (USkeletalMeshComponent* BodyMesh : BodyMeshes)
	{
		for (int32 SlotIndex = 0; SlotIndex < BodyMesh->GetNumMaterials(); ++SlotIndex)
		{
			UMaterialInterface* Material = BodyMesh->GetMaterial(SlotIndex);
			if (!Material)
			{
				continue;
			}
			UMaterialInstanceDynamic*& Instance = Dynamic.FindOrAdd(Material);
			if (!Instance)
			{
				Instance = UMaterialInstanceDynamic::Create(Material, this);
				EyeGlowMaterials.Add(Instance);
			}
			BodyMesh->SetMaterial(SlotIndex, Instance);
		}
	}
	EyeGlow = -1.f;
	SetEyeGlow(EyeGlowIdle);

	// hit reactions live in TakeDamage
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AEnemy::HandleDeath);
	}

	if (USlashEncounterDirector* Director = GetWorld()->GetSubsystem<USlashEncounterDirector>())
	{
		Director->RegisterEnemy(this);
	}
}

void AEnemy::StartAttack()
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Attacks.IsEmpty() || !AnimInstance || !HealthComponent || !HealthComponent->IsAlive())
	{
		if (BrainComponent)
		{
			BrainComponent->NotifyAttackFinished();
		}
		return;
	}

	ActiveAttackIndex = FMath::RandRange(0, Attacks.Num() - 1);
	const FZombieAttack& Attack = Attacks[ActiveAttackIndex];
	const float PlayRate = FMath::Max(Attack.PlayRate, 0.01f);

	UAnimMontage* Montage = Attack.Anim
		? AnimInstance->PlaySlotAnimationAsDynamicMontage(Attack.Anim, AttackSlot, 0.15f, 0.25f, PlayRate)
		: nullptr;
	if (!Montage)
	{
		ActiveAttackIndex = INDEX_NONE;
		if (BrainComponent)
		{
			BrainComponent->NotifyAttackFinished();
		}
		return;
	}

	ActiveAttackMontage = Montage;
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AEnemy::OnAttackMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);

	// raw clips have no notifies so the hit uses a timer
	GetWorldTimerManager().SetTimer(AttackHitTimerHandle, this, &AEnemy::LandAttack,
		FMath::Max(Attack.Anim->GetPlayLength() * Attack.HitFraction / PlayRate, 0.01f), false);

	if (Attack.LungeSpeed > 0.f)
	{
		LaunchCharacter(GetActorForwardVector() * Attack.LungeSpeed, true, false);
	}
}

void AEnemy::LandAttack()
{
	if (!Attacks.IsValidIndex(ActiveAttackIndex))
	{
		return;
	}
	const FZombieAttack& Attack = Attacks[ActiveAttackIndex];
	ApplyStrike(Attack.Reach, Attack.Radius, Attack.Damage);
}

int32 AEnemy::ApplyStrike(float Reach, float Radius, float Damage)
{
	const FVector Center = GetActorLocation() + GetActorForwardVector() * Reach;
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ZombieAttack), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius), Params);

	TSet<AActor*> Struck;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();

		// no friendly fire
		if (!Victim || Victim->IsA<AEnemy>() || Struck.Contains(Victim))
		{
			continue;
		}
		const UHealthComponent* VictimHealth = Victim->FindComponentByClass<UHealthComponent>();
		if (!VictimHealth || !VictimHealth->IsAlive())
		{
			continue;
		}
		Struck.Add(Victim);

		FHitResult Hit;
		Hit.ImpactPoint = Victim->GetActorLocation();
		Hit.Location = Hit.ImpactPoint;
		const FVector ShotDirection = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		UGameplayStatics::ApplyPointDamage(Victim, Damage, ShotDirection, Hit, GetController(), this, nullptr);
	}

	return Struck.Num();
}

void AEnemy::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveAttackMontage.Get())
	{
		return;
	}
	ActiveAttackMontage = nullptr;
	ActiveAttackIndex = INDEX_NONE;
	GetWorldTimerManager().ClearTimer(AttackHitTimerHandle);

	if (BrainComponent)
	{
		BrainComponent->NotifyAttackFinished();
	}
}

void AEnemy::CancelAttack()
{
	GetWorldTimerManager().ClearTimer(AttackHitTimerHandle);
	ActiveAttackIndex = INDEX_NONE;

	bLeapStrikePending = false;

	// clear before stopping so the end delegate is ignored
	UAnimMontage* Montage = ActiveAttackMontage.Get();
	ActiveAttackMontage = nullptr;
	if (UAnimInstance* AnimInstance = Montage && GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(0.1f, Montage);
	}
}

void AEnemy::LeapTo(const FVector& Destination)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const FVector Start = GetActorLocation();
	const FVector Delta(Destination.X - Start.X, Destination.Y - Start.Y, 0.f);
	const float Distance = Delta.Size();
	const float WorldGravity = FMath::Abs(GetWorld()->GetGravityZ());

	if (Distance < KINDA_SMALL_NUMBER || WorldGravity < KINDA_SMALL_NUMBER)
	{
		// nowhere to go so count it as landed
		if (BrainComponent)
		{
			BrainComponent->NotifyLanded();
		}
		return;
	}

	// flight time from speed then gravity to hit the apex, h = g*T^2/8 and vz = 4h/T
	const float FlightTime = FMath::Clamp(Distance / FMath::Max(LeapSpeed, 1.f), LeapMinFlightTime, LeapMaxFlightTime);
	const float Gravity = 8.f * LeapApexHeight / (FlightTime * FlightTime);

	if (!bLeaping)
	{
		PreLeapGravityScale = Movement->GravityScale;
	}
	bLeaping = true;
	Movement->GravityScale = Gravity / WorldGravity;

	const FVector LaunchVelocity = Delta / FlightTime + FVector::UpVector * (4.f * LeapApexHeight / FlightTime);
	LaunchCharacter(LaunchVelocity, true, true);

	// start the claw clip so it connects on landing
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (LeapAnim && AnimInstance)
	{
		const float StartAt = FMath::Max(LeapAnimContactTime - FlightTime, 0.f);
		if (UAnimMontage* Montage = AnimInstance->PlaySlotAnimationAsDynamicMontage(LeapAnim, AttackSlot, 0.08f, 0.25f,
			1.f, 1, -1.f, StartAt))
		{
			ActiveAttackIndex = INDEX_NONE;
			ActiveAttackMontage = Montage;
			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &AEnemy::OnAttackMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);
		}
	}
	bLeapStrikePending = true;

	UE_LOG(LogTemp, Log, TEXT("[Zombie] %s leaps %.0fcm in %.2fs"), *GetName(), Distance, FlightTime);
}

void AEnemy::EndLeap()
{
	if (!bLeaping)
	{
		return;
	}
	bLeaping = false;
	GetCharacterMovement()->GravityScale = PreLeapGravityScale;
}

void AEnemy::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	const bool bWasLeaping = bLeaping;
	EndLeap();

	// the landing is the hit
	if (bWasLeaping && bLeapStrikePending)
	{
		bLeapStrikePending = false;
		const int32 Struck = ApplyStrike(LeapReach, LeapRadius, LeapDamage);
		UE_LOG(LogTemp, Log, TEXT("[Zombie] %s pounce lands, %d struck"), *GetName(), Struck);
	}

	if (BrainComponent)
	{
		BrainComponent->NotifyLanded();
	}
}

void AEnemy::PlaySpawnRise()
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!SpawnAnim || !AnimInstance)
	{
		return;
	}

	AnimInstance->PlaySlotAnimationAsDynamicMontage(SpawnAnim, AttackSlot, 0.f, 0.25f);
	if (BrainComponent)
	{
		BrainComponent->NotifyRising(SpawnAnim->GetPlayLength());
	}
}

void AEnemy::HandleDeath(AActor* Killer)
{
	// untargetable first or the player locks onto the corpse
	if (TargetableComponent)
	{
		TargetableComponent->SetTargetable(false);
	}

	// capsule is the hurtbox so this stops more sword hits
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CancelAttack();
	if (BrainComponent)
	{
		BrainComponent->NotifyDied();
	}
	if (USlashEncounterDirector* Director = GetWorld()->GetSubsystem<USlashEncounterDirector>())
	{
		Director->UnregisterEnemy(this);
	}
	if (AController* AI = GetController())
	{
		AI->StopMovement();
	}
	EndLeap();
	GetCharacterMovement()->DisableMovement();

	SetEyeGlow(0.f);

	SlashCombatAnimation::PlayAndHoldLastFrame(this, DeathAnim, HitReactionSlot);

	if (ASlashGameMode* GameMode = GetWorld()->GetAuthGameMode<ASlashGameMode>())
	{
		GameMode->NotifyEnemyKilled(this);
	}

	SetLifeSpan(CorpseLifeSpan);
}

EEnemyHitZone AEnemy::ClassifyHitZone(const FVector& ImpactPoint) const
{
	// hit location in the enemy's local space
	const FVector Local = GetActorTransform().InverseTransformPosition(ImpactPoint);

	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();

	// 0 at the feet and 1 at the top, clamped
	const float Height = FMath::Clamp((Local.Z + HalfHeight) / FMath::Max(HalfHeight * 2.f, KINDA_SMALL_NUMBER), 0.f, 1.f);

	if (Height >= HeadZoneHeight)
	{
		return EEnemyHitZone::Head;
	}

	if (Height >= ChestZoneHeight)
	{
		// shoulders are chest-height hits that land off-centre
		const float Lateral = Local.Y / FMath::Max(Radius, KINDA_SMALL_NUMBER);
		if (FMath::Abs(Lateral) >= ShoulderLateralFraction)
		{
			return Lateral > 0.f ? EEnemyHitZone::ShoulderRight : EEnemyHitZone::ShoulderLeft;
		}

		return EEnemyHitZone::Chest;
	}

	// legs count as stomach
	return EEnemyHitZone::Stomach;
}

void AEnemy::PlayHitReaction(EEnemyHitZone Zone)
{
	const TObjectPtr<UAnimSequenceBase>* Anim = HitReactionAnims.Find(Zone);
	if (!Anim || !*Anim)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	// dynamic montage in the slot, a new hit restarts it
	AnimInstance->PlaySlotAnimationAsDynamicMontage(*Anim, HitReactionSlot,
		HitReactionBlendIn, HitReactionBlendOut, HitReactionPlayRate);
}

void AEnemy::EndHitFlash()
{
	SetBodyOverlayMaterial(nullptr);
}

void AEnemy::SetBodyOverlayMaterial(UMaterialInterface* Material)
{
	TInlineComponentArray<USkeletalMeshComponent*> BodyMeshes(this);
	for (USkeletalMeshComponent* BodyMesh : BodyMeshes)
	{
		BodyMesh->SetOverlayMaterial(Material);
	}
}

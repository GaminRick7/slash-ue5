// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlashCharacter.h"
#include "Character/HeroLighting.h"
#include "Components/PointLightComponent.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/RootMotionSource.h"
#include "HAL/IConsoleManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/SlashCameraRigComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Combat/CombatAnimation.h"
#include "Combat/ComboComponent.h"
#include "Combat/HealthComponent.h"
#include "Framework/SlashGameMode.h"
#include "UObject/ConstructorHelpers.h"
#include "Combat/MeleeWeaponComponent.h"
#include "Combat/ComboTreeAsset.h"
#include "Enemy/TargetableComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Targeting/SlashTargetingComponent.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld DodgeCommand(
	TEXT("slash.Dodge"),
	TEXT("Makes the player dodge as if the dodge button were pressed. For testing without a controller."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (ASlashCharacter* Player = Cast<ASlashCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)))
		{
			Player->TryDodge();
		}
	}));
#endif

ASlashCharacter::ASlashCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->RotationRate = FRotator(0.f, 500.f, 0.f);
	Movement->JumpZVelocity = 680.f;
	Movement->AirControl = 0.4f;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MinAnalogWalkSpeed = 20.f;
	Movement->MaxAcceleration = 2400.f;

	// separate braking friction to accelerate fast and still stop short
	Movement->bUseSeparateBrakingFriction = true;
	Movement->BrakingFriction = 6.f;
	Movement->GroundFriction = 8.f;
	Movement->BrakingDecelerationWalking = 2000.f;

	// extra air control only when nearly still so a standing jump can steer
	Movement->AirControlBoostMultiplier = 2.f;
	Movement->AirControlBoostVelocityThreshold = 25.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	CameraRig = CreateDefaultSubobject<USlashCameraRigComponent>(TEXT("CameraRig"));

	HeroLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HeroLight"));
	HeroLight->SetupAttachment(FollowCamera);
	HeroLight->SetRelativeLocation(FVector(40.f, 60.f, 70.f));
	HeroLight->SetIntensityUnits(ELightUnits::Candelas);
	HeroLight->SetIntensity(75.f);
	HeroLight->SetAttenuationRadius(1400.f);
	// cool white against the warm lanterns
	HeroLight->SetLightColor(FLinearColor(0.82f, 0.88f, 1.f));
	HeroLight->SetSourceRadius(40.f);
	HeroLight->SetSpecularScale(0.5f);
	HeroLight->SetCastShadows(false);
	HeroLight->bAffectTranslucentLighting = false;
	HeroLight->LightingChannels.bChannel0 = false;
	HeroLight->LightingChannels.bChannel1 = true;

	// socket name is hardcoded because the attachment is baked into the CDO
	SwordComponent = CreateDefaultSubobject<UMeleeWeaponComponent>(TEXT("SwordComponent"));
	SwordComponent->SetupAttachment(GetMesh(), TEXT("RightHandSocket"));
	SwordComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SwordComponent->SetGenerateOverlapEvents(false);

	// SM_Sword imports ~40 m tall so scale to a ~110 cm blade and lift the grip into the hand
	SwordComponent->SetRelativeScale3D(FVector(0.027f));
	SwordComponent->SetRelativeLocation(FVector(0.f, 0.f, 9.2f));

	ComboComponent = CreateDefaultSubobject<UComboComponent>(TEXT("ComboComponent"));
	TargetingComponent = CreateDefaultSubobject<USlashTargetingComponent>(TEXT("TargetingComponent"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> HitAnimAsset(TEXT("/Game/Animations/UAL1_Standard/SkeletalMeshes/UAL1_StandardHit_Chest.UAL1_StandardHit_Chest"));
	if (HitAnimAsset.Succeeded())
	{
		HitReactionAnim = HitAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> DodgeAnimAsset(TEXT("/Game/Animations/UAL1_Standard/SkeletalMeshes/UAL1_StandardRoll.UAL1_StandardRoll"));
	if (DodgeAnimAsset.Succeeded())
	{
		DodgeAnim = DodgeAnimAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> DodgeActionAsset(TEXT("/Game/Input/IA_Dodge.IA_Dodge"));
	if (DodgeActionAsset.Succeeded())
	{
		DodgeAction = DodgeActionAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> DeathAnimAsset(TEXT("/Game/Animations/UAL1_Standard/SkeletalMeshes/UAL1_StandardDeath01.UAL1_StandardDeath01"));
	if (DeathAnimAsset.Succeeded())
	{
		DeathAnim = DeathAnimAsset.Object;
	}
}

void ASlashCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	Outfit.Apply(this, GetMesh());
	SlashHeroLighting::AddToHeroChannel(this);
}

void ASlashCharacter::BeginPlay()
{
	Super::BeginPlay();

	// placed actors may have components saved before the hero channel existed
	SlashHeroLighting::AddToHeroChannel(this);

	if (ComboComponent)
	{
		ComboComponent->OnComboNodeEntered.AddDynamic(this, &ASlashCharacter::HandleComboNodeEntered);
		ComboComponent->OnAttackWindowOpened.AddDynamic(this, &ASlashCharacter::HandleAttackWindowOpened);
		ComboComponent->OnAttackWindowClosed.AddDynamic(this, &ASlashCharacter::HandleAttackWindowClosed);
		ComboComponent->OnComboEnded.AddDynamic(this, &ASlashCharacter::HandleComboEnded);
	}

	if (SwordComponent)
	{
		SwordComponent->OnWeaponHit.AddDynamic(this, &ASlashCharacter::HandleWeaponHit);
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &ASlashCharacter::HandleDeath);
	}

	if (CameraRig)
	{
		CameraRig->InitializeRig(CameraBoom, FollowCamera);
	}

	BaseGravityScale = GetCharacterMovement()->GravityScale;
	PreviousYaw = GetActorRotation().Yaw;

	DodgeSpeedCurve = NewObject<UCurveFloat>(this);
	DodgeSpeedCurve->FloatCurve.AddKey(0.f, 1.35f);
	DodgeSpeedCurve->FloatCurve.AddKey(0.55f, 1.f);
	DodgeSpeedCurve->FloatCurve.AddKey(1.f, 0.2f);
	constexpr int32 Samples = 32;
	float Sum = 0.f;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		Sum += DodgeSpeedCurve->GetFloatValue((Index + 0.5f) / Samples);
	}
	DodgeCurveMean = FMath::Max(Sum / Samples, 0.05f);
}

void ASlashCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateSprintState(DeltaSeconds);
	UpdateFacing(DeltaSeconds);
	UpdateAirState(DeltaSeconds);
	UpdateLeap(DeltaSeconds);
	UpdateDodge(DeltaSeconds);

	// lean from the real turn rate scaled by speed
	const float CurrentYaw = static_cast<float>(GetActorRotation().Yaw);
	const float YawDelta = FMath::FindDeltaAngleDegrees(PreviousYaw, CurrentYaw);
	PreviousYaw = CurrentYaw;

	float TargetLean = 0.f;
	if (DeltaSeconds > 0.f && LeanYawRateScale > 0.f)
	{
		const float GroundSpeed = static_cast<float>(GetVelocity().Size2D());
		const float SpeedRatio = FMath::Clamp(GroundSpeed / FMath::Max(WalkSpeed, 1.f), 0.f, 1.f);
		TargetLean = FMath::Clamp((YawDelta / DeltaSeconds) / LeanYawRateScale, -1.f, 1.f) * SpeedRatio;
	}
	LeanAmount = FMath::FInterpTo(LeanAmount, TargetLean, DeltaSeconds, LeanInterpSpeed);

	if (CameraRig)
	{
		FSlashCameraRigInput RigInput;
		RigInput.SprintAlpha = SprintAlpha;
		RigInput.bInCombat = bIsInCombo;
		RigInput.bInAir = GetCharacterMovement()->IsFalling();
		CameraRig->UpdateRig(DeltaSeconds, RigInput);
	}
}

void ASlashCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASlashCharacter::Move);
		}
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASlashCharacter::Look);
		}
		if (SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &ASlashCharacter::StartSprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &ASlashCharacter::StopSprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Canceled, this, &ASlashCharacter::StopSprint);
		}
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ASlashCharacter::StartJump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ASlashCharacter::StopJump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ASlashCharacter::StopJump);
		}
		if (AttackAction)
		{
			EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &ASlashCharacter::RequestAttack);
		}
		if (AttackHoldAction)
		{
			EnhancedInputComponent->BindAction(AttackHoldAction, ETriggerEvent::Triggered, this, &ASlashCharacter::RequestAttack);
		}
		if (AttackHeavyAction)
		{
			EnhancedInputComponent->BindAction(AttackHeavyAction, ETriggerEvent::Triggered, this, &ASlashCharacter::RequestAttack);
		}
		if (DodgeAction)
		{
			EnhancedInputComponent->BindAction(DodgeAction, ETriggerEvent::Started, this, &ASlashCharacter::RequestDodge);
		}
	}
}

void ASlashCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D MovementVector = Value.Get<FVector2D>();

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// stored before the combo check since attacks still steer by the stick
	CachedMoveDirection = (ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X).GetSafeNormal();
	MoveInputFrame = GFrameCounter;

	// stick input during recovery cancels the rest of the attack, deadzone keeps drift from cancelling
	if (bIsInCombo && ComboComponent && ComboComponent->IsInRecovery()
		&& MovementVector.SizeSquared() > FMath::Square(RecoveryCancelInputThreshold))
	{
		// CancelCombo clears bIsInCombo right away so this input moves the character
		ComboComponent->CancelCombo(ComboCancelBlendTime);
	}

	// no movement during attacks or rolls, facing can still turn
	if (bIsInCombo || bIsDodging)
	{
		return;
	}

	if (bIsSprinting)
	{
		AddMovementInput(ForwardDirection, FMath::Max(MovementVector.Y, 0.f));
	}
	else
	{
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X * StrafeSpeedMultiplier);
	}
}

void ASlashCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller)
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void ASlashCharacter::StartSprint(const FInputActionValue& Value)
{
	bWantsToSprint = true;
}

void ASlashCharacter::StopSprint(const FInputActionValue& Value)
{
	bWantsToSprint = false;
}

void ASlashCharacter::StartJump(const FInputActionValue& Value)
{
	if (bIsInCombo || bIsDodging)
	{
		return;
	}

	if (CanJump())
	{
		Jump();
		return;
	}

	// too early so buffer it for landing
	JumpBufferRemaining = JumpBufferTime;
}

void ASlashCharacter::StopJump(const FInputActionValue& Value)
{
	StopJumping();

	// releasing jump while rising cuts the jump short
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement->IsFalling() && Movement->Velocity.Z > 0.f)
	{
		Movement->Velocity.Z *= JumpCutMultiplier;
	}
}

bool ASlashCharacter::CanJumpWhileFalling() const
{
	// coyote time for the first jump after walking off a ledge
	return !bRootMotionMovementActive
		&& JumpCurrentCount == 0
		&& TimeSinceLeftGround <= CoyoteTime
		&& GetCharacterMovement()->Velocity.Z <= 0.f;
}

void ASlashCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	const float ImpactSpeed = PeakFallSpeed;
	PeakFallSpeed = 0.f;
	TimeSinceLeftGround = 0.f;
	GetCharacterMovement()->GravityScale = BaseGravityScale;

	if (CameraRig && ImpactSpeed > LandShakeMinSpeed)
	{
		const float Strength = FMath::Clamp(
			(ImpactSpeed - LandShakeMinSpeed) / FMath::Max(LandShakeMaxSpeed - LandShakeMinSpeed, 1.f), 0.f, 1.f);

		CameraRig->PlayLandShake(Strength);
		CameraRig->AddPitchDip(LandMaxPitchDip * Strength);
	}
}

void ASlashCharacter::CheckJumpInput(float DeltaTime)
{
	// buffered jump spent here since Landed runs inside PerformMovement and gets cleared
	if (JumpBufferRemaining > 0.f && !bPressedJump && !bIsInCombo && !bIsDodging && CanJump())
	{
		JumpBufferRemaining = 0.f;
		Jump();
	}

	Super::CheckJumpInput(DeltaTime);
}

void ASlashCharacter::UpdateSprintState(float DeltaSeconds)
{
	bool bMovingForward = false;
	if (Controller && (GFrameCounter - MoveInputFrame) <= 1 && !CachedMoveDirection.IsNearlyZero())
	{
		const FRotator ControlYaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector ControlForward = FRotationMatrix(ControlYaw).GetUnitAxis(EAxis::X);
		bMovingForward = FVector::DotProduct(CachedMoveDirection, ControlForward) > SprintForwardDotThreshold;
	}

	// attacking cancels sprint
	bIsSprinting = bWantsToSprint && bMovingForward && !bIsInCombo && !bIsDodging;

	SprintAlpha = FMath::FInterpTo(SprintAlpha, bIsSprinting ? 1.f : 0.f, DeltaSeconds, SprintSpeedInterpSpeed);
	if (SprintAlpha < 0.001f)
	{
		SprintAlpha = 0.f;
	}

	GetCharacterMovement()->MaxWalkSpeed = FMath::Lerp(WalkSpeed, SprintSpeed, SprintAlpha);
}

void ASlashCharacter::UpdateFacing(float DeltaSeconds)
{
	if (!Controller || bIsDodging)
	{
		return;
	}

	const bool bHasFreshInput = (GFrameCounter - MoveInputFrame) <= 1 && !CachedMoveDirection.IsNearlyZero();
	if (!bHasFreshInput)
	{
		return;
	}

	// face the camera out of combat and the stick during a combo
	const FRotator TargetRotation = bIsInCombo
		? FRotator(0.f, CachedMoveDirection.Rotation().Yaw, 0.f)
		: FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f);

	// turn slower at higher speed
	float InterpSpeed = FMath::Lerp(TurnInterpSpeed, SprintTurnInterpSpeed, SprintAlpha);
	if (bIsInCombo)
	{
		InterpSpeed = CombatTurnInterpSpeed;
	}

	SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaSeconds, InterpSpeed));
}

void ASlashCharacter::UpdateAirState(float DeltaSeconds)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (Movement->IsMovingOnGround())
	{
		TimeSinceLeftGround = 0.f;
	}
	else
	{
		TimeSinceLeftGround += DeltaSeconds;
	}

	if (Movement->IsFalling())
	{
		PeakFallSpeed = FMath::Max(PeakFallSpeed, static_cast<float>(-Movement->Velocity.Z));
	}

	// heavier gravity on the way down except in root motion combos
	const bool bFastFalling = Movement->IsFalling() && Movement->Velocity.Z < 0.f && !bRootMotionMovementActive;
	Movement->GravityScale = bFastFalling ? FallGravityScale : BaseGravityScale;

	JumpBufferRemaining = FMath::Max(JumpBufferRemaining - DeltaSeconds, 0.f);
}

void ASlashCharacter::RequestAttack(const FInputActionInstance& Instance)
{
	const UInputAction* Action = Instance.GetSourceAction();

	// mid roll attack now if late enough otherwise queue it
	if (bIsDodging)
	{
		if (DodgeElapsed < DodgeAttackCancelTime)
		{
			QueuedAttackAction = Action;
			return;
		}
		EndDodge(/*bInterrupted=*/true);
	}

	if (ComboComponent)
	{
		ComboComponent->RequestAttack(const_cast<UInputAction*>(Action));
	}
}

void ASlashCharacter::RequestDodge(const FInputActionValue& Value)
{
	TryDodge();
}

void ASlashCharacter::TryDodge()
{
	if (CanDodge())
	{
		StartDodge();
		return;
	}

	// can't roll yet so buffer the press
	DodgeBufferRemaining = DodgeBufferTime;
}

bool ASlashCharacter::CanDodge() const
{
	// can roll out of attacks, flinches and leaps but not another roll, ground only
	return !bIsDead && !bIsDodging && !bRootMotionMovementActive && GetCharacterMovement()->IsMovingOnGround();
}

void ASlashCharacter::StartDodge()
{
	DodgeBufferRemaining = 0.f;
	QueuedAttackAction = nullptr;

	if (bIsInCombo && ComboComponent)
	{
		ComboComponent->CancelCombo(0.05f);
	}
	if (SwordComponent)
	{
		SwordComponent->EndWindow();
	}
	EndLeap();

	// roll toward the stick or forward, snap to face it
	const bool bHasFreshInput = (GFrameCounter - MoveInputFrame) <= 1 && !CachedMoveDirection.IsNearlyZero();
	DodgeDirection = (bHasFreshInput ? CachedMoveDirection : GetActorForwardVector()).GetSafeNormal2D();
	if (DodgeDirection.IsNearlyZero())
	{
		DodgeDirection = GetActorForwardVector();
	}
	SetActorRotation(FRotator(0.f, DodgeDirection.Rotation().Yaw, 0.f));

	bIsDodging = true;
	DodgeElapsed = 0.f;

	// root motion source keeps normal collision and gravity
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	TSharedPtr<FRootMotionSource_ConstantForce> Roll = MakeShared<FRootMotionSource_ConstantForce>();
	Roll->InstanceName = TEXT("Dodge");
	Roll->AccumulateMode = ERootMotionAccumulateMode::Override;
	Roll->Priority = 10;
	Roll->Force = DodgeDirection * (DodgeDistance / (FMath::Max(DodgeDuration, 0.05f) * DodgeCurveMean));
	Roll->Duration = DodgeDuration;
	Roll->StrengthOverTime = DodgeSpeedCurve;
	Roll->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Roll->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Roll->FinishVelocityParams.SetVelocity = DodgeDirection * DodgeExitSpeed;
	DodgeRootMotionID = Movement->ApplyRootMotionSource(Roll);

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr; AnimInstance && DodgeAnim)
	{
		DodgeMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(DodgeAnim, ReactionSlot, 0.06f, 0.2f, DodgeAnimPlayRate);
	}
}

void ASlashCharacter::UpdateDodge(float DeltaSeconds)
{
	if (!bIsDodging)
	{
		DodgeBufferRemaining = FMath::Max(DodgeBufferRemaining - DeltaSeconds, 0.f);
		if (DodgeBufferRemaining > 0.f && CanDodge())
		{
			StartDodge();
		}
		return;
	}

	DodgeElapsed += DeltaSeconds;

	if (QueuedAttackAction && DodgeElapsed >= DodgeAttackCancelTime)
	{
		const UInputAction* Action = QueuedAttackAction;
		QueuedAttackAction = nullptr;
		EndDodge(/*bInterrupted=*/true);
		if (ComboComponent)
		{
			ComboComponent->RequestAttack(const_cast<UInputAction*>(Action));
		}
		return;
	}

	if (DodgeElapsed >= DodgeDuration)
	{
		EndDodge(/*bInterrupted=*/false);
	}
}

void ASlashCharacter::EndDodge(bool bInterrupted)
{
	if (!bIsDodging)
	{
		return;
	}
	bIsDodging = false;

	// source already gone if the roll finished, remove it if cut short
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bInterrupted && DodgeRootMotionID != 0)
	{
		Movement->RemoveRootMotionSourceByID(DodgeRootMotionID);
	}
	DodgeRootMotionID = 0;

	// blend out instead of playing the slow stand up
	if (!bInterrupted)
	{
		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr; AnimInstance && DodgeMontage.IsValid())
		{
			AnimInstance->Montage_Stop(0.25f, DodgeMontage.Get());
		}
	}
	DodgeMontage = nullptr;
}

bool ASlashCharacter::IsDodgeInvulnerable() const
{
	return bIsDodging && DodgeElapsed >= DodgeInvulnerableStart && DodgeElapsed <= DodgeInvulnerableEnd;
}

void ASlashCharacter::HandleComboNodeEntered(FName NodeId, bool bIsComboStart)
{
	bIsInCombo = true;

	// early chaining can skip the AttackHit end so close the window here
	if (SwordComponent)
	{
		SwordComponent->EndWindow();
	}

	const FComboNode* Node = ComboComponent->GetCurrentNode();
	if (!Node)
	{
		return;
	}

	// end any running leap first so the movement mode is right
	EndLeap();

	// FOV kick on the wind-up
	if (CameraRig)
	{
		CameraRig->AddFOVPunch(AttackFOVPunch * Node->CameraShakeScale);
	}

	SetRootMotionMovementActive(Node->bRootMotionMovement);

	if (Node->bRootMotionMovement)
	{
		return;
	}

	// leap at the target on every node, min distance stops it once in range
	if (LeapTowardTarget())
	{
		return;
	}

	if (Node->DashSpeed != 0.f)
	{
		LaunchCharacter(GetActorForwardVector() * Node->DashSpeed, true, false);
	}
}

void ASlashCharacter::HandleAttackWindowOpened(FName NodeId)
{
	if (SwordComponent)
	{
		SwordComponent->BeginWindow();
	}
}

void ASlashCharacter::HandleWeaponHit(AActor* HitActor, const FHitResult& Hit, int32 HitIndex)
{
	// CameraShakeScale drives both the FOV kick and the impact shake for now
	const FComboNode* Node = ComboComponent ? ComboComponent->GetCurrentNode() : nullptr;
	const float Scale = Node ? Node->CameraShakeScale : 1.f;

	if (CameraRig)
	{
		CameraRig->PlayImpactShake(Scale);
	}

	// hit stop on the first target only
	if (HitIndex == 0)
	{
		ApplyHitStop(HitStopDuration * Scale, HitStopTimeDilation);
	}

	// point damage so the enemy knows where it was hit
	const FVector ShotDirection = (Hit.ImpactPoint - GetActorLocation()).GetSafeNormal();
	UGameplayStatics::ApplyPointDamage(HitActor, AttackDamage, ShotDirection, Hit, GetController(), this, nullptr);
}

void ASlashCharacter::HandleAttackWindowClosed(FName NodeId)
{
	if (SwordComponent)
	{
		SwordComponent->EndWindow();
	}
}

void ASlashCharacter::HandleComboEnded(FName NodeId, bool bWasInterrupted)
{
	bIsInCombo = false;
	SetRootMotionMovementActive(false);

	// an interrupted montage doesn't send the notify end
	if (SwordComponent)
	{
		SwordComponent->EndWindow();
	}

	// combo can be cancelled mid leap so finish the leap
	if (bIsLeaping)
	{
		bIsLeaping = false;
		SetActorLocation(LeapEndLocation, true);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
}

float ASlashCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// hit invulnerability, skip Super so health never sees it
	const double Now = GetWorld()->GetTimeSeconds();
	if (bIsDead || Now - LastDamageTakenTime < HitInvulnerabilityTime)
	{
		return 0.f;
	}

	if (IsDodgeInvulnerable())
	{
		UE_LOG(LogTemp, Log, TEXT("[Dodge] evaded %s at %.2fs into the roll"), *GetNameSafe(DamageCauser), DodgeElapsed);
		return 0.f;
	}

	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage <= 0.f || bIsDead)
	{
		return ActualDamage;
	}
	LastDamageTakenTime = Now;

	// getting hit cancels the current attack
	if (ComboComponent)
	{
		ComboComponent->CancelCombo(0.05f);
	}
	EndLeap();
	EndDodge(/*bInterrupted=*/true);

	FVector Away = DamageCauser ? GetActorLocation() - DamageCauser->GetActorLocation() : -GetActorForwardVector();
	Away.Z = 0.f;
	LaunchCharacter(Away.GetSafeNormal() * HitKnockbackSpeed + FVector::UpVector * 120.f, true, true);

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr; AnimInstance && HitReactionAnim)
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(HitReactionAnim, ReactionSlot, 0.05f, 0.2f, 1.3f);
	}

	if (CameraRig)
	{
		CameraRig->PlayImpactShake(1.5f);
	}

	return ActualDamage;
}

void ASlashCharacter::HandleDeath(AActor* Killer)
{
	bIsDead = true;

	// the director stops enemies attacking once the player can't be damaged
	SetCanBeDamaged(false);

	if (ComboComponent)
	{
		ComboComponent->CancelCombo();
	}
	if (SwordComponent)
	{
		SwordComponent->EndWindow();
	}
	EndLeap();
	EndDodge(/*bInterrupted=*/true);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	SlashCombatAnimation::PlayAndHoldLastFrame(this, DeathAnim, ReactionSlot);

	if (ASlashGameMode* GameMode = GetWorld()->GetAuthGameMode<ASlashGameMode>())
	{
		GameMode->NotifyPlayerDied();
	}
}

void ASlashCharacter::ApplyHitStop(float Duration, float TimeDilation)
{
	if (Duration <= 0.f)
	{
		return;
	}

	// slow only this character, timer runs on real time
	CustomTimeDilation = FMath::Clamp(TimeDilation, 0.01f, 1.f);
	GetWorldTimerManager().SetTimer(HitStopTimerHandle, this, &ASlashCharacter::EndHitStop, Duration, false);
}

void ASlashCharacter::EndHitStop()
{
	CustomTimeDilation = 1.f;
}

bool ASlashCharacter::LeapTowardTarget()
{
	const UTargetableComponent* Target = TargetingComponent ? TargetingComponent->GetPersistentTarget() : nullptr;
	if (!Target)
	{
		return false;
	}

	const FVector Origin = GetActorLocation();
	FVector ToTarget = Target->GetTargetPoint() - Origin;
	ToTarget.Z = 0.f;
	

	const float Distance = ToTarget.Size();
	if (Distance < AttackLeapMinDistance)
	{
		return false;
	}

	const FVector Direction = ToTarget / Distance;
	const float TravelDistance = FMath::Max(Distance - AttackLeapStopDistance, 0.f);

	if (TravelDistance <= KINDA_SMALL_NUMBER)
	{
		// already close enough, true so the dash doesn't overshoot
		return true;
	}

	bIsLeaping = true;
	LeapStartLocation = Origin;
	LeapEndLocation = Origin + Direction * TravelDistance;
	LeapStartYaw = GetActorRotation().Yaw;
	LeapTargetYaw = Direction.Rotation().Yaw;
	LeapElapsed = 0.f;
	LeapDuration = FMath::Clamp(TravelDistance / FMath::Max(AttackLeapSpeed, 1.f), AttackLeapMinDuration, AttackLeapMaxDuration);
	
	SetActorRotation(FRotator(0.0f,LeapTargetYaw, 0.0f));

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_None);

	return true;
}

void ASlashCharacter::UpdateLeap(float DeltaSeconds)
{
	if (!bIsLeaping)
	{
		return;
	}

	LeapElapsed += DeltaSeconds;
	const float Alpha = LeapDuration > 0.f ? FMath::Clamp(LeapElapsed / LeapDuration, 0.f, 1.f) : 1.f;

	SetActorLocation(FMath::Lerp(LeapStartLocation, LeapEndLocation, Alpha), true);

	if (Alpha >= 1.f)
	{
		EndLeap();
	}
}

void ASlashCharacter::EndLeap()
{
	if (!bIsLeaping)
	{
		return;
	}

	// back to walking first since MOVE_None drops pending launches
	bIsLeaping = false;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

void ASlashCharacter::SetRootMotionMovementActive(bool bActive)
{
	if (bActive == bRootMotionMovementActive)
	{
		return;
	}

	bRootMotionMovementActive = bActive;

	// only flying keeps vertical root motion, back to falling after
	GetCharacterMovement()->SetMovementMode(bActive ? MOVE_Flying : MOVE_Falling);
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlashCameraRigComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "SlashCameraShake.h"

USlashCameraRigComponent::USlashCameraRigComponent()
{
	// updated from the owner's Tick instead
	PrimaryComponentTick.bCanEverTick = false;

	// default over-the-shoulder view
	DefaultProfile.ArmLength = 300.f;
	DefaultProfile.SocketOffset = FVector(0.f, 55.f, 55.f);
	DefaultProfile.FieldOfView = 85.f;
	DefaultProfile.CameraLagSpeed = 15.f;
	DefaultProfile.RotationLagSpeed = 25.f;
	DefaultProfile.BlendSpeed = 6.f;

	// sprint pulls back and widens toward centre
	SprintProfile.ArmLength = 375.f;
	SprintProfile.SocketOffset = FVector(0.f, 32.f, 62.f);
	SprintProfile.FieldOfView = 96.f;
	SprintProfile.CameraLagSpeed = 9.f;
	SprintProfile.RotationLagSpeed = 20.f;
	SprintProfile.BlendSpeed = 4.f;

	// combat moves in closer with a narrower fov
	CombatProfile.ArmLength = 255.f;
	CombatProfile.SocketOffset = FVector(0.f, 62.f, 48.f);
	CombatProfile.FieldOfView = 78.f;
	CombatProfile.CameraLagSpeed = 20.f;
	CombatProfile.RotationLagSpeed = 30.f;
	CombatProfile.BlendSpeed = 9.f;

	// airborne drops lower so the ground stays in view
	AirProfile.ArmLength = 345.f;
	AirProfile.SocketOffset = FVector(0.f, 48.f, 30.f);
	AirProfile.FieldOfView = 88.f;
	AirProfile.CameraLagSpeed = 12.f;
	AirProfile.RotationLagSpeed = 22.f;
	AirProfile.BlendSpeed = 5.f;

	SprintShakeClass = USlashSprintCameraShake::StaticClass();
	ImpactShakeClass = USlashImpactCameraShake::StaticClass();
	LandShakeClass = USlashLandCameraShake::StaticClass();
}

void USlashCameraRigComponent::InitializeRig(USpringArmComponent* InCameraBoom, UCameraComponent* InFollowCamera)
{
	CameraBoom = InCameraBoom;
	FollowCamera = InFollowCamera;

	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->bUseCameraLagSubstepping = true;
	CameraBoom->CameraLagMaxTimeStep = 1.f / 60.f;
	CameraBoom->CameraLagMaxDistance = CameraLagMaxDistance;
	CameraBoom->bEnableCameraRotationLag = bEnableRotationLag;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = CameraProbeSize;

	FollowCamera->bUsePawnControlRotation = false;

	// snap to the default profile on the first frame
	ApplyProfile(DefaultProfile);
}

FSlashCameraProfile USlashCameraRigComponent::BlendProfiles(const FSlashCameraProfile& From, const FSlashCameraProfile& To, float Alpha)
{
	if (Alpha <= 0.f)
	{
		return From;
	}

	FSlashCameraProfile Result;
	Result.ArmLength = FMath::Lerp(From.ArmLength, To.ArmLength, Alpha);
	Result.SocketOffset = FMath::Lerp(From.SocketOffset, To.SocketOffset, Alpha);
	Result.TargetOffset = FMath::Lerp(From.TargetOffset, To.TargetOffset, Alpha);
	Result.FieldOfView = FMath::Lerp(From.FieldOfView, To.FieldOfView, Alpha);
	Result.CameraLagSpeed = FMath::Lerp(From.CameraLagSpeed, To.CameraLagSpeed, Alpha);
	Result.RotationLagSpeed = FMath::Lerp(From.RotationLagSpeed, To.RotationLagSpeed, Alpha);
	Result.BlendSpeed = FMath::Lerp(From.BlendSpeed, To.BlendSpeed, Alpha);
	return Result;
}

void USlashCameraRigComponent::UpdateRig(float DeltaSeconds, const FSlashCameraRigInput& Input)
{
	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	AirAlpha = FMath::FInterpTo(AirAlpha, Input.bInAir ? 1.f : 0.f, DeltaSeconds, AirProfile.BlendSpeed);
	CombatAlpha = FMath::FInterpTo(CombatAlpha, Input.bInCombat ? 1.f : 0.f, DeltaSeconds, CombatProfile.BlendSpeed);

	// profiles layer instead of switching, combat last so it wins over sprint
	FSlashCameraProfile Target = DefaultProfile;
	Target = BlendProfiles(Target, SprintProfile, FMath::Clamp(Input.SprintAlpha, 0.f, 1.f));
	Target = BlendProfiles(Target, AirProfile, AirAlpha);
	Target = BlendProfiles(Target, CombatProfile, CombatAlpha);

	// weights already eased so no extra smoothing

	FOVPunch = FMath::FInterpTo(FOVPunch, 0.f, DeltaSeconds, FOVPunchRecoverySpeed);

	// damped spring so a landing dip overshoots a little
	const float DipAcceleration = (-PitchDipStiffness * PitchDip) - (PitchDipDamping * PitchDipVelocity);
	PitchDipVelocity += DipAcceleration * DeltaSeconds;
	PitchDip += PitchDipVelocity * DeltaSeconds;

	ApplyProfile(Target);
	UpdateSprintShake(Input.SprintAlpha);
}

void USlashCameraRigComponent::ApplyProfile(const FSlashCameraProfile& Profile)
{
	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	CameraBoom->TargetArmLength = Profile.ArmLength;
	CameraBoom->SocketOffset = Profile.SocketOffset;
	CameraBoom->TargetOffset = Profile.TargetOffset;
	CameraBoom->CameraLagSpeed = Profile.CameraLagSpeed;
	CameraBoom->CameraRotationLagSpeed = Profile.RotationLagSpeed;

	FollowCamera->SetFieldOfView(Profile.FieldOfView + FOVPunch);
	FollowCamera->SetRelativeRotation(FRotator(-PitchDip, 0.f, 0.f));
}

void USlashCameraRigComponent::UpdateSprintShake(float SprintAlpha)
{
	APlayerCameraManager* CameraManager = GetCameraManager();
	if (!CameraManager || !SprintShakeClass)
	{
		return;
	}

	if (SprintAlpha > KINDA_SMALL_NUMBER)
	{
		if (!SprintShakeInstance.IsValid())
		{
			SprintShakeInstance = CameraManager->StartCameraShake(SprintShakeClass, 1.f);
		}

		// shake strength follows the sprint amount
		if (UCameraShakeBase* Shake = SprintShakeInstance.Get())
		{
			Shake->ShakeScale = SprintAlpha;
		}
	}
	else if (SprintShakeInstance.IsValid())
	{
		CameraManager->StopCameraShake(SprintShakeInstance.Get(), false);
		SprintShakeInstance.Reset();
	}
}

void USlashCameraRigComponent::PlayImpactShake(float Scale)
{
	if (APlayerCameraManager* CameraManager = GetCameraManager())
	{
		if (ImpactShakeClass)
		{
			CameraManager->StartCameraShake(ImpactShakeClass, Scale);
		}
	}
}

void USlashCameraRigComponent::PlayLandShake(float Scale)
{
	if (APlayerCameraManager* CameraManager = GetCameraManager())
	{
		if (LandShakeClass)
		{
			CameraManager->StartCameraShake(LandShakeClass, Scale);
		}
	}
}

void USlashCameraRigComponent::AddFOVPunch(float Degrees)
{
	FOVPunch += Degrees;
}

void USlashCameraRigComponent::AddPitchDip(float Degrees)
{
	// added to velocity so it bounces instead of jumping
	PitchDipVelocity += Degrees;
}

APlayerCameraManager* USlashCameraRigComponent::GetCameraManager() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	return PlayerController ? PlayerController->PlayerCameraManager : nullptr;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SlashCameraRigComponent.generated.h"

class APlayerCameraManager;
class UCameraComponent;
class UCameraShakeBase;
class USpringArmComponent;

// one camera setup, blended as a whole
USTRUCT(BlueprintType)
struct FSlashCameraProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	float ArmLength = 320.f;

	// boom space offset, y over the shoulder, z up
	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	FVector SocketOffset = FVector(0.f, 55.f, 55.f);

	// world-space offset applied to the boom origin
	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	FVector TargetOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	float FieldOfView = 85.f;

	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	float CameraLagSpeed = 15.f;

	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	float RotationLagSpeed = 25.f;

	// how fast this profile blends in and out
	UPROPERTY(EditAnywhere, Category = "Camera Profile")
	float BlendSpeed = 6.f;
};

// state passed in by the owner each frame
USTRUCT(BlueprintType)
struct FSlashCameraRigInput
{
	GENERATED_BODY()

	// eased 0..1 sprint amount used as a blend weight
	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	float SprintAlpha = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	bool bInCombat = false;

	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	bool bInAir = false;
};

// third person camera rig with profiles, lag, shakes, fov punch and pitch dip, fed by the owner each frame
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API USlashCameraRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USlashCameraRigComponent();

	// stores the components and does one-time spring arm setup
	void InitializeRig(USpringArmComponent* InCameraBoom, UCameraComponent* InFollowCamera);

	// call from the owner's Tick after movement for the frame is done
	void UpdateRig(float DeltaSeconds, const FSlashCameraRigInput& Input);

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void PlayImpactShake(float Scale = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void PlayLandShake(float Scale = 1.f);

	// extra fov in degrees that decays back to the profile
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void AddFOVPunch(float Degrees);

	// kick to the pitch dip spring in degrees, positive looks down
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void AddPitchDip(float Degrees);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Profiles")
	FSlashCameraProfile DefaultProfile;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Profiles")
	FSlashCameraProfile SprintProfile;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Profiles")
	FSlashCameraProfile CombatProfile;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Profiles")
	FSlashCameraProfile AirProfile;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Shakes")
	TSubclassOf<UCameraShakeBase> SprintShakeClass;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Shakes")
	TSubclassOf<UCameraShakeBase> ImpactShakeClass;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Shakes")
	TSubclassOf<UCameraShakeBase> LandShakeClass;

	// limit on lag distance so a dash doesn't fling the camera
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Lag")
	float CameraLagMaxDistance = 140.f;

	// off since rotation lag makes look input feel delayed
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Lag")
	bool bEnableRotationLag = false;

	// boom collision sweep radius, bigger than the lens so the near plane stays out of walls
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Collision")
	float CameraProbeSize = 24.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Punch")
	float FOVPunchRecoverySpeed = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Punch")
	float PitchDipStiffness = 240.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Punch")
	float PitchDipDamping = 22.f;

private:
	static FSlashCameraProfile BlendProfiles(const FSlashCameraProfile& From, const FSlashCameraProfile& To, float Alpha);

	void ApplyProfile(const FSlashCameraProfile& Profile);
	void UpdateSprintShake(float SprintAlpha);
	APlayerCameraManager* GetCameraManager() const;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> FollowCamera;

	TWeakObjectPtr<UCameraShakeBase> SprintShakeInstance;

	float AirAlpha = 0.f;
	float CombatAlpha = 0.f;

	float FOVPunch = 0.f;
	float PitchDip = 0.f;
	float PitchDipVelocity = 0.f;
};

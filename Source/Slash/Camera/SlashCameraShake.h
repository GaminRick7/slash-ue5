// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "SlashCameraShake.generated.h"

// one shake channel, amplitude in cm or degrees, frequency in hz or perlin noise units
USTRUCT(BlueprintType)
struct FSlashShakeChannel
{
	GENERATED_BODY()

	FSlashShakeChannel() = default;

	FSlashShakeChannel(float InAmplitude, float InFrequency)
		: Amplitude(InAmplitude)
		, Frequency(InFrequency)
	{
	}

	UPROPERTY(EditAnywhere, Category = "Shake")
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Shake")
	float Frequency = 1.f;
};

// oscillating shake pattern, the engine versions live in the EngineCameras plugin
UCLASS(meta = (DisplayName = "Slash Oscillator Shake Pattern"))
class SLASH_API USlashOscillatorShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	USlashOscillatorShakePattern(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
	}

	// 0 or less runs until StopShake
	UPROPERTY(EditAnywhere, Category = "Timing")
	float Duration = 0.f;

	UPROPERTY(EditAnywhere, Category = "Timing")
	float BlendInTime = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Timing")
	float BlendOutTime = 0.2f;

	// perlin for long drift, sine for short jolts
	UPROPERTY(EditAnywhere, Category = "Shake")
	bool bUsePerlinNoise = true;

	UPROPERTY(EditAnywhere, Category = "Shake|Location")
	FSlashShakeChannel LocationX;

	UPROPERTY(EditAnywhere, Category = "Shake|Location")
	FSlashShakeChannel LocationY;

	UPROPERTY(EditAnywhere, Category = "Shake|Location")
	FSlashShakeChannel LocationZ;

	UPROPERTY(EditAnywhere, Category = "Shake|Rotation")
	FSlashShakeChannel Pitch;

	UPROPERTY(EditAnywhere, Category = "Shake|Rotation")
	FSlashShakeChannel Yaw;

	UPROPERTY(EditAnywhere, Category = "Shake|Rotation")
	FSlashShakeChannel Roll;

	UPROPERTY(EditAnywhere, Category = "Shake|FOV")
	FSlashShakeChannel FieldOfView;

private:
	// UCameraShakePattern interface
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override;
	virtual void TeardownShakePatternImpl() override;

	float SampleChannel(const FSlashShakeChannel& Channel, float& InOutPhase, float DeltaTime) const;
	float ComputeBlendWeight() const;

	float ElapsedTime = 0.f;
	bool bStopping = false;
	float StopBlendOutRemaining = 0.f;

	float PhaseLocationX = 0.f;
	float PhaseLocationY = 0.f;
	float PhaseLocationZ = 0.f;
	float PhasePitch = 0.f;
	float PhaseYaw = 0.f;
	float PhaseRoll = 0.f;
	float PhaseFieldOfView = 0.f;
};

// base for the game's shakes, swaps in the oscillator pattern
UCLASS(Abstract)
class SLASH_API USlashCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	USlashCameraShake(const FObjectInitializer& ObjectInitializer);

protected:
	USlashOscillatorShakePattern* GetOscillator() const;
};

// light drift while sprinting, scaled by the rig with the sprint amount
UCLASS()
class SLASH_API USlashSprintCameraShake : public USlashCameraShake
{
	GENERATED_BODY()

public:
	USlashSprintCameraShake(const FObjectInitializer& ObjectInitializer);
};

// short jolt when the sword hits
UCLASS()
class SLASH_API USlashImpactCameraShake : public USlashCameraShake
{
	GENERATED_BODY()

public:
	USlashImpactCameraShake(const FObjectInitializer& ObjectInitializer);
};

// mostly vertical bump on landing scaled by fall speed
UCLASS()
class SLASH_API USlashLandCameraShake : public USlashCameraShake
{
	GENERATED_BODY()

public:
	USlashLandCameraShake(const FObjectInitializer& ObjectInitializer);
};

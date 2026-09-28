// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlashCameraShake.h"

void USlashOscillatorShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = (Duration > 0.f) ? FCameraShakeDuration(Duration) : FCameraShakeDuration::Infinite();
	OutInfo.BlendIn = BlendInTime;
	OutInfo.BlendOut = BlendOutTime;
}

void USlashOscillatorShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	ElapsedTime = 0.f;
	bStopping = false;
	StopBlendOutRemaining = 0.f;

	// random start phases so repeated shakes don't look identical
	PhaseLocationX = FMath::FRand() * 100.f;
	PhaseLocationY = FMath::FRand() * 100.f;
	PhaseLocationZ = FMath::FRand() * 100.f;
	PhasePitch = FMath::FRand() * 100.f;
	PhaseYaw = FMath::FRand() * 100.f;
	PhaseRoll = FMath::FRand() * 100.f;
	PhaseFieldOfView = FMath::FRand() * 100.f;
}

float USlashOscillatorShakePattern::SampleChannel(const FSlashShakeChannel& Channel, float& InOutPhase, float DeltaTime) const
{
	if (Channel.Amplitude == 0.f)
	{
		return 0.f;
	}

	InOutPhase += DeltaTime * Channel.Frequency;

	const float Sample = bUsePerlinNoise
		? FMath::PerlinNoise1D(InOutPhase)
		: FMath::Sin(InOutPhase * 2.f * PI);

	return Channel.Amplitude * Sample;
}

float USlashOscillatorShakePattern::ComputeBlendWeight() const
{
	const float BlendIn = (BlendInTime > 0.f) ? FMath::Clamp(ElapsedTime / BlendInTime, 0.f, 1.f) : 1.f;

	float BlendOut = 1.f;
	if (bStopping)
	{
		BlendOut = (BlendOutTime > 0.f) ? FMath::Clamp(StopBlendOutRemaining / BlendOutTime, 0.f, 1.f) : 0.f;
	}
	else if (Duration > 0.f && BlendOutTime > 0.f)
	{
		BlendOut = FMath::Clamp((Duration - ElapsedTime) / BlendOutTime, 0.f, 1.f);
	}

	return BlendIn * BlendOut;
}

void USlashOscillatorShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	const float DeltaTime = Params.DeltaTime;

	ElapsedTime += DeltaTime;
	if (bStopping)
	{
		StopBlendOutRemaining = FMath::Max(StopBlendOutRemaining - DeltaTime, 0.f);
	}

	const float Weight = ComputeBlendWeight();

	OutResult.Location = FVector(
		SampleChannel(LocationX, PhaseLocationX, DeltaTime),
		SampleChannel(LocationY, PhaseLocationY, DeltaTime),
		SampleChannel(LocationZ, PhaseLocationZ, DeltaTime)) * Weight;

	OutResult.Rotation = FRotator(
		SampleChannel(Pitch, PhasePitch, DeltaTime),
		SampleChannel(Yaw, PhaseYaw, DeltaTime),
		SampleChannel(Roll, PhaseRoll, DeltaTime)) * Weight;

	OutResult.FOV = SampleChannel(FieldOfView, PhaseFieldOfView, DeltaTime) * Weight;
}

bool USlashOscillatorShakePattern::IsFinishedImpl() const
{
	if (bStopping)
	{
		return StopBlendOutRemaining <= 0.f;
	}

	return Duration > 0.f && ElapsedTime >= Duration;
}

void USlashOscillatorShakePattern::StopShakePatternImpl(const FCameraShakePatternStopParams& Params)
{
	bStopping = true;
	StopBlendOutRemaining = Params.bImmediately ? 0.f : BlendOutTime;
}

void USlashOscillatorShakePattern::TeardownShakePatternImpl()
{
	ElapsedTime = 0.f;
	bStopping = false;
	StopBlendOutRemaining = 0.f;
}

USlashCameraShake::USlashCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USlashOscillatorShakePattern>(TEXT("RootShakePattern")))
{
}

USlashOscillatorShakePattern* USlashCameraShake::GetOscillator() const
{
	return Cast<USlashOscillatorShakePattern>(GetRootShakePattern());
}

USlashSprintCameraShake::USlashSprintCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// restart the existing sprint shake instead of stacking another
	bSingleInstance = true;

	if (USlashOscillatorShakePattern* Oscillator = GetOscillator())
	{
		Oscillator->Duration = 0.f;
		Oscillator->BlendInTime = 0.35f;
		Oscillator->BlendOutTime = 0.45f;
		Oscillator->bUsePerlinNoise = true;

		// kept subtle
		Oscillator->LocationY = FSlashShakeChannel(0.9f, 3.1f);
		Oscillator->LocationZ = FSlashShakeChannel(1.4f, 4.7f);
		Oscillator->Pitch = FSlashShakeChannel(0.25f, 4.1f);
		Oscillator->Yaw = FSlashShakeChannel(0.20f, 2.9f);
		Oscillator->Roll = FSlashShakeChannel(0.45f, 2.3f);
	}
}

USlashImpactCameraShake::USlashImpactCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (USlashOscillatorShakePattern* Oscillator = GetOscillator())
	{
		Oscillator->Duration = 0.28f;
		Oscillator->BlendInTime = 0.f;
		Oscillator->BlendOutTime = 0.22f;
		// sine for a clean sharp hit
		Oscillator->bUsePerlinNoise = false;

		Oscillator->LocationY = FSlashShakeChannel(2.2f, 14.f);
		Oscillator->LocationZ = FSlashShakeChannel(2.6f, 17.f);
		Oscillator->Pitch = FSlashShakeChannel(0.85f, 16.f);
		Oscillator->Roll = FSlashShakeChannel(1.10f, 11.f);
		Oscillator->FieldOfView = FSlashShakeChannel(1.20f, 9.f);
	}
}

USlashLandCameraShake::USlashLandCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (USlashOscillatorShakePattern* Oscillator = GetOscillator())
	{
		Oscillator->Duration = 0.32f;
		Oscillator->BlendInTime = 0.f;
		Oscillator->BlendOutTime = 0.26f;
		Oscillator->bUsePerlinNoise = false;

		// mostly vertical
		Oscillator->LocationZ = FSlashShakeChannel(3.4f, 12.f);
		Oscillator->LocationY = FSlashShakeChannel(0.8f, 9.f);
		Oscillator->Pitch = FSlashShakeChannel(1.10f, 10.f);
		Oscillator->Roll = FSlashShakeChannel(0.55f, 7.f);
	}
}

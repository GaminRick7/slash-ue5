// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "MeleeWeaponComponent.generated.h"

// HitIndex counts targets this window so hit stop fires once
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWeaponHit, AActor*, HitActor, const FHitResult&, Hit, int32, HitIndex);

// sword mesh that sweeps its blade for hits, see Docs/HitDetection_Plan.md
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API UMeleeWeaponComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMeleeWeaponComponent();

	// does nothing if the blade sockets weren't found in BeginPlay
	void BeginWindow();

	// safe to call twice, also called on node entry and combo end
	void EndWindow();

	bool IsWindowOpen() const { return bWindowOpen; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnWeaponHit OnWeaponHit;

protected:
	virtual void BeginPlay() override;

	// blade base and tip sockets, both required

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
	FName BladeBaseSocket = "BladeBaseSocket";

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
	FName BladeTipSocket = "BladeTipSocket";

	// blade thickness, gap free up to about 25 deg of turn per frame
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace", meta = (ClampMin = "0.1"))
	float TraceRadius = 10.f;

	// hits the enemy movement capsule
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
	TArray<TEnumAsByte<EObjectTypeQuery>> HittableObjectTypes;

private:
	// capsule along the blade
	void GetBladeCapsule(FVector& OutMid, FQuat& OutRotation, float& OutHalfHeight) const;

	bool bWindowOpen = false;

	// false if a socket was missing
	bool bBladeValid = false;

	// last frame's blade so sweeps cover the gap between frames
	FVector PrevMid = FVector::ZeroVector;
	FQuat PrevRotation = FQuat::Identity;

	// actors already hit this window
	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;

	int32 HitCount = 0;
};

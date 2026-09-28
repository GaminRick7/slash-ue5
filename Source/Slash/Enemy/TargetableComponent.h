// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "TargetableComponent.generated.h"

#define COLLISION_TARGETABLE ECC_GameTraceChannel1

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API UTargetableComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UTargetableComponent();

	FVector GetTargetPoint() const;

	UFUNCTION(BlueprintCallable, Category = "Targeting")
	void SetTargetable(bool bNewTargetable);

	UFUNCTION(BlueprintPure, Category = "Targeting")
	bool IsTargetable() const;

	// score bias for this target
	UPROPERTY(EditAnywhere, Category = "Targeting")
	float TargetPriorityBias = 0.f;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Targeting")
	bool bTargetable = true;

private:
	void ApplyTargetableCollision();
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterOutfit.generated.h"

class AActor;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;

// clothing or gear meshes that copy the base mesh pose, needs matching bone names
USTRUCT(BlueprintType)
struct FCharacterOutfit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Outfit")
	TArray<TObjectPtr<USkeletalMesh>> Parts;

	// material replacements keyed by the original material
	UPROPERTY(EditAnywhere, Category = "Outfit")
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> MaterialSwaps;

	// hide the base mesh under the outfit, off when the base mesh is the visible body
	UPROPERTY(EditAnywhere, Category = "Outfit")
	bool bHideBaseMesh = true;

	// spawns the parts on Owner following Leader, call from OnConstruction
	void Apply(AActor* Owner, USkeletalMeshComponent* Leader) const;
};

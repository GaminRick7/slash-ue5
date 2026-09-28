// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ComboTreeAsset.generated.h"

class UInputAction;
class UAnimMontage;

USTRUCT(BlueprintType)
struct FComboTransition
{
	GENERATED_BODY()

	// input that triggers this transition
	UPROPERTY(EditAnywhere, Category = "Combo")
	TObjectPtr<UInputAction> TriggerAction;

	// node to go to
	UPROPERTY(EditAnywhere, Category = "Combo")
	FName NextNodeId;
};

USTRUCT(BlueprintType)
struct FComboNode
{
	GENERATED_BODY()

	// unique id in the tree like Light_1
	UPROPERTY(EditAnywhere, Category = "Combo")
	FName NodeId;

	UPROPERTY(EditAnywhere, Category = "Combo")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, Category = "Combo")
	FName SectionName;

	UPROPERTY(EditAnywhere, Category = "Combo")
	float PlayRate = 1.f;

	// forward impulse on entry, ignored with root motion
	UPROPERTY(EditAnywhere, Category = "Combo")
	float DashSpeed = 0.f;

	// use montage root motion instead of DashSpeed
	UPROPERTY(EditAnywhere, Category = "Combo")
	bool bRootMotionMovement = false;

	// scales fov kick, impact shake and hit stop
	UPROPERTY(EditAnywhere, Category = "Combo")
	float CameraShakeScale = 1.f;

	// empty means this node ends the combo
	UPROPERTY(EditAnywhere, Category = "Combo")
	TArray<FComboTransition> Transitions;
};

UCLASS(BlueprintType)
class SLASH_API UComboTreeAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Combo")
	TArray<FComboNode> Nodes;

	// root node with no montage, its transitions start combos
	UPROPERTY(EditAnywhere, Category = "Combo")
	FName EntryNodeId;

	const FComboNode* FindNode(FName NodeId) const;
};

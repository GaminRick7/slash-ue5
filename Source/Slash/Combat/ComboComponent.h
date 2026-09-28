// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ComboComponent.generated.h"

class UComboTreeAsset;
class UInputAction;
class UAnimMontage;
struct FComboNode;
struct FBranchingPointNotifyPayload;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComboNodeEvent, FName, NodeId, bool, bIsComboStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnComboHitWindow, FName, NodeId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComboEnded, FName, NodeId, bool, bWasInterrupted);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API UComboComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UComboComponent();

	void SetActiveTree(UComboTreeAsset* NewTree);
	void RequestAttack(UInputAction* Action);

	// 0 snaps the pose, use a short blend for player cancels
	void CancelCombo(float BlendOutTime = 0.f);

	// true inside the section's Recovery notify
	bool IsInRecovery() const { return CurrentNode != nullptr && bInRecovery; }

	const FComboNode* GetCurrentNode() const { return CurrentNode; }

	UPROPERTY(BlueprintAssignable, Category = "Combo")
	FOnComboNodeEvent OnComboNodeEntered;

	// damage window start and end, the end can be skipped when chaining

	UPROPERTY(BlueprintAssignable, Category = "Combo")
	FOnComboHitWindow OnAttackWindowOpened;

	UPROPERTY(BlueprintAssignable, Category = "Combo")
	FOnComboHitWindow OnAttackWindowClosed;

	UPROPERTY(BlueprintAssignable, Category = "Combo")
	FOnComboEnded OnComboEnded;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Combo")
	TObjectPtr<UComboTreeAsset> ActiveTree;

	UPROPERTY(EditDefaultsOnly, Category = "Combo")
	FName ComboWindowNotifyName = "ComboWindow";

	// notify window for the hit
	UPROPERTY(EditDefaultsOnly, Category = "Combo")
	FName AttackHitNotifyName = "AttackHit";

	// notify window for the cancellable wind down
	UPROPERTY(EditDefaultsOnly, Category = "Combo")
	FName RecoveryNotifyName = "Recovery";

	// all notify windows come through these, matched by name

	UFUNCTION()
	void HandleMontageNotifyBegin(FName NotifyName, const FBranchingPointNotifyPayload& NotifyPayload);

	UFUNCTION()
	void HandleMontageNotifyEnd(FName NotifyName, const FBranchingPointNotifyPayload& NotifyPayload);

	UFUNCTION()
	void HandleAttackMontageFinished(UAnimMontage* Montage, bool bInterrupted);

private:
	void EnterNode(FName NodeId, bool bIsComboStart);
	bool TryTransition(const FComboNode* FromNode, UInputAction* Action, bool bIsComboStart);

	// montage instance this combo owns, events from other instances are ignored
	int32 ActiveMontageInstanceID = INDEX_NONE;

	const FComboNode* CurrentNode = nullptr;
	bool bWindowOpen = false;
	bool bInRecovery = false;
	bool bHasBufferedInput = false;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BufferedAction;
};

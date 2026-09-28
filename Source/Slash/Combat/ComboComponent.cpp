// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComboComponent.h"
#include "ComboTreeAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameFramework/Character.h"
#include "InputAction.h"

UComboComponent::UComboComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UComboComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInstance->OnPlayMontageNotifyBegin.AddDynamic(this, &UComboComponent::HandleMontageNotifyBegin);
			AnimInstance->OnPlayMontageNotifyEnd.AddDynamic(this, &UComboComponent::HandleMontageNotifyEnd);
		}
	}
}

void UComboComponent::SetActiveTree(UComboTreeAsset* NewTree)
{
	ActiveTree = NewTree;
}

void UComboComponent::RequestAttack(UInputAction* Action)
{
	if (!ActiveTree || !Action)
	{
		return;
	}

	if (!CurrentNode)
	{
		TryTransition(ActiveTree->FindNode(ActiveTree->EntryNodeId), Action, true);
		return;
	}

	if (bWindowOpen)
	{
		TryTransition(CurrentNode, Action, false);
		return;
	}

	// window closed mid combo so buffer one press
	if (!bHasBufferedInput)
	{
		bHasBufferedInput = true;
		BufferedAction = Action;
	}
}

void UComboComponent::CancelCombo(float BlendOutTime)
{
	if (!CurrentNode)
	{
		return;
	}

	const FName EndedNodeId = CurrentNode->NodeId;
	UAnimMontage* Montage = CurrentNode->Montage;

	// clear the id first since the old instance keeps sending events while blending out
	ActiveMontageInstanceID = INDEX_NONE;

	CurrentNode = nullptr;
	bWindowOpen = false;
	bInRecovery = false;
	bHasBufferedInput = false;
	BufferedAction = nullptr;

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInstance->Montage_Stop(BlendOutTime, Montage);
		}
	}

	OnComboEnded.Broadcast(EndedNodeId, true);
}

void UComboComponent::EnterNode(FName NodeId, bool bIsComboStart)
{
	const FComboNode* Node = ActiveTree ? ActiveTree->FindNode(NodeId) : nullptr;
	if (!Node || !Node->Montage)
	{
		return;
	}

	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	UAnimInstance* AnimInstance = OwnerCharacter && OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	CurrentNode = Node;
	bWindowOpen = false;
	bInRecovery = false;
	bHasBufferedInput = false;
	BufferedAction = nullptr;

	if (bIsComboStart)
	{
		AnimInstance->Montage_Play(Node->Montage, Node->PlayRate);
		AnimInstance->Montage_JumpToSection(Node->SectionName, Node->Montage);

		// remember the instance for chained nodes
		const FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(Node->Montage);
		ActiveMontageInstanceID = Instance ? Instance->GetInstanceID() : INDEX_NONE;

		// ignore end events from older instances
		const int32 InstanceID = ActiveMontageInstanceID;
		FOnMontageEnded EndDelegate;
		EndDelegate.BindWeakLambda(this, [this, InstanceID](UAnimMontage* EndedMontage, bool bInterrupted)
		{
			if (InstanceID == ActiveMontageInstanceID)
			{
				HandleAttackMontageFinished(EndedMontage, bInterrupted);
			}
		});
		AnimInstance->Montage_SetEndDelegate(EndDelegate, Node->Montage);
	}
	else
	{
		AnimInstance->Montage_JumpToSection(Node->SectionName, Node->Montage);

		// chained nodes share the instance so set this node's play rate
		AnimInstance->Montage_SetPlayRate(Node->Montage, Node->PlayRate);
	}

	// stop the section flowing on by itself
	AnimInstance->Montage_SetNextSection(Node->SectionName, NAME_None, Node->Montage);

	OnComboNodeEntered.Broadcast(Node->NodeId, bIsComboStart);
}

bool UComboComponent::TryTransition(const FComboNode* FromNode, UInputAction* Action, bool bIsComboStart)
{
	if (!FromNode)
	{
		return false;
	}

	for (const FComboTransition& Transition : FromNode->Transitions)
	{
		if (Transition.TriggerAction == Action)
		{
			EnterNode(Transition.NextNodeId, bIsComboStart);
			return true;
		}
	}
	return false;
}

void UComboComponent::HandleMontageNotifyBegin(FName NotifyName, const FBranchingPointNotifyPayload& NotifyPayload)
{
	if (NotifyPayload.MontageInstanceID != ActiveMontageInstanceID)
	{
		return;
	}

	// hit window
	if (NotifyName == AttackHitNotifyName)
	{
		OnAttackWindowOpened.Broadcast(CurrentNode ? CurrentNode->NodeId : NAME_None);
		return;
	}

	// recovery started
	if (NotifyName == RecoveryNotifyName)
	{
		bInRecovery = true;
		return;
	}

	if (NotifyName != ComboWindowNotifyName)
	{
		return;
	}

	bWindowOpen = true;

	if (bHasBufferedInput)
	{
		UInputAction* Action = BufferedAction;
		bHasBufferedInput = false;
		BufferedAction = nullptr;
		TryTransition(CurrentNode, Action, false);
	}
}

void UComboComponent::HandleMontageNotifyEnd(FName NotifyName, const FBranchingPointNotifyPayload& NotifyPayload)
{
	// ignore ends from older instances
	if (NotifyPayload.MontageInstanceID != ActiveMontageInstanceID)
	{
		return;
	}

	// hit window close, skipped when chaining out mid window
	if (NotifyName == AttackHitNotifyName)
	{
		OnAttackWindowClosed.Broadcast(CurrentNode ? CurrentNode->NodeId : NAME_None);
		return;
	}

	// also skipped when chaining out, EnterNode resets it
	if (NotifyName == RecoveryNotifyName)
	{
		bInRecovery = false;
		return;
	}

	if (NotifyName != ComboWindowNotifyName || !bWindowOpen)
	{
		return;
	}

	// window passed with no input
	bWindowOpen = false;
}

void UComboComponent::HandleAttackMontageFinished(UAnimMontage* Montage, bool bInterrupted)
{
	if (!CurrentNode)
	{
		// already ended by CancelCombo
		return;
	}

	const FName EndedNodeId = CurrentNode->NodeId;
	ActiveMontageInstanceID = INDEX_NONE;
	CurrentNode = nullptr;
	bWindowOpen = false;
	bInRecovery = false;
	bHasBufferedInput = false;
	BufferedAction = nullptr;

	OnComboEnded.Broadcast(EndedNodeId, bInterrupted);
}

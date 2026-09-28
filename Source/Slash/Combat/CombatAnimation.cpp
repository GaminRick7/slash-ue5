// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatAnimation.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"

void SlashCombatAnimation::PlayAndHoldLastFrame(ACharacter* Character, UAnimSequenceBase* Anim, FName SlotName, float PlayRate)
{
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Anim || PlayRate <= 0.f)
	{
		return;
	}

	// stop attacks and flinches so they don't blend with the death clip
	AnimInstance->StopAllMontages(0.1f);
	AnimInstance->PlaySlotAnimationAsDynamicMontage(Anim, SlotName, 0.1f, 0.f, PlayRate);

	// pause just before the end so it never blends out, outfit parts freeze too
	const float HoldAt = FMath::Max(Anim->GetPlayLength() / PlayRate - 0.05f, 0.01f);
	TWeakObjectPtr<USkeletalMeshComponent> WeakMesh = Mesh;
	FTimerHandle Handle;
	Character->GetWorldTimerManager().SetTimer(Handle, [WeakMesh]()
	{
		if (USkeletalMeshComponent* PinnedMesh = WeakMesh.Get())
		{
			PinnedMesh->bPauseAnims = true;
		}
	}, HoldAt, false);
}

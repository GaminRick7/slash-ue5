// Copyright Epic Games, Inc. All Rights Reserved.

#include "CharacterOutfit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

void FCharacterOutfit::Apply(AActor* Owner, USkeletalMeshComponent* Leader) const
{
	if (!Owner || !Leader)
	{
		return;
	}

	bool bWearingOutfit = false;
	for (USkeletalMesh* Part : Parts)
	{
		if (!Part)
		{
			continue;
		}
		bWearingOutfit = true;

		// not transient since placed actors reach PIE without rerunning construction
		USkeletalMeshComponent* PartComponent = NewObject<USkeletalMeshComponent>(Owner);
		PartComponent->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		PartComponent->SetupAttachment(Leader);
		PartComponent->SetSkeletalMeshAsset(Part);
		PartComponent->SetLeaderPoseComponent(Leader);
		PartComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PartComponent->SetGenerateOverlapEvents(false);

		for (int32 SlotIndex = 0; SlotIndex < PartComponent->GetNumMaterials(); ++SlotIndex)
		{
			if (const TObjectPtr<UMaterialInterface>* Swap = MaterialSwaps.Find(PartComponent->GetMaterial(SlotIndex)))
			{
				PartComponent->SetMaterial(SlotIndex, *Swap);
			}
		}

		PartComponent->RegisterComponent();
	}

	// hidden meshes stop updating bones by default and would freeze the parts
	const bool bHideLeader = bWearingOutfit && bHideBaseMesh;
	Leader->SetVisibility(!bHideLeader);
	Leader->VisibilityBasedAnimTickOption = bHideLeader
		? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		: EVisibilityBasedAnimTickOption::AlwaysTickPose;

	// no anim tick in the editor so pose the hidden leader once or the parts collapse to the origin
	if (bHideLeader && Leader->GetWorld() && !Leader->GetWorld()->IsGameWorld())
	{
		Leader->RefreshBoneTransforms();
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/HeroLighting.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"

void SlashHeroLighting::AddToHeroChannel(AActor* Owner)
{
	if (!Owner)
	{
		return;
	}

	TInlineComponentArray<UMeshComponent*> Meshes(Owner);
	for (UMeshComponent* Mesh : Meshes)
	{
		Mesh->SetLightingChannels(/*bChannel0=*/true, /*bChannel1=*/true, /*bChannel2=*/false);
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

// hero light only hits lighting channel 1, characters sit in 0 and 1 so the level never gets it
namespace SlashHeroLighting
{
	// puts every mesh on Owner into the hero channel, call after meshes are created
	void AddToHeroChannel(AActor* Owner);
}

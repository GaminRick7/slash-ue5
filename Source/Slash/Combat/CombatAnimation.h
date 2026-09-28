// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ACharacter;
class UAnimSequenceBase;

namespace SlashCombatAnimation
{
	// plays a clip and holds the last frame, used for death
	void PlayAndHoldLastFrame(ACharacter* Character, UAnimSequenceBase* Anim, FName SlotName, float PlayRate = 1.f);
}

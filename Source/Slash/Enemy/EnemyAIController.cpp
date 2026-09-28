// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/EnemyAIController.h"

AEnemyAIController::AEnemyAIController()
{
	// turn the body toward the focus set by the brain
	bSetControlRotationFromPawnOrientation = false;
}

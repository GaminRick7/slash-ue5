// Copyright Epic Games, Inc. All Rights Reserved.

#include "TargetableComponent.h"

UTargetableComponent::UTargetableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	InitSphereRadius(60.f);
	SetCollisionObjectType(COLLISION_TARGETABLE);
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore); // queries only, blocks nothing
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
}

FVector UTargetableComponent::GetTargetPoint() const
{
	return GetComponentLocation();
}

void UTargetableComponent::SetTargetable(bool bNewTargetable)
{
	bTargetable = bNewTargetable;
	ApplyTargetableCollision();
}

bool UTargetableComponent::IsTargetable() const
{
	return bTargetable && IsValid(GetOwner());
}

void UTargetableComponent::ApplyTargetableCollision()
{
	SetCollisionEnabled(bTargetable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

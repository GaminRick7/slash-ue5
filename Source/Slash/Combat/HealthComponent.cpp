// Copyright Epic Games, Inc. All Rights Reserved.

#include "HealthComponent.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;

	if (AActor* Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.AddDynamic(this, &UHealthComponent::HandleTakeAnyDamage);
	}
}

float UHealthComponent::GetHealthPercent() const
{
	return MaxHealth > 0.f ? Health / MaxHealth : 0.f;
}

void UHealthComponent::HandleTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	AController* InstigatedBy, AActor* DamageCauser)
{
	// ignore damage once dead so OnDeath fires once
	if (Damage <= 0.f || !IsAlive())
	{
		return;
	}

	const float PreviousHealth = Health;
	Health = FMath::Clamp(Health - Damage, 0.f, MaxHealth);

	OnHealthChanged.Broadcast(Health, Health - PreviousHealth, DamageCauser);

	if (!IsAlive())
	{
		OnDeath.Broadcast(DamageCauser);
	}
}

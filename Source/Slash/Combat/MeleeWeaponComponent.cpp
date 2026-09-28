// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeleeWeaponComponent.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarMeleeTraceDebug(
	TEXT("slash.MeleeTrace.Debug"),
	0,
	TEXT("Draw the blade capsule every frame while a hit window is open. Persists until flushed."),
	ECVF_Cheat);

UMeleeWeaponComponent::UMeleeWeaponComponent()
{
	// only ticks while a hit window is open
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);

	HittableObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
}

void UMeleeWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	// missing socket would collapse the blade so log and disable
	bBladeValid = DoesSocketExist(BladeBaseSocket) && DoesSocketExist(BladeTipSocket);
	if (!bBladeValid)
	{
		UE_LOG(LogTemp, Error, TEXT("[MeleeWeapon] %s: blade sockets '%s'/'%s' missing. Detection off."),
			*GetNameSafe(GetOwner()), *BladeBaseSocket.ToString(), *BladeTipSocket.ToString());
	}

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
		{
			// tick after the mesh so the sweep uses this frame's pose
			PrimaryComponentTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
		}
	}
}

void UMeleeWeaponComponent::GetBladeCapsule(FVector& OutMid, FQuat& OutRotation, float& OutHalfHeight) const
{
	// world space sockets so the component scale is already in
	const FVector Start = GetSocketLocation(BladeBaseSocket);
	const FVector End = GetSocketLocation(BladeTipSocket);

	OutMid = (Start + End) * 0.5f;
	OutHalfHeight = FVector::Dist(Start, End) * 0.5f;

	// point the capsule's z down the blade
	OutRotation = FRotationMatrix::MakeFromZ(End - Start).ToQuat();
}

void UMeleeWeaponComponent::BeginWindow()
{
	// TEMP window timing log
	UE_LOG(LogTemp, Warning, TEXT("[MeleeWeapon] BeginWindow frame %llu bladeValid=%d"), GFrameCounter, bBladeValid);

	if (!bBladeValid)
	{
		return;
	}

	// a new window can open before the last one closed, treat it as a new swing
	HitActorsThisWindow.Reset();
	HitCount = 0;

	// start from the current pose
	float HalfHeight = 0.f;
	GetBladeCapsule(PrevMid, PrevRotation, HalfHeight);

	bWindowOpen = true;
	SetComponentTickEnabled(true);
}

void UMeleeWeaponComponent::EndWindow()
{
	// TEMP remove with the BeginWindow log
	UE_LOG(LogTemp, Warning, TEXT("[MeleeWeapon] EndWindow frame %llu open=%d"), GFrameCounter, bWindowOpen);

	if (!bWindowOpen)
	{
		return;
	}

	bWindowOpen = false;
	SetComponentTickEnabled(false);

	HitActorsThisWindow.Reset();
}

void UMeleeWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!bWindowOpen || !World)
	{
		return;
	}

	FVector Mid = FVector::ZeroVector;
	FQuat BladeRot = FQuat::Identity;
	float HalfHeight = 0.f;
	GetBladeCapsule(Mid, BladeRot, HalfHeight);

	// sweeps can't rotate so use the halfway rotation
	const FQuat SweepRot = FQuat::Slerp(PrevRotation, BladeRot, 0.5f);

	FCollisionObjectQueryParams ObjectParams;
	for (const TEnumAsByte<EObjectTypeQuery>& ObjectType : HittableObjectTypes)
	{
		ObjectParams.AddObjectTypesToQuery(UEngineTypes::ConvertToCollisionChannel(ObjectType));
	}

	// ignore our own capsule
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MeleeTrace), /*bTraceComplex=*/false, GetOwner());

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, PrevMid, Mid, SweepRot, ObjectParams,
		FCollisionShape::MakeCapsule(TraceRadius, HalfHeight), QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor)
		{
			continue;
		}

		// only living things with health
		const UHealthComponent* Health = HitActor->FindComponentByClass<UHealthComponent>();
		if (!Health || !Health->IsAlive())
		{
			continue;
		}

		bool bAlreadyHit = false;
		HitActorsThisWindow.Add(HitActor, &bAlreadyHit);
		if (bAlreadyHit)
		{
			continue;
		}

		// TEMP hit log
		UE_LOG(LogTemp, Warning, TEXT("[MeleeWeapon] HIT %s index=%d frame=%llu"),
			*HitActor->GetName(), HitCount, GFrameCounter);

		OnWeaponHit.Broadcast(HitActor, Hit, HitCount++);
	}

	if (CVarMeleeTraceDebug.GetValueOnGameThread() > 0)
	{
		// leaves a capsule per frame so the whole swing shows, gaps are misses
		DrawDebugCapsule(World, Mid, HalfHeight, TraceRadius, BladeRot, FColor::Cyan, /*bPersistentLines=*/true);
	}

	// store the real rotation, not the blended one
	PrevMid = Mid;
	PrevRotation = BladeRot;
}

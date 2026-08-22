// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeWeaponPart.h"
#include "GuLiStrikeProjectile.h"
#include "Engine/World.h"

void UGuLiStrikeWeaponPart::Fire_Implementation(AActor* Instigator)
{
	if (!ProjectileClass || !GetWorld())
	{
		return;
	}

	// 部件自己执行射速冷却
	const float Now = GetWorld()->GetTimeSeconds();
	const float FireInterval = 1.0f / FMath::Max(FireRate, 0.01f);
	if (Now - LastFireTime < FireInterval)
	{
		return;
	}

	LastFireTime = Now;

	// 从自身炮口生成投射物
	FTransform MuzzleTransform = GetComponentTransform();
	MuzzleTransform.SetLocation(MuzzleTransform.TransformPosition(MuzzleOffset));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Owner = Instigator;

	GetWorld()->SpawnActor<AGuLiStrikeProjectile>(ProjectileClass, MuzzleTransform, SpawnParameters);
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeWeaponPart.h"
#include "GuLiStrikeShip.h"
#include "GuLiStrikeProjectile.h"
#include "Engine/World.h"

void UGuLiStrikeWeaponPart::Fire_Implementation(AActor* Instigator)
{
	const AGuLiStrikeShip* Ship = Cast<AGuLiStrikeShip>(Instigator);
	// native 父实现也独立守门：只有服务器当前 Air Pawn 的已装武器能够产生真实弹丸。
	if (!Ship || !Ship->CanExecuteServerWeapon(this) || !ProjectileClass || !GetWorld())
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

	// 炮口以服务器装配及胶囊权威姿态为准，不能读取带 Listen Server 视觉平滑偏移的世界变换。
	FTransform MuzzleTransform;
	if (!Ship->GetServerPartTransform(this, MuzzleTransform)) { return; }
	MuzzleTransform.SetLocation(MuzzleTransform.TransformPosition(MuzzleOffset));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Owner = Instigator;
	SpawnParameters.Instigator = Cast<APawn>(Instigator);

	GetWorld()->SpawnActor<AGuLiStrikeProjectile>(ProjectileClass, MuzzleTransform, SpawnParameters);
}

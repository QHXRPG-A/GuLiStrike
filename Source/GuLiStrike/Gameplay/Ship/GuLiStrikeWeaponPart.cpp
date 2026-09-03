// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeWeaponPart.h"
#include "GuLiStrikeShip.h"
#include "GuLiStrikeProjectile.h"
#include "Engine/World.h"

void UGuLiStrikeWeaponPart::Fire_Implementation(AActor* Instigator)
{
	AGuLiStrikeShip* Ship = Cast<AGuLiStrikeShip>(Instigator);
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

	// Deferred spawning guarantees the immutable ledger identity is present before
	// collision/movement components activate, including for a muzzle already touching a target.
	if (AGuLiStrikeProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGuLiStrikeProjectile>(
		ProjectileClass,
		MuzzleTransform,
		Ship,
		Ship,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		// 不迁移到 GA：沿用现有 WeaponPart -> replicated Actor projectile 链，
		// 只附加稳定 Source TargetHandle、一次性事件 ID 与 Damage Ledger 数值。
		Projectile->ConfigureServerDamageLedger(*Ship, Damage);
		Projectile->FinishSpawning(MuzzleTransform);
	}
}

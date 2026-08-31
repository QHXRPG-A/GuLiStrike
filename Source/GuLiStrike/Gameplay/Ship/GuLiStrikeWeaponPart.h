// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeWeaponPart.generated.h"

class AGuLiStrikeProjectile;

/**
 *  武器部件：安装期间从自身变换位置发射投射物。
 *  射速冷却与出弹逻辑由部件自己实现。
 */
UCLASS(abstract)
class UGuLiStrikeWeaponPart : public UGuLiStrikeShipPartComponent
{
	GENERATED_BODY()

public:

	/** 投射物伤害值（供后续伤害系统与 UI 使用） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon", meta=(ClampMin = 0))
	float Damage = 10.0f;

	/** 按住开火键时的每秒发射次数 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon", meta=(ClampMin = 0.01))
	float FireRate = 3.0f;

	/** 本部件要生成的投射物类 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon")
	TSubclassOf<AGuLiStrikeProjectile> ProjectileClass;

	/** 相对本部件的炮口偏移（部件的 +X 是它的射击方向） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon")
	FVector MuzzleOffset = FVector(100.0f, 0.0f, 0.0f);

	/** 上一次射击的世界时间（部件自己执行射速冷却） */
	float LastFireTime = -TNumericLimits<float>::Max();

protected:

	/** 服务器本地部件事件（不是 RPC）：检查当前 Air Pawn/已装部件资格，自查冷却，从服务器炮口出弹。 */
	virtual void Fire_Implementation(AActor* Instigator) override;
};

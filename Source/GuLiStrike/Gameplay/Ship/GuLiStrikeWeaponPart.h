// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeWeaponPart.generated.h"

/**
 *  武器部件的安装与视觉挂点容器。
 */
UCLASS(abstract)
class UGuLiStrikeWeaponPart : public UGuLiStrikeShipPartComponent
{
	GENERATED_BODY()

public:

	/** 相对本部件的炮口偏移（部件的 +X 是它的射击方向） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon")
	FVector MuzzleOffset = FVector(100.0f, 0.0f, 0.0f);

	/** 可选的视觉网格炮口 Socket；空时保留旧 MuzzleOffset 行为。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon")
	FName MuzzleSocketName;

	/** 炮口相对于部件安装根；不读取可能带网络平滑偏移的世界姿态。 */
	UFUNCTION(BlueprintPure, Category="Ship Part|Weapon")
	bool GetMuzzleTransformRelativeToPart(FTransform& OutTransform) const;

};

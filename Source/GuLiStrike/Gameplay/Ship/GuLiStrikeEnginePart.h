// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeEnginePart.generated.h"

/**
 *  引擎部件：提供推力并增加质量。
 *  飞船的极速与加速度由全部引擎的总推重比决定。
 */
UCLASS(abstract)
class UGuLiStrikeEnginePart : public UGuLiStrikeShipPartComponent
{
	GENERATED_BODY()

public:

	/** 为飞船贡献的推力 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Engine", meta=(ClampMin = 0))
	float Thrust = 600.0f;

protected:

	/** 引擎贡献推力（质量由基类贡献；蓝图子类可再重写） */
	virtual void ContributeStats_Implementation(FGuLiStrikeShipStats& OutStats) override;
};

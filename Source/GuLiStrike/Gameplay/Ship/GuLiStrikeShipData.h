// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeShipData.generated.h"

class AGuLiStrikeProjectile;

/**
 *  部件数值表（DT_ShipParts）的行结构。
 *  一行对应一个部件蓝图，RowName = 部件的 PartId（一般为蓝图资产名）。
 *  引擎行填 Thrust，武器行填 Damage/FireRate/MuzzleOffset/ProjectileClass，
 *  用不到的列保持默认值即可；网格、挂接变换与槽位兼容性仍配在部件蓝图上。
 *  安装部件时由 AGuLiStrikeShip::ApplyPartRow 查行覆盖实例数值。
 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipPartRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 安装后为飞船增加的质量 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part", meta=(ClampMin = 0, Units = "kg"))
	float PartMass = 10.0f;

	/** 配装/调试界面显示的部件名 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part")
	FText PartDisplayName;

	/** 引擎：为飞船贡献的推力 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part|Engine", meta=(ClampMin = 0))
	float Thrust = 600.0f;

	/** 武器：投射物伤害值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part|Weapon", meta=(ClampMin = 0))
	float Damage = 10.0f;

	/** 武器：按住开火键时的每秒发射次数 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part|Weapon", meta=(ClampMin = 0.01))
	float FireRate = 3.0f;

	/** 武器：相对本部件的炮口偏移（部件的 +X 是它的射击方向） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part|Weapon")
	FVector MuzzleOffset = FVector(100.0f, 0.0f, 0.0f);

	/** 武器：要生成的投射物类（软引用，安装部件时同步加载；空 = 沿用部件蓝图配置） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship Part|Weapon")
	TSoftClassPtr<AGuLiStrikeProjectile> ProjectileClass;
};

/**
 *  飞船调参表（DT_ShipTuning）的行结构。
 *  一行是一套完整飞行手感预设，RowName = 预设名（如 Default）。
 *  字段与 AGuLiStrikeShip 的同名 UPROPERTY 一一对应，
 *  出生时由 AGuLiStrikeShip::ApplyTuningRow 整行覆盖。
 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipTuningRow : public FTableRowBase
{
	GENERATED_BODY()

	// ---- Stats：推重比与极速推导 ----

	/** 裸舰体质量（不含任何部件） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float HullMass = 100.0f;

	/** 标称推重比对应的极速 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 0))
	float BaseMaxSpeed = 1200.0f;

	/** 标称推重比对应的加速度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 0))
	float BaseAcceleration = 400.0f;

	/** 映射到基础飞行性能的推重比 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 0.01))
	float NominalThrustRatio = 6.0f;

	/** 推重比速度倍率的下限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 0.1))
	float SpeedMultiplierMin = 0.5f;

	/** 推重比速度倍率的上限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float SpeedMultiplierMax = 2.0f;

	/** 按住加力键时的额外推力倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float BoostThrustMultiplier = 2.0f;

	// ---- Handling：操纵手感 ----

	/** 鼠标 Y 增量每单位对应的相机俯仰角度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling")
	float MousePitchScale = 1.0f;

	/** 鼠标 X 增量每单位对应的相机偏航角度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling")
	float MouseYawScale = 1.0f;

	/** 偏航最大角速度（度/秒，Q/E） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float YawRate = 40.0f;

	/** 偏航响应速度：按键后角速度爬升到目标的快慢 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = 0.1))
	float YawResponseSpeed = 3.0f;

	/** 偏航惯性衰减速度：松键后角速度归零的快慢 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = 0.05))
	float YawStopDamping = 1.0f;

	/** 转向时机身向转弯侧的倾斜角（压弯效果） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = 0, ClampMax = 45))
	float MaxBankAngle = 5.0f;

	/** 倾斜回正的插值速度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = 0.1))
	float BankInterpSpeed = 4.0f;

	/** 自动转向的插值速度（越小转向越沉稳） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = 0.1))
	float OrientTurnSpeed = 2.5f;

	/** 推进时是否自动转向推进方向 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling")
	bool bOrientToMovement = false;

	/** 推进意图与船头夹角余弦低于此值时不自动转向 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling", meta=(ClampMin = -1, ClampMax = 1))
	float OrientMinForwardDot = 0.3f;

	/** 相机臂俯仰下限（度） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling")
	float CameraPitchMin = -80.0f;

	/** 相机臂俯仰上限（度） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Handling")
	float CameraPitchMax = 80.0f;

	/** 舰体网格体偏移，让飞船几何中心对齐 Actor 原点 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship|Components")
	FVector HullMeshOffset = FVector(0.0f, 0.0f, 700.0f);
};

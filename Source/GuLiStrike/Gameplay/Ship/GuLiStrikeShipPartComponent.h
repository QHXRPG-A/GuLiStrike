// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GuLiStrikeShipPartComponent.generated.h"

class AGuLiStrikeShip;

/** 装配统计聚合：各部件把自己的数值贡献进来，飞船只做汇总与应用 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipStats
{
	GENERATED_BODY()

	/** 全部部件的质量和 */
	float PartMassSum = 0.0f;

	/** 全部引擎部件的推力和 */
	float ThrustSum = 0.0f;
};

/**
 *  所有可安装飞船部件的基类。
 *  具体部件由蓝图子类配置：网格、数值，以及允许接入的舰体 socket 集合。
 *  部件行为（数值贡献、开火等）写在部件类里，装上即拥有；
 *  ContributeStats/Fire 为 BlueprintNativeEvent——蓝图子类可直接重写实现新行为。
 */
UCLASS(abstract)
class UGuLiStrikeShipPartComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:

	/** 部件数值表的行键（一般 = 部件蓝图资产名）；空 = 不走数据表，安装时沿用蓝图默认数值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FName PartId;

	/** 本部件允许安装的舰体 socket 名集合（一个部件可接入多个槽位） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	TArray<FName> CompatibleSockets;

	/** 挂接到 socket 之后叠加的变换微调（位置/朝向/缩放） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FTransform PartRelativeTransform;

	/** 安装后为飞船增加的质量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part", meta=(ClampMin = 0, Units = "kg"))
	float PartMass = 10.0f;

	/** 配装/调试界面显示的部件名 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FText PartDisplayName;

	/** 构造函数 */
	UGuLiStrikeShipPartComponent();

	/** 本部件是否允许安装在指定的舰体 socket 上 */
	UFUNCTION(BlueprintPure, Category="Ship Part")
	bool CanAttachToSocket(FName SocketName) const;

	/** 把本部件的数值贡献进聚合统计（C++ 默认贡献质量；蓝图/C++ 子类重写叠加自己的贡献） */
	UFUNCTION(BlueprintNativeEvent, Category="Ship Part")
	void ContributeStats(UPARAM(ref) FGuLiStrikeShipStats& OutStats);

	/** 响应开火输入（C++ 默认不响应；武器子类重写为出弹）——行为属于组件，装上即拥有 */
	UFUNCTION(BlueprintNativeEvent, Category="Ship Part")
	void Fire(AActor* Instigator);

protected:

	/** 部件销毁时通知宿主飞船同步注册表（部件可能被外部直接销毁，如部件击毁玩法） */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
};

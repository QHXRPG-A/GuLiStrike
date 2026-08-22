// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeShip.h"
#include "GuLiStrike.h"

UGuLiStrikeShipPartComponent::UGuLiStrikeShipPartComponent()
{
	// 部件是纯视觉件：飞船的碰撞由舰体胶囊提供
	SetCollisionProfileName(FName("NoCollision"));
	SetGenerateOverlapEvents(false);
}

bool UGuLiStrikeShipPartComponent::CanAttachToSocket(FName SocketName) const
{
	return CompatibleSockets.Contains(SocketName);
}

void UGuLiStrikeShipPartComponent::ContributeStats_Implementation(FGuLiStrikeShipStats& OutStats)
{
	// 基类贡献：所有部件都增加质量
	OutStats.PartMassSum += PartMass;
}

void UGuLiStrikeShipPartComponent::Fire_Implementation(AActor* Instigator)
{
	// 非武器部件不响应开火输入；蓝图子类可重写实现自己的行为
}

void UGuLiStrikeShipPartComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// 通知宿主飞船同步注册表（绕过 UninstallPart 的销毁路径由此兜底）
	if (AGuLiStrikeShip* Ship = GetOwner<AGuLiStrikeShip>())
	{
		Ship->NotifyPartDestroyed(this);
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

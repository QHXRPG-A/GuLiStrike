// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeEnginePart.h"

void UGuLiStrikeEnginePart::ContributeStats_Implementation(FGuLiStrikeShipStats& OutStats)
{
	Super::ContributeStats_Implementation(OutStats);

	// 引擎额外贡献推力
	OutStats.ThrustSum += Thrust;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commander/Framework/GuLiCommanderGameMode.h"
#include "GuLiShipTestGameMode.generated.h"

/** 飞船测试预设：只调整角色优先顺序，登录、席位、复活和比赛阶段均继承公共实现。 */
UCLASS()
class GULISTRIKE_API AGuLiShipTestGameMode : public AGuLiCommanderGameMode
{
	GENERATED_BODY()

public:
	AGuLiShipTestGameMode();
};

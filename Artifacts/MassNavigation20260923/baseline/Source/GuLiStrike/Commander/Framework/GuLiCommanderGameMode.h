// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "GuLiCommanderGameMode.generated.h"

class UGuLiCommanderWorldReplicationComponent;

/** 兼容原地图/蓝图的战局配置：公共登录、角色和复活继承 BattleGameMode，士兵发布交给专门组件。 */
UCLASS()
class AGuLiCommanderGameMode : public AGuLiBattleGameMode
{
	GENERATED_BODY()

public:
	AGuLiCommanderGameMode();

private:
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TObjectPtr<UGuLiCommanderWorldReplicationComponent> WorldReplicationComponent;
};

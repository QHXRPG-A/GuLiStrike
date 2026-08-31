// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GuLiCommanderPlayerState.generated.h"

/**
 * 指挥官旧反射路径的兼容类；身份、角色、两种就绪位及旧 API 均继承自公共 BattlePlayerState。
 * 新玩法直接使用 BattlePlayerState，无需为了复用身份而依赖 Commander 类型。
 */
UCLASS()
class AGuLiCommanderPlayerState : public AGuLiBattlePlayerState
{
	GENERATED_BODY()
};

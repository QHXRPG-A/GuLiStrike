// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GuLiBattleTypes.generated.h"

/** 战局连接的公共协议版本；不等于 MatchEpoch、快照版本或每连接同步代次。 */
inline constexpr uint16 GULI_BATTLE_PROTOCOL_VERSION = 6u;

/** 历史指挥官协议常量保留别名，现有士兵 RPC/序列化仍使用原名称与数值。 */
inline constexpr uint16 GULI_COMMANDER_PROTOCOL_VERSION = GULI_BATTLE_PROTOCOL_VERSION;

/** 战局内通用阵营。反射名称及枚举值保持不变，现有蓝图与存量协议无需改名。 */
UENUM(BlueprintType)
enum class EGuLiTeam : uint8
{
	Unassigned = 0,
	Red,
	Blue
};

/** 通用参战角色；Commander 旧类型名仅为反射兼容，Ground/Air 也使用此枚举。 */
UENUM(BlueprintType)
enum class EGuLiCommanderRole : uint8
{
	Unassigned = 0,
	Commander,
	Ground,
	Air,
	Observer
};

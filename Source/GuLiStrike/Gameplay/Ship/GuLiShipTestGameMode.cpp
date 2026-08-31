// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiShipTestGameMode.h"

AGuLiShipTestGameMode::AGuLiShipTestGameMode()
{
	// 仍通过同一张 5v5 席位表；客户端不能借测试预设指定自己身份。
	InitialRolePriority = {EGuLiCommanderRole::Air, EGuLiCommanderRole::Ground, EGuLiCommanderRole::Commander};
}

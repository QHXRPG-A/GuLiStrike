// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "GuLiCommanderGameMode.generated.h"


/** 兼容原地图/蓝图的战局配置：角色与出生配置继承 BattleGameMode；同步由对象负责人承担。 */
UCLASS()
class AGuLiCommanderGameMode : public AGuLiBattleGameMode
{
	GENERATED_BODY()

public:
	AGuLiCommanderGameMode();

protected:
	virtual UClass* ResolveConfiguredPawnClass(EGuLiCommanderRole AssignedRole) const override;
	/** Keep the Ground Blueprint out of native class construction/module registration. */
	UPROPERTY(EditDefaultsOnly, Category="Battle|Spawning")
	TSoftClassPtr<APawn> DefaultGroundPawnClass;

};

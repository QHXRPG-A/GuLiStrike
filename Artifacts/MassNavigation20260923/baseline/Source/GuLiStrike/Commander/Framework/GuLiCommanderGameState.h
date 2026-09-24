// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "GuLiCommanderGameState.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FGuLiCommanderRuntimeTuningChangedSignature,
	float,
	uint32);

/**
 * 指挥官地图的兼容 GameState：身份、战局和席位复制由 Battle 基类统一维护。
 * 本类只额外发布士兵表现预测使用的已提交速度；保留旧 UCLASS 路径及访问 API。
 */
UCLASS()
class AGuLiCommanderGameState : public AGuLiBattleGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器本地发布已提交士兵速度（cm/s）及调参版本，不是客户端调参 RPC。 */
	void SetAuthoritativeSoldierMovementTuning(float EffectiveMoveSpeedCmPerSecond, uint32 TuningRevision);

	UFUNCTION(BlueprintPure, Category = "Commander|Tuning")
	float GetEffectiveSoldierMoveSpeedCmPerSecond() const
	{
		return EffectiveSoldierMoveSpeedCmPerSecond;
	}

	uint32 GetRuntimeTuningRevision() const { return RuntimeTuningRevision; }

	// 本进程通知：服务器 setter 与客户端 RepNotify 都会广播，不是 NetMulticast。
	FGuLiCommanderRuntimeTuningChangedSignature OnRuntimeTuningChanged;

private:
	UFUNCTION()
	void OnRep_RuntimeTuning();

	UPROPERTY(ReplicatedUsing = OnRep_RuntimeTuning)
	float EffectiveSoldierMoveSpeedCmPerSecond = 720.0f;

	// 两个独立复制属性；客户端不得将两个回调顺序当作原子事务。
	UPROPERTY(ReplicatedUsing = OnRep_RuntimeTuning)
	uint32 RuntimeTuningRevision = 0u;
};

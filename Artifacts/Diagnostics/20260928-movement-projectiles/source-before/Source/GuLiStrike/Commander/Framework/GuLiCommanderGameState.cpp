// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderGameState.h"

#include "Net/UnrealNetwork.h"

void AGuLiCommanderGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiCommanderGameState, EffectiveSoldierMoveSpeedCmPerSecond);
	DOREPLIFETIME(AGuLiCommanderGameState, RuntimeTuningRevision);
}

void AGuLiCommanderGameState::SetAuthoritativeSoldierMovementTuning(
	const float EffectiveMoveSpeedCmPerSecond,
	const uint32 TuningRevision)
{
	if (!HasAuthority() || !FMath::IsFinite(EffectiveMoveSpeedCmPerSecond)
		|| EffectiveMoveSpeedCmPerSecond <= 0.0f)
	{
		return;
	}

	this->EffectiveSoldierMoveSpeedCmPerSecond = EffectiveMoveSpeedCmPerSecond;
	RuntimeTuningRevision = TuningRevision;
	OnRuntimeTuningChanged.Broadcast(
		this->EffectiveSoldierMoveSpeedCmPerSecond,
		RuntimeTuningRevision);
	ForceNetUpdate();
}

// 仅士兵调参的本地消费入口；公共席位和战局复制仍由基类处理。
void AGuLiCommanderGameState::OnRep_RuntimeTuning()
{
	OnRuntimeTuningChanged.Broadcast(
		EffectiveSoldierMoveSpeedCmPerSecond,
		RuntimeTuningRevision);
}

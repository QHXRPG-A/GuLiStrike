// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderGameState.h"

#include "Net/UnrealNetwork.h"

void AGuLiCommanderGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiCommanderGameState, EffectiveSoldierMoveSpeedCmPerSecond);
	DOREPLIFETIME(AGuLiCommanderGameState, RuntimeTuningRevision);
	DOREPLIFETIME(AGuLiCommanderGameState, UnitMovementMultipliers);
}

float AGuLiCommanderGameState::GetEffectiveUnitMoveSpeedCmPerSecond(EGuLiTeam Team, uint16 UnitTypeId) const
{
	const auto* Entry = UnitMovementMultipliers.FindByPredicate(
		[Team, UnitTypeId](const FGuLiCommanderUnitMovementMultiplier& Row)
		{ return Row.Team == Team && Row.UnitTypeId == UnitTypeId; });
	const float Speed = EffectiveSoldierMoveSpeedCmPerSecond * (Entry ? Entry->Multiplier : 1.0f);
	return FMath::IsFinite(Speed) && Speed > 0.0f ? Speed : EffectiveSoldierMoveSpeedCmPerSecond;
}

void AGuLiCommanderGameState::SetAuthoritativeUnitMovementMultiplier(EGuLiTeam Team, uint16 UnitTypeId, float Multiplier)
{
	if (!HasAuthority() || (Team != EGuLiTeam::Red && Team != EGuLiTeam::Blue)
		|| UnitTypeId == 0 || !FMath::IsFinite(Multiplier) || Multiplier <= 0.0f) return;
	auto* Entry = UnitMovementMultipliers.FindByPredicate(
		[Team, UnitTypeId](const FGuLiCommanderUnitMovementMultiplier& Row)
		{ return Row.Team == Team && Row.UnitTypeId == UnitTypeId; });
	if (Entry && Entry->Multiplier == Multiplier) return;
	if (!Entry)
	{
		Entry = &UnitMovementMultipliers.AddDefaulted_GetRef();
		Entry->Team = Team;
		Entry->UnitTypeId = UnitTypeId;
	}
	Entry->Multiplier = Multiplier;
	ForceNetUpdate();
}

void AGuLiCommanderGameState::ResetAuthoritativeUnitMovementMultipliers()
{
	if (!HasAuthority() || UnitMovementMultipliers.IsEmpty()) return;
	UnitMovementMultipliers.Reset();
	ForceNetUpdate();
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

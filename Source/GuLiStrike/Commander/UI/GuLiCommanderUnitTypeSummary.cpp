// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/UI/GuLiCommanderUnitTypeSummary.h"

namespace GuLiCommanderUnitTypeSummary
{
	enum class ECommandStatus : uint8
	{
		Executing,
		Received,
		PartiallyReceived,
		Idle,
		NoSelection,
		InvalidTarget,
		PathFailed,
		RateLimited,
		Unauthorized,
		SelectionChanged,
		Failed,
		Count
	};

	const TCHAR* StatusText(const ECommandStatus Status)
	{
		switch (Status)
		{
		case ECommandStatus::Executing: return TEXT("执行中");
		case ECommandStatus::Received: return TEXT("已接收");
		case ECommandStatus::PartiallyReceived: return TEXT("部分接收");
		case ECommandStatus::Idle: return TEXT("待命");
		case ECommandStatus::NoSelection: return TEXT("无选中");
		case ECommandStatus::InvalidTarget: return TEXT("目标无效");
		case ECommandStatus::PathFailed: return TEXT("寻路失败");
		case ECommandStatus::RateLimited: return TEXT("请求过快");
		case ECommandStatus::Unauthorized: return TEXT("未授权");
		case ECommandStatus::SelectionChanged: return TEXT("选择已变");
		default: return TEXT("指令失败");
		}
	}

	ECommandStatus ResolveCommandStatus(
		const FGuLiSoldierStateItem& Soldier,
		const EGuLiCommandAckResult* AckResult)
	{
		if (!AckResult)
		{
			return Soldier.ActiveOrderId != 0u ? ECommandStatus::Executing : ECommandStatus::Idle;
		}
		switch (*AckResult)
		{
		case EGuLiCommandAckResult::Accepted:
			return Soldier.ActiveOrderId != 0u ? ECommandStatus::Executing : ECommandStatus::Received;
		case EGuLiCommandAckResult::PartiallyAccepted: return ECommandStatus::PartiallyReceived;
		case EGuLiCommandAckResult::NoSelection: return ECommandStatus::NoSelection;
		case EGuLiCommandAckResult::InvalidTarget: return ECommandStatus::InvalidTarget;
		case EGuLiCommandAckResult::PathFailed: return ECommandStatus::PathFailed;
		case EGuLiCommandAckResult::RateLimited: return ECommandStatus::RateLimited;
		case EGuLiCommandAckResult::Unauthorized: return ECommandStatus::Unauthorized;
		case EGuLiCommandAckResult::StaleSelectionRevision: return ECommandStatus::SelectionChanged;
		default: return ECommandStatus::Failed;
		}
	}
}

float FGuLiCommanderUnitTypeSummary::GetHealthFraction() const
{
	return !bSyncing && FMath::IsFinite(TotalHealth) && FMath::IsFinite(TotalMaxHealth)
		&& TotalMaxHealth > 0.0f
		? FMath::Clamp(TotalHealth / TotalMaxHealth, 0.0f, 1.0f)
		: 0.0f;
}

FGuLiCommanderUnitTypeSummary BuildGuLiCommanderUnitTypeSummary(
	const FGuLiCommanderSelectionState& Selection,
	const FGuLiCommandAck& Ack,
	const TConstArrayView<FGuLiSoldierStateItem> SoldierStates,
	const bool bReliableStateReady)
{
	using namespace GuLiCommanderUnitTypeSummary;
	FGuLiCommanderUnitTypeSummary Summary;
	TMap<FGuLiSoldierId, FGuLiControlCohortId> PendingMembers;
	PendingMembers.Reserve(Selection.Cohorts.Num() * static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));
	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
		{
			if (SoldierId.IsValid() && !PendingMembers.Contains(SoldierId))
			{
				PendingMembers.Add(SoldierId, Cohort.CohortId);
			}
		}
	}
	Summary.SelectedCount = PendingMembers.Num();
	if (Summary.SelectedCount == 0)
	{
		return Summary;
	}
	if (!bReliableStateReady)
	{
		Summary.bSyncing = true;
		Summary.CommandStatus = FText::FromString(TEXT("同步中"));
		return Summary;
	}

	// 仅把当前选择版本的移动 ACK 用作反馈；其他版本/选兵回执不能污染当前兵种卡片。
	const bool bHasCurrentMoveAck = Ack.CommandKind == EGuLiCommandKind::Move
		&& Ack.ServerSelectionRevision == Selection.SelectionRevision;
	TMap<FGuLiControlCohortId, EGuLiCommandAckResult> CohortResults;
	if (bHasCurrentMoveAck)
	{
		CohortResults.Reserve(Ack.CohortResults.Num());
		for (const FGuLiCohortCommandAck& CohortAck : Ack.CohortResults)
		{
			CohortResults.Add(CohortAck.CohortId, CohortAck.Result);
		}
	}

	int32 StatusCounts[static_cast<int32>(ECommandStatus::Count)] = {};
	for (const FGuLiSoldierStateItem& Soldier : SoldierStates)
	{
		FGuLiControlCohortId CohortId;
		// Removal also prevents any repeated snapshot identity from being counted twice.
		if (!PendingMembers.RemoveAndCopyValue(Soldier.SoldierId, CohortId))
		{
			continue;
		}
		if (!FMath::IsFinite(Soldier.Health) || !FMath::IsFinite(Soldier.MaxHealth)
			|| Soldier.MaxHealth <= 0.0f || Soldier.Health < 0.0f || Soldier.Health > Soldier.MaxHealth)
		{
			Summary.bSyncing = true;
			continue;
		}
		if (!Soldier.IsAlive())
		{
			continue;
		}
		++Summary.AliveCount;
		if (Summary.UnitTypeId == 0) Summary.UnitTypeId = Soldier.UnitTypeId;
		else if (Summary.UnitTypeId != Soldier.UnitTypeId) Summary.bMixedUnitTypes = true;
		Summary.TotalHealth += Soldier.Health;
		Summary.TotalMaxHealth += Soldier.MaxHealth;

		const EGuLiCommandAckResult* EffectiveResult = nullptr;
		if (bHasCurrentMoveAck)
		{
			EffectiveResult = CohortResults.Find(CohortId);
			if (!EffectiveResult)
			{
				EffectiveResult = &Ack.Result;
			}
		}
		++StatusCounts[static_cast<int32>(ResolveCommandStatus(Soldier, EffectiveResult))];
	}
	Summary.bSyncing |= !PendingMembers.IsEmpty();
	if (Summary.bSyncing)
	{
		Summary.CommandStatus = FText::FromString(TEXT("同步中"));
		return Summary;
	}

	int32 DistinctStatusCount = 0;
	for (const int32 Count : StatusCounts)
	{
		DistinctStatusCount += Count > 0 ? 1 : 0;
	}
	TArray<FString> StatusParts;
	StatusParts.Reserve(DistinctStatusCount);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(StatusCounts); ++Index)
	{
		if (StatusCounts[Index] > 0)
		{
			const TCHAR* Label = StatusText(static_cast<ECommandStatus>(Index));
			StatusParts.Add(DistinctStatusCount == 1
				? FString(Label)
				: FString::Printf(TEXT("%s %d"), Label, StatusCounts[Index]));
		}
	}
	Summary.CommandStatus = FText::FromString(FString::Join(StatusParts, TEXT(" · ")));
	return Summary;
}

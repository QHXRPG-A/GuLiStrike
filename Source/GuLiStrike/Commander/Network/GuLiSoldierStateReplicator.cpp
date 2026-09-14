// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Network/GuLiSoldierStateReplicator.h"

#include "Net/UnrealNetwork.h"

AGuLiSoldierStateReplicator::AGuLiSoldierStateReplicator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = true;
	SetReplicateMovement(false);
	// Actor 更新频率是复制调度参数；不等于名册每秒必变 20 次，更不保证每客户端收包 20 Hz。
	SetNetUpdateFrequency(20.0f);
	SetMinNetUpdateFrequency(2.0f);
}

void AGuLiSoldierStateReplicator::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiSoldierStateReplicator, ReplicatedSoldiers);
	DOREPLIFETIME(AGuLiSoldierStateReplicator, SnapshotMatchEpoch);
	DOREPLIFETIME(AGuLiSoldierStateReplicator, SnapshotRevision);
}

int32 AGuLiSoldierStateReplicator::ApplyAuthoritySnapshot(
	const TConstArrayView<FGuLiSoldierStateItem> InStates,
	const uint32 MatchEpoch)
{
	if (!HasAuthority() || MatchEpoch == 0u)
	{
		return 0;
	}
	const bool bEpochChanged = SnapshotMatchEpoch != MatchEpoch;
	if (bEpochChanged)
	{
		SnapshotMatchEpoch = MatchEpoch;
		// Keep the RepNotify high-water mark monotonic across matches so a client
		// can never miss an epoch transition because the replicated value repeats.
	}

	TSet<FGuLiSoldierId> SnapshotIds;
	SnapshotIds.Reserve(InStates.Num());
	int32 ChangedItemCount = 0;

	for (const FGuLiSoldierStateItem& InState : InStates)
	{
		FGuLiSoldierStateItem SanitizedState = InState;
		SanitizedState.Sanitize();
		if (!SanitizedState.SoldierId.IsValid()
			|| SnapshotIds.Contains(SanitizedState.SoldierId))
		{
			continue;
		}

		SnapshotIds.Add(SanitizedState.SoldierId);
		const int32 ExistingIndex = FindItemIndex(SanitizedState.SoldierId);
		if (ExistingIndex == INDEX_NONE)
		{
			FGuLiSoldierStateItem& NewItem = ReplicatedSoldiers.Items.AddDefaulted_GetRef();
			NewItem.SoldierId = SanitizedState.SoldierId;
			NewItem.Team = SanitizedState.Team;
			NewItem.UnitTypeId = SanitizedState.UnitTypeId;
			NewItem.LifeState = SanitizedState.LifeState;
			NewItem.Health = SanitizedState.Health;
			NewItem.MaxHealth = SanitizedState.MaxHealth;
			NewItem.StateRevision = SanitizedState.StateRevision;
			NewItem.ActiveOrderId = SanitizedState.ActiveOrderId;
		NewItem.bPhased = SanitizedState.bPhased;
		NewItem.bExternalActionsLocked = SanitizedState.bExternalActionsLocked;
		NewItem.DisplacementFrameFloor = SanitizedState.DisplacementFrameFloor;
		NewItem.DisplacementLocation = SanitizedState.DisplacementLocation;
		NewItem.DisplacementYaw = SanitizedState.DisplacementYaw;
		NewItem.DisplacementSimulationTime = SanitizedState.DisplacementSimulationTime;

			// 新增条目需 MarkItemDirty 分配/更新 FastArray 复制标识；业务 SoldierId 与内部复制 ID 不同。
			ReplicatedSoldiers.MarkItemDirty(NewItem);
			++ChangedItemCount;
			continue;
		}

		FGuLiSoldierStateItem& ExistingItem = ReplicatedSoldiers.Items[ExistingIndex];
		if (ExistingItem.Team == SanitizedState.Team
			&& ExistingItem.UnitTypeId == SanitizedState.UnitTypeId
			&& ExistingItem.LifeState == SanitizedState.LifeState
			&& ExistingItem.Health == SanitizedState.Health
			&& ExistingItem.MaxHealth == SanitizedState.MaxHealth
			&& ExistingItem.StateRevision == SanitizedState.StateRevision
			&& ExistingItem.ActiveOrderId == SanitizedState.ActiveOrderId)
		{
			continue;
		}

		ExistingItem.Team = SanitizedState.Team;
		ExistingItem.UnitTypeId = SanitizedState.UnitTypeId;
		ExistingItem.LifeState = SanitizedState.LifeState;
		ExistingItem.Health = SanitizedState.Health;
		ExistingItem.MaxHealth = SanitizedState.MaxHealth;
		ExistingItem.StateRevision = SanitizedState.StateRevision;
		ExistingItem.ActiveOrderId = SanitizedState.ActiveOrderId;
		ExistingItem.bPhased = SanitizedState.bPhased;
		ExistingItem.bExternalActionsLocked = SanitizedState.bExternalActionsLocked;
		ExistingItem.DisplacementFrameFloor = SanitizedState.DisplacementFrameFloor;
		ExistingItem.DisplacementLocation = SanitizedState.DisplacementLocation;
		ExistingItem.DisplacementYaw = SanitizedState.DisplacementYaw;
		ExistingItem.DisplacementSimulationTime = SanitizedState.DisplacementSimulationTime;

		// 只给变化条目标脏，静止不变的离散状态不随每次姿态捕获重复发送。
		ReplicatedSoldiers.MarkItemDirty(ExistingItem);
		++ChangedItemCount;
	}

	bool bRemovedAny = false;
	for (int32 Index = ReplicatedSoldiers.Items.Num() - 1; Index >= 0; --Index)
	{
		if (SnapshotIds.Contains(ReplicatedSoldiers.Items[Index].SoldierId))
		{
			continue;
		}

		ReplicatedSoldiers.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		bRemovedAny = true;
		++ChangedItemCount;
	}

	if (bRemovedAny)
	{
		// 删除改变集合结构，必须标记数组脏；只修改 TArray 本身不足以告知 FastArray 增量系统。
		ReplicatedSoldiers.MarkArrayDirty();
	}
	if (ChangedItemCount > 0 || bEpochChanged)
	{
		++SnapshotRevision;
		if (SnapshotRevision == 0u)
		{
			++SnapshotRevision;
		}
		ForceNetUpdate();
		OnSoldierStatesChanged.Broadcast(SnapshotRevision);
	}
	return ChangedItemCount;
}

// 客户端版本到达后广播本地通知；不能由此推导所有相关复制字段已原子到齐。
void AGuLiSoldierStateReplicator::OnRep_SnapshotRevision()
{
	OnSoldierStatesChanged.Broadcast(SnapshotRevision);
}

const FGuLiSoldierStateItem* AGuLiSoldierStateReplicator::FindSoldierState(
	const FGuLiSoldierId SoldierId) const
{
	const int32 Index = FindItemIndex(SoldierId);
	return Index != INDEX_NONE ? &ReplicatedSoldiers.Items[Index] : nullptr;
}

TArray<FGuLiSoldierStateItem> AGuLiSoldierStateReplicator::GetAllSoldierStates() const
{
	return ReplicatedSoldiers.Items;
}

int32 AGuLiSoldierStateReplicator::FindItemIndex(const FGuLiSoldierId SoldierId) const
{
	return ReplicatedSoldiers.Items.IndexOfByPredicate(
		[SoldierId](const FGuLiSoldierStateItem& Item)
		{
			return Item.SoldierId == SoldierId;
		});
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Network/GuLiSoldierStateReplicator.h"


AGuLiSoldierStateReplicator::AGuLiSoldierStateReplicator()
{
	bReplicates = false;
	SetReplicateMovement(false);
}



int32 AGuLiSoldierStateReplicator::ApplyAuthoritySnapshot(TConstArrayView<FGuLiSoldierStateItem> States, uint32 Epoch)
{ return ApplyAuthorityState(States,Epoch,true); }
int32 AGuLiSoldierStateReplicator::ApplyAuthorityDelta(TConstArrayView<FGuLiSoldierStateItem> States, uint32 Epoch)
{ return ApplyAuthorityState(States,Epoch,false); }

int32 AGuLiSoldierStateReplicator::ApplyAuthorityState(
	const TConstArrayView<FGuLiSoldierStateItem> InStates,
	const uint32 MatchEpoch, const bool bComplete)
{
	if (!bComplete && SnapshotMatchEpoch != MatchEpoch) return 0;
	if (MatchEpoch == 0u)
	{
		return 0;
	}
	const bool bEpochChanged = SnapshotMatchEpoch != MatchEpoch;
	if (bEpochChanged)
	{
		SnapshotMatchEpoch = MatchEpoch;
		// can never miss an epoch transition because the replicated value repeats.
	}

	TSet<FGuLiSoldierId> SnapshotIds;
	SnapshotIds.Reserve(InStates.Num());
	int32 ChangedItemCount = 0;
	TArray<FGuLiSoldierStateItem> ChangedStates;

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
		const int32* KnownIndex = AuthorityItemIndices.Find(SanitizedState.SoldierId);
		const int32 ExistingIndex = KnownIndex ? *KnownIndex : INDEX_NONE;
		if (ExistingIndex == INDEX_NONE)
		{
			FGuLiSoldierStateItem& NewItem = ReplicatedSoldiers.Items.AddDefaulted_GetRef();
			AuthorityItemIndices.Add(SanitizedState.SoldierId,ReplicatedSoldiers.Items.Num()-1);
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

			ChangedStates.Add(NewItem);
			++ChangedItemCount;
			continue;
		}

		// DisplacementYaw is filled from live facing by the authority snapshot.
		// Ordinary turning must not dirty the reliable roster; a new displacement
		// frame floor carries its yaw with the rest of that discrete event.
		FGuLiSoldierStateItem& ExistingItem = ReplicatedSoldiers.Items[ExistingIndex];
		if (ExistingItem.Team == SanitizedState.Team
			&& ExistingItem.UnitTypeId == SanitizedState.UnitTypeId
			&& ExistingItem.LifeState == SanitizedState.LifeState
			&& ExistingItem.Health == SanitizedState.Health
			&& ExistingItem.MaxHealth == SanitizedState.MaxHealth
			&& ExistingItem.StateRevision == SanitizedState.StateRevision
			&& ExistingItem.ActiveOrderId == SanitizedState.ActiveOrderId
			&& ExistingItem.bPhased == SanitizedState.bPhased
			&& ExistingItem.bExternalActionsLocked == SanitizedState.bExternalActionsLocked
			&& ExistingItem.DisplacementFrameFloor == SanitizedState.DisplacementFrameFloor
			&& ExistingItem.DisplacementLocation == SanitizedState.DisplacementLocation
			&& ExistingItem.DisplacementSimulationTime == SanitizedState.DisplacementSimulationTime)
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
		ChangedStates.Add(ExistingItem);
		++ChangedItemCount;
	}

	bool bRemovedAny = false;
	TArray<FGuLiSoldierId> RemovedIds;
	for (int32 Index = bComplete ? ReplicatedSoldiers.Items.Num() - 1 : -1; Index >= 0; --Index)
	{
		if (SnapshotIds.Contains(ReplicatedSoldiers.Items[Index].SoldierId))
		{
			continue;
		}

		RemovedIds.Add(ReplicatedSoldiers.Items[Index].SoldierId);
		AuthorityItemIndices.Remove(ReplicatedSoldiers.Items[Index].SoldierId);
		ReplicatedSoldiers.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		if (ReplicatedSoldiers.Items.IsValidIndex(Index)) AuthorityItemIndices.Add(ReplicatedSoldiers.Items[Index].SoldierId,Index);
		bRemovedAny = true;
		++ChangedItemCount;
	}

	if (bRemovedAny)
	{
		ReplicatedSoldiers.OnRemoved.Broadcast(RemovedIds);
	}
	if (ChangedItemCount > 0 || bEpochChanged)
	{
		ApplyLocalDelta(ChangedStates, RemovedIds);
		++SnapshotRevision;
		if (SnapshotRevision == 0u)
		{
			++SnapshotRevision;
		}
		OnSoldierStatesChanged.Broadcast(SnapshotRevision);
	}
	return ChangedItemCount;
}

const FGuLiSoldierStateItem* AGuLiSoldierStateReplicator::FindSoldierState(
	const FGuLiSoldierId SoldierId) const
{
	return LocalSoldierStates.Find(SoldierId);
}

void AGuLiSoldierStateReplicator::RefreshLocalEpoch()
{
	if (LocalCacheEpoch == SnapshotMatchEpoch) return;
	LocalCacheEpoch = SnapshotMatchEpoch;
	LocalSoldierStates.Reset();
	LocalSoldierStates.Reserve(ReplicatedSoldiers.Items.Num());
	FGuLiSoldierRosterDelta Delta;
	Delta.bReset = true;
	for (const FGuLiSoldierStateItem& State : ReplicatedSoldiers.Items)
	{
		if (!State.SoldierId.IsValid()) continue;
		LocalSoldierStates.Add(State.SoldierId, State);
		Delta.Added.Add(State.SoldierId);
	}
	OnRosterDelta.Broadcast(Delta);
}

void AGuLiSoldierStateReplicator::ApplyLocalDelta(
	const TArray<FGuLiSoldierStateItem>& States, const TArray<FGuLiSoldierId>& Removed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderRoster_ApplyDelta);
	RefreshLocalEpoch();
	FGuLiSoldierRosterDelta Delta;
	for (const FGuLiSoldierId Id : Removed)
	{
		if (LocalSoldierStates.Remove(Id)) Delta.Removed.Add(Id);
	}
	for (const FGuLiSoldierStateItem& State : States)
	{
		if (!State.SoldierId.IsValid()) continue;
		const FGuLiSoldierStateItem* Previous = LocalSoldierStates.Find(State.SoldierId);
		if (!Previous)
		{
			Delta.Added.Add(State.SoldierId);
		}
		else
		{
			EGuLiSoldierStateChange Flags = EGuLiSoldierStateChange::None;
			if (Previous->UnitTypeId != State.UnitTypeId) Flags |= EGuLiSoldierStateChange::Type;
			if (Previous->Team != State.Team) Flags |= EGuLiSoldierStateChange::Team;
			if (Previous->Health != State.Health || Previous->MaxHealth != State.MaxHealth) Flags |= EGuLiSoldierStateChange::Health;
			if (Previous->LifeState != State.LifeState || Previous->IsAlive() != State.IsAlive()) Flags |= EGuLiSoldierStateChange::Life;
			if (Previous->ActiveOrderId != State.ActiveOrderId) Flags |= EGuLiSoldierStateChange::Order;
			if (Previous->bPhased != State.bPhased || Previous->bExternalActionsLocked != State.bExternalActionsLocked) Flags |= EGuLiSoldierStateChange::Phase;
			if (Previous->DisplacementFrameFloor != State.DisplacementFrameFloor
				|| Previous->DisplacementLocation != State.DisplacementLocation
				|| Previous->DisplacementSimulationTime != State.DisplacementSimulationTime) Flags |= EGuLiSoldierStateChange::Displacement;
			if (Flags != EGuLiSoldierStateChange::None) Delta.Changed.FindOrAdd(State.SoldierId) |= Flags;
		}
		LocalSoldierStates.Add(State.SoldierId, State);
	}
	if (!Delta.IsEmpty()) OnRosterDelta.Broadcast(Delta);
}

TArray<FGuLiSoldierStateItem> AGuLiSoldierStateReplicator::GetAllSoldierStates() const
{
	return ReplicatedSoldiers.Items;
}

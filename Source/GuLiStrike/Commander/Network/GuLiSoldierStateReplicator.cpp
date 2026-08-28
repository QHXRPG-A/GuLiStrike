// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Network/GuLiSoldierStateReplicator.h"

#include "Net/UnrealNetwork.h"

AGuLiSoldierStateReplicator::AGuLiSoldierStateReplicator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = true;
	SetReplicateMovement(false);
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
		SnapshotRevision = 0u;
	}

	TSet<FGuLiSoldierId> SnapshotIds;
	SnapshotIds.Reserve(InStates.Num());
	int32 ChangedItemCount = 0;

	for (const FGuLiSoldierStateItem& InState : InStates)
	{
		if (!InState.SoldierId.IsValid() || SnapshotIds.Contains(InState.SoldierId))
		{
			continue;
		}

		SnapshotIds.Add(InState.SoldierId);
		const int32 ExistingIndex = FindItemIndex(InState.SoldierId);
		if (ExistingIndex == INDEX_NONE)
		{
			FGuLiSoldierStateItem& NewItem = ReplicatedSoldiers.Items.AddDefaulted_GetRef();
			NewItem.SoldierId = InState.SoldierId;
			NewItem.Team = InState.Team;
			NewItem.LifeState = InState.LifeState;
			NewItem.Health = InState.Health;
			NewItem.StateRevision = InState.StateRevision;
			NewItem.ActiveOrderId = InState.ActiveOrderId;
			ReplicatedSoldiers.MarkItemDirty(NewItem);
			++ChangedItemCount;
			continue;
		}

		FGuLiSoldierStateItem& ExistingItem = ReplicatedSoldiers.Items[ExistingIndex];
		if (ExistingItem.Team == InState.Team
			&& ExistingItem.LifeState == InState.LifeState
			&& ExistingItem.Health == InState.Health
			&& ExistingItem.StateRevision == InState.StateRevision
			&& ExistingItem.ActiveOrderId == InState.ActiveOrderId)
		{
			continue;
		}

		ExistingItem.Team = InState.Team;
		ExistingItem.LifeState = InState.LifeState;
		ExistingItem.Health = InState.Health;
		ExistingItem.StateRevision = InState.StateRevision;
		ExistingItem.ActiveOrderId = InState.ActiveOrderId;
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
	}
	return ChangedItemCount;
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

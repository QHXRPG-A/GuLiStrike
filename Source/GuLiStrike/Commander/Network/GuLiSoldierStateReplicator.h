// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/Info.h"
#include "GuLiSoldierStateReplicator.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiSoldierStatesChangedSignature, uint32);

enum class EGuLiSoldierStateChange : uint8
{
	None = 0, Type = 1, Team = 2, Health = 4, Life = 8,
	Order = 16, Displacement = 32, Phase = 64, All = 127
};
ENUM_CLASS_FLAGS(EGuLiSoldierStateChange);

/** Native derived state only; never serialized or used as authority. */
struct FGuLiSoldierRosterDelta
{
	TArray<FGuLiSoldierId> Added;
	TArray<FGuLiSoldierId> Removed;
	TMap<FGuLiSoldierId, EGuLiSoldierStateChange> Changed;
	bool bReset = false;
	bool IsEmpty() const { return !bReset && Added.IsEmpty() && Removed.IsEmpty() && Changed.IsEmpty(); }
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiSoldierRosterDeltaSignature, const FGuLiSoldierRosterDelta&);

/**
 * Public local roster read model, populated from owned entities and P2P state.
 */
// 保留现有反射类路径；仅保存本进程名册，运动使用P2P显式编码。
UCLASS(BlueprintType)
class GULISTRIKE_API AGuLiSoldierStateReplicator final : public AInfo
{
	GENERATED_BODY()

public:
	AGuLiSoldierStateReplicator();


	/** Applies a coherent local read-model capture and notifies actual changes. */
	// 返回新增、修改或删除条目数。
	int32 ApplyAuthoritySnapshot(
		TConstArrayView<FGuLiSoldierStateItem> InStates,
		uint32 MatchEpoch);
	int32 ApplyAuthorityDelta(TConstArrayView<FGuLiSoldierStateItem> States, uint32 MatchEpoch);

	// 读取本端当前状态；未找到返回 nullptr。
	const FGuLiSoldierStateItem* FindSoldierState(FGuLiSoldierId SoldierId) const;
	bool ContainsSoldier(FGuLiSoldierId SoldierId) const { return LocalSoldierStates.Contains(SoldierId); }
	SIZE_T GetLocalCacheAllocatedSize() const { return LocalSoldierStates.GetAllocatedSize(); }
	UFUNCTION(BlueprintPure, Category="Commander|Units")
	TArray<FGuLiSoldierStateItem> GetAllSoldierStates() const;

	const TArray<FGuLiSoldierStateItem>& GetItems() const
	{
		return ReplicatedSoldiers.Items;
	}

	uint32 GetSnapshotMatchEpoch() const { return SnapshotMatchEpoch; }

	uint32 GetSnapshotRevision() const { return SnapshotRevision; }


	/** One local batch notification per changed snapshot. */
	// 本地批量变更通知；不是网络多播，也不保证一次通知对应一次完整网络事务。
	FGuLiSoldierStatesChangedSignature OnSoldierStatesChanged;
	FGuLiSoldierRosterDeltaSignature OnRosterDelta;
	FGuLiSoldierRemovalSignature& OnSoldiersRemoved() { return ReplicatedSoldiers.OnRemoved; }

private:
	void ApplyLocalDelta(const TArray<FGuLiSoldierStateItem>& States, const TArray<FGuLiSoldierId>& Removed);
	void RefreshLocalEpoch();
	TMap<FGuLiSoldierId,int32> AuthorityItemIndices;
	int32 ApplyAuthorityState(TConstArrayView<FGuLiSoldierStateItem> States, uint32 MatchEpoch, bool bComplete);
	TMap<FGuLiSoldierId, FGuLiSoldierStateItem> LocalSoldierStates;
	uint32 LocalCacheEpoch = 0;


	UPROPERTY(Transient)
	FGuLiSoldierStateFastArray ReplicatedSoldiers;

	/** Local read-model epoch and revision. */
	UPROPERTY(Transient)
	uint32 SnapshotMatchEpoch = 0u;

	UPROPERTY(Transient)
	uint32 SnapshotRevision = 0u;
};

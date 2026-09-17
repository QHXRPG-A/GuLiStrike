// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/Info.h"
#include "GuLiSoldierStateReplicator.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiSoldierStatesChangedSignature, uint32);

/**
 * Reliable, always-relevant roster and gameplay state for the current Soldier population.
 * Continuous transforms deliberately travel through the owning controller's unreliable
 * pose stream instead of this FastArray.
 */
// 服务器维护、相关客户端读取的公共士兵名册 Actor；bAlwaysRelevant，连续姿态另走 NetSync。
UCLASS(BlueprintType)
class GULISTRIKE_API AGuLiSoldierStateReplicator final : public AInfo
{
	GENERATED_BODY()

public:
	AGuLiSoldierStateReplicator();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority only. Applies one coherent roster capture and dirties changed items only. */
	// 服务器本地应用同次捕获的名册；返回新增/修改/删除条目数，单独战局切换仍可能返回 0。
	int32 ApplyAuthoritySnapshot(
		TConstArrayView<FGuLiSoldierStateItem> InStates,
		uint32 MatchEpoch);

	// 读取本端当前副本；未找到返回 nullptr，客户端读取结果不代表服务器实时状态。
	const FGuLiSoldierStateItem* FindSoldierState(FGuLiSoldierId SoldierId) const;
	UFUNCTION(BlueprintPure, Category="Commander|Units")
	TArray<FGuLiSoldierStateItem> GetAllSoldierStates() const;

	const TArray<FGuLiSoldierStateItem>& GetItems() const
	{
		return ReplicatedSoldiers.Items;
	}

	uint32 GetSnapshotMatchEpoch() const { return SnapshotMatchEpoch; }

	uint32 GetSnapshotRevision() const { return SnapshotRevision; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Exercises the actual native RepNotify body without relying on ProcessEvent reflection in a transient test world. */
	void TestOnly_InvokeSnapshotRevisionRepNotify() { OnRep_SnapshotRevision(); }
#endif

	/** One batch notification per changed authority snapshot or replicated client revision. */
	// 本地批量变更通知；不是网络多播，也不保证一次通知对应一次完整网络事务。
	FGuLiSoldierStatesChangedSignature OnSoldierStatesChanged;

private:
	int32 FindItemIndex(FGuLiSoldierId SoldierId) const;

	UFUNCTION()
	void OnRep_SnapshotRevision();

	UPROPERTY(Replicated)
	FGuLiSoldierStateFastArray ReplicatedSoldiers;

	/** Match identity and high-water mark applied with the reliable FastArray snapshot. */
	// 和名册版本均是独立复制属性；Bootstrap 还会核对名册数量和有效唯一 ID，不能只看版本通知。
	UPROPERTY(Replicated)
	uint32 SnapshotMatchEpoch = 0u;

	UPROPERTY(ReplicatedUsing = OnRep_SnapshotRevision)
	uint32 SnapshotRevision = 0u;
};

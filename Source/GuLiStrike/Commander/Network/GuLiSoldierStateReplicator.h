// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/Info.h"
#include "GuLiSoldierStateReplicator.generated.h"

/**
 * Reliable, always-relevant roster and gameplay state for the current Soldier population.
 * Continuous transforms deliberately travel through the owning controller's unreliable
 * pose stream instead of this FastArray.
 */
UCLASS(BlueprintType)
class GULISTRIKE_API AGuLiSoldierStateReplicator final : public AInfo
{
	GENERATED_BODY()

public:
	AGuLiSoldierStateReplicator();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority only. Applies one coherent roster capture and dirties changed items only. */
	int32 ApplyAuthoritySnapshot(
		TConstArrayView<FGuLiSoldierStateItem> InStates,
		uint32 MatchEpoch);

	const FGuLiSoldierStateItem* FindSoldierState(FGuLiSoldierId SoldierId) const;
	TArray<FGuLiSoldierStateItem> GetAllSoldierStates() const;

	const TArray<FGuLiSoldierStateItem>& GetItems() const
	{
		return ReplicatedSoldiers.Items;
	}

	uint32 GetSnapshotMatchEpoch() const { return SnapshotMatchEpoch; }

	uint32 GetSnapshotRevision() const { return SnapshotRevision; }

private:
	int32 FindItemIndex(FGuLiSoldierId SoldierId) const;

	UPROPERTY(Replicated)
	FGuLiSoldierStateFastArray ReplicatedSoldiers;

	/** Match identity and high-water mark applied with the reliable FastArray snapshot. */
	UPROPERTY(Replicated)
	uint32 SnapshotMatchEpoch = 0u;

	UPROPERTY(Replicated)
	uint32 SnapshotRevision = 0u;
};

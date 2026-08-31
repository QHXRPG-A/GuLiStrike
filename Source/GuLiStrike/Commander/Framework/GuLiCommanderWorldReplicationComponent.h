// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiCommanderWorldReplicationComponent.generated.h"

class AGuLiSoldierStateReplicator;
class AGuLiCommanderPresentationActor;

/** 服务器士兵世界发布器：挂在战局 GameMode 上，独占快照捕获及逐连接姿态调度；自身不是 RPC 对象。 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class UGuLiCommanderWorldReplicationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiCommanderWorldReplicationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void EnsureSoldierStateReplicator();
	void EnsurePresentationActor();
	void PublishSoldierSnapshotAndPoses();

	UPROPERTY(Transient)
	TObjectPtr<AGuLiSoldierStateReplicator> SoldierStateReplicator;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;

	uint32 LastPublishedPoseSimTick = 0u;
	uint32 LastPoseChunkDispatchSimTick = 0u;
	uint32 PublishedMatchEpoch = 0u;
	TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
	int32 PendingPoseChunkOffset = 0;
	int32 PendingPoseChunkStartIndex = 0;
	bool bOwnsSoldierSimulation = false;
};

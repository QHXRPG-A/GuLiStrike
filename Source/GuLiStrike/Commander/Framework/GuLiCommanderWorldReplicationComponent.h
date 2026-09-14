// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiCommanderWorldReplicationComponent.generated.h"

class AGuLiSoldierStateReplicator;
class AGuLiCommanderPresentationActor;

/** 服务器士兵世界发布器：挂在战局 GameMode 上，独占快照捕获及逐连接姿态调度；自身不是 RPC 对象。 */
UCLASS(ClassGroup = (GuLiStrike), Config = Game, meta = (BlueprintSpawnableComponent))
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

	/** Moving soldiers inside this planar distance retain the full 10 Hz pose rate. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Network", meta = (ClampMin = "0.0", Units = "cm"))
	float FullRatePoseDistanceCentimeters = 160000.0f;

	/** Moving soldiers beyond FullRatePoseDistance use 10 / divisor Hz. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Network", meta = (ClampMin = "1", ClampMax = "10"))
	uint8 FarMovingPoseFrameDivisor = 2u;

	/** Idle/destroyed soldiers use 10 / divisor Hz independent of distance. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Network", meta = (ClampMin = "1", ClampMax = "10"))
	uint8 StationaryPoseFrameDivisor = 2u;

	uint32 LastPublishedPoseSimTick = 0u;
	uint32 LastPoseChunkDispatchSimTick = 0u;
	uint32 PublishedMatchEpoch = 0u;
	TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
	uint8 PendingPoseDispatchPhase = 0u;
	bool bOwnsSoldierSimulation = false;
	bool bSoldierSimulationStarted = false;
};

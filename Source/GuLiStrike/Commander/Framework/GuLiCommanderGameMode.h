// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/GameMode.h"
#include "GuLiCommanderGameMode.generated.h"

class AGuLiCommanderPresentationActor;
class AGuLiCommanderPlayerController;
class AGuLiCommanderPlayerState;
class AGuLiSoldierStateReplicator;

/** 仅服务器存在：分配 5v5 席位、启动初始同步，并调度权威士兵状态/姿态的发布。 */
UCLASS()
class AGuLiCommanderGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AGuLiCommanderGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	// 服务器登录回调；连接建立后分配席位，再通过该玩家的 NetSync 发起 Bootstrap。
	virtual void PostLogin(APlayerController* NewPlayer) override;
	// 服务器退出回调；按席位和玩家 GUID 双重核对后释放，避免旧连接误删新占用者。
	virtual void Logout(AController* Exiting) override;

private:
	void EnsureSoldierStateReplicator();
	void EnsurePresentationActor();
	// 服务器本地调度，非 RPC：离散快照交给 Replicator，姿态块交给各连接的 NetSync。
	void PublishSoldierSnapshotAndPoses();
	void AssignRoleAndBootstrap(AGuLiCommanderPlayerController& CommanderController);

	UPROPERTY(Transient)
	TObjectPtr<AGuLiSoldierStateReplicator> SoldierStateReplicator;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;

	uint32 LastPublishedPoseSimTick = 0u;
	uint32 LastPoseChunkDispatchSimTick = 0u;
	// 本地待发送帧缓存；不作为属性复制，下一批发送进度由下方偏移量维护。
	TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
	int32 PendingPoseChunkOffset = 0;
	int32 PendingPoseChunkStartIndex = 0;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderWorldReplicationComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"

UGuLiCommanderWorldReplicationComponent::UGuLiCommanderWorldReplicationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGuLiCommanderWorldReplicationComponent::BeginPlay()
{
	Super::BeginPlay();
	// 一个 World 只允许其权威 GameMode 上的首个发布组件调度，防止蓝图误重复添加后双倍发送。
	if (!GetWorld() || !GetOwner() || GetOwner() != GetWorld()->GetAuthGameMode()
		|| GetOwner()->FindComponentByClass<UGuLiCommanderWorldReplicationComponent>() != this)
	{
		SetComponentTickEnabled(false);
		return;
	}
	bOwnsSoldierSimulation = true;
	if (UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		Authority->SetSoldierSimulationEnabled(true);
	}
	EnsureSoldierStateReplicator();
	EnsurePresentationActor();
}

void UGuLiCommanderWorldReplicationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bOwnsSoldierSimulation && GetWorld())
	{
		if (UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
		{
			Authority->SetSoldierSimulationEnabled(false);
		}
	}
	bOwnsSoldierSimulation = false;
	Super::EndPlay(EndPlayReason);
}

void UGuLiCommanderWorldReplicationComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		PublishSoldierSnapshotAndPoses();
	}
}

void UGuLiCommanderWorldReplicationComponent::EnsurePresentationActor()
{
	if ((!GetOwner() || !GetOwner()->HasAuthority()) || IsValid(PresentationActor) || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		PresentationActor = *It;
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("GuLiCommanderPresentation");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PresentationActor = GetWorld()->SpawnActor<AGuLiCommanderPresentationActor>(
		AGuLiCommanderPresentationActor::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
}

void UGuLiCommanderWorldReplicationComponent::EnsureSoldierStateReplicator()
{
	if ((!GetOwner() || !GetOwner()->HasAuthority()) || IsValid(SoldierStateReplicator) || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It)
	{
		SoldierStateReplicator = *It;
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("GuLiSoldierStateReplicator");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SoldierStateReplicator = GetWorld()->SpawnActor<AGuLiSoldierStateReplicator>(
		AGuLiSoldierStateReplicator::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
}

void UGuLiCommanderWorldReplicationComponent::PublishSoldierSnapshotAndPoses()
{
	EnsureSoldierStateReplicator();
	if (!IsValid(SoldierStateReplicator) || !GetWorld())
	{
		return;
	}

	UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	if (!Authority || !Authority->HasSpawnedAuthorityPopulation())
	{
		return;
	}
	const uint32 CurrentSimTick = Authority->GetServerSimTick();
	// 30 Hz 权威模拟每三个 Tick 目标捕获一次；渲染 Tick 不等于模拟 Tick，也不等于网络包到达频率。
	constexpr uint32 SimulationTicksPerPoseFrame = 30u / GULI_POSE_CAPTURE_RATE_HZ;
	if (CurrentSimTick == 0u || CurrentSimTick == LastPoseChunkDispatchSimTick)
	{
		return;
	}
	LastPoseChunkDispatchSimTick = CurrentSimTick;

	const AGuLiBattleGameState* CommanderGameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	const uint32 MatchEpoch = CommanderGameState ? CommanderGameState->GetMatchEpoch() : 0u;
	if (MatchEpoch == 0u)
	{
		return;
	}

	if (PublishedMatchEpoch != MatchEpoch)
	{
		PublishedMatchEpoch = MatchEpoch;
		PendingPoseChunks.Reset();
		PendingPoseChunkOffset = 0;
		PendingPoseChunkStartIndex = 0;
		LastPublishedPoseSimTick = 0u;
	}

	// 上一帧的块发送完才捕获新帧；同次捕获分别生成离散状态与连续姿态，两条通路独立到达。
	const bool bCapturePoseFrame = PendingPoseChunks.IsEmpty()
		&& (LastPublishedPoseSimTick == 0u
			|| CurrentSimTick - LastPublishedPoseSimTick >= SimulationTicksPerPoseFrame);
	if (bCapturePoseFrame)
	{
		LastPublishedPoseSimTick = CurrentSimTick;
		TArray<FGuLiSoldierStateItem> SoldierStates;
		Authority->BuildSoldierStateSnapshot(SoldierStates);
		SoldierStateReplicator->ApplyAuthoritySnapshot(SoldierStates, MatchEpoch);

		PendingPoseChunks.Reset();
		Authority->CaptureSoldierPoseChunks(PendingPoseChunks, MatchEpoch);
		PendingPoseChunkOffset = 0;
		PendingPoseChunkStartIndex = PendingPoseChunks.IsEmpty()
			? 0
			: static_cast<int32>(
				PendingPoseChunks[0].FrameSequence
				% static_cast<uint32>(PendingPoseChunks.Num()));
	}

	if (PendingPoseChunks.IsEmpty()
		|| PendingPoseChunkOffset >= PendingPoseChunks.Num())
	{
		return;
	}

	// 把一帧的块分摊到三个模拟步；轮换起始块，避免固定尾部总是较晚发出。
	const int32 ChunksPerDispatch = FMath::DivideAndRoundUp(
		PendingPoseChunks.Num(),
		static_cast<int32>(SimulationTicksPerPoseFrame));
	const int32 DispatchChunkCount = FMath::Min(
		ChunksPerDispatch,
		PendingPoseChunks.Num() - PendingPoseChunkOffset);
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		if (UGuLiCommanderNetSyncComponent* NetSync = It->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
		{
			NetSync->EnsureServerBootstrapForMatch(MatchEpoch);
			if (!NetSync->IsSoldierStreamReady())
			{
				// 首次登录可能早于 Mass 创建，无缝切图也可能带着旧的非零代次。
				// EnsureServerBootstrapForMatch 兼顾两种情况，不会反复递增已经有效的当前代次。
				continue;
			}

			NetSync->RefreshServerSelection();
			for (int32 ChunkOffset = 0; ChunkOffset < DispatchChunkCount; ++ChunkOffset)
			{
				const int32 OrderedChunkOffset = PendingPoseChunkOffset + ChunkOffset;
				const int32 ChunkIndex = (PendingPoseChunkStartIndex + OrderedChunkOffset) % PendingPoseChunks.Num();
				// 这里才逐连接调用发送入口；压缩/分块已在 Authority 完成，NetSync 负责 Client RPC。
				NetSync->SendPoseChunk(
					PendingPoseChunks[ChunkIndex]);
			}
		}
	}

	// 姿态是通过不可靠 RPC 发送的时效性快照，没有就绪客户端也应推进并丢弃过时帧。
	// 否则空服可能一直保留同一帧，下一位晚加入者会先收到积压的旧姿态。
	PendingPoseChunkOffset += DispatchChunkCount;
	if (PendingPoseChunkOffset >= PendingPoseChunks.Num())
	{
		PendingPoseChunks.Reset();
		PendingPoseChunkOffset = 0;
		PendingPoseChunkStartIndex = 0;
	}
}

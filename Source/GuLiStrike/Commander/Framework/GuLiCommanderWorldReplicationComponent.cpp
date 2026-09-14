// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderWorldReplicationComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"

namespace
{
	static_assert(30u % GULI_POSE_CAPTURE_RATE_HZ == 0u);
	constexpr uint8 PoseDispatchPhaseCount = 30u / GULI_POSE_CAPTURE_RATE_HZ;

	uint8 GetPoseRateDivisor(
		const FGuLiCompressedSoldierPose& Pose,
		const FVector& WorldLocation,
		const FVector& ViewLocation,
		const float FullRateDistanceCentimeters,
		const uint8 FarMovingDivisor,
		const uint8 StationaryDivisor)
	{
		if (Pose.State != EGuLiSoldierPoseState::Moving)
		{
			return StationaryDivisor;
		}
		return FVector::DistSquared2D(WorldLocation, ViewLocation)
			> FMath::Square(static_cast<double>(FullRateDistanceCentimeters))
			? FarMovingDivisor
			: 1u;
	}

	bool BuildConnectionPoseChunk(
		const FGuLiSoldierPoseChunk& Source,
		const FVector& ViewLocation,
		const float FullRateDistanceCentimeters,
		const uint8 FarMovingDivisor,
		const uint8 StationaryDivisor,
		FGuLiSoldierPoseChunk& OutChunk)
	{
		OutChunk = Source;
		OutChunk.Samples.Reset(Source.Samples.Num());
		for (const FGuLiCompressedSoldierPose& Pose : Source.Samples)
		{
			const FVector WorldLocation = FVector(Source.Anchor)
				+ Pose.GetRelativeLocationCentimeters();
			const uint8 Divisor = GetPoseRateDivisor(
				Pose,
				WorldLocation,
				ViewLocation,
				FullRateDistanceCentimeters,
				FarMovingDivisor,
				StationaryDivisor);
			const bool bScheduledFrame = Source.FrameSequence % Divisor
				== Pose.SoldierId.Value % Divisor;
			if (Pose.IsTeleport() || bScheduledFrame)
			{
				OutChunk.Samples.Add(Pose);
			}
		}
		return !OutChunk.Samples.IsEmpty();
	}
}

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
	check(FullRatePoseDistanceCentimeters >= 0.0f);
	check(FarMovingPoseFrameDivisor >= 1u);
	check(StationaryPoseFrameDivisor >= 1u);
	EnsureSoldierStateReplicator();
	EnsurePresentationActor();
}

void UGuLiCommanderWorldReplicationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bOwnsSoldierSimulation && bSoldierSimulationStarted && GetWorld())
	{
		if (UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
		{
			Authority->SetSoldierSimulationEnabled(false);
		}
	}
	bOwnsSoldierSimulation = false;
	bSoldierSimulationStarted = false;
	Super::EndPlay(EndPlayReason);
}

void UGuLiCommanderWorldReplicationComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!bSoldierSimulationStarted)
		{
			const UGuLiResourceWorldSubsystem* Resources =
				GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
			if (Resources && Resources->IsRuntimeReady())
			{
				if (UGuLiBattleAuthoritySubsystem* Authority =
					GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
				{
					Authority->SetSoldierSimulationEnabled(true);
					bSoldierSimulationStarted = true;
				}
			}
		}
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
		PendingPoseDispatchPhase = 0u;
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
		PendingPoseDispatchPhase = 0u;
	}

	if (PendingPoseChunks.IsEmpty())
	{
		return;
	}

	// SoldierId 永久映射到一个 30 Hz 发送槽；空间重排不会改变单兵的块发送相位。
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
			FVector ViewLocation;
			FRotator ViewRotation;
			It->GetPlayerViewPoint(ViewLocation, ViewRotation);
			check(!ViewLocation.ContainsNaN());
			for (const FGuLiSoldierPoseChunk& SourceChunk : PendingPoseChunks)
			{
				check(!SourceChunk.Samples.IsEmpty());
				const uint8 ChunkPhase = static_cast<uint8>(
					SourceChunk.Samples[0].SoldierId.Value % PoseDispatchPhaseCount);
				if (ChunkPhase != PendingPoseDispatchPhase)
				{
					continue;
				}
				FGuLiSoldierPoseChunk ConnectionChunk;
				if (BuildConnectionPoseChunk(
					SourceChunk,
					ViewLocation,
					FullRatePoseDistanceCentimeters,
					FarMovingPoseFrameDivisor,
					StationaryPoseFrameDivisor,
					ConnectionChunk))
				{
					NetSync->SendPoseChunk(ConnectionChunk);
				}
			}
		}
	}

	// 姿态是通过不可靠 RPC 发送的时效性快照，没有就绪客户端也应推进并丢弃过时帧。
	// 否则空服可能一直保留同一帧，下一位晚加入者会先收到积压的旧姿态。
	++PendingPoseDispatchPhase;
	if (PendingPoseDispatchPhase >= PoseDispatchPhaseCount)
	{
		PendingPoseChunks.Reset();
		PendingPoseDispatchPhase = 0u;
	}
}

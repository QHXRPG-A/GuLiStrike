// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "GameFramework/PlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "GuLiStrike.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace GuLiCommanderNetwork
{
	enum class EBootstrapAuthorityAction : uint8
	{
		None,
		StartNewGeneration,
		ResendPendingMarker,
	};

	constexpr int32 MaxCommanderRequestsPerSecond = 10;
	constexpr int32 MaxPendingCommandAcks = 64;
	constexpr int32 MaxQueuedSelectionIntents = 64;
	constexpr double CommanderRequestWindowSeconds = 1.0;
	constexpr double SecurityLogIntervalSeconds = 1.0;
	// 重试阈值为真实时间：约 15 ms 后再发一次快速请求，约 150 ms 后可靠回退；由 Tick 检查，非硬实时。
	constexpr double FastCommandRetryDelaySeconds = 0.015;
	constexpr double ReliableCommandFallbackDelaySeconds = 0.15;

	// 幂等判断不仅比较 ID，还比较选择版本/预设/修饰键/中心；向量比较容差为 0.5 cm。
	static bool IsSameSelectionRequest(
		const FGuLiSelectionRequest& Lhs,
		const FGuLiSelectionRequest& Rhs)
	{
		return Lhs.ClientRequestId == Rhs.ClientRequestId
			&& Lhs.KnownSelectionRevision == Rhs.KnownSelectionRevision
			&& Lhs.RadiusPreset == Rhs.RadiusPreset
			&& Lhs.Modifier == Rhs.Modifier
			&& FVector(Lhs.Center).Equals(FVector(Rhs.Center), 0.5f);
	}

	static bool IsSameMoveRequest(const FGuLiMoveRequest& Lhs, const FGuLiMoveRequest& Rhs)
	{
		return Lhs.ClientCommandId == Rhs.ClientCommandId
			&& Lhs.SelectionRevision == Rhs.SelectionRevision
			&& FVector(Lhs.Target).Equals(FVector(Rhs.Target), 0.5f);
	}

	// 跨 Actor 的属性与 RPC 不构成原子快照：显式核对同战局、非零版本和名册数量。
	// 允许已应用的快照比启动标记更新，不能要求版本严格相等而把持续更新的客户端卡住。
	static bool IsBootstrapSnapshotCompatible(
		const uint16 ProtocolVersion,
		const uint32 GameStateMatchEpoch,
		const uint32 SnapshotMatchEpoch,
		const uint32 SnapshotRevision,
		const int32 RosterCount,
		const uint32 ExpectedMatchEpoch,
		const uint32 ExpectedSnapshotRevision,
		const uint16 ExpectedRosterCount)
	{
		const bool bSnapshotRevisionAccepted = SnapshotRevision == ExpectedSnapshotRevision
			|| static_cast<int32>(SnapshotRevision - ExpectedSnapshotRevision) > 0;
		return ProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION
			&& GameStateMatchEpoch != 0u
			&& GameStateMatchEpoch == ExpectedMatchEpoch
			&& SnapshotMatchEpoch == ExpectedMatchEpoch
			&& SnapshotRevision != 0u
			&& ExpectedSnapshotRevision != 0u
			&& bSnapshotRevisionAccepted
			&& ExpectedRosterCount != 0u
			&& RosterCount >= static_cast<int32>(ExpectedRosterCount);
	}

	static bool IsClientPoseChunkAcceptable(
		const FGuLiSoldierPoseChunk& Chunk,
		const bool bClientPoseReady,
		const uint32 ClientAcceptedMatchEpoch)
	{
		return Chunk.ProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION
			&& bClientPoseReady
			&& ClientAcceptedMatchEpoch != 0u
			&& Chunk.AuthorityEpoch == ClientAcceptedMatchEpoch
			&& Chunk.FrameSequence != 0u
			&& Chunk.ChunkCount != 0u
			&& Chunk.ChunkIndex < Chunk.ChunkCount
			&& !Chunk.Samples.IsEmpty();
	}

	static EBootstrapAuthorityAction EvaluateBootstrapAuthorityAction(
		const uint32 ExistingSyncGeneration,
		const uint32 ExistingBootstrapMatchEpoch,
		const uint32 AuthorityMatchEpoch,
		const bool bPlayerSyncReady)
	{
		if (AuthorityMatchEpoch == 0u)
		{
			return EBootstrapAuthorityAction::None;
		}
		if (ExistingSyncGeneration == 0u
			|| ExistingBootstrapMatchEpoch != AuthorityMatchEpoch)
		{
			return EBootstrapAuthorityAction::StartNewGeneration;
		}
		return bPlayerSyncReady
			? EBootstrapAuthorityAction::None
			: EBootstrapAuthorityAction::ResendPendingMarker;
	}
}

UGuLiCommanderNetSyncComponent::UGuLiCommanderNetSyncComponent()
{
	// 组件默认参与复制；RPC 的连接仍取自所属 PlayerController，不是组件自己建立网络连接。
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.0f;
	PendingCommandAcks.Reserve(8);
	PendingPoseChunks.Reserve(static_cast<int32>(GULI_MAX_POSE_CHUNKS_PER_FRAME));
	QueuedSelectionIntents.Reserve(8);
}

void UGuLiCommanderNetSyncComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickSoldierBootstrap();
	TryCompleteClientBootstrap();
	TickPendingCommandRetries();
}

bool UGuLiCommanderNetSyncComponent::IsSoldierStreamReady() const
{
	if (!IsConnectionReady())
	{
		return false;
	}
	const AGuLiBattleGameState* BattleGameState = GetBattleGameState();
	if (!BattleGameState || BattleGameState->GetMatchEpoch() == 0u)
	{
		return false;
	}
	if (SoldierBootstrapBinding.ConnectionGeneration != GetConnectionGeneration()
		|| SoldierBootstrapBinding.SoldierSyncGeneration != SyncGeneration)
	{
		return false;
	}
	if (GetOwner()->HasAuthority())
	{
		const AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
		return BattlePlayerState && BattlePlayerState->IsSoldierStreamReady()
			&& SyncGeneration != 0u && ServerAcceptedSyncGeneration == SyncGeneration
			&& ServerAcceptedMatchEpoch == BattleGameState->GetMatchEpoch();
	}
	return bClientPoseReady && ClientAcceptedSyncGeneration == SyncGeneration
		&& ClientAcceptedMatchEpoch == BattleGameState->GetMatchEpoch();
}

void UGuLiCommanderNetSyncComponent::ResetClientSoldierState()
{
	PendingPoseChunks.Reset();
	PendingCommandAcks.Reset();
	QueuedSelectionIntents.Reset();
	bClientPoseReady = false;
	ClientAcceptedMatchEpoch = 0u;
	ClientAcceptedSyncGeneration = 0u;
	ClientAppliedRosterCount = 0u;
	ClientAppliedSnapshotRevision = 0u;
	PendingBootstrapGeneration = 0u;
	PendingBootstrapMatchEpoch = 0u;
	PendingBootstrapRosterCount = 0u;
	PendingBootstrapSnapshotRevision = 0u;
	bPendingSelectionIntent = false;
	bPendingSelectionFastRetry = false;
	bPendingSelectionReliableFallback = false;
	bPendingMoveIntent = false;
	bPendingMoveFastRetry = false;
	bPendingMoveReliableFallback = false;
	bQueuedMoveAfterSelection = false;
	PendingSelectionFastRetryTimeSeconds = 0.0;
	PendingSelectionReliableFallbackTimeSeconds = 0.0;
	PendingMoveFastRetryTimeSeconds = 0.0;
	PendingMoveReliableFallbackTimeSeconds = 0.0;
	PendingSelectionIntent = FGuLiSelectionRequest{};
	PendingMoveIntent = FGuLiMoveRequest{};
	QueuedMoveAfterSelection = FGuLiMoveRequest{};
	LastCommandAck = FGuLiCommandAck{};
	LastDeliveredAckKind = EGuLiCommandKind::None;
	LastDeliveredAckCommandId = 0u;
	bLoggedBootstrapProtocolMismatch = false;
#if !UE_BUILD_SHIPPING
	AcceptedPoseFrameCount = 0u;
	LastAcceptedPoseFrameSequence = 0u;
	LastAcceptedPoseReceiveTimeSeconds = 0.0;
#endif
}

void UGuLiCommanderNetSyncComponent::OnConnectionBootstrapReset()
{
	Super::OnConnectionBootstrapReset();
	ResetClientSoldierState();
	SelectionState = FGuLiCommanderSelectionState{};
	LastSelectionAck = FGuLiCommandAck{};
	LastMoveAck = FGuLiCommandAck{};
	LastSelectionRequest = FGuLiSelectionRequest{};
	LastMoveRequest = FGuLiMoveRequest{};
	// 同一 PlayerController 的输入序号仍递增；保留高水位，避免迟到的旧不可靠请求在重握手后被当成新命令。
	// 请求内容、回执与重试队列均清空；新 Controller 的新组件本来就从零开始。
	LastBootstrapRequestId = 0u;
	HighestAckedStreamSeq = 0u;
	BootstrapMatchEpoch = 0u;
	BootstrapExpectedRosterCount = 0u;
	BootstrapExpectedSnapshotRevision = 0u;
	ServerAcceptedSyncGeneration = 0u;
	ServerAcceptedMatchEpoch = 0u;
	NextServerBootstrapMarkerTime = 0.0;
	SelectionCachedFastReplayCount = 0u;
	MoveCachedFastReplayCount = 0u;
	CommanderRequestWindow.Reset();
	LastSecurityLogTime = -1.0;
	SuppressedSecurityLogCount = 0u;
	// SyncGeneration 保留为递增计数器；清空期望战局撤销当前代次，下一次启动不会复用旧数字。
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// 客户端不能清写此复制属性：下一代绑定可能先于公共 marker 到达，清写会丢掉已复制的值。
		SoldierBootstrapBinding = FGuLiSoldierBootstrapBinding{};
		if (AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState())
		{
			BattlePlayerState->SetServerSoldierStreamReady(false);
			MirrorSyncReadyToRoleSlot(*BattlePlayerState, false);
		}
		GetOwner()->ForceNetUpdate();
	}
	NotifySelectionChanged();
	OnCommandAckChanged.Broadcast(LastCommandAck);
}

void UGuLiCommanderNetSyncComponent::OnConnectionBootstrapReady()
{
	Super::OnConnectionBootstrapReady();
	TickSoldierBootstrap();
	TryCompleteClientBootstrap();
}

void UGuLiCommanderNetSyncComponent::TickSoldierBootstrap()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsConnectionReady() || !GetWorld())
	{
		return;
	}
	const AGuLiBattleGameState* BattleGameState = GetBattleGameState();
	if (!BattleGameState)
	{
		return;
	}
	EnsureServerBootstrapForMatch(BattleGameState->GetMatchEpoch());
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (!IsSoldierStreamReady() && SyncGeneration != 0u
		&& Now >= NextServerBootstrapMarkerTime)
	{
		NextServerBootstrapMarkerTime = Now + 2.0;
		ResendServerBootstrapMarker();
	}
}

void UGuLiCommanderNetSyncComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, SelectionState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, SyncGeneration, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, SoldierBootstrapBinding, COND_OwnerOnly);
}

// 服务器先发布权威名册，再发送期望值标记；这只是发送侧顺序，客户端仍必须检查到达条件。
void UGuLiCommanderNetSyncComponent::StartServerBootstrap()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsConnectionReady())
	{
		return;
	}

	AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	if (!BattlePlayerState)
	{
		return;
	}

	UWorld* World = GetWorld();
	const AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	UGuLiBattleAuthoritySubsystem* Authority = World
		? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
		: nullptr;
	AGuLiSoldierStateReplicator* SoldierReplicator = nullptr;
	if (World)
	{
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
		{
			SoldierReplicator = *It;
			break;
		}
	}

	const int32 ExpectedRosterCount = Authority ? Authority->GetAuthoritativeMemberCount() : 0;
	if (!BattleGameState || BattleGameState->GetProtocolVersion() != GULI_COMMANDER_PROTOCOL_VERSION
		|| BattleGameState->GetMatchEpoch() == 0u || !SoldierReplicator
		|| ExpectedRosterCount <= 0 || ExpectedRosterCount > MAX_uint16)
	{
		return;
	}
	if (GuLiCommanderNetwork::EvaluateBootstrapAuthorityAction(
		SyncGeneration,
		BootstrapMatchEpoch,
		BattleGameState->GetMatchEpoch(),
		BattlePlayerState->IsSoldierStreamReady())
		!= GuLiCommanderNetwork::EBootstrapAuthorityAction::StartNewGeneration)
	{
		return;
	}

	TArray<FGuLiSoldierStateItem> SoldierStates;
	Authority->BuildSoldierStateSnapshot(SoldierStates);
	SoldierReplicator->ApplyAuthoritySnapshot(
		SoldierStates,
		BattleGameState->GetMatchEpoch());
	if (SoldierReplicator->GetItems().Num() < ExpectedRosterCount
		|| SoldierReplicator->GetSnapshotMatchEpoch() != BattleGameState->GetMatchEpoch()
		|| SoldierReplicator->GetSnapshotRevision() == 0u)
	{
		return;
	}

	// 战局切换时撤销就绪并清除上一局的选择/去重缓存；普通重试不会反复创建新代次。
	const bool bMatchEpochChanged = BootstrapMatchEpoch != 0u
		&& BootstrapMatchEpoch != BattleGameState->GetMatchEpoch();
	BattlePlayerState->SetServerSoldierStreamReady(false);
	ServerAcceptedSyncGeneration = 0u;
	ServerAcceptedMatchEpoch = 0u;
	MirrorSyncReadyToRoleSlot(*BattlePlayerState, false);
	if (bMatchEpochChanged)
	{
		SelectionState = FGuLiCommanderSelectionState{};
		LastCommandAck = FGuLiCommandAck{};
		LastSelectionAck = FGuLiCommandAck{};
		LastMoveAck = FGuLiCommandAck{};
		LastSelectionRequest = FGuLiSelectionRequest{};
		LastMoveRequest = FGuLiMoveRequest{};
		SelectionCachedFastReplayCount = 0u;
		MoveCachedFastReplayCount = 0u;
		CommanderRequestWindow.Reset();
		NotifySelectionChanged();
	}

	++SyncGeneration;
	if (SyncGeneration == 0u)
	{
		++SyncGeneration;
	}
	SoldierBootstrapBinding.ConnectionGeneration = GetConnectionGeneration();
	SoldierBootstrapBinding.SoldierSyncGeneration = SyncGeneration;
	HighestAckedStreamSeq = 0u;
	BootstrapMatchEpoch = BattleGameState->GetMatchEpoch();
	BootstrapExpectedRosterCount = static_cast<uint16>(ExpectedRosterCount);
	BootstrapExpectedSnapshotRevision = SoldierReplicator->GetSnapshotRevision();
	GetOwner()->ForceNetUpdate();
	NextServerBootstrapMarkerTime = World->GetRealTimeSeconds() + 2.0;
	ResendServerBootstrapMarker();
}

void UGuLiCommanderNetSyncComponent::EnsureServerBootstrapForMatch(
	const uint32 AuthorityMatchEpoch)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || AuthorityMatchEpoch == 0u
		|| !IsConnectionReady())
	{
		return;
	}
	const AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	if (!BattlePlayerState)
	{
		return;
	}
	if (GuLiCommanderNetwork::EvaluateBootstrapAuthorityAction(
		SyncGeneration,
		BootstrapMatchEpoch,
		AuthorityMatchEpoch,
		BattlePlayerState->IsSoldierStreamReady())
		== GuLiCommanderNetwork::EBootstrapAuthorityAction::StartNewGeneration)
	{
		StartServerBootstrap();
	}
}

void UGuLiCommanderNetSyncComponent::SendPoseChunk(const FGuLiSoldierPoseChunk& Chunk)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| !IsSoldierStreamReady() || Chunk.AuthorityEpoch != ServerAcceptedMatchEpoch
		|| Chunk.ProtocolVersion != GULI_COMMANDER_PROTOCOL_VERSION)
	{
		return;
	}
	// 调用 RPC 包装函数才会按所有权路由；不要在发送处直接调用 _Implementation。
	ClientReceiveSoldierPoseChunk(Chunk);
}

bool UGuLiCommanderNetSyncComponent::RefreshServerSelection()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !IsSoldierStreamReady())
	{
		return false;
	}
	AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	if (!BattlePlayerState || !Authority
		|| !Authority->RefreshSelection(BattlePlayerState->GetTeam(), SelectionState))
	{
		return false;
	}
	NotifySelectionChanged();
	GetOwner()->ForceNetUpdate();
	return true;
}

void UGuLiCommanderNetSyncComponent::ConsumePendingPoseChunks(
	TArray<FGuLiSoldierPoseChunk>& OutChunks)
{
	OutChunks = MoveTemp(PendingPoseChunks);
	PendingPoseChunks.Reset();
}

void UGuLiCommanderNetSyncComponent::ConsumePendingCommandAcks(
	TArray<FGuLiCommandAck>& OutAcks)
{
	OutAcks = MoveTemp(PendingCommandAcks);
	PendingCommandAcks.Reset();
}

// 服务器本机路径直接调用可靠入口；远端拥有客户端才使用快速发送与延迟回退状态机。
void UGuLiCommanderNetSyncComponent::SubmitSelectionRequest(
	const FGuLiSelectionRequest& Request)
{
	if (!GetOwner() || GetOwner()->HasAuthority())
	{
		ServerRequestSelection(Request);
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || Request.ClientRequestId == 0u)
	{
		ServerRequestSelection(Request);
		return;
	}
	if (bPendingSelectionIntent)
	{
		if (QueuedSelectionIntents.Num() >= GuLiCommanderNetwork::MaxQueuedSelectionIntents)
		{
			QueuedSelectionIntents.RemoveAt(0, 1, EAllowShrinking::No);
		}
		QueuedSelectionIntents.Add(Request);
		return;
	}
	BeginSelectionIntent(Request);
}


// 保存同一份请求供重试使用，避免重发时换 ID 或改变内容而被服务器视为冲突。
void UGuLiCommanderNetSyncComponent::BeginSelectionIntent(
	const FGuLiSelectionRequest& Request)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double NowSeconds = World->GetRealTimeSeconds();
	PendingSelectionIntent = Request;
	bPendingSelectionIntent = true;
	bPendingSelectionFastRetry = true;
	bPendingSelectionReliableFallback = true;
	PendingSelectionFastRetryTimeSeconds = NowSeconds + GuLiCommanderNetwork::FastCommandRetryDelaySeconds;
	PendingSelectionReliableFallbackTimeSeconds = NowSeconds + GuLiCommanderNetwork::ReliableCommandFallbackDelaySeconds;
	ServerRequestSelectionFast(Request);
}

// 选兵尚未确认时暂存最新移动；普通移动没有 FIFO，BeginMoveIntent 会替换当前待重试移动。
void UGuLiCommanderNetSyncComponent::SubmitMoveRequest(const FGuLiMoveRequest& Request)
{
	if (!GetOwner() || GetOwner()->HasAuthority())
	{
		ServerIssueMove(Request);
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || Request.ClientCommandId == 0u)
	{
		ServerIssueMove(Request);
		return;
	}
	if (bPendingSelectionIntent || !QueuedSelectionIntents.IsEmpty())
	{
		QueuedMoveAfterSelection = Request;
		bQueuedMoveAfterSelection = true;
		return;
	}
	BeginMoveIntent(Request);
}


void UGuLiCommanderNetSyncComponent::BeginMoveIntent(const FGuLiMoveRequest& Request)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double NowSeconds = World->GetRealTimeSeconds();
	PendingMoveIntent = Request;
	bPendingMoveIntent = true;
	bPendingMoveFastRetry = true;
	bPendingMoveReliableFallback = true;
	PendingMoveFastRetryTimeSeconds = NowSeconds + GuLiCommanderNetwork::FastCommandRetryDelaySeconds;
	PendingMoveReliableFallbackTimeSeconds = NowSeconds + GuLiCommanderNetwork::ReliableCommandFallbackDelaySeconds;
	ServerIssueMoveFast(Request);
}

// 快速补发与可靠回退各最多触发一次；收到匹配的业务 ACK 会清除对应标志。
void UGuLiCommanderNetSyncComponent::TickPendingCommandRetries()
{
	if (!GetOwner() || GetOwner()->HasAuthority() || !IsSoldierStreamReady())
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double NowSeconds = World->GetRealTimeSeconds();
	if (bPendingSelectionIntent)
	{
		if (bPendingSelectionFastRetry
			&& NowSeconds >= PendingSelectionFastRetryTimeSeconds)
		{
			bPendingSelectionFastRetry = false;
			ServerRequestSelectionFast(PendingSelectionIntent);
		}
		if (bPendingSelectionReliableFallback
			&& NowSeconds >= PendingSelectionReliableFallbackTimeSeconds)
		{
			bPendingSelectionReliableFallback = false;
			ServerRequestSelection(PendingSelectionIntent);
		}
	}

	if (bPendingMoveIntent)
	{
		if (bPendingMoveFastRetry && NowSeconds >= PendingMoveFastRetryTimeSeconds)
		{
			bPendingMoveFastRetry = false;
			ServerIssueMoveFast(PendingMoveIntent);
		}
		if (bPendingMoveReliableFallback
			&& NowSeconds >= PendingMoveReliableFallbackTimeSeconds)
		{
			bPendingMoveReliableFallback = false;
			ServerIssueMove(PendingMoveIntent);
		}
	}
}

void UGuLiCommanderNetSyncComponent::ServerRequestSelection_Implementation(
	const FGuLiSelectionRequest& Request)
{
	HandleSelectionRequest(Request, true);
}

void UGuLiCommanderNetSyncComponent::ServerRequestSelectionFast_Implementation(
	const FGuLiSelectionRequest& Request)
{
	HandleSelectionRequest(Request, false);
}

// 两种 Server RPC 的共用服务器处理路径：先去重，再校验权限/限流与输入，最后交给 Authority。
// 重复最近请求只重放缓存结果，不再次执行选兵；这不是无限历史请求的结果存储。
void UGuLiCommanderNetSyncComponent::HandleSelectionRequest(
	const FGuLiSelectionRequest& Request,
	const bool bReliableAck)
{
	FGuLiCommandAck Ack;
	InitializeAck(Ack, Request.ClientRequestId, EGuLiCommandKind::Selection);

	if (Request.ClientRequestId == 0)
	{
		if (!ConsumeCommandRateLimit())
		{
			LogRejectedRpc(TEXT("ServerRequestSelectionInvalidId"), EGuLiCommandAckResult::RateLimited);
			return;
		}
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		PublishAck(Ack, bReliableAck);
		return;
	}

	if (LastSelectionRequestId != 0 && !IsNewerSerial(Request.ClientRequestId, LastSelectionRequestId))
	{
		if (Request.ClientRequestId == LastSelectionRequestId)
		{
			if (!GuLiCommanderNetwork::IsSameSelectionRequest(Request, LastSelectionRequest))
			{
				if (ConsumeCommandRateLimit())
				{
					LogRejectedRpc(
						TEXT("ServerRequestSelectionConflictingDuplicate"),
						EGuLiCommandAckResult::InvalidRequest);
					PublishAck(Ack, bReliableAck);
				}
				return;
			}
			if (bReliableAck || ConsumeCachedAckReplayLimit(SelectionCachedFastReplayCount))
			{
				PublishAck(LastSelectionAck, bReliableAck);
			}
			return;
		}
		if (!ConsumeCommandRateLimit())
		{
			LogRejectedRpc(TEXT("ServerRequestSelectionDuplicate"), EGuLiCommandAckResult::RateLimited);
			return;
		}
		Ack = LastSelectionAck;
		Ack.ClientCommandId = Request.ClientRequestId;
		Ack.Result = EGuLiCommandAckResult::Duplicate;
		PublishAck(Ack, bReliableAck);
		return;
	}
	// 新序号在业务校验前登记；即使被拒绝，结果也会缓存，同 ID 重试不会重新执行。
	LastSelectionRequestId = Request.ClientRequestId;
	LastSelectionRequest = Request;
	SelectionCachedFastReplayCount = 0u;

	if (!CanProcessCommanderRequest(Ack, TEXT("ServerRequestSelection")))
	{
		LastSelectionAck = Ack;
		PublishAck(Ack, bReliableAck);
		return;
	}

	AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	UGuLiBattleAuthoritySubsystem* Authority = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
		: nullptr;
	if (!BattlePlayerState || !Authority || !Request.IsWellFormed())
	{
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		LastSelectionAck = Ack;
		PublishAck(Ack, bReliableAck);
		return;
	}

	// 先在候选副本上解算；仅接受后替换复制状态，拒绝不能破坏原有选择。
	FGuLiCommanderSelectionState CandidateSelection = SelectionState;
	const bool bAccepted = Authority->ResolveSelection(
		*BattlePlayerState,
		Request,
		CandidateSelection,
		Ack);

	if (bAccepted)
	{
		CandidateSelection.AcceptedClientRequestId = Request.ClientRequestId;
		SelectionState = MoveTemp(CandidateSelection);
		Ack.ServerSelectionRevision = SelectionState.SelectionRevision;
		NotifySelectionChanged();
		GetOwner()->ForceNetUpdate();
	}
	else if (Ack.Result == EGuLiCommandAckResult::Accepted
		|| Ack.Result == EGuLiCommandAckResult::PartiallyAccepted)
	{
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
	}

	LastSelectionAck = Ack;
	PublishAck(Ack, bReliableAck);
}

void UGuLiCommanderNetSyncComponent::ServerIssueMove_Implementation(const FGuLiMoveRequest& Request)
{
	HandleMoveRequest(Request, true);
}

void UGuLiCommanderNetSyncComponent::ServerIssueMoveFast_Implementation(
	const FGuLiMoveRequest& Request)
{
	HandleMoveRequest(Request, false);
}

// 与选兵采用相同去重规则：最近同 ID 同内容重放；同 ID 异内容拒绝；更旧 ID 返回 Duplicate。
// 可靠回退可重放缓存 ACK，快速重复回执有每命令次数上限。
void UGuLiCommanderNetSyncComponent::HandleMoveRequest(
	const FGuLiMoveRequest& Request,
	const bool bReliableAck)
{
	FGuLiCommandAck Ack;
	InitializeAck(Ack, Request.ClientCommandId, EGuLiCommandKind::Move);

	if (Request.ClientCommandId == 0)
	{
		if (!ConsumeCommandRateLimit())
		{
			LogRejectedRpc(TEXT("ServerIssueMoveInvalidId"), EGuLiCommandAckResult::RateLimited);
			return;
		}
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		PublishAck(Ack, bReliableAck);
		return;
	}

	if (LastMoveCommandId != 0 && !IsNewerSerial(Request.ClientCommandId, LastMoveCommandId))
	{
		if (Request.ClientCommandId == LastMoveCommandId)
		{
			if (!GuLiCommanderNetwork::IsSameMoveRequest(Request, LastMoveRequest))
			{
				if (ConsumeCommandRateLimit())
				{
					LogRejectedRpc(
						TEXT("ServerIssueMoveConflictingDuplicate"),
						EGuLiCommandAckResult::InvalidRequest);
					PublishAck(Ack, bReliableAck);
				}
				return;
			}
			if (bReliableAck || ConsumeCachedAckReplayLimit(MoveCachedFastReplayCount))
			{
				PublishAck(LastMoveAck, bReliableAck);
			}
			return;
		}
		if (!ConsumeCommandRateLimit())
		{
			LogRejectedRpc(TEXT("ServerIssueMoveDuplicate"), EGuLiCommandAckResult::RateLimited);
			return;
		}
		Ack = LastMoveAck;
		Ack.ClientCommandId = Request.ClientCommandId;
		Ack.Result = EGuLiCommandAckResult::Duplicate;
		PublishAck(Ack, bReliableAck);
		return;
	}
	LastMoveCommandId = Request.ClientCommandId;
	LastMoveRequest = Request;
	MoveCachedFastReplayCount = 0u;

	if (!CanProcessCommanderRequest(Ack, TEXT("ServerIssueMove")))
	{
		LastMoveAck = Ack;
		PublishAck(Ack, bReliableAck);
		return;
	}

	AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	UGuLiBattleAuthoritySubsystem* Authority = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
		: nullptr;
	if (!BattlePlayerState || !Authority || !Request.IsWellFormed())
	{
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		LastMoveAck = Ack;
		PublishAck(Ack, bReliableAck);
		return;
	}

	// Authority 决定版本/归属/目标/寻路和部分接受，bool 表示是否至少接受部分，详细原因写入 Ack。
	const bool bAccepted = Authority->IssueMove(
		*BattlePlayerState,
		Request,
		SelectionState,
		Ack);
	if (!bAccepted && (Ack.Result == EGuLiCommandAckResult::Accepted
		|| Ack.Result == EGuLiCommandAckResult::PartiallyAccepted))
	{
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
	}

	LastMoveAck = Ack;
	PublishAck(Ack, bReliableAck);
}

// 客户端请求可重发当前未完成代次的标记；不要把每次重试都变成新的 SyncGeneration。
void UGuLiCommanderNetSyncComponent::ServerRequestBootstrap_Implementation(uint32 ClientBootstrapRequestId)
{
	EnsureServerConnectionBootstrap();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsConnectionReady())
	{
		return;
	}
	if (ClientBootstrapRequestId == 0)
	{
		LogRejectedRpc(TEXT("ServerRequestBootstrap"), EGuLiCommandAckResult::InvalidRequest);
		return;
	}

	if (LastBootstrapRequestId != 0
		&& !IsNewerSerial(ClientBootstrapRequestId, LastBootstrapRequestId))
	{
		if (!ConsumeCommandRateLimit())
		{
			LogRejectedRpc(TEXT("ServerRequestBootstrapDuplicate"), EGuLiCommandAckResult::RateLimited);
			return;
		}
		if (ClientBootstrapRequestId == LastBootstrapRequestId && SyncGeneration != 0)
		{
			ResendServerBootstrapMarker();
		}
		return;
	}

	LastBootstrapRequestId = ClientBootstrapRequestId;
	if (!GetBattlePlayerState() || !ConsumeCommandRateLimit())
	{
		LogRejectedRpc(TEXT("ServerRequestBootstrap"), EGuLiCommandAckResult::RateLimited);
		return;
	}
	const AGuLiBattleGameState* BattleGameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	const uint32 AuthorityMatchEpoch = BattleGameState
		? BattleGameState->GetMatchEpoch()
		: 0u;
	const GuLiCommanderNetwork::EBootstrapAuthorityAction BootstrapAction = GuLiCommanderNetwork::EvaluateBootstrapAuthorityAction(
			SyncGeneration,
			BootstrapMatchEpoch,
			AuthorityMatchEpoch,
			GetBattlePlayerState()->IsSoldierStreamReady());
	if (BootstrapAction == GuLiCommanderNetwork::EBootstrapAuthorityAction::StartNewGeneration)
	{
		StartServerBootstrap();
		return;
	}
	if (BootstrapAction == GuLiCommanderNetwork::EBootstrapAuthorityAction::ResendPendingMarker)
	{
		ResendServerBootstrapMarker();
		return;
	}
}

// 服务器再次核对客户端的就绪声明，旧代次或旧战局 ACK 不能打开当前连接的就绪门。
void UGuLiCommanderNetSyncComponent::ServerAcknowledgeBootstrap_Implementation(
	const uint32 InSyncGeneration,
	const uint16 ClientProtocolVersion,
	const uint32 ClientMatchEpoch,
	const uint16 AppliedRosterCount,
	const uint32 AppliedSnapshotRevision)
{
	EnsureServerConnectionBootstrap();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsConnectionReady())
	{
		return;
	}
	const AGuLiBattleGameState* BattleGameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	if (InSyncGeneration == 0u || InSyncGeneration != SyncGeneration
		|| SoldierBootstrapBinding.ConnectionGeneration != GetConnectionGeneration()
		|| SoldierBootstrapBinding.SoldierSyncGeneration != SyncGeneration
		|| !BattleGameState
		|| BattleGameState->GetProtocolVersion() != GULI_COMMANDER_PROTOCOL_VERSION
		|| !GuLiCommanderNetwork::IsBootstrapSnapshotCompatible(
			ClientProtocolVersion,
			BattleGameState->GetMatchEpoch(),
			ClientMatchEpoch,
			AppliedSnapshotRevision,
			static_cast<int32>(AppliedRosterCount),
			BootstrapMatchEpoch,
			BootstrapExpectedSnapshotRevision,
			BootstrapExpectedRosterCount))
	{
		LogRejectedRpc(TEXT("ServerAcknowledgeBootstrap"), EGuLiCommandAckResult::InvalidRequest);
		return;
	}

	if (AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState())
	{
		ServerAcceptedSyncGeneration = SyncGeneration;
		ServerAcceptedMatchEpoch = BootstrapMatchEpoch;
		BattlePlayerState->SetServerSoldierStreamReady(true);
		MirrorSyncReadyToRoleSlot(*BattlePlayerState, true);
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Commander bootstrap ready: generation=%u owner=%s player=%s."),
			SyncGeneration,
			*GetNameSafe(GetOwner()),
			*BattlePlayerState->GetPlayerGuid().ToString());
	}
}

// 占位路径只推进 HighestAckedStreamSeq；当前没有在此组装、发送或补传事实分块。
void UGuLiCommanderNetSyncComponent::ServerAcknowledgeFacts_Implementation(
	uint32 InSyncGeneration,
	uint32 HighestContiguousStreamSeq)
{
	const AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	if (!BattlePlayerState || !IsSoldierStreamReady()
		|| InSyncGeneration == 0 || InSyncGeneration != SyncGeneration)
	{
		return;
	}

	if (HighestAckedStreamSeq == 0 || IsNewerSerial(HighestContiguousStreamSeq, HighestAckedStreamSeq))
	{
		HighestAckedStreamSeq = HighestContiguousStreamSeq;
	}
}

// 收到启动标记先清空旧接收/重试状态并关闭姿态门；满足名册条件后才重新开放。
void UGuLiCommanderNetSyncComponent::ClientBootstrapStarted_Implementation(
	const uint32 NewSyncGeneration,
	const uint16 ServerProtocolVersion,
	const uint32 MatchEpoch,
	const uint16 ExpectedRosterCount,
	const uint32 ExpectedSnapshotRevision)
{
	if (NewSyncGeneration == 0u || MatchEpoch == 0u || ExpectedRosterCount == 0u
		|| ExpectedSnapshotRevision == 0u || !IsConnectionReady()
		|| SoldierBootstrapBinding.ConnectionGeneration != GetConnectionGeneration()
		|| SoldierBootstrapBinding.SoldierSyncGeneration != NewSyncGeneration)
	{
		return;
	}
	const AGuLiBattleGameState* BattleGameState = GetBattleGameState();
	if (!BattleGameState || BattleGameState->GetMatchEpoch() != MatchEpoch
		|| (SyncGeneration != 0u && NewSyncGeneration != SyncGeneration
			&& !IsNewerSerial(NewSyncGeneration, SyncGeneration)))
	{
		return;
	}
	// 同代次标记重发只补查依赖，不能清除已确认命令；旧代次不能覆盖新名册门。
	if (ServerProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION
		&& bClientPoseReady && ClientAcceptedSyncGeneration == NewSyncGeneration
		&& ClientAcceptedMatchEpoch == MatchEpoch)
	{
		ServerAcknowledgeBootstrap(
			NewSyncGeneration, ServerProtocolVersion, MatchEpoch,
			ClientAppliedRosterCount, ClientAppliedSnapshotRevision);
		return;
	}
	if (ServerProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION
		&& PendingBootstrapGeneration == NewSyncGeneration
		&& PendingBootstrapMatchEpoch == MatchEpoch
		&& PendingBootstrapRosterCount == ExpectedRosterCount
		&& PendingBootstrapSnapshotRevision == ExpectedSnapshotRevision)
	{
		TryCompleteClientBootstrap();
		return;
	}
	ResetClientSoldierState();
	if (SyncGeneration != NewSyncGeneration)
	{
		SyncGeneration = NewSyncGeneration;
		OnRep_SyncGeneration();
	}
	PendingBootstrapGeneration = NewSyncGeneration;
	PendingBootstrapMatchEpoch = MatchEpoch;
	PendingBootstrapRosterCount = ExpectedRosterCount;
	PendingBootstrapSnapshotRevision = ExpectedSnapshotRevision;
	if (ServerProtocolVersion != GULI_COMMANDER_PROTOCOL_VERSION)
	{
		if (!bLoggedBootstrapProtocolMismatch)
		{
			UE_LOG(
				LogGuLiStrike,
				Error,
				TEXT("Commander bootstrap protocol mismatch: server=%u client=%u generation=%u."),
				ServerProtocolVersion,
				GULI_COMMANDER_PROTOCOL_VERSION,
				NewSyncGeneration);
			bLoggedBootstrapProtocolMismatch = true;
		}
		return;
	}
	bLoggedBootstrapProtocolMismatch = false;
	TryCompleteClientBootstrap();
}

void UGuLiCommanderNetSyncComponent::ClientReceiveCommandAck_Implementation(
	const FGuLiCommandAck& Ack)
{
	ReceiveCommandAck(Ack);
}

void UGuLiCommanderNetSyncComponent::ClientReceiveCommandAckFast_Implementation(
	const FGuLiCommandAck& Ack)
{
	ReceiveCommandAck(Ack);
}

// 可靠/快速 ACK 共用本地消费路径；先按命令种类和 ID 停止重试，再排队并通知 UI。
void UGuLiCommanderNetSyncComponent::ReceiveCommandAck(const FGuLiCommandAck& Ack)
{
	// 无拥有者的对象仅用于原生协议测试；真实连接在公共握手失效后不能继续消费旧 ACK。
	if (GetOwner() && !IsConnectionReady())
	{
		return;
	}
	FGuLiCommandAck Sanitized = Ack;
	Sanitized.Sanitize();
	if (Sanitized.CommandKind == EGuLiCommandKind::None
		|| Sanitized.ClientCommandId == 0u)
	{
		return;
	}
	bool bStartNextSelection = false;
	bool bStartDeferredMove = false;
	FGuLiSelectionRequest NextSelectionRequest;
	FGuLiMoveRequest DeferredMoveRequest;
	if (Sanitized.CommandKind == EGuLiCommandKind::Selection
		&& bPendingSelectionIntent
		&& Sanitized.ClientCommandId == PendingSelectionIntent.ClientRequestId)
	{
		bPendingSelectionIntent = false;
		bPendingSelectionFastRetry = false;
		bPendingSelectionReliableFallback = false;
		if (!QueuedSelectionIntents.IsEmpty())
		{
			NextSelectionRequest = QueuedSelectionIntents[0];
			QueuedSelectionIntents.RemoveAt(0, 1, EAllowShrinking::No);
			// 后续意图使用 ACK 携带的服务器选择版本，不依赖 SelectionState 属性恰好先到达。
			// 此分支没有以 IsAccepted 过滤：拒绝回执也会推动队列，下一请求仍由服务器独立校验。
			NextSelectionRequest.KnownSelectionRevision = Sanitized.ServerSelectionRevision;
			bStartNextSelection = true;
		}
		else if (bQueuedMoveAfterSelection)
		{
			DeferredMoveRequest = QueuedMoveAfterSelection;
			DeferredMoveRequest.SelectionRevision = Sanitized.ServerSelectionRevision;
			bQueuedMoveAfterSelection = false;
			bStartDeferredMove = true;
		}
	}
	else if (Sanitized.CommandKind == EGuLiCommandKind::Move
		&& bPendingMoveIntent
		&& Sanitized.ClientCommandId == PendingMoveIntent.ClientCommandId)
	{
		bPendingMoveIntent = false;
		bPendingMoveFastRetry = false;
		bPendingMoveReliableFallback = false;
	}

	// 这里只抑制与上一次相同种类/ID 的重复通知，不是全历史去重集合。
	if (LastDeliveredAckKind == Sanitized.CommandKind
		&& LastDeliveredAckCommandId == Sanitized.ClientCommandId)
	{
		return;
	}
	LastDeliveredAckKind = Sanitized.CommandKind;
	LastDeliveredAckCommandId = Sanitized.ClientCommandId;

	LastCommandAck = Sanitized;
	if (PendingCommandAcks.Num() >= GuLiCommanderNetwork::MaxPendingCommandAcks)
	{
		PendingCommandAcks.RemoveAt(
			0,
			PendingCommandAcks.Num() - GuLiCommanderNetwork::MaxPendingCommandAcks + 1,
			EAllowShrinking::No);
	}
	PendingCommandAcks.Add(Sanitized);
	OnCommandAckChanged.Broadcast(LastCommandAck);
	if (bStartNextSelection)
	{
		BeginSelectionIntent(NextSelectionRequest);
	}
	else if (bStartDeferredMove)
	{
		BeginMoveIntent(DeferredMoveRequest);
	}
}

// 先检查协议/初始同步/战局，再整理数据并入队；后续样本排序与旧数据过滤在 PresentationActor。
void UGuLiCommanderNetSyncComponent::ClientReceiveSoldierPoseChunk_Implementation(
	const FGuLiSoldierPoseChunk& Chunk)
{
	// 测试注入没有 Actor/World，仍复用下方原始协议谓词；网络接收须先通过公共连接门。
	if (GetOwner() && !IsSoldierStreamReady())
	{
		return;
	}
	if (!GuLiCommanderNetwork::IsClientPoseChunkAcceptable(
		Chunk,
		bClientPoseReady,
		ClientAcceptedMatchEpoch))
	{
		return;
	}

	FGuLiSoldierPoseChunk Sanitized = Chunk;
	Sanitized.Sanitize();
	const int32 MaximumPendingPoseChunks = static_cast<int32>(GULI_MAX_POSE_CHUNKS_PER_FRAME);
	if (PendingPoseChunks.Num() >= MaximumPendingPoseChunks)
	{
		PendingPoseChunks.RemoveAt(
			0,
			PendingPoseChunks.Num() - MaximumPendingPoseChunks + 1,
			EAllowShrinking::No);
	}
	PendingPoseChunks.Add(Sanitized);
#if !UE_BUILD_SHIPPING
	// 仅用于非 Shipping 的新帧统计，不意味着整帧所有块已收齐，也不会据此拒绝其他块。
	if (LastAcceptedPoseFrameSequence == 0u
		|| IsNewerSerial(Sanitized.FrameSequence, LastAcceptedPoseFrameSequence))
	{
		LastAcceptedPoseFrameSequence = Sanitized.FrameSequence;
		++AcceptedPoseFrameCount;
	}
	LastAcceptedPoseReceiveTimeSeconds = FPlatformTime::Seconds();
#endif
	OnPoseChunkReceived.Broadcast(PendingPoseChunks.Last());
}

void UGuLiCommanderNetSyncComponent::OnRep_SelectionState()
{
	NotifySelectionChanged();
}

void UGuLiCommanderNetSyncComponent::OnRep_SyncGeneration()
{
	if ((PendingBootstrapGeneration != 0u && PendingBootstrapGeneration != SyncGeneration)
		|| (ClientAcceptedSyncGeneration != 0u && ClientAcceptedSyncGeneration != SyncGeneration))
	{
		bClientPoseReady = false;
		ClientAcceptedSyncGeneration = 0u;
		ClientAcceptedMatchEpoch = 0u;
		PendingPoseChunks.Reset();
	}
}

void UGuLiCommanderNetSyncComponent::ResendServerBootstrapMarker()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || SyncGeneration == 0u
		|| BootstrapMatchEpoch == 0u || BootstrapExpectedRosterCount == 0u
		|| BootstrapExpectedSnapshotRevision == 0u || !IsConnectionReady())
	{
		return;
	}

	ClientBootstrapStarted(
		SyncGeneration,
		GULI_COMMANDER_PROTOCOL_VERSION,
		BootstrapMatchEpoch,
		BootstrapExpectedRosterCount,
		BootstrapExpectedSnapshotRevision);
}

// 本地控制端持续检查跨对象依赖；GameState、快照版本和有效唯一 SoldierId 名册都满足后确认。
void UGuLiCommanderNetSyncComponent::TryCompleteClientBootstrap()
{
	APlayerController* Controller = GetOwningPlayerController();
	UWorld* World = GetWorld();
	if (!Controller || !Controller->IsLocalController() || !World
		|| PendingBootstrapGeneration == 0u || PendingBootstrapMatchEpoch == 0u
		|| PendingBootstrapRosterCount == 0u || PendingBootstrapSnapshotRevision == 0u
		|| bClientPoseReady || !IsConnectionReady()
		|| SoldierBootstrapBinding.ConnectionGeneration != GetConnectionGeneration()
		|| SoldierBootstrapBinding.SoldierSyncGeneration != PendingBootstrapGeneration)
	{
		return;
	}

	const AGuLiBattleGameState* BattleGameState = World->GetGameState<AGuLiBattleGameState>();
	if (!BattleGameState)
	{
		return;
	}
	if (BattleGameState->GetProtocolVersion() != GULI_COMMANDER_PROTOCOL_VERSION)
	{
		if (!bLoggedBootstrapProtocolMismatch)
		{
			UE_LOG(
				LogGuLiStrike,
				Error,
				TEXT("Commander GameState protocol mismatch: server=%u client=%u generation=%u."),
				BattleGameState->GetProtocolVersion(),
				GULI_COMMANDER_PROTOCOL_VERSION,
				PendingBootstrapGeneration);
			bLoggedBootstrapProtocolMismatch = true;
		}
		return;
	}
	if (BattleGameState->GetMatchEpoch() == 0u
		|| BattleGameState->GetMatchEpoch() != PendingBootstrapMatchEpoch)
	{
		return;
	}

	const AGuLiSoldierStateReplicator* SoldierReplicator = nullptr;
	for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
	{
		SoldierReplicator = *It;
		break;
	}
	if (!SoldierReplicator
		|| !GuLiCommanderNetwork::IsBootstrapSnapshotCompatible(
			BattleGameState->GetProtocolVersion(),
			BattleGameState->GetMatchEpoch(),
			SoldierReplicator->GetSnapshotMatchEpoch(),
			SoldierReplicator->GetSnapshotRevision(),
			SoldierReplicator->GetItems().Num(),
			PendingBootstrapMatchEpoch,
			PendingBootstrapSnapshotRevision,
			PendingBootstrapRosterCount))
	{
		return;
	}

	TSet<FGuLiSoldierId> AppliedRoster;
	AppliedRoster.Reserve(SoldierReplicator->GetItems().Num());
	for (const FGuLiSoldierStateItem& Item : SoldierReplicator->GetItems())
	{
		if (Item.SoldierId.IsValid())
		{
			AppliedRoster.Add(Item.SoldierId);
		}
	}
	if (AppliedRoster.Num() < PendingBootstrapRosterCount)
	{
		return;
	}

	const uint32 CompletedGeneration = PendingBootstrapGeneration;
	const uint32 CompletedMatchEpoch = PendingBootstrapMatchEpoch;
	const uint16 CompletedRosterCount = PendingBootstrapRosterCount;
	const uint32 CompletedSnapshotRevision = SoldierReplicator->GetSnapshotRevision();
	PendingBootstrapGeneration = 0u;
	PendingBootstrapMatchEpoch = 0u;
	PendingBootstrapRosterCount = 0u;
	PendingBootstrapSnapshotRevision = 0u;
	ClientAcceptedMatchEpoch = CompletedMatchEpoch;
	ClientAcceptedSyncGeneration = CompletedGeneration;
	ClientAppliedRosterCount = CompletedRosterCount;
	ClientAppliedSnapshotRevision = CompletedSnapshotRevision;
	// 客户端先打开本地姿态门并回报；服务器收到下面的可靠 ACK 后才设置 PlayerState 的 bSyncReady。
	bClientPoseReady = true;
	ServerAcknowledgeBootstrap(
		CompletedGeneration,
		GULI_COMMANDER_PROTOCOL_VERSION,
		CompletedMatchEpoch,
		CompletedRosterCount,
		CompletedSnapshotRevision);
}

#if WITH_DEV_AUTOMATION_TESTS
bool UGuLiCommanderNetSyncComponent::TestOnly_IsBootstrapSnapshotCompatible(
	const uint16 ProtocolVersion,
	const uint32 GameStateMatchEpoch,
	const uint32 SnapshotMatchEpoch,
	const uint32 SnapshotRevision,
	const int32 RosterCount,
	const uint32 ExpectedMatchEpoch,
	const uint32 ExpectedSnapshotRevision,
	const uint16 ExpectedRosterCount)
{
	return GuLiCommanderNetwork::IsBootstrapSnapshotCompatible(
		ProtocolVersion,
		GameStateMatchEpoch,
		SnapshotMatchEpoch,
		SnapshotRevision,
		RosterCount,
		ExpectedMatchEpoch,
		ExpectedSnapshotRevision,
		ExpectedRosterCount);
}

bool UGuLiCommanderNetSyncComponent::TestOnly_ShouldStartNewServerBootstrap(
	const uint32 ExistingSyncGeneration,
	const uint32 ExistingBootstrapMatchEpoch,
	const uint32 AuthorityMatchEpoch,
	const bool bPlayerSyncReady)
{
	return GuLiCommanderNetwork::EvaluateBootstrapAuthorityAction(
		ExistingSyncGeneration,
		ExistingBootstrapMatchEpoch,
		AuthorityMatchEpoch,
		bPlayerSyncReady)
		== GuLiCommanderNetwork::EBootstrapAuthorityAction::StartNewGeneration;
}

void UGuLiCommanderNetSyncComponent::TestOnly_ReceiveCommandAck(const FGuLiCommandAck& Ack)
{
	ClientReceiveCommandAck_Implementation(Ack);
}

void UGuLiCommanderNetSyncComponent::TestOnly_ConfigureClientPoseGate(
	const bool bReady,
	const uint32 AcceptedMatchEpoch)
{
	bClientPoseReady = bReady;
	ClientAcceptedMatchEpoch = AcceptedMatchEpoch;
	PendingPoseChunks.Reset();
}

void UGuLiCommanderNetSyncComponent::TestOnly_ReceivePoseChunk(
	const FGuLiSoldierPoseChunk& Chunk)
{
	ClientReceiveSoldierPoseChunk_Implementation(Chunk);
}
#endif

// 拥有 RPC 通道不等于具有玩法权限：还要求服务器端、Commander 角色和初始同步就绪。
// 未就绪与非指挥官在当前实现中都返回 Unauthorized；通过后才消耗普通命令限流额度。
bool UGuLiCommanderNetSyncComponent::CanProcessCommanderRequest(
	FGuLiCommandAck& InOutAck,
	const TCHAR* RpcName)
{
	AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !BattlePlayerState
		|| !BattlePlayerState->IsCommander() || !IsConnectionReady() || !IsSoldierStreamReady())
	{
		InOutAck.Result = EGuLiCommandAckResult::Unauthorized;
		LogRejectedRpc(RpcName, InOutAck.Result);
		return false;
	}

	if (!ConsumeCommandRateLimit())
	{
		InOutAck.Result = EGuLiCommandAckResult::RateLimited;
		LogRejectedRpc(RpcName, InOutAck.Result);
		return false;
	}

	return true;
}

// 每连接共享一秒滑动窗口，保留最近请求时间，最多十次；超限分支可能静默丢弃而不再发 ACK。
bool UGuLiCommanderNetSyncComponent::ConsumeCommandRateLimit()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return CommanderRequestWindow.Consume(
		World->GetRealTimeSeconds(),
		GuLiCommanderNetwork::MaxCommanderRequestsPerSecond,
		GuLiCommanderNetwork::CommanderRequestWindowSeconds);
}

// 快速缓存回执每命令最多补发两次；此预算独立于普通新命令的一秒窗口。
bool UGuLiCommanderNetSyncComponent::ConsumeCachedAckReplayLimit(
	uint8& ReplayCount)
{
	constexpr uint8 MaximumFastAckReplaysPerCommand = 2u;
	if (ReplayCount >= MaximumFastAckReplaysPerCommand)
	{
		return false;
	}
	++ReplayCount;
	return true;
}

void UGuLiCommanderNetSyncComponent::InitializeAck(
	FGuLiCommandAck& Ack,
	const uint32 ClientId,
	const EGuLiCommandKind CommandKind) const
{
	Ack.CommandKind = CommandKind;
	Ack.ClientCommandId = ClientId;
	Ack.BatchOrderId = 0;
	Ack.Result = EGuLiCommandAckResult::InvalidRequest;
	Ack.ServerSelectionRevision = SelectionState.SelectionRevision;
	Ack.CohortResults.Reset();
}

// 服务器把业务回执送回拥有客户端；可靠性由请求入口决定，LastCommandAck 本身没有属性复制。
void UGuLiCommanderNetSyncComponent::PublishAck(
	const FGuLiCommandAck& Ack,
	const bool bReliableDelivery)
{
	LastCommandAck = Ack;
	LastCommandAck.Sanitize();
	if (AActor* Owner = GetOwner())
	{
		if (Owner->HasAuthority())
		{
			if (bReliableDelivery)
			{
				ClientReceiveCommandAck(LastCommandAck);
			}
			else
			{
				ClientReceiveCommandAckFast(LastCommandAck);
			}
		}
	}
}

void UGuLiCommanderNetSyncComponent::NotifySelectionChanged()
{
	OnSelectionChanged.Broadcast(SelectionState);
}

void UGuLiCommanderNetSyncComponent::LogRejectedRpc(
	const TCHAR* RpcName,
	EGuLiCommandAckResult Result)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : 0.0;
	if (LastSecurityLogTime < 0.0
		|| Now - LastSecurityLogTime >= GuLiCommanderNetwork::SecurityLogIntervalSeconds)
	{
		UE_LOG(
			LogGuLiStrike,
			Warning,
			TEXT("Commander RPC rejected: %s result=%u suppressed=%u owner=%s"),
			RpcName,
			static_cast<uint8>(Result),
			SuppressedSecurityLogCount,
			*GetNameSafe(GetOwner()));
		LastSecurityLogTime = Now;
		SuppressedSecurityLogCount = 0;
	}
	else
	{
		++SuppressedSecurityLogCount;
	}
}

void UGuLiCommanderNetSyncComponent::MirrorSyncReadyToRoleSlot(
	const AGuLiBattlePlayerState& BattlePlayerState,
	bool bReady) const
{
	if (BattlePlayerState.GetBattleSlotIndex() == AGuLiBattlePlayerState::InvalidSlotIndex)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (AGuLiBattleGameState* BattleGameState = World->GetGameState<AGuLiBattleGameState>())
		{
			BattleGameState->SetRoleSlotSyncReady(
				BattlePlayerState.GetBattleSlotIndex(),
				BattlePlayerState.GetPlayerGuid(),
				bReady);
		}
	}
}

// 无符号差值再转有符号值用于序号回绕比较；前提是新旧距离小于半个 uint32 序号空间。
bool UGuLiCommanderNetSyncComponent::IsNewerSerial(uint32 Candidate, uint32 Baseline)
{
	return static_cast<int32>(Candidate - Baseline) > 0;
}

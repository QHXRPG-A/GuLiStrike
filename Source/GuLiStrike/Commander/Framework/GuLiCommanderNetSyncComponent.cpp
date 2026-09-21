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
#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
	constexpr uint32 SelectionSnapshotMoveDependencyKey = 0u;
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
			&& Lhs.Kind == Rhs.Kind
			&& Lhs.SeedSoldierId == Rhs.SeedSoldierId
			&& FVector(Lhs.RayOrigin).Equals(FVector(Rhs.RayOrigin), 0.5f)
			&& FVector(Lhs.RayDirection).Equals(FVector(Rhs.RayDirection), 0.0001f)
			&& FMath::IsNearlyEqual(Lhs.PickHalfAngleRadians, Rhs.PickHalfAngleRadians, 0.00001f)
			&& FVector(Lhs.BoxTopLeftRay).Equals(FVector(Rhs.BoxTopLeftRay), 0.0001f)
			&& FVector(Lhs.BoxTopRightRay).Equals(FVector(Rhs.BoxTopRightRay), 0.0001f)
			&& FVector(Lhs.BoxBottomRightRay).Equals(FVector(Rhs.BoxBottomRightRay), 0.0001f)
			&& FVector(Lhs.BoxBottomLeftRay).Equals(FVector(Rhs.BoxBottomLeftRay), 0.0001f)
			&& Lhs.KnownSelectionRevision == Rhs.KnownSelectionRevision
			&& Lhs.RadiusPreset == Rhs.RadiusPreset
			&& Lhs.Modifier == Rhs.Modifier
			&& FVector(Lhs.Center).Equals(FVector(Rhs.Center), 0.5f);
	}

	static bool IsSameMoveRequest(const FGuLiMoveRequest& Lhs, const FGuLiMoveRequest& Rhs)
	{
		return Lhs.ClientCommandId == Rhs.ClientCommandId
			&& Lhs.SelectionRevision == Rhs.SelectionRevision
			&& Lhs.TargetTerritoryId == Rhs.TargetTerritoryId
			&& Lhs.MiningOrderType == Rhs.MiningOrderType
			&& Lhs.TargetClusterId == Rhs.TargetClusterId
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
	TickPoseAcknowledgments();
	TickOrderedCommands();
	if (GetOwner() && GetOwner()->HasAuthority() && GetWorld())
	{
		AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
		UGuLiBattleAuthoritySubsystem* Authority =
			GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		if (bServerMovePlanningPending)
		{
			const AGuLiBattlePlayerState* PlanningPlayerState =
				PendingServerMovePlanningPlayerState.Get();
			const bool bPlanningOwnerStillCurrent = PlanningPlayerState
				&& PlanningPlayerState == BattlePlayerState
				&& PendingServerMovePlanningConnectionGeneration != 0u
				&& PendingServerMovePlanningConnectionGeneration == GetConnectionGeneration()
				&& IsConnectionReady();
			if (!bPlanningOwnerStillCurrent || !Authority)
			{
				CancelPendingServerMovePlanning();
			}
			else
			{
				FGuLiCommandAck FinalAck;
				FGuLiCommanderSelectionState UpdatedSelection;
				bool bSelectionChanged = false;
				const EGuLiMovePlanningStatus Status = Authority->PollMovePlanning(
					*PlanningPlayerState,
					PendingServerMovePlanningRequest.ClientCommandId,
					FinalAck,
					UpdatedSelection,
					bSelectionChanged);
				if (Status == EGuLiMovePlanningStatus::Completed)
				{
					FinalizeServerMovePlanning(
						PendingServerMovePlanningRequest,
						FinalAck,
						bSelectionChanged ? &UpdatedSelection : nullptr);
					NextServerMoveEndpointRefreshTimeSeconds = 0.0;
				}
				else if (Status == EGuLiMovePlanningStatus::NotFound)
				{
					// The authority population may have been rebuilt independently of the connection.
					// Resolve the client intent instead of leaving this component permanently locked pending.
					InitializeAck(
						FinalAck,
						PendingServerMovePlanningRequest.ClientCommandId,
						EGuLiCommandKind::Move);
					FinalAck.Result = EGuLiCommandAckResult::InvalidRequest;
					FinalizeServerMovePlanning(
						PendingServerMovePlanningRequest,
						FinalAck,
						nullptr);
				}
			}
		}

		const bool bCanPublishMoveEndpoints = BattlePlayerState && Authority
			&& BattlePlayerState->IsCommander() && IsSoldierStreamReady();
		if (!bCanPublishMoveEndpoints)
		{
			// Do not repopulate a just-reset OwnerOnly array before the replacement identity has
			// completed its soldier bootstrap, and never expose a team's routes to a non-commander.
			ServerClearMoveEndpoints();
		}
		const double Now = GetWorld()->GetRealTimeSeconds();
		if (bCanPublishMoveEndpoints && Now >= NextServerMoveEndpointRefreshTimeSeconds)
		{
			NextServerMoveEndpointRefreshTimeSeconds = Now + (1.0 / 30.0);
			TArray<FGuLiMoveEndpointSnapshot> AuthorityEndpoints;
			Authority->BuildActiveMoveEndpointSnapshot(
				BattlePlayerState->GetTeam(),
				AuthorityEndpoints);
			TArray<FGuLiMoveEndpointItem> ReplicatedEndpoints;
			ReplicatedEndpoints.Reserve(AuthorityEndpoints.Num());
			for (const FGuLiMoveEndpointSnapshot& Source : AuthorityEndpoints)
			{
				FGuLiMoveEndpointItem& Target = ReplicatedEndpoints.AddDefaulted_GetRef();
				Target.SoldierId = Source.SoldierId;
				Target.ActiveOrderId = Source.ActiveOrderId;
				Target.CommandStart = Source.CommandStart;
				Target.FinalDestination = Source.FinalDestination;
				Target.Revision = FMath::Max(1u, Source.Revision);
			}
			ServerBootstrapMoveEndpoints(ReplicatedEndpoints);
		}
	}
}

void UGuLiCommanderNetSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPendingServerMovePlanning();
	if (PoseRoster.IsValid()) PoseRoster->OnSoldiersRemoved().RemoveAll(this);
	PoseSender.Reset(); PoseReceiver.Reset();
	Super::EndPlay(EndPlayReason);
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
	LastAppliedClientSelection = FGuLiCommanderSelectionState{};
	PoseReceiver.Reset();
	NextPoseAckTime = 0.0;
	PendingPoseChunks.Reset();
	PendingCommandAcks.Reset();
	QueuedSelectionIntents.Reset();
	PendingCommandRecoveryId = 0u;
	PendingSelectionDeadline = PendingMoveDeadline = SelectionSnapshotDeadline = 0.0;
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
	bHasDeferredMoveAfterCurrentAck = false;
	bDeferredMoveHasSelectionDependency = false;
	bAwaitingSelectionSnapshot = false;
	bResolvedSelectionAccepted = false;
	bAdvancingSelectionQueue = false;
	ResolvedSelectionRequestId = 0u;
	ResolvedSelectionRevision = 0u;
	DeferredMoveSelectionDependencyKey = 0u;
	AwaitedSelectionRevision = 0u;
	PendingSelectionFastRetryTimeSeconds = 0.0;
	PendingSelectionReliableFallbackTimeSeconds = 0.0;
	PendingMoveFastRetryTimeSeconds = 0.0;
	PendingMoveReliableFallbackTimeSeconds = 0.0;
	PendingSelectionIntent = FGuLiSelectionRequest{};
	PendingMoveIntent = FGuLiMoveRequest{};
	DeferredMoveAfterCurrentAck = FGuLiMoveRequest{};
	MovesAwaitingSelection.Reset();
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
	PendingOrderedSelections.Reset(); PendingOrderedTasks.Reset(); LastOrderedSelectionSequence = 0;
	LastTaskFeedback.Reset(); TaskSummaries.Reset(); bOrderedSelectionValid = true;
	PendingTaskSnapshot.Reset(); ReceivedTaskSnapshot.Reset(); PendingGroupCounts.Reset(); ReceivedGroupCounts.Reset();
	RelatedControlGroups = PendingRelatedGroups = ReceivedRelatedGroups = 0;
	TaskSnapshotOffset = INDEX_NONE; TaskSnapshotRevision = 0; ReceivedTaskSnapshotRevision = 0; TaskSnapshotSelectionRevision = 0;
	// The current PlayerState may already be the replacement identity. Cancel through the identity
	// captured when planning began so an old connection cannot commit after this reset.
	CancelPendingServerMovePlanning();
	Super::OnConnectionBootstrapReset();
	PoseSender.Reset();
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
	NextServerMoveEndpointRefreshTimeSeconds = 0.0;
	SelectionCachedFastReplayCount = 0u;
	MoveCachedFastReplayCount = 0u;
	CommanderRequestWindow.Reset();
	LastSecurityLogTime = -1.0;
	SuppressedSecurityLogCount = 0u;
	// SyncGeneration 保留为递增计数器；清空期望战局撤销当前代次，下一次启动不会复用旧数字。
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerClearMoveEndpoints();
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
	DOREPLIFETIME_CONDITION(UGuLiCommanderNetSyncComponent, MoveEndpoints, COND_OwnerOnly);
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
		ControlGroups.Reset(); ControlGroupCounts.Reset(); TaskSummaries.Reset();
		bOrderedSelectionValid = true; LastOrderedSequence = 0; OrderedGeneration = 0;
		ServerClearMoveEndpoints();
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
	PoseSender.Reset(SyncGeneration, BootstrapMatchEpoch);
	BindPoseRoster(*SoldierReplicator);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Send);
	GuLiCommanderPoseMetrics::FScope Measure(GuLiCommanderPoseMetrics::EScope::Send);
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| !IsSoldierStreamReady() || Chunk.AuthorityEpoch != ServerAcceptedMatchEpoch)
	{
		return;
	}
	TArray<FGuLiEncodedPoseBlock> Blocks;
	PoseSender.Encode(Chunk, Blocks);
	for (const FGuLiEncodedPoseBlock& Block : Blocks) ClientReceiveEncodedPoseBlock(Block);
}

bool UGuLiCommanderNetSyncComponent::RefreshServerSelection()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !IsSoldierStreamReady())
	{
		return false;
	}
	AGuLiBattlePlayerState* BattlePlayerState = GetBattlePlayerState();
	UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	if (!BattlePlayerState || !Authority) return false;
	bool bChanged = Authority->RefreshSelection(BattlePlayerState->GetTeam(), SelectionState);
	FGuLiCommanderControlGroup Vehicles; Vehicles.Actors = SelectionState.ActorIds;
	PruneControlGroup(Vehicles, BattlePlayerState->GetTeam());
	if (Vehicles.Actors != SelectionState.ActorIds)
	{
		SelectionState.ActorIds = MoveTemp(Vehicles.Actors);
		if (!++SelectionState.SelectionRevision) ++SelectionState.SelectionRevision;
		bChanged = true;
	}
	if (!bChanged) return false;
	ClientOrderedSelection(SelectionState, 0, false, GetConnectionGeneration());
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

// 本机和远端拥有者共用同一串行状态机，避免 Standalone/ListenServer 在异步移动期间
// 直接发出下一次选兵而被服务器拒绝。
void UGuLiCommanderNetSyncComponent::SubmitSelectionRequest(
	const FGuLiSelectionRequest& Request)
{
	if (!GetOwner())
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
	if (Request.Modifier == EGuLiSelectionModifier::Clear || Request.Modifier == EGuLiSelectionModifier::Replace)
	{
		if (bPendingSelectionIntent) ClientRetiredSelectionThrough = PendingSelectionIntent.ClientRequestId;
		for (const auto& Queued : QueuedSelectionIntents)
			if (!ClientRetiredSelectionThrough || IsNewerSerial(Queued.ClientRequestId, ClientRetiredSelectionThrough))
				ClientRetiredSelectionThrough = Queued.ClientRequestId;
		QueuedSelectionIntents.Reset();
		bPendingSelectionIntent = bPendingSelectionFastRetry = bPendingSelectionReliableFallback = false;
		bAwaitingSelectionSnapshot = false; AwaitedSelectionRevision = 0u;
		PendingCommandRecoveryId = 0u;
		const uint32 CancelThrough = RetireLocalMoveIntents(EGuLiCommandAckResult::Cancelled);
		PendingSelectionIntent = Request;
		bPendingSelectionIntent = true;
		PendingSelectionDeadline = World->GetRealTimeSeconds() + 8.0;
		ServerReplaceSelection(Request, CancelThrough, GetConnectionGeneration());
		return;
	}
	if (bPendingSelectionIntent || bAwaitingSelectionSnapshot || bPendingMoveIntent || PendingCommandRecoveryId)
	{
		if (QueuedSelectionIntents.Num() >= GuLiCommanderNetwork::MaxQueuedSelectionIntents)
		{
			RejectDeferredMove(QueuedSelectionIntents[0].ClientRequestId, EGuLiCommandAckResult::RateLimited);
			QueuedSelectionIntents.RemoveAt(0, 1, EAllowShrinking::No);
		}
		QueuedSelectionIntents.Add(Request);
		return;
	}
	FGuLiSelectionRequest CurrentRequest = Request;
	if (ResolvedSelectionRevision != 0u
		&& IsNewerSerial(ResolvedSelectionRevision, CurrentRequest.KnownSelectionRevision))
	{
		CurrentRequest.KnownSelectionRevision = ResolvedSelectionRevision;
	}
	BeginSelectionIntent(CurrentRequest);
}

bool UGuLiCommanderNetSyncComponent::ServerUpsertMoveEndpoint(
	const FGuLiSoldierId SoldierId,
	const uint32 ActiveOrderId,
	const FVector& CommandStart,
	const FVector& FinalDestination)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| !MoveEndpoints.Upsert(SoldierId, ActiveOrderId, CommandStart, FinalDestination))
	{
		return false;
	}
	GetOwner()->ForceNetUpdate();
	OnMoveEndpointsChanged.Broadcast(MoveEndpoints);
	return true;
}

bool UGuLiCommanderNetSyncComponent::ServerRemoveMoveEndpoint(const FGuLiSoldierId SoldierId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !MoveEndpoints.Remove(SoldierId))
	{
		return false;
	}
	GetOwner()->ForceNetUpdate();
	OnMoveEndpointsChanged.Broadcast(MoveEndpoints);
	return true;
}

int32 UGuLiCommanderNetSyncComponent::ServerBootstrapMoveEndpoints(
	const TConstArrayView<FGuLiMoveEndpointItem> Endpoints)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	const int32 ChangedCount = MoveEndpoints.ReplaceWith(Endpoints);
	if (ChangedCount > 0)
	{
		GetOwner()->ForceNetUpdate();
		OnMoveEndpointsChanged.Broadcast(MoveEndpoints);
	}
	return ChangedCount;
}

bool UGuLiCommanderNetSyncComponent::ServerClearMoveEndpoints()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !MoveEndpoints.ResetEndpoints())
	{
		return false;
	}
	GetOwner()->ForceNetUpdate();
	OnMoveEndpointsChanged.Broadcast(MoveEndpoints);
	return true;
}

bool UGuLiCommanderNetSyncComponent::BeginServerMovePlanning(
	const FGuLiMoveRequest& Request,
	const bool bReliableDelivery)
{
	AGuLiBattlePlayerState* PlanningPlayerState = GetBattlePlayerState();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !PlanningPlayerState
		|| !IsConnectionReady() || !Request.IsWellFormed()
		|| LastMoveCommandId != Request.ClientCommandId
		|| !GuLiCommanderNetwork::IsSameMoveRequest(LastMoveRequest, Request))
	{
		return false;
	}
	if (bServerMovePlanningPending)
	{
		if (!GuLiCommanderNetwork::IsSameMoveRequest(PendingServerMovePlanningRequest, Request))
		{
			return false;
		}
		bPendingServerMoveReliableDelivery |= bReliableDelivery;
		return true;
	}
	bServerMovePlanningPending = true;
	// The planner may span many frames; its one final mask/BatchOrderId result is always reliable.
	bPendingServerMoveReliableDelivery = true;
	PendingServerMovePlanningRequest = Request;
	PendingServerMovePlanningPlayerState = PlanningPlayerState;
	PendingServerMovePlanningConnectionGeneration = GetConnectionGeneration();
	return true;
}

bool UGuLiCommanderNetSyncComponent::FinalizeServerMovePlanning(
	const FGuLiMoveRequest& Request,
	const FGuLiCommandAck& FinalAck,
	const FGuLiCommanderSelectionState* UpdatedSelection)
{
	const AGuLiBattlePlayerState* PlanningPlayerState =
		PendingServerMovePlanningPlayerState.Get();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bServerMovePlanningPending
		|| !GuLiCommanderNetwork::IsSameMoveRequest(PendingServerMovePlanningRequest, Request))
	{
		return false;
	}
	if (!PlanningPlayerState || PlanningPlayerState != GetBattlePlayerState()
		|| PendingServerMovePlanningConnectionGeneration == 0u
		|| PendingServerMovePlanningConnectionGeneration != GetConnectionGeneration()
		|| !IsConnectionReady())
	{
		CancelPendingServerMovePlanning();
		return false;
	}

	FGuLiCommandAck SanitizedAck = FinalAck;
	SanitizedAck.Sanitize();
	if (SanitizedAck.CommandKind != EGuLiCommandKind::Move
		|| SanitizedAck.ClientCommandId != Request.ClientCommandId)
	{
		return false;
	}

	if (UpdatedSelection)
	{
		FGuLiCommanderSelectionState SanitizedSelection = *UpdatedSelection;
		SanitizedSelection.Sanitize();
		if (SanitizedSelection.SelectionRevision != SanitizedAck.ServerSelectionRevision)
		{
			return false;
		}
		SelectionState = MoveTemp(SanitizedSelection);
		NotifySelectionChanged();
		GetOwner()->ForceNetUpdate();
	}

	const bool bReliableDelivery = bPendingServerMoveReliableDelivery
		|| SanitizedAck.Result == EGuLiCommandAckResult::Cancelled
		|| SanitizedAck.Result == EGuLiCommandAckResult::TimedOut;
	bServerMovePlanningPending = false;
	bPendingServerMoveReliableDelivery = false;
	PendingServerMovePlanningRequest = FGuLiMoveRequest{};
	PendingServerMovePlanningPlayerState.Reset();
	PendingServerMovePlanningConnectionGeneration = 0u;
	LastMoveAck = SanitizedAck;
	PublishAck(LastMoveAck, bReliableDelivery);
	return true;
}

void UGuLiCommanderNetSyncComponent::CancelPendingServerMovePlanning()
{
	const TWeakObjectPtr<const AGuLiBattlePlayerState> PlanningPlayerState =
		PendingServerMovePlanningPlayerState;
	if (GetOwner() && GetOwner()->HasAuthority() && PlanningPlayerState.IsValid())
	{
		if (UGuLiBattleAuthoritySubsystem* Authority = GetWorld()
			? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
			: nullptr)
		{
			Authority->CancelMovePlanning(*PlanningPlayerState.Get());
		}
	}
	bServerMovePlanningPending = false;
	bPendingServerMoveReliableDelivery = false;
	PendingServerMovePlanningRequest = FGuLiMoveRequest{};
	PendingServerMovePlanningPlayerState.Reset();
	PendingServerMovePlanningConnectionGeneration = 0u;
}

bool UGuLiCommanderNetSyncComponent::IsServerMovePlanningPending(
	const FGuLiMoveRequest& Request) const
{
	return bServerMovePlanningPending
		&& GuLiCommanderNetwork::IsSameMoveRequest(PendingServerMovePlanningRequest, Request);
}

bool UGuLiCommanderNetSyncComponent::IsMoveCommandPending(const uint32 ClientCommandId) const
{
	if (PendingOrderedTasks.Contains(ClientCommandId)) return true;
	if (ClientCommandId == 0u)
	{
		return false;
	}
	if ((bPendingMoveIntent && PendingMoveIntent.ClientCommandId == ClientCommandId)
		|| (bHasDeferredMoveAfterCurrentAck
			&& DeferredMoveAfterCurrentAck.ClientCommandId == ClientCommandId)
		|| (bServerMovePlanningPending
			&& PendingServerMovePlanningRequest.ClientCommandId == ClientCommandId))
	{
		return true;
	}
	for (const TPair<uint32, FGuLiMoveRequest>& Deferred : MovesAwaitingSelection)
	{
		if (Deferred.Value.ClientCommandId == ClientCommandId)
		{
			return true;
		}
	}
	return false;
}

bool UGuLiCommanderNetSyncComponent::HasUnresolvedSelectionIntent() const
{
	return !PendingOrderedSelections.IsEmpty() || bPendingSelectionIntent || bAwaitingSelectionSnapshot || PendingCommandRecoveryId || !QueuedSelectionIntents.IsEmpty();
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
	PendingSelectionDeadline = NowSeconds + 8.0;
	bPendingSelectionFastRetry = true;
	bPendingSelectionReliableFallback = true;
	PendingSelectionFastRetryTimeSeconds = NowSeconds + GuLiCommanderNetwork::FastCommandRetryDelaySeconds;
	PendingSelectionReliableFallbackTimeSeconds = NowSeconds + GuLiCommanderNetwork::ReliableCommandFallbackDelaySeconds;
	ServerRequestSelectionFast(Request);
}

// 每次选兵意图保留自己的最新移动目标；不同选兵意图不能互相覆盖未发出的移动。
void UGuLiCommanderNetSyncComponent::SubmitMoveRequest(const FGuLiMoveRequest& Request)
{
	if (Request.ClientCommandId && (!LatestSubmittedMoveId || IsNewerSerial(Request.ClientCommandId, LatestSubmittedMoveId)))
		LatestSubmittedMoveId = Request.ClientCommandId;
	if (!GetOwner())
	{
		OnMoveReadyToSend.Broadcast(Request, SelectionState);
		ServerIssueMove(Request);
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || Request.ClientCommandId == 0u)
	{
		ServerIssueMove(Request);
		return;
	}
	if (bPendingMoveIntent)
	{
		DeferMoveUntilCurrentAck(Request);
		return;
	}
	QueueOrBeginMoveIntent(Request);
}

void UGuLiCommanderNetSyncComponent::QueueOrBeginMoveIntent(
	const FGuLiMoveRequest& Request)
{
	if (HasUnresolvedSelectionIntent())
	{
		const uint32 SelectionRequestId = !QueuedSelectionIntents.IsEmpty()
			? QueuedSelectionIntents.Last().ClientRequestId
			: (bPendingSelectionIntent
				? PendingSelectionIntent.ClientRequestId
				: (bAwaitingSelectionSnapshot || PendingCommandRecoveryId
					? GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey
					: ResolvedSelectionRequestId));
		MovesAwaitingSelection.Add(SelectionRequestId, Request);
		return;
	}
	FGuLiMoveRequest CurrentRequest = Request;
	CurrentRequest.SelectionRevision = SelectionState.SelectionRevision;
	BeginMoveIntent(CurrentRequest);
}

void UGuLiCommanderNetSyncComponent::DeferMoveUntilCurrentAck(
	const FGuLiMoveRequest& Request)
{
	// An application-level resubmit of the command already in flight must stay attached to that
	// command. Treating it as a future click would execute the same player intent twice.
	if (bPendingMoveIntent
		&& Request.ClientCommandId == PendingMoveIntent.ClientCommandId)
	{
		return;
	}
	DeferredMoveAfterCurrentAck = Request;
	bHasDeferredMoveAfterCurrentAck = true;
	bDeferredMoveHasSelectionDependency = HasUnresolvedSelectionIntent();
	DeferredMoveSelectionDependencyKey = bDeferredMoveHasSelectionDependency
		? (!QueuedSelectionIntents.IsEmpty()
			? QueuedSelectionIntents.Last().ClientRequestId
			: (bPendingSelectionIntent
				? PendingSelectionIntent.ClientRequestId
				: (bAwaitingSelectionSnapshot || PendingCommandRecoveryId
					? GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey
					: ResolvedSelectionRequestId)))
		: 0u;
}

void UGuLiCommanderNetSyncComponent::ReleaseDeferredMoveAfterCurrentAck()
{
	if (!bHasDeferredMoveAfterCurrentAck || bPendingMoveIntent)
	{
		return;
	}
	FGuLiMoveRequest DeferredMove = DeferredMoveAfterCurrentAck;
	const bool bHasFrozenSelectionDependency = bDeferredMoveHasSelectionDependency;
	const uint32 FrozenSelectionDependencyKey = DeferredMoveSelectionDependencyKey;
	bHasDeferredMoveAfterCurrentAck = false;
	bDeferredMoveHasSelectionDependency = false;
	DeferredMoveSelectionDependencyKey = 0u;
	DeferredMoveAfterCurrentAck = FGuLiMoveRequest{};
	if (bHasFrozenSelectionDependency)
	{
		MovesAwaitingSelection.Add(FrozenSelectionDependencyKey, DeferredMove);
		return;
	}
	if (bAwaitingSelectionSnapshot)
	{
		// The move click preceded any later selection intents, but the completed command pruned
		// membership. Wait only for that authority snapshot and retain the original action order.
		MovesAwaitingSelection.Add(GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey, DeferredMove);
		return;
	}
	DeferredMove.SelectionRevision = SelectionState.SelectionRevision;
	BeginMoveIntent(DeferredMove);
}


void UGuLiCommanderNetSyncComponent::BeginMoveIntent(const FGuLiMoveRequest& Request)
{
#if WITH_DEV_AUTOMATION_TESTS
	TestDispatchedMove = Request;
#endif
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double NowSeconds = World->GetRealTimeSeconds();
	PendingMoveIntent = Request;
	PendingMoveDeadline = NowSeconds + 8.0;
	bPendingMoveIntent = true;
	bPendingMoveFastRetry = true;
	bPendingMoveReliableFallback = true;
	PendingMoveFastRetryTimeSeconds = NowSeconds + GuLiCommanderNetwork::FastCommandRetryDelaySeconds;
	PendingMoveReliableFallbackTimeSeconds = NowSeconds + GuLiCommanderNetwork::ReliableCommandFallbackDelaySeconds;
	OnMoveReadyToSend.Broadcast(Request, SelectionState);
	ServerIssueMoveFast(Request);
}

void UGuLiCommanderNetSyncComponent::RejectDeferredMove(const uint32 SelectionRequestId, const EGuLiCommandAckResult Result)
{
	FGuLiMoveRequest Move;
	if (!MovesAwaitingSelection.RemoveAndCopyValue(SelectionRequestId, Move)) return;
	FGuLiCommandAck Rejection;
	Rejection.CommandKind = EGuLiCommandKind::Move;
	Rejection.ClientCommandId = Move.ClientCommandId;
	Rejection.ServerSelectionRevision = SelectionState.SelectionRevision;
	Rejection.Result = Result;
	ReceiveCommandAck(Rejection);
}

void UGuLiCommanderNetSyncComponent::AdvanceSelectionQueue()
{
	if (bAdvancingSelectionQueue || bPendingSelectionIntent || bPendingMoveIntent || PendingCommandRecoveryId) return;
	TGuardValue<bool> AdvancingGuard(bAdvancingSelectionQueue, true);
	// A rejected selection can cancel its dependent move immediately. A newer revision in the
	// rejection may still gate later commands, but there is no membership snapshot that can make
	// this particular deferred move valid.
	if (ResolvedSelectionRequestId != 0u && !bResolvedSelectionAccepted
		&& MovesAwaitingSelection.Contains(ResolvedSelectionRequestId))
	{
		RejectDeferredMove(ResolvedSelectionRequestId, EGuLiCommandAckResult::NoSelection);
	}
	if (bAwaitingSelectionSnapshot)
	{
		// ACK RPC and SelectionState property are separate channels. Do not dispatch against the older membership.
		if (SelectionState.SelectionRevision != AwaitedSelectionRevision
			&& !IsNewerSerial(SelectionState.SelectionRevision, AwaitedSelectionRevision)) return;
		bAwaitingSelectionSnapshot = false;
		AwaitedSelectionRevision = 0u;
	}
	if (const FGuLiMoveRequest* DeferredMove = MovesAwaitingSelection.Find(
		GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey))
	{
		if (SelectionState.Cohorts.IsEmpty() && SelectionState.ActorIds.IsEmpty())
		{
			RejectDeferredMove(
				GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey,
				EGuLiCommandAckResult::NoSelection);
		}
		else
		{
			FGuLiMoveRequest Move = *DeferredMove;
			Move.SelectionRevision = SelectionState.SelectionRevision;
			MovesAwaitingSelection.Remove(GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey);
			BeginMoveIntent(Move);
			return;
		}
	}
	if (const FGuLiMoveRequest* DeferredMove = MovesAwaitingSelection.Find(ResolvedSelectionRequestId))
	{
		if (!bResolvedSelectionAccepted
			|| (SelectionState.Cohorts.IsEmpty() && SelectionState.ActorIds.IsEmpty()))
		{
			RejectDeferredMove(ResolvedSelectionRequestId, EGuLiCommandAckResult::NoSelection);
		}
		else
		{
			FGuLiMoveRequest Move = *DeferredMove;
			Move.SelectionRevision = SelectionState.SelectionRevision;
			MovesAwaitingSelection.Remove(ResolvedSelectionRequestId);
			BeginMoveIntent(Move);
			// Fast RPC delivery is not ordered. Do not replace the server selection until this move is acknowledged.
			return;
		}
	}
	if (!QueuedSelectionIntents.IsEmpty())
	{
		FGuLiSelectionRequest Next = QueuedSelectionIntents[0];
		QueuedSelectionIntents.RemoveAt(0, 1, EAllowShrinking::No);
		Next.KnownSelectionRevision = ResolvedSelectionRevision;
		if (IsNewerSerial(SelectionState.SelectionRevision, Next.KnownSelectionRevision))
		{
			Next.KnownSelectionRevision = SelectionState.SelectionRevision;
		}
		BeginSelectionIntent(Next);
	}
}

// 快速补发与可靠回退各最多触发一次；收到匹配的业务 ACK 会清除对应标志。
void UGuLiCommanderNetSyncComponent::TickPendingCommandRetries()
{
	if (!GetOwner() || !IsSoldierStreamReady())
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double NowSeconds = World->GetRealTimeSeconds();
	if (PendingCommandRecoveryId)
	{
		if (NowSeconds >= PendingCommandRecoveryDeadline)
		{
			PendingCommandRecoveryDeadline = NowSeconds + 8.0;
			ServerRecoverCommanderCommands(PendingCommandRecoveryId, ClientRetiredMoveThrough, ClientRetiredSelectionThrough, GetConnectionGeneration());
		}
		return;
	}
	if ((bPendingSelectionIntent && NowSeconds >= PendingSelectionDeadline)
		|| (bPendingMoveIntent && NowSeconds >= PendingMoveDeadline)
		|| (bAwaitingSelectionSnapshot && SelectionSnapshotDeadline > 0 && NowSeconds >= SelectionSnapshotDeadline))
	{
		RequestCommandRecovery();
		return;
	}
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

uint32 UGuLiCommanderNetSyncComponent::RetireLocalMoveIntents(const EGuLiCommandAckResult Result)
{
	TArray<uint32> Retired;
	if (bPendingMoveIntent) Retired.AddUnique(PendingMoveIntent.ClientCommandId);
	if (bHasDeferredMoveAfterCurrentAck) Retired.AddUnique(DeferredMoveAfterCurrentAck.ClientCommandId);
	for (const auto& Entry : MovesAwaitingSelection) Retired.AddUnique(Entry.Value.ClientCommandId);
	bPendingMoveIntent = bPendingMoveFastRetry = bPendingMoveReliableFallback = false;
	bHasDeferredMoveAfterCurrentAck = bDeferredMoveHasSelectionDependency = false;
	MovesAwaitingSelection.Reset();
	for (uint32 Id : Retired)
		if (Id && (!LatestSubmittedMoveId || IsNewerSerial(Id, LatestSubmittedMoveId))) LatestSubmittedMoveId = Id;
	if (LatestSubmittedMoveId && (!ClientRetiredMoveThrough || IsNewerSerial(LatestSubmittedMoveId, ClientRetiredMoveThrough)))
		ClientRetiredMoveThrough = LatestSubmittedMoveId;
	// Notify prediction/HUD owners after draining the queues; callbacks cannot dispatch an old click.
	for (uint32 Id : Retired)
	{
		FGuLiCommandAck Ack; InitializeAck(Ack, Id, EGuLiCommandKind::Move); Ack.Result = Result;
		if (PendingCommandAcks.Num() >= GuLiCommanderNetwork::MaxPendingCommandAcks)
			PendingCommandAcks.RemoveAt(0, 1, EAllowShrinking::No);
		PendingCommandAcks.Add(Ack); LastCommandAck = Ack; OnCommandAckChanged.Broadcast(Ack);
	}
	return ClientRetiredMoveThrough;
}

void UGuLiCommanderNetSyncComponent::CancelServerMovesThrough(uint32 CommandId, EGuLiCommandAckResult Result)
{
	if (!CommandId) return;
	if (!ServerCancelledMoveThrough || IsNewerSerial(CommandId, ServerCancelledMoveThrough)) ServerCancelledMoveThrough = CommandId;
	if (!bServerMovePlanningPending || IsNewerSerial(PendingServerMovePlanningRequest.ClientCommandId, CommandId)) return;
	const FGuLiMoveRequest Pending = PendingServerMovePlanningRequest;
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	FGuLiCommandAck Ack; FGuLiCommanderSelectionState Updated; bool bChanged = false;
	// A job may have committed before the NetSync tick consumes its final receipt.
	if (Authority && PendingServerMovePlanningPlayerState.IsValid()
		&& Authority->PollMovePlanning(*PendingServerMovePlanningPlayerState.Get(), Pending.ClientCommandId, Ack, Updated, bChanged)
			== EGuLiMovePlanningStatus::Completed)
	{
		FinalizeServerMovePlanning(Pending, Ack, bChanged ? &Updated : nullptr);
		return;
	}
	CancelPendingServerMovePlanning();
	InitializeAck(Ack, Pending.ClientCommandId, EGuLiCommandKind::Move); Ack.Result = Result;
	LastMoveAck = Ack; PublishAck(Ack, true);
}

void UGuLiCommanderNetSyncComponent::ServerReplaceSelection_Implementation(
	const FGuLiSelectionRequest& Request, uint32 CancelMoveThrough, uint32 ConnectionGeneration)
{
	const auto* Player = GetBattlePlayerState();
	if (!IsConnectionReady() || ConnectionGeneration != GetConnectionGeneration() || !Player || !Player->IsCommander()
		|| !Request.IsWellFormed() || (Request.Modifier != EGuLiSelectionModifier::Clear && Request.Modifier != EGuLiSelectionModifier::Replace)) return;
	if (!LastSelectionRequestId || IsNewerSerial(Request.ClientRequestId, LastSelectionRequestId))
		CancelServerMovesThrough(CancelMoveThrough, EGuLiCommandAckResult::Cancelled);
	HandleSelectionRequest(Request, true, true);
}

void UGuLiCommanderNetSyncComponent::RequestCommandRecovery()
{
	if (bPendingSelectionIntent) ClientRetiredSelectionThrough = PendingSelectionIntent.ClientRequestId;
	for (const auto& Queued : QueuedSelectionIntents)
		if (!ClientRetiredSelectionThrough || IsNewerSerial(Queued.ClientRequestId, ClientRetiredSelectionThrough))
			ClientRetiredSelectionThrough = Queued.ClientRequestId;
	QueuedSelectionIntents.Reset();
	bPendingSelectionIntent = bPendingSelectionFastRetry = bPendingSelectionReliableFallback = false;
	bAwaitingSelectionSnapshot = false; AwaitedSelectionRevision = 0;
	PendingCommandRecoveryId = NextCommandRecoveryId++;
	if (!NextCommandRecoveryId) ++NextCommandRecoveryId;
	const uint32 Cutoff = RetireLocalMoveIntents(EGuLiCommandAckResult::TimedOut);
	PendingCommandRecoveryDeadline = GetWorld()->GetRealTimeSeconds() + 8.0;
	ServerRecoverCommanderCommands(PendingCommandRecoveryId, Cutoff, ClientRetiredSelectionThrough, GetConnectionGeneration());
}

void UGuLiCommanderNetSyncComponent::ServerRecoverCommanderCommands_Implementation(
	uint32 RecoveryId, uint32 CancelMoveThrough, uint32 CancelSelectionThrough, uint32 ConnectionGeneration)
{
	const auto* Player = GetBattlePlayerState();
	if (!RecoveryId || !IsConnectionReady() || ConnectionGeneration != GetConnectionGeneration()
		|| !Player || !Player->IsCommander() || !ConsumeCommandRateLimit()) return;
	CancelServerMovesThrough(CancelMoveThrough, EGuLiCommandAckResult::TimedOut);
	if (CancelSelectionThrough && (!LastSelectionRequestId || IsNewerSerial(CancelSelectionThrough, LastSelectionRequestId)))
		LastSelectionRequestId = CancelSelectionThrough;
	ClientRecoverCommanderCommands(RecoveryId, ConnectionGeneration, SelectionState);
}

void UGuLiCommanderNetSyncComponent::ClientRecoverCommanderCommands_Implementation(
	uint32 RecoveryId, uint32 ConnectionGeneration, const FGuLiCommanderSelectionState& State)
{
	if (!RecoveryId || RecoveryId != PendingCommandRecoveryId || ConnectionGeneration != GetConnectionGeneration() || !IsConnectionReady()) return;
	PendingCommandRecoveryId = 0;
	if (!IsNewerSerial(SelectionState.SelectionRevision, State.SelectionRevision)) SelectionState = State;
	ResolvedSelectionRevision = SelectionState.SelectionRevision;
	ResolvedSelectionRequestId = SelectionState.AcceptedClientRequestId;
	bResolvedSelectionAccepted = true;
	NotifySelectionChanged(); AdvanceSelectionQueue();
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
	const bool bReliableAck, const bool bAbsoluteSelection)
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
	if (bServerMovePlanningPending)
	{
		Ack.Result = EGuLiCommandAckResult::RateLimited;
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
	FGuLiSelectionRequest EffectiveRequest = Request;
	if (bAbsoluteSelection) EffectiveRequest.KnownSelectionRevision = SelectionState.SelectionRevision;
	const bool bAccepted = Authority->ResolveSelection(
		*BattlePlayerState,
		EffectiveRequest,
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
	if (Request.ClientCommandId && ServerCancelledMoveThrough
		&& !IsNewerSerial(Request.ClientCommandId, ServerCancelledMoveThrough))
	{
		if (!ConsumeCommandRateLimit()) return;
		Ack.Result = EGuLiCommandAckResult::Cancelled;
		PublishAck(Ack, bReliableAck);
		return;
	}

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
	if (bServerMovePlanningPending
		&& Request.ClientCommandId != PendingServerMovePlanningRequest.ClientCommandId)
	{
		Ack.Result = EGuLiCommandAckResult::RateLimited;
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
			if (IsServerMovePlanningPending(Request))
			{
				bPendingServerMoveReliableDelivery |= bReliableAck;
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

	// Authority only validates and attaches here. Candidate projection and routing advance on later world frames.
	const bool bPlanningStarted = Authority->BeginMovePlanning(
		*BattlePlayerState,
		Request,
		SelectionState,
		Ack);
	if (!bPlanningStarted)
	{
		LastMoveAck = Ack;
		PublishAck(Ack, bReliableAck);
		return;
	}
	if (!BeginServerMovePlanning(Request, bReliableAck))
	{
		// BeginMovePlanning already created the authority job. If the connection identity changed
		// before this component could attach its lifecycle guard, cancel that orphan immediately.
		Authority->CancelMovePlanning(*BattlePlayerState);
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		LastMoveAck = Ack;
		PublishAck(Ack, bReliableAck);
	}
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
	bool bResolvedCurrentMove = false;
	if ((Sanitized.CommandKind == EGuLiCommandKind::Move && ClientRetiredMoveThrough
		&& !IsNewerSerial(Sanitized.ClientCommandId, ClientRetiredMoveThrough))
		|| (Sanitized.CommandKind == EGuLiCommandKind::Selection && ClientRetiredSelectionThrough
		&& !IsNewerSerial(Sanitized.ClientCommandId, ClientRetiredSelectionThrough))) return;
	if (Sanitized.CommandKind == EGuLiCommandKind::Selection
		&& bPendingSelectionIntent
		&& Sanitized.ClientCommandId == PendingSelectionIntent.ClientRequestId)
	{
		bPendingSelectionIntent = false;
		bPendingSelectionFastRetry = false;
		bPendingSelectionReliableFallback = false;
		ResolvedSelectionRequestId = Sanitized.ClientCommandId;
		ResolvedSelectionRevision = Sanitized.ServerSelectionRevision;
		bResolvedSelectionAccepted = Sanitized.IsAccepted();
	}
	else if (Sanitized.CommandKind == EGuLiCommandKind::Move
		&& bPendingMoveIntent
		&& Sanitized.ClientCommandId == PendingMoveIntent.ClientCommandId)
	{
		bPendingMoveIntent = false;
		bPendingMoveFastRetry = false;
		bPendingMoveReliableFallback = false;
		bResolvedCurrentMove = true;
	}

	if (Sanitized.ServerSelectionRevision != 0u
		&& SelectionState.SelectionRevision != Sanitized.ServerSelectionRevision
		&& IsNewerSerial(Sanitized.ServerSelectionRevision, SelectionState.SelectionRevision))
	{
		if (AwaitedSelectionRevision == 0u
			|| IsNewerSerial(Sanitized.ServerSelectionRevision, AwaitedSelectionRevision))
		{
			AwaitedSelectionRevision = Sanitized.ServerSelectionRevision;
		}
		if (!bAwaitingSelectionSnapshot && GetWorld()) SelectionSnapshotDeadline = GetWorld()->GetRealTimeSeconds() + 8.0;
		bAwaitingSelectionSnapshot = true;
	}
	// 这里只抑制与上一次相同种类/ID 的重复通知，不是全历史去重集合。
	if (LastDeliveredAckKind == Sanitized.CommandKind
		&& LastDeliveredAckCommandId == Sanitized.ClientCommandId)
	{
		if (bResolvedCurrentMove)
		{
			ReleaseDeferredMoveAfterCurrentAck();
		}
		AdvanceSelectionQueue();
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
	// Consumers resolve this ACK synchronously before a deferred move is allowed to replace its
	// per-soldier prediction records through OnMoveReadyToSend.
	OnCommandAckChanged.Broadcast(LastCommandAck);
	if (bResolvedCurrentMove)
	{
		// Route the latest queued click only after the first command has a final result. If that
		// result pruned selection, the release path records the new high-water dependency
		// and waits for the OwnerOnly SelectionState property before it starts prediction or RPCs.
		ReleaseDeferredMoveAfterCurrentAck();
	}
	AdvanceSelectionQueue();
}

void UGuLiCommanderNetSyncComponent::ClientReceiveEncodedPoseBlock_Implementation(const FGuLiEncodedPoseBlock& Block)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Receive);
	GuLiCommanderPoseMetrics::FScope Measure(GuLiCommanderPoseMetrics::EScope::Receive);
	if (!IsSoldierStreamReady()) return;
	DecodeAndQueuePoseBlock(Block);
}

void UGuLiCommanderNetSyncComponent::DecodeAndQueuePoseBlock(const FGuLiEncodedPoseBlock& Block)
{
	FGuLiSoldierPoseChunk Chunk;
	const auto Result = PoseReceiver.Decode(Block, Chunk);
	if (Result == GuLiCommanderPoseCodec::EDecodeResult::Decoded) QueueDecodedPoseChunk(MoveTemp(Chunk));
	else if (Result == GuLiCommanderPoseCodec::EDecodeResult::MissingBaseline
		|| Result == GuLiCommanderPoseCodec::EDecodeResult::InvalidPayload) GuLiCommanderPoseMetrics::RecordFailure();
}

void UGuLiCommanderNetSyncComponent::QueueDecodedPoseChunk(FGuLiSoldierPoseChunk Chunk)
{
	const int32 MaximumPendingPoseChunks = static_cast<int32>(GULI_MAX_POSE_CHUNKS_PER_FRAME);
	if (PendingPoseChunks.Num() >= MaximumPendingPoseChunks)
		PendingPoseChunks.RemoveAt(0, PendingPoseChunks.Num() - MaximumPendingPoseChunks + 1, EAllowShrinking::No);
	GuLiCommanderPoseMetrics::RecordDecoded(Chunk);
#if !UE_BUILD_SHIPPING
	// A diagnostic capture-frame counter is never an acknowledgment of any block.
	if (LastAcceptedPoseFrameSequence == 0u || IsNewerSerial(Chunk.FrameSequence, LastAcceptedPoseFrameSequence))
	{
		LastAcceptedPoseFrameSequence = Chunk.FrameSequence;
		++AcceptedPoseFrameCount;
	}
	LastAcceptedPoseReceiveTimeSeconds = FPlatformTime::Seconds();
#endif
	PendingPoseChunks.Add(MoveTemp(Chunk));
	OnPoseChunkReceived.Broadcast(PendingPoseChunks.Last());
}

void UGuLiCommanderNetSyncComponent::TickPoseAcknowledgments()
{
	const APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || !Controller->IsLocalController() || !IsSoldierStreamReady()) return;
	const double Now = FPlatformTime::Seconds();
	if (Now < NextPoseAckTime) return;
	// Keep 10 Hz without accumulating frame-rounding drift; a stall skips old ACK slots.
	NextPoseAckTime = NextPoseAckTime == 0.0 ? Now + 0.1
		: NextPoseAckTime + (FMath::FloorToDouble((Now - NextPoseAckTime) / 0.1) + 1.0) * 0.1;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Ack);
	GuLiCommanderPoseMetrics::FScope Measure(GuLiCommanderPoseMetrics::EScope::Ack);
	FGuLiPoseAcknowledgment Ack;
	if (PoseReceiver.BuildAcknowledgment(Ack)) ServerAcknowledgePoseBlocks(Ack);
}

void UGuLiCommanderNetSyncComponent::ServerAcknowledgePoseBlocks_Implementation(const FGuLiPoseAcknowledgment& Ack)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Ack);
	GuLiCommanderPoseMetrics::FScope Measure(GuLiCommanderPoseMetrics::EScope::Ack);
	if (!IsSoldierStreamReady()) return;
	PoseSender.Confirm(Ack);
}

void UGuLiCommanderNetSyncComponent::BindPoseRoster(AGuLiSoldierStateReplicator& Roster)
{
	if (PoseRoster.Get() == &Roster) return;
	if (PoseRoster.IsValid()) PoseRoster->OnSoldiersRemoved().RemoveAll(this);
	PoseRoster = &Roster;
	Roster.OnSoldiersRemoved().AddUObject(this, &UGuLiCommanderNetSyncComponent::ForgetPoseSoldiers);
}

void UGuLiCommanderNetSyncComponent::ForgetPoseSoldiers(TConstArrayView<FGuLiSoldierId> Removed)
{
	PoseSender.Forget(Removed);
	PoseReceiver.Forget(Removed);
}

void UGuLiCommanderNetSyncComponent::OnRep_SelectionState()
{
	NotifySelectionChanged();
}

void UGuLiCommanderNetSyncComponent::OnRep_MoveEndpoints()
{
	MoveEndpoints.Sanitize();
	OnMoveEndpointsChanged.Broadcast(MoveEndpoints);
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
		PoseReceiver.Reset();
		NextPoseAckTime = 0.0;
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

	AGuLiSoldierStateReplicator* SoldierReplicator = nullptr;
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
	PoseReceiver.Reset(CompletedGeneration, CompletedMatchEpoch);
	BindPoseRoster(*SoldierReplicator);
	NextPoseAckTime = 0.0;
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
void UGuLiCommanderNetSyncComponent::TestOnly_ConfigureDeferredSelectionMove(
	const FGuLiSelectionRequest& Selection, const FGuLiMoveRequest& Move)
{
	PendingSelectionIntent = Selection;
	bPendingSelectionIntent = true;
	MovesAwaitingSelection.Add(Selection.ClientRequestId, Move);
}

void UGuLiCommanderNetSyncComponent::TestOnly_ApplySelectionSnapshot(const FGuLiCommanderSelectionState& State)
{
	SelectionState = State;
	NotifySelectionChanged();
}

bool UGuLiCommanderNetSyncComponent::TestOnly_IsSameSelectionRequest(
	const FGuLiSelectionRequest& A, const FGuLiSelectionRequest& B)
{
	return GuLiCommanderNetwork::IsSameSelectionRequest(A, B);
}

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

void UGuLiCommanderNetSyncComponent::TestOnly_QueueMoveUntilSelectionSnapshot(
	const FGuLiMoveRequest& Move)
{
	MovesAwaitingSelection.Add(GuLiCommanderNetwork::SelectionSnapshotMoveDependencyKey, Move);
	AdvanceSelectionQueue();
}

void UGuLiCommanderNetSyncComponent::TestOnly_ConfigurePendingMoveIntent(
	const FGuLiMoveRequest& Move)
{
	PendingMoveIntent = Move;
	bPendingMoveIntent = true;
	bPendingMoveFastRetry = true;
	bPendingMoveReliableFallback = true;
}

void UGuLiCommanderNetSyncComponent::TestOnly_DeferMoveUntilCurrentAck(
	const FGuLiMoveRequest& Move)
{
	DeferMoveUntilCurrentAck(Move);
}

void UGuLiCommanderNetSyncComponent::TestOnly_ConfigureClientPoseGate(
	const bool bReady,
	const uint32 AcceptedMatchEpoch)
{
	bClientPoseReady = bReady;
	ClientAcceptedMatchEpoch = AcceptedMatchEpoch;
	PendingPoseChunks.Reset();
	PoseReceiver.Reset(1u, AcceptedMatchEpoch);
}

void UGuLiCommanderNetSyncComponent::TestOnly_ReceivePoseBlock(
	const FGuLiEncodedPoseBlock& Block)
{
	if (bClientPoseReady) DecodeAndQueuePoseBlock(Block);
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
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		if (IsNewerSerial(LastAppliedClientSelection.SelectionRevision, SelectionState.SelectionRevision)
			|| (LastAppliedClientSelection.SelectionRevision == SelectionState.SelectionRevision
				&& IsNewerSerial(LastAppliedClientSelection.AcceptedClientRequestId, SelectionState.AcceptedClientRequestId)))
			SelectionState = LastAppliedClientSelection;
		else LastAppliedClientSelection = SelectionState;
	}
	OnSelectionChanged.Broadcast(SelectionState);
	AdvanceSelectionQueue();
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

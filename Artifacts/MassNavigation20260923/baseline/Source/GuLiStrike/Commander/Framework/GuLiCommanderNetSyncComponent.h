// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Network/GuLiCommanderPoseCodec.h"
#include "Commander/Network/GuLiCommanderStateStream.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "GuLiCommanderNetSyncComponent.generated.h"

class AGuLiBattlePlayerState;
class AGuLiSoldierStateReplicator;
namespace GuLiOrderNetworkProbe { struct FRun; }

/** 把专业名册代次绑定到公共连接；单独属性用于保留旧 Bootstrap RPC 的参数布局。 */
USTRUCT()
struct FGuLiSoldierBootstrapBinding
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 ConnectionGeneration = 0u;

	UPROPERTY()
	uint32 SoldierSyncGeneration = 0u;
};

DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderSelectionChangedSignature,
	const FGuLiCommanderSelectionState&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderCommandAckChangedSignature,
	const FGuLiCommandAck&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiSoldierPoseChunkReceivedSignature,
	const FGuLiSoldierPoseChunk&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiMoveEndpointsChangedSignature,
	const FGuLiMoveEndpointFastArray&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiMoveReadyToSendSignature,
	const FGuLiMoveRequest&, const FGuLiCommanderSelectionState&);

/**
 * 公共连接组件的指挥官扩展：命令意图、士兵名册初始同步、业务 ACK 和姿态 RPC。
 * 公共身份握手由基类完成；士兵就绪是独立门，Ground/Air 的公共就绪不替代它。
 * 组件借用拥有者的连接路由；SelectionState/SyncGeneration 属性仅复制给拥有者。
 * 网络层负责校验/去重/限流，具体选兵、成员归属与寻路仍由 Authority 判定。
 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiCommanderNetSyncComponent : public UGuLiPlayerNetSyncComponent
{
	GENERATED_BODY()
	friend struct GuLiOrderNetworkProbe::FRun;

public:
	UGuLiCommanderNetSyncComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 服务器本地入口：准备名册并启动新同步代次；依赖尚未就绪时返回，后续可再尝试。 */
	void StartServerBootstrap();

	/** 服务器检查当前战局；仅缺少代次或战局改变时启动，不因重复 Tick 持续增加代次。 */
	void EnsureServerBootstrapForMatch(uint32 AuthorityMatchEpoch);

	/** 公共连接及当前战局的士兵名册都已就绪；发送姿态和下指挥命令必须检查此门。 */
	UFUNCTION(BlueprintPure, Category = "Commander|Network")
	bool IsSoldierStreamReady() const;

	/** Queue latest samples for this connection. Capture remains 10 Hz; admission runs each Tick. */
	void SendPoseChunk(const FGuLiSoldierPoseChunk& Chunk);
	/** Called before the authoritative wreck is retired; preserves a queued terminal death. */
	void QueueSoldierRetirement(const FGuLiSoldierStateItem& FinalState, uint32 MatchEpoch);
	uint64 GetPoseCodecAllocatedBytes() const { return PoseSender.GetAllocatedBytes() + PoseReceiver.GetAllocatedBytes(); }

	/** 服务器刷新选择中的存活/命令摘要并剔除全灭控制组；仅发生变化时返回 true 并请求复制。 */
	bool RefreshServerSelection();

	/** 客户端本地消费：将接收队列移到 OutChunks 并清空内部队列；非网络调用。 */
	void ConsumePendingPoseChunks(TArray<FGuLiSoldierPoseChunk>& OutChunks);

	/** 拥有者本地按队列取走 ACK；避免只读 LastCommandAck 时漏掉同帧多个结果，队列有上限。 */
	void ConsumePendingCommandAcks(TArray<FGuLiCommandAck>& OutAcks);

	/** 拥有者提交选兵意图：快速 RPC、一次快速重试及延迟可靠回退；同类选兵请求串行排队。 */
	void SubmitSelectionRequest(const FGuLiSelectionRequest& Request);
	// 拥有者提交移动意图；有待确认选兵时只保留最新延后移动，ACK 到达后补入选择版本。
	void SubmitMoveRequest(const FGuLiMoveRequest& Request);
	bool HasUnresolvedSelectionIntent() const;
	/** New UI commands share one reliable actor channel, preserving input order across selection changes. */
	void SubmitOrderedSelection(FGuLiSelectionRequest Request);
	bool SubmitPanelSelection(FGuLiPanelSelectionRequest Request);
	void SubmitOrderedTask(FGuLiUnitTaskCommand Command);
	void SubmitControlGroup(uint8 Slot, bool bSet, bool bAppend, bool bSteal, bool bFocus);
	const TArray<FGuLiUnitTaskSummary>& GetTaskSummaries() const;
	const TArray<int32>& GetControlGroupCounts() const { return ControlGroupCounts; }
	uint16 GetRelatedControlGroups() const { return DisplayedTaskSelectionRevision == SelectionState.SelectionRevision ? RelatedControlGroups : 0; }
	const FString& GetLastTaskFeedback() const { return LastTaskFeedback; }
	bool IsDispatchingTaskReceipt() const { return bDispatchingTaskReceipt; }
	FGuLiMoveReadyToSendSignature OnMoveReadyToSend;

	// 拥有客户端 → 服务器，可靠选兵 RPC；Request 只含意图，处理结果经 Client ACK 返回。
	UFUNCTION(Server, Reliable)
	void ServerRequestSelection(const FGuLiSelectionRequest& Request);
	/** Absolute selection supersedes only commands that have not committed. */
	UFUNCTION(Server, Reliable)
	void ServerReplaceSelection(const FGuLiSelectionRequest& Request, uint32 CancelMoveThrough, uint32 ConnectionGeneration);
	UFUNCTION(Server, Reliable)
	void ServerRecoverCommanderCommands(uint32 RecoveryId, uint32 CancelMoveThrough, uint32 CancelSelectionThrough, uint32 ConnectionGeneration);
	UFUNCTION(Client, Reliable)
	void ClientRecoverCommanderCommands(uint32 RecoveryId, uint32 ConnectionGeneration, const FGuLiCommanderSelectionState& State);

	// 拥有客户端 → 服务器，可靠移动 RPC；无同步返回值，不能将调用返回视为接令成功。
	UFUNCTION(Server, Reliable)
	void ServerIssueMove(const FGuLiMoveRequest& Request);

	// 拥有客户端 → 服务器，请求初始同步；非零请求序号用于去重，未就绪时可重发已有标记。
	UFUNCTION(Server, Reliable)
	void ServerRequestBootstrap(uint32 ClientBootstrapRequestId);

	// AppliedSnapshotRevision is the completed state batch sequence, not a global roster revision.
	// Server verifies the frozen connection-specific completion count, ID hash and sequence.
	// 参数来自客户端当前已应用的数据，不能只凭收到 ClientBootstrapStarted 就确认。
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeBootstrap(
		uint32 InSyncGeneration,
		uint16 ClientProtocolVersion,
		uint32 ClientMatchEpoch,
		uint16 AppliedRosterCount,
		uint32 AppliedSnapshotRevision,
		uint64 AppliedIdHash);

	/** Application ACK releases the bounded reliable window, including during bootstrap. */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeFacts(uint32 InSyncGeneration, uint32 HighestContiguousStreamSeq);

	const FGuLiCommanderSelectionState& GetSelectionState() const { return SelectionState; }

	const FGuLiCommandAck& GetLastCommandAck() const { return LastCommandAck; }

	/** Owner-visible endpoint read model; consumers must still check IsSoldierStreamReady(). */
	const FGuLiMoveEndpointFastArray& GetMoveEndpoints() const { return MoveEndpoints; }
	const FGuLiMoveEndpointItem* FindMoveEndpoint(FGuLiSoldierId SoldierId) const
	{
		return MoveEndpoints.Find(SoldierId);
	}

	/** Authority-only endpoint mutation API used by the movement planner and completion/blocked cleanup. */
	bool ServerUpsertMoveEndpoint(
		FGuLiSoldierId SoldierId,
		uint32 ActiveOrderId,
		const FVector& CommandStart,
		const FVector& FinalDestination);
	bool ServerRemoveMoveEndpoint(FGuLiSoldierId SoldierId);
	int32 ServerBootstrapMoveEndpoints(TConstArrayView<FGuLiMoveEndpointItem> Endpoints);
	bool ServerClearMoveEndpoints();

	/**
	 * Async planner bridge. Begin suppresses duplicate-request ACK replay while work is pending;
	 * Finalize atomically publishes the final ACK and optional pruned selection snapshot.
	 */
	bool BeginServerMovePlanning(const FGuLiMoveRequest& Request, bool bReliableDelivery);
	bool FinalizeServerMovePlanning(
		const FGuLiMoveRequest& Request,
		const FGuLiCommandAck& FinalAck,
		const FGuLiCommanderSelectionState* UpdatedSelection = nullptr);
	bool IsServerMovePlanningPending(const FGuLiMoveRequest& Request) const;
	bool IsMoveCommandPending(uint32 ClientCommandId) const;

	uint32 GetSyncGeneration() const { return SyncGeneration; }

#if !UE_BUILD_SHIPPING
	/** Non-shipping acceptance evidence; counts unique admitted 10 Hz pose frames. */
	uint64 GetAcceptedPoseFrameCount() const { return AcceptedPoseFrameCount; }
	double GetLastAcceptedPoseReceiveTimeSeconds() const { return LastAcceptedPoseReceiveTimeSeconds; }
#endif

	// 以下三个委托只在本进程广播；分别由属性通知、ACK 接收、姿态接收驱动。
	FGuLiCommanderSelectionChangedSignature OnSelectionChanged;

	FGuLiCommanderCommandAckChangedSignature OnCommandAckChanged;

	FGuLiSoldierPoseChunkReceivedSignature OnPoseChunkReceived;

	FGuLiMoveEndpointsChangedSignature OnMoveEndpointsChanged;

#if WITH_DEV_AUTOMATION_TESTS
	void TestOnly_ConfigureDeferredSelectionMove(const FGuLiSelectionRequest& Selection, const FGuLiMoveRequest& Move);
	void TestOnly_ApplySelectionSnapshot(const FGuLiCommanderSelectionState& State);
	const FGuLiMoveRequest& TestOnly_GetDispatchedMove() const { return TestDispatchedMove; }
	bool TestOnly_HasQueuedMove() const { return !MovesAwaitingSelection.IsEmpty(); }
	void TestOnly_QueueMoveForSelection(uint32 SelectionRequestId, const FGuLiMoveRequest& Move) { MovesAwaitingSelection.Add(SelectionRequestId, Move); }
	int32 TestOnly_GetQueuedMoveCount() const { return MovesAwaitingSelection.Num(); }
	static bool TestOnly_IsSameSelectionRequest(const FGuLiSelectionRequest& A, const FGuLiSelectionRequest& B);
	/** Test-only access to the shared bootstrap contract predicate. Not compiled into Shipping. */
	static bool TestOnly_IsBootstrapSnapshotCompatible(
		uint16 ProtocolVersion,
		uint32 GameStateMatchEpoch,
		uint32 SnapshotMatchEpoch,
		uint32 SnapshotRevision,
		int32 RosterCount,
		uint32 ExpectedMatchEpoch,
		uint32 ExpectedSnapshotRevision,
		uint16 ExpectedRosterCount);

	/** Test-only view of the authority lifecycle decision used across seamless travel. */
	static bool TestOnly_ShouldStartNewServerBootstrap(
		uint32 ExistingSyncGeneration,
		uint32 ExistingBootstrapMatchEpoch,
		uint32 AuthorityMatchEpoch,
		bool bPlayerSyncReady);

	/** Test-only injection through the same client ACK implementation used by the reliable RPC. */
	void TestOnly_ReceiveCommandAck(const FGuLiCommandAck& Ack);
	bool TestOnly_IsAwaitingSelectionSnapshot() const { return bAwaitingSelectionSnapshot; }
	uint32 TestOnly_GetAwaitedSelectionRevision() const { return AwaitedSelectionRevision; }
	void TestOnly_QueueMoveUntilSelectionSnapshot(const FGuLiMoveRequest& Move);
	void TestOnly_ConfigurePendingMoveIntent(const FGuLiMoveRequest& Move);
	void TestOnly_DeferMoveUntilCurrentAck(const FGuLiMoveRequest& Move);
	uint32 TestOnly_GetPendingMoveCommandId() const
	{
		return bPendingMoveIntent ? PendingMoveIntent.ClientCommandId : 0u;
	}
	uint32 TestOnly_GetDeferredMoveCommandId() const
	{
		return bHasDeferredMoveAfterCurrentAck ? DeferredMoveAfterCurrentAck.ClientCommandId : 0u;
	}

	/** Test-only configuration for exercising the production pose admission gate. */
	void TestOnly_ConfigureClientPoseGate(bool bReady, uint32 AcceptedMatchEpoch);

	/** Test-only injection through the same client pose implementation used by the unreliable RPC. */
	void TestOnly_ReceivePoseBlock(const FGuLiEncodedPoseBlock& Block);
#endif

protected:
	virtual void OnConnectionBootstrapReset() override;
	virtual void OnConnectionBootstrapReady() override;

private:
	UFUNCTION(Client, Reliable)
	void ClientReceiveStateBatch(const FGuLiEncodedStateBatch& Batch);
	UFUNCTION(Server, Reliable)
	void ServerRequestStateResync(uint32 ConnectionGeneration, uint32 ExpectedSyncGeneration);
	void TickStateStream();
	void ApplyPendingStateBatches();
	void ResetStateStream();
	GuLiCommanderStateStream::FSender StateSender;
	GuLiCommanderStateStream::FReceiver StateReceiver;
	GuLiCommanderStateStream::FDiagnostics StreamDiagnostics;
	TMap<FGuLiSoldierId, GuLiCommanderStateStream::FPendingPose> QueuedPoses;
	TArray<FGuLiEncodedStateBatch> ReceivedStateBatches;
	uint64 ClientAppliedIdHash = 0;
	uint32 PendingStateResyncGeneration = 0;
	double NextStateResyncTime = 0;
	UFUNCTION(Server, Reliable) void ServerPanelSelection(FGuLiPanelSelectionRequest Request, uint32 Sequence, uint32 Generation);
	UFUNCTION(Server, Reliable) void ServerOrderedSelection(FGuLiSelectionRequest Request, uint32 Sequence, uint32 Generation);
	UFUNCTION(Server, Reliable) void ServerOrderedTask(FGuLiUnitTaskCommand Command, uint32 Sequence, uint32 Generation);
	UFUNCTION(Server, Reliable) void ServerControlGroup(uint8 Slot, bool bSet, bool bAppend, bool bSteal, bool bFocus, uint32 Sequence, uint32 Generation);
	UFUNCTION(Client, Reliable) void ClientOrderedSelection(const FGuLiCommanderSelectionState& State, uint32 Sequence, bool bFocus, uint32 Generation);
	UFUNCTION(Client, Reliable) void ClientTaskReceipt(const FGuLiCommandAck& Ack, const FString& Message, uint32 Generation);
	UFUNCTION(Client, Reliable) void ClientTaskSnapshot(uint32 Revision, int32 Total, int32 Offset,
		const TArray<FGuLiUnitTaskSummary>& Chunk, const TArray<int32>& Counts, uint16 RelatedGroups, uint32 SelectionRevision, uint32 Generation);
	bool AdmitOrderedSequence(uint32 Sequence, uint32 Generation);
	void TickOrderedCommands();
	void PruneControlGroup(FGuLiCommanderControlGroup& Group, EGuLiTeam Team) const;
	uint32 NextOrderedSequence = 1;
	uint32 LastOrderedSequence = 0;
	uint32 OrderedGeneration = 0;
	uint32 LastOrderedSelectionSequence = 0;
	TSet<uint32> PendingOrderedSelections;
	TSet<uint32> PendingOrderedTasks;
	bool bDispatchingTaskReceipt = false;
	TArray<FGuLiCommanderControlGroup> ControlGroups;
	bool bOrderedSelectionValid = true;
	double NextOrderedSummaryTime = 0;
	TArray<FGuLiUnitTaskSummary> TaskSummaries;
	TArray<int32> ControlGroupCounts;
	TArray<FGuLiUnitTaskSummary> PendingTaskSnapshot;
	TArray<FGuLiUnitTaskSummary> ReceivedTaskSnapshot;
	TArray<int32> PendingGroupCounts;
	TArray<int32> ReceivedGroupCounts;
	uint16 RelatedControlGroups = 0, PendingRelatedGroups = 0, ReceivedRelatedGroups = 0;
	uint32 TaskSnapshotRevision = 0;
	uint32 ReceivedTaskSnapshotRevision = 0;
	uint32 TaskSnapshotSelectionRevision = 0;
	uint32 DisplayedTaskSelectionRevision = 0;
	int32 TaskSnapshotOffset = INDEX_NONE;
	FString LastTaskFeedback;
	// 拥有客户端 → 服务器，不可靠快速选兵入口；与可靠版本共用 HandleSelectionRequest。
	UFUNCTION(Server, Unreliable)
	void ServerRequestSelectionFast(const FGuLiSelectionRequest& Request);

	// 拥有客户端 → 服务器，不可靠快速移动入口；同一命令重试必须保留原序号与内容。
	UFUNCTION(Server, Unreliable)
	void ServerIssueMoveFast(const FGuLiMoveRequest& Request);

	// 服务器 → 拥有客户端，可靠同步标记；携带期望值，不携带完整名册，也不保证其他 Actor 已到达。
	UFUNCTION(Client, Reliable)
	void ClientBootstrapStarted(
		uint32 NewSyncGeneration,
		uint16 ServerProtocolVersion,
		uint32 MatchEpoch,
		uint16 ExpectedRosterCount,
		uint32 ExpectedSnapshotRevision);

	// 服务器 → 拥有客户端，可靠业务回执；Ack 是数据结构，RPC 才是运输入口。
	UFUNCTION(Client, Reliable)
	void ClientReceiveCommandAck(const FGuLiCommandAck& Ack);

	// 服务器 → 拥有客户端，不可靠快速回执；丢失时客户端重发请求，服务器可重放缓存 ACK。
	UFUNCTION(Client, Unreliable)
	void ClientReceiveCommandAckFast(const FGuLiCommandAck& Ack);

	// 服务器 → 拥有客户端，不可靠姿态块；每块可独立消费，不等待整帧凑齐。
	UFUNCTION(Client, Unreliable)
	void ClientReceiveEncodedPoseBlock(const FGuLiEncodedPoseBlock& Block);

	UFUNCTION(Server, Unreliable)
	void ServerAcknowledgePoseBlocks(const FGuLiPoseAcknowledgment& Ack);

	void DecodeAndQueuePoseBlock(const FGuLiEncodedPoseBlock& Block);
	void QueueDecodedPoseChunk(FGuLiSoldierPoseChunk Chunk);
	void TickPoseAcknowledgments();
	void BindPoseRoster(AGuLiSoldierStateReplicator& Roster);
	void ForgetPoseSoldiers(TConstArrayView<FGuLiSoldierId> Removed);
	GuLiCommanderPoseCodec::FSender PoseSender;
	GuLiCommanderPoseCodec::FReceiver PoseReceiver;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> PoseRoster;
	double NextPoseAckTime = 0.0;

	UFUNCTION()
	void OnRep_SelectionState();

	UFUNCTION()
	void OnRep_SyncGeneration();

	UFUNCTION()
	void OnRep_MoveEndpoints();

	void BeginSelectionIntent(const FGuLiSelectionRequest& Request);
	void BeginMoveIntent(const FGuLiMoveRequest& Request);
	void QueueOrBeginMoveIntent(const FGuLiMoveRequest& Request);
	void DeferMoveUntilCurrentAck(const FGuLiMoveRequest& Request);
	void ReleaseDeferredMoveAfterCurrentAck();
	void AdvanceSelectionQueue();
	void RejectDeferredMove(uint32 SelectionRequestId, EGuLiCommandAckResult Result);
	void TickPendingCommandRetries();
	void TickSoldierBootstrap();
	void ResetClientSoldierState();
	void CancelPendingServerMovePlanning();
	void HandleSelectionRequest(const FGuLiSelectionRequest& Request, bool bReliableAck, bool bAbsoluteSelection = false);
	uint32 RetireLocalMoveIntents(EGuLiCommandAckResult Result);
	void CancelServerMovesThrough(uint32 CommandId, EGuLiCommandAckResult Result);
	void RequestCommandRecovery();
	void HandleMoveRequest(const FGuLiMoveRequest& Request, bool bReliableAck);
	void ReceiveCommandAck(const FGuLiCommandAck& Ack);
	bool CanProcessCommanderRequest(FGuLiCommandAck& InOutAck, const TCHAR* RpcName);
	bool ConsumeCommandRateLimit();
	bool ConsumeCachedAckReplayLimit(uint8& ReplayCount);
	void InitializeAck(FGuLiCommandAck& Ack, uint32 ClientId, EGuLiCommandKind CommandKind) const;
	void PublishAck(const FGuLiCommandAck& Ack, bool bReliableDelivery);
	void ResendServerBootstrapMarker();
	void TryCompleteClientBootstrap();
	void NotifySelectionChanged();
	void LogRejectedRpc(const TCHAR* RpcName, EGuLiCommandAckResult Result);
	void MirrorSyncReadyToRoleSlot(const AGuLiBattlePlayerState& BattlePlayerState, bool bReady) const;
	static bool IsNewerSerial(uint32 Candidate, uint32 Baseline);

	// 服务器确认的选择，OwnerOnly 属性复制；客户端预测不能修改服务器这份权威值。
	UPROPERTY(ReplicatedUsing = OnRep_SelectionState)
	FGuLiCommanderSelectionState SelectionState;
	FGuLiCommanderSelectionState LastAppliedClientSelection;

	// 本地最近回执缓存，不是复制属性；跨网传递在 PublishAck 的 Client RPC 中完成。
	FGuLiCommandAck LastCommandAck;

	// 每连接的同步代次，OwnerOnly 复制；0 表示尚无有效同步代次。
	UPROPERTY(ReplicatedUsing = OnRep_SyncGeneration)
	uint32 SyncGeneration = 0;

	// 跨属性/RPC 顺序不作假设；客户端等此绑定与公共代次、启动标记都一致后才处理名册。
	UPROPERTY(Replicated)
	FGuLiSoldierBootstrapBinding SoldierBootstrapBinding;

	// Local container retained for consumers. The owning connection publishes it in state batches.
	UPROPERTY(Transient)
	FGuLiMoveEndpointFastArray MoveEndpoints;

	// 服务器去重状态：选兵与移动各有独立序号空间，并分别缓存最近请求及 ACK。
	uint32 LastSelectionRequestId = 0;
	uint32 LastMoveCommandId = 0;
	uint32 ServerCancelledMoveThrough = 0u;
	uint32 ClientRetiredMoveThrough = 0u;
	uint32 ClientRetiredSelectionThrough = 0u;
	uint32 LatestSubmittedMoveId = 0u;
	uint32 PendingCommandRecoveryId = 0u;
	uint32 NextCommandRecoveryId = 1u;
	double PendingCommandRecoveryDeadline = 0.0;
	double PendingSelectionDeadline = 0.0;
	double PendingMoveDeadline = 0.0;
	double SelectionSnapshotDeadline = 0.0;
	uint32 LastBootstrapRequestId = 0;
	uint32 HighestAckedStreamSeq = 0;
	uint32 BootstrapMatchEpoch = 0;
	uint16 BootstrapExpectedRosterCount = 0;
	uint32 BootstrapExpectedSnapshotRevision = 0;
	uint32 ServerAcceptedSyncGeneration = 0u;
	uint32 ServerAcceptedMatchEpoch = 0u;
	double NextServerBootstrapMarkerTime = 0.0;
	double NextServerMoveEndpointRefreshTimeSeconds = 0.0;
	// 客户端待满足的同步条件；与服务器 Bootstrap* 期望值分开保存。
	uint32 PendingBootstrapGeneration = 0;
	uint32 PendingBootstrapMatchEpoch = 0;
	uint16 PendingBootstrapRosterCount = 0;
	uint32 PendingBootstrapSnapshotRevision = 0;
	uint32 ClientAcceptedMatchEpoch = 0;
	uint32 ClientAcceptedSyncGeneration = 0u;
	uint16 ClientAppliedRosterCount = 0u;
	uint32 ClientAppliedSnapshotRevision = 0u;
	bool bLoggedBootstrapProtocolMismatch = false;
	bool bClientPoseReady = false;
	// 客户端重试状态机；这是本地缓存，不是需要复制给服务器的字段。
	bool bPendingSelectionIntent = false;
	bool bPendingSelectionFastRetry = false;
	bool bPendingSelectionReliableFallback = false;
	bool bPendingMoveIntent = false;
	bool bPendingMoveFastRetry = false;
	bool bPendingMoveReliableFallback = false;
	bool bHasDeferredMoveAfterCurrentAck = false;
	bool bDeferredMoveHasSelectionDependency = false;
	bool bAwaitingSelectionSnapshot = false;
	bool bResolvedSelectionAccepted = false;
	bool bAdvancingSelectionQueue = false;
	uint32 ResolvedSelectionRequestId = 0u;
	uint32 ResolvedSelectionRevision = 0u;
	uint32 DeferredMoveSelectionDependencyKey = 0u;
	// Any ACK may carry a selection revision newer than the property; queues wait for this high-water mark.
	uint32 AwaitedSelectionRevision = 0u;
#if WITH_DEV_AUTOMATION_TESTS
	FGuLiMoveRequest TestDispatchedMove;
#endif
	double PendingSelectionFastRetryTimeSeconds = 0.0;
	double PendingSelectionReliableFallbackTimeSeconds = 0.0;
	double PendingMoveFastRetryTimeSeconds = 0.0;
	double PendingMoveReliableFallbackTimeSeconds = 0.0;
	FGuLiSelectionRequest PendingSelectionIntent;
	FGuLiMoveRequest PendingMoveIntent;
	// A move already in flight owns prediction/retry state until its final ACK. Later clicks
	// coalesce here and are admitted through the normal selection-revision gate afterward.
	FGuLiMoveRequest DeferredMoveAfterCurrentAck;
	// Coalesce repeated destinations only within one selection intent; preserve orders for other selections.
	TMap<uint32, FGuLiMoveRequest> MovesAwaitingSelection;
	FGuLiSelectionRequest LastSelectionRequest;
	FGuLiMoveRequest LastMoveRequest;
	TArray<FGuLiSelectionRequest> QueuedSelectionIntents;
	FGuLiCommandAck LastSelectionAck;
	FGuLiCommandAck LastMoveAck;
	bool bServerMovePlanningPending = false;
	bool bPendingServerMoveReliableDelivery = false;
	FGuLiMoveRequest PendingServerMovePlanningRequest;
	TWeakObjectPtr<const AGuLiBattlePlayerState> PendingServerMovePlanningPlayerState;
	uint32 PendingServerMovePlanningConnectionGeneration = 0u;
	uint8 SelectionCachedFastReplayCount = 0u;
	uint8 MoveCachedFastReplayCount = 0u;
	EGuLiCommandKind LastDeliveredAckKind = EGuLiCommandKind::None;
	uint32 LastDeliveredAckCommandId = 0u;
	FGuLiNetworkRequestWindow CommanderRequestWindow;
	double LastSecurityLogTime = -1.0;
	uint32 SuppressedSecurityLogCount = 0;
	// 本地有界消费队列：ACK 上限 64，姿态上限为一帧协议允许的块数。
	TArray<FGuLiCommandAck> PendingCommandAcks;
	TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
#if !UE_BUILD_SHIPPING
	uint64 AcceptedPoseFrameCount = 0u;
	uint32 LastAcceptedPoseFrameSequence = 0u;
	double LastAcceptedPoseReceiveTimeSeconds = 0.0;
#endif
};

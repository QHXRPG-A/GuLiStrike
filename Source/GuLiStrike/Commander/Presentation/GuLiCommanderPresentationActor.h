// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Presentation/GuLiCommanderPresentationPerformanceSettings.h"
#include "Commander/Presentation/GuLiCommanderRefreshCadence.h"
#include "GameFramework/Actor.h"
#include "MassArchetypeTypes.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "GuLiCommanderPresentationActor.generated.h"

class APlayerController;
class AGuLiBattlePlayerState;
class UGuLiCommanderNetSyncComponent;
class AGuLiSoldierStateReplicator;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMassEntitySubsystem;
class USceneComponent;
class UGuLiCommanderRouteLineComponent;
class UStaticMesh;
struct FGuLiSoldierRosterDelta;

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiCommanderVisualStatesChanged, const TArray<FGuLiSoldierId>&);

struct FGuLiCommanderMirrorCreate
{
	FMassEntityHandle Entity;
	FGuLiSoldierStateItem State;
	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Live;
};

/** One decoded authoritative sample in world coordinates, ready for interpolation. */
// 本地解压后的带时间戳样本；SampleIndex/ChunkIndex 留作诊断，位置已恢复到世界坐标。
struct FGuLiCommanderBufferedSoldierPose
{
	double ServerTimeSeconds = 0.0;
	uint32 FrameSequence = 0u;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float FacingYawDegrees = 0.0f;
	uint32 ActiveOrderId = 0u;
	EGuLiSoldierPoseState State = EGuLiSoldierPoseState::Idle;
	uint16 ChunkIndex = 0u;
	uint8 SampleIndex = 0u;
	bool bTeleport = false;
};

/** Client-only interpolation state. It intentionally contains no Mass movement fragments. */
// 每个 SoldierId 的客户端缓存，最多保留有限历史样本；AuthoritativeTransform 是样本求值结果，不是服务器实时对象。
struct FGuLiCommanderPresentedSoldier
{
	TArray<FGuLiCommanderBufferedSoldierPose> Samples;
	uint32 DisplacementFrameFloor = 0;
	FTransform AuthoritativeTransform = FTransform::Identity;
	FTransform PresentedTransform = FTransform::Identity;
	EGuLiSoldierLifeState LastLifeState = EGuLiSoldierLifeState::Alive;
	float LastObservedHealth = 0.0f;
	float HitFlashStartTime = -1000.0f;
	double LastPoseReceiptLocalTimeSeconds = 0.0;
	double MaximumPoseReceiptGapSeconds = 0.0;
	double PreviousAdaptivePoseReceiptLocalTimeSeconds = 0.0;
	double SmoothedPoseReceiptIntervalSeconds = 0.0;
	double SmoothedPoseReceiptJitterSeconds = 0.0;
	double RenderServerTimeSeconds = 0.0;
	FVector LastUntaggedHardSnapDelta = FVector::ZeroVector;
	FVector LastHardSnapPriorSampleDelta = FVector::ZeroVector;
	FVector LastHardSnapSampleVelocity = FVector::ZeroVector;
	FVector LastHardSnapCurrentLocation = FVector::ZeroVector;
	FVector LastHardSnapPreviousLocation = FVector::ZeroVector;
	double LastHardSnapServerTimeGapSeconds = 0.0;
	uint32 LastHardSnapFrameGap = 0u;
	uint16 LastHardSnapCurrentChunkIndex = 0u;
	uint16 LastHardSnapPreviousChunkIndex = 0u;
	uint8 LastHardSnapCurrentSampleIndex = 0u;
	uint8 LastHardSnapPreviousSampleIndex = 0u;
	uint64 UntaggedHardSnapCount = 0u;
	uint64 TeleportSnapCount = 0u;
	bool bHasAuthoritativeTransform = false;
	bool bHasPresentedTransform = false;
	bool bLifeStateInitialized = false;
	bool bRenderClockInitialized = false;
	// Explicit displacement only; ordinary pose corrections never bypass continuity.
	bool bResetPresentationOnNextPose = false;
};

/** Non-authoritative diagnostics used by the development network acceptance gate. */
struct FGuLiCommanderSoldierPresentationDiagnostics
{
	double MaximumPoseReceiptGapSeconds = 0.0;
	FVector LastUntaggedHardSnapDelta = FVector::ZeroVector;
	FVector LastHardSnapPriorSampleDelta = FVector::ZeroVector;
	FVector LastHardSnapSampleVelocity = FVector::ZeroVector;
	FVector LastHardSnapCurrentLocation = FVector::ZeroVector;
	FVector LastHardSnapPreviousLocation = FVector::ZeroVector;
	double LastHardSnapServerTimeGapSeconds = 0.0;
	uint32 LastHardSnapFrameGap = 0u;
	uint16 LastHardSnapCurrentChunkIndex = 0u;
	uint16 LastHardSnapPreviousChunkIndex = 0u;
	uint8 LastHardSnapCurrentSampleIndex = 0u;
	uint8 LastHardSnapPreviousSampleIndex = 0u;
	uint64 UntaggedHardSnapCount = 0u;
	uint64 TeleportSnapCount = 0u;
};

/** Bounded, presentation-only movement anticipation for one selected soldier. */
// 纯表现预测：ClientCommandId 关联请求，ExpectedOrderId 关联 ACK 接受的批次，不复制到服务器。
struct FGuLiCommanderPredictedMove
{
	FGuLiControlCohortId CohortId;
	uint8 FrozenMemberIndex = 0u;
	uint32 ClientCommandId = 0u;
	uint32 ExpectedOrderId = 0u;
	double StartTimeSeconds = 0.0;
	double ResolveStartTimeSeconds = 0.0;
	FVector Direction = FVector::ZeroVector;
	float MaximumDistance = 0.0f;
	float TargetYawDegrees = 0.0f;
	FVector LastAppliedOffset = FVector::ZeroVector;
	float LastAppliedYawOffsetDegrees = 0.0f;
	FVector ResolveStartOffset = FVector::ZeroVector;
	float ResolveStartYawOffsetDegrees = 0.0f;
	bool bAwaitingAuthoritativeOrder = false;
	bool bResolving = false;
};

/** Stable local render handle; unit and ring indices intentionally have separate lifetimes. */
struct FGuLiCommanderSoldierInstanceHandle
{
	uint16 RequestedUnitTypeId = 0u;
	uint16 BatchUnitTypeId = 0u;
	EGuLiTeam BatchTeam = EGuLiTeam::Unassigned;
	int32 UnitInstanceIndex = INDEX_NONE;
	int32 RingInstanceIndex = INDEX_NONE;
};

/** Non-UObject state owned by one (UnitTypeId, Team) ISM batch. */
struct FGuLiCommanderUnitInstanceBatchState
{
	TArray<int32> DirtyTransformSlots;
	TArray<FTransform> CachedTransforms;
	TArray<int32> FreeInstanceIndices;
};

#if !UE_BUILD_SHIPPING
/** Opt-in, bounded single-soldier capture. No strings or file writes on the sampled frame. */
enum class EGuLiPredictionTraceEvent : uint8
{
	Start, Input, MouseInput, ReplacedPrediction, AckAccepted, AckRejected, SampleArrival,
	RenderHandoff, Resolve, Frame, HardSnap, NetworkReset, Stop
};

struct FGuLiPredictionTraceRow
{
	EGuLiPredictionTraceEvent Event = EGuLiPredictionTraceEvent::Frame;
	double LocalSeconds = 0.0;
	double RenderServerSeconds = 0.0;
	double SampleServerSeconds = 0.0;
	uint64 LocalFrame = 0;
	uint32 SoldierId = 0;
	uint32 CommandId = 0;
	uint32 OrderId = 0;
	uint32 SampleFrame = 0;
	FVector Authoritative = FVector::ZeroVector;
	FVector Presented = FVector::ZeroVector;
	FVector Offset = FVector::ZeroVector;
	FVector RequestedOffset = FVector::ZeroVector;
	FVector SampleLocation = FVector::ZeroVector;
	FVector SampleVelocity = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	double SignedPresentedSpeed = 0.0;
	double SignedAuthoritativeSpeed = 0.0;
	bool bHasSpeed = false;
	bool bHasPrediction = false;
	bool bResolving = false;
};

/** Retained independently of the short-lived prediction, so late ACKs remain observable. */
struct FGuLiPredictionTraceCommand
{
	uint32 CommandId = 0;
	FGuLiControlCohortId CohortId;
	uint8 FrozenMemberIndex = 0u;
	FVector Direction = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	bool bAckRecorded = false;
};
#endif

/**
 * Actor 本身复制以便客户端发现；单位表现由名册与拥有者姿态流在本地重建。
 * ISM 实例变换、样本缓存与预测偏移不做属性复制，客户端 Mass 镜像不承担权威模拟。
 */
UCLASS(Config = Game)
class GULISTRIKE_API AGuLiCommanderPresentationActor : public AActor
{
	GENERATED_BODY()
UPROPERTY(Transient) TObjectPtr<UGuLiCommanderRouteLineComponent> RouteLines;

public:
	AGuLiCommanderPresentationActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Commander|Presentation")
	UInstancedStaticMeshComponent* GetUnitInstances() const { return UnitInstances; }

	/** Exact model/team lookup. Unassigned batches also supply team-independent mesh metadata. */
	UInstancedStaticMeshComponent* FindUnitInstances(uint16 UnitTypeId,
		EGuLiTeam Team = EGuLiTeam::Unassigned) const;
	/** Effective mesh scale; logical pose queries deliberately remain unit scale. */
	float GetUnitPresentationScale(uint16 UnitTypeId) const;
	/** Scaled mesh-local bounds in actual centimeters; consumers must not scale them again. */
	FBox GetUnitModelBoundsCentimeters(uint16 UnitTypeId) const;

	/** Returns all model/team batches in stable order for diagnostics and shared settings. */
	void GetUnitInstanceComponents(
		TArray<UInstancedStaticMeshComponent*>& OutComponents) const;

	UFUNCTION(BlueprintPure, Category = "Commander|Presentation")
	UInstancedStaticMeshComponent* GetRingInstances() const { return RingInstances; }

	/** Returns the current interpolated/predicted visual transform without exposing authority writes. */
	// 本地只读查询；有效时写 OutTransform 并返回 true，供 HUD/诊断使用，不开放权威写入。
	bool TryGetPresentedSoldierTransform(FGuLiSoldierId SoldierId, FTransform& OutTransform) const;
	/** Same timestamp used by the hit-white overlay; no independent health-delta detector in the HUD. */
	float GetSoldierHitStartTime(FGuLiSoldierId SoldierId) const;
	FGuLiCommanderVisualStatesChanged OnVisualStatesChanged;

	/**
	 * Returns the accepted-pose timeline result before local command prediction is applied.
	 * Combat target acquisition must use this API, never PresentedTransform.
	 */
	bool TryGetAuthoritativeSoldierTransform(
		FGuLiSoldierId SoldierId,
		FTransform& OutTransform) const;

	/** Starts a bounded visual prediction immediately before the caller sends the move RPC. */
	// 本地发送前调用；Selection 必须来自已确认选择，只对已存在且存活的士兵添加有限偏移。
	void BeginPredictedMove(
		const FGuLiCommanderSelectionState& Selection,
		const FVector& Target,
		uint32 ClientCommandId);

	/** Accepts or rejects only the matching prediction; authority state is never mutated here. */
	// 本地消费匹配移动 ACK；按控制组区分接受/拒绝，接受后等待对应权威命令样本收敛。
	void ResolvePredictedMove(const FGuLiCommandAck& Ack);

#if !UE_BUILD_SHIPPING
	/** Temporary diagnostic override; does not change config, server movement or prediction timing. */
	bool StartPredictionTrace(bool bDisableDisplacement, uint32 SoldierId, float DurationSeconds);
	bool StopPredictionTrace(FString& OutCsvPath);
	bool IsPredictionTraceActive() const { return bPredictionTraceActive; }
	void TraceCommanderMoveInput(uint32 ClientCommandId, const FVector& Target);
#endif

	/** Resets and reads a per-Soldier observation window without changing presentation state. */
	void ResetSoldierPresentationDiagnostics(FGuLiSoldierId SoldierId);
	bool TryGetSoldierPresentationDiagnostics(
		FGuLiSoldierId SoldierId,
		FGuLiCommanderSoldierPresentationDiagnostics& OutDiagnostics) const;
	float GetLatestClockRoundTripMilliseconds() const { return LatestClockRoundTripMilliseconds; }
	bool IsLatestClockRoundTripFromConnectionStats() const
	{
		return bLatestClockRoundTripFromConnectionStats;
	}

	/** C++-only, client-local performance tuning. These values are never replicated or persisted. */
	const FGuLiCommanderPresentationPerformanceSettings&
	GetBaselinePresentationPerformanceSettings() const
	{
		return PerformanceSettingsRegistry.GetBaselineSettings();
	}
	FGuLiCommanderPresentationPerformanceSettings
	GetEffectivePresentationPerformanceSettings() const
	{
		return PerformanceSettingsRegistry.GetEffectiveSettings();
	}
	TArray<FGuLiCommanderPresentationSettingView> ListPresentationPerformanceSettings(
		const FString& Prefix = FString()) const;
	FGuLiCommanderPresentationSettingResult GetPresentationPerformanceSetting(
		const FString& Key) const;
	FGuLiCommanderPresentationSettingResult ApplyLocalPresentationPerformanceOverride(
		const FString& Key,
		const FString& Value);
	TArray<FGuLiCommanderPresentationSettingResult> ClearLocalPresentationPerformanceOverrides(
		const FString& KeyOrAll);

#if WITH_DEV_AUTOMATION_TESTS
	/** Pure clock helpers used to prove that RTT/2 is removed before the 100 ms render delay. */
	static double TestOnly_EstimateServerNowAtPoseReceipt(
		double SampleServerTimeSeconds,
		float RoundTripMilliseconds);
	static double TestOnly_CalculateRenderServerTime(
		double EstimatedServerNowSeconds,
		float BackTimeSeconds);
	static double TestOnly_AdvanceEstimatedServerTime(
		double CurrentEstimateSeconds,
		double MeasuredServerNowAtReceiptSeconds,
		double SecondsSinceReceipt,
		float DeltaSeconds);
	static float TestOnly_SelectClockRoundTripMilliseconds(
		float ConnectionAverageLagSeconds,
		double ConnectionRawPingSeconds,
		float PlayerStateRoundTripMilliseconds,
		bool& bOutFromConnectionStats);
#endif

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FGuLiCommanderUnitTypeBatchRoutingTest;
	friend class FGuLiCommanderClientMaintenanceTest;
	friend class FGuLiCommanderHealthBarActivityTest;
	friend class FGuLiCommanderMiniMapCacheTest;
	friend class FGuLiScale020InterpolationContract;
#endif

	void InitializePresentationPerformanceSettings();
	void ApplyPresentationPerformanceSettings();
	void InitializeUnitInstanceBatches();
	void UpdatePhasedInstances(const TMap<uint16,TArray<FTransform>>& Desired);
	void UpdateWreckInstances(const TMap<uint16,TArray<FTransform>>& Desired);
	void UpdateHitFlashInstances(const TMap<uint16,TArray<FTransform>>& Transforms,
		const TMap<uint16,TArray<float>>& StartTimes);
	void ConfigureUnitInstanceComponent(UInstancedStaticMeshComponent& Component) const;
	void SetUnitInstanceBatchesVisibility(bool bVisible);
	uint16 ResolveUnitBatchTypeId(uint16 RequestedUnitTypeId);
	static uint32 MakeUnitBatchKey(uint16 UnitTypeId, EGuLiTeam Team);
	UInstancedStaticMeshComponent* FindUnitInstancesByBatch(uint32 BatchKey) const;
	UInstancedStaticMeshComponent* EnsureUnitTeamBatch(uint16 UnitTypeId, EGuLiTeam Team);
	int32 AcquireUnitInstanceSlot(uint16 BatchUnitTypeId, EGuLiTeam Team);
	void ReleaseUnitInstanceSlot(uint16 BatchUnitTypeId, EGuLiTeam Team, int32 InstanceIndex);
	void ResolveSoftAssets();
	AGuLiSoldierStateReplicator* FindStateReplicator();
	APlayerController* FindLocalController();
	// 本地拉取 NetSync 队列并按样本时间排序；不发送 RPC，也不等待整帧重组。
	void ConsumePoseChunks(double LocalNowSeconds);
	void IngestPoseChunk(const FGuLiSoldierPoseChunk& Chunk, double LocalNowSeconds);
	void InsertPoseSample(
		FGuLiSoldierId SoldierId,
		const FGuLiCommanderBufferedSoldierPose& Sample,
		double LocalNowSeconds);
	// 按服务器渲染时间求值：有样本区间则插值，越过最新样本只短时外推；无任何有效姿态返回 false。
	bool EvaluateAuthoritativeTransform(
		FGuLiCommanderPresentedSoldier& Soldier,
		double RenderServerTimeSeconds,
		FTransform& OutTransform) const;
	void ApplyPrediction(
		FGuLiSoldierId SoldierId,
		double LocalNowSeconds,
		FTransform& InOutTransform);
	void BeginPredictionResolution(FGuLiCommanderPredictedMove& Prediction, double LocalNowSeconds);
	// 本地创建仅含身份/生命/变换的 Mass 镜像；Dedicated Server 不创建，不接入客户端移动模拟。
	bool EnsureClientMirrorArchetype();
	void EnsureClientMirrorEntity(const FGuLiSoldierStateItem& ReliableState);
	void FlushClientMirrorUpdates(double Now);
	void DestroyClientMirrorEntities();
	void ResetNetworkPresentationState();
	// 每 Tick 只检查来源/就绪边沿；失效时一次性清样本与镜像，保留稳定 ISM 槽位。
	bool UpdateNetworkPresentationSource(AGuLiSoldierStateReplicator* Replicator);
	void EnsureStableInstancePool(AGuLiSoldierStateReplicator& Replicator);
	void MaintainInstancePool(AGuLiSoldierStateReplicator& Replicator, bool bMaintenanceDue);
	void BindRosterSource(AGuLiSoldierStateReplicator* Replicator);
	void HandleRosterDelta(const FGuLiSoldierRosterDelta& Delta);
	void HandleSelectionChanged(const FGuLiCommanderSelectionState& Selection);
	void ApplyReliableStateChanges(const AGuLiSoldierStateReplicator& Replicator, double Now);
	void HideInstancePool();
	void RebuildLocalInstances(float DeltaSeconds);
	FTransform BuildRingTransform(const FTransform& SoldierTransform) const;
	static bool IsAckResultAccepted(EGuLiCommandAckResult Result);

#if !UE_BUILD_SHIPPING
	void AppendPredictionTrace(EGuLiPredictionTraceEvent Event,
		const FGuLiCommanderBufferedSoldierPose* Sample = nullptr);
	void CapturePredictionTraceFrame(FGuLiSoldierId SoldierId, double RenderServerSeconds);
	void TracePredictionAck(const FGuLiCommandAck& Ack);
	TArray<FGuLiPredictionTraceRow> PredictionTraceRows;
	TArray<FGuLiPredictionTraceCommand> PredictionTraceCommands;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> PredictionTraceAckSource;
	FDelegateHandle PredictionTraceAckHandle;
	TMap<uint32, double> PredictionTraceOrderSampleTimes;
	FGuLiSoldierId PredictionTraceSoldier;
	FVector PredictionTraceDirection = FVector::ZeroVector;
	FVector PredictionTraceTarget = FVector::ZeroVector;
	FVector PredictionTraceRequestedOffset = FVector::ZeroVector;
	FVector PredictionTracePreviousPresented = FVector::ZeroVector;
	FVector PredictionTracePreviousAuthoritative = FVector::ZeroVector;
	double PredictionTracePreviousTime = 0.0;
	double PredictionTraceEndTime = 0.0;
	double PredictionTraceRenderServerTime = 0.0;
	uint32 PredictionTraceCommandId = 0;
	uint32 PredictionTraceOrderId = 0;
	bool bPredictionTraceActive = false;
	bool bPredictionTraceDisableDisplacement = false;
	bool bPredictionTraceHasPreviousFrame = false;
	bool bPredictionTraceHandoffRecorded = false;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> UnitInstances;

	/** Actor-owned components; the unassigned default model aliases UnitInstances. */
	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UInstancedStaticMeshComponent>> UnitInstancesByBatch;
	UPROPERTY(Transient) TMap<uint16,TObjectPtr<UInstancedStaticMeshComponent>> PhasedInstancesByType;
	UPROPERTY(Transient) TMap<uint16,TObjectPtr<UInstancedStaticMeshComponent>> HitFlashInstancesByType;
	UPROPERTY(Transient) TMap<uint16,TObjectPtr<UInstancedStaticMeshComponent>> WreckInstancesByType;
	TMap<uint16,TArray<FTransform>> CachedWreckTransforms;
	TMap<uint16,TArray<FTransform>> CachedPhasedTransforms;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> RingInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UStaticMesh> UnitMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	int32 RingMeshVfxId = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UMaterialInterface> UnitMaterialAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	int32 RingMaterialVfxId = 0;

	// 基线大于 10 Hz 的一个采样周期；每名士兵再按自己的实际收包间隔自适应增加。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float InterpolationBackTimeSeconds = 0.12f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float MaximumAdaptiveInterpolationBackTimeSeconds = 0.35f;

	// 缺少新样本时最多外推的时间，超过后停在有界估计位置，避免持续漂移。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float MaximumExtrapolationSeconds = 0.1f;

	// Total displayed travel speed during catch-up, relative to the published Mass move speed.
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float MaximumCorrectionSpeedMultiplier = 3.0f;

	// 本地预表现最长时间；再受距离上限约束，不能代替服务器寻路。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0"))
	float PredictionDurationSeconds = 0.25f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumPredictionDistanceCentimeters = 180.0f;

	// 纠偏时把本地偏移渐退到 0 的时间；最终基准仍来自服务器样本。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0"))
	float PredictionResolveSeconds = 0.15f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation", meta = (ClampMin = "0.0"))
	float WreckLifetimeSeconds = 5.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Performance", meta = (Units = "cm"))
	int32 UnitCullDistanceCentimeters = FGuLiCommanderPresentationPerformanceSettings::DefaultUnitCullDistanceCentimeters;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Performance", meta = (Units = "cm"))
	int32 RingCullDistanceCentimeters = FGuLiCommanderPresentationPerformanceSettings::DefaultRingCullDistanceCentimeters;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Performance")
	bool bUnitCastShadow = false;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Performance")
	bool bUnitAffectDistanceFieldLighting = false;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Performance")
	bool bUnitAffectDynamicIndirectLighting = false;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Performance")
	bool bUnitVisibleInRayTracing = false;

	FGuLiCommanderPresentationPerformanceRegistry PerformanceSettingsRegistry;

	TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
	TWeakObjectPtr<APlayerController> LocalController;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> ObservedNetSyncComponent;
	TWeakObjectPtr<AGuLiBattlePlayerState> ObservedPlayerState;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> ObservedStateReplicator;
	TWeakObjectPtr<UMassEntitySubsystem> ClientMirrorMassSubsystem;
	FMassArchetypeHandle ClientMirrorArchetype;
	// SoldierId 到本地实体句柄的映射；两端 FMassEntityHandle 不相同，不能当网络身份发送。
	TMap<FGuLiSoldierId, FMassEntityHandle> ClientMirrorEntities;
	TMap<FGuLiSoldierId, TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe>> MirrorLiveTokens;
	TArray<FGuLiCommanderMirrorCreate> PendingMirrorCreates;
	TSet<FGuLiSoldierId> PendingMirrorStates;
	TSet<FGuLiSoldierId> PendingMirrorTransforms;
	TSet<FGuLiSoldierId> PendingPoolIds;
	TSet<FGuLiSoldierId> RetryPoolIds;
	TSet<FGuLiSoldierId> PendingRemovedIds;
	TSet<FGuLiSoldierId> PendingStateIds;
	TSet<FGuLiSoldierId> PendingVisualChangeIds;
	TSet<FGuLiSoldierId> PendingDestructionIds;
	TSet<FGuLiSoldierId> DirtyRingColorIds;
	TSet<FGuLiSoldierId> SelectedSoldiers;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> BoundRosterSource;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> BoundSelectionSource;
	FDelegateHandle RosterDeltaHandle;
	FDelegateHandle SelectionChangedHandle;
	FGuLiCommanderRefreshCadence MaintenanceCadence;
	FGuLiCommanderRefreshCadence MirrorCadence;
	FGuLiCommanderRefreshCadence ReplicatorResolveCadence;
	FGuLiCommanderRefreshCadence ControllerResolveCadence;
	bool bReconcilePool = true;
	bool bPoolChangesPending = false;
	TArray<int32> DirtyRingTransformSlots;
	TMap<FGuLiSoldierId, FGuLiCommanderSoldierInstanceHandle> SoldierInstanceHandles;
	TArray<int32> FreeRingInstanceIndices;
	TMap<uint32, FGuLiCommanderUnitInstanceBatchState> UnitInstanceBatchStates;
	TSet<uint16> LoggedMissingUnitBatchTypes;
	TMap<FGuLiSoldierId, FGuLiCommanderPresentedSoldier> PresentedSoldiers;
	TMap<FGuLiSoldierId, FGuLiCommanderPredictedMove> PredictedMoves;
	TMap<FGuLiSoldierId, double> WreckExpireTimes;
	TArray<FTransform> CachedRingTransforms;
	TArray<FLinearColor> CachedRingColors;
	double LatestMeasuredServerNowSeconds = 0.0;
	double LatestClockMeasurementLocalTimeSeconds = 0.0;
	double EstimatedServerTimeSeconds = 0.0;
	float LatestClockRoundTripMilliseconds = 0.0f;
	uint32 LatestClockFrameSequence = 0u;
	// 战局与同步代次变化都会触发本地重建；Epoch 只比较身份，不按数值大小判断新旧。
	uint32 CurrentAuthorityEpoch = 0u;
	uint32 LastObservedSyncGeneration = 0u;
	uint32 LastObservedConnectionGeneration = 0u;
	uint32 LastObservedMatchEpoch = 0u;
	uint16 DefaultUnitTypeId = 1u;
	bool bObservedSoldierStreamReady = false;
	bool bNetworkPresentationHidden = false;
	bool bServerClockInitialized = false;
	bool bLatestClockRoundTripFromConnectionStats = false;
	bool bLoggedInstancePoolFailure = false;
	bool bLoggedInvalidPerformanceConfig = false;
	bool bUnitBatchCapacityReserved = false;
};

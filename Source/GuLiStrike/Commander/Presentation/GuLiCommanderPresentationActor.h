// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Presentation/GuLiCommanderPresentationPerformanceSettings.h"
#include "GameFramework/Actor.h"
#include "MassArchetypeTypes.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "GuLiCommanderPresentationActor.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiSoldierStateReplicator;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMassEntitySubsystem;
class USceneComponent;
class UStaticMesh;

/** One authoritative 10 Hz sample after resolving its chunk-relative position. */
// 本地解压后的带时间戳样本；SampleIndex/ChunkIndex 留作诊断，位置已恢复到世界坐标。
struct FGuLiCommanderBufferedSoldierPose
{
	double ServerTimeSeconds = 0.0;
	uint32 FrameSequence = 0u;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector ChunkAnchor = FVector::ZeroVector;
	FVector RelativeLocation = FVector::ZeroVector;
	float FacingYawDegrees = 0.0f;
	uint32 ActiveOrderId = 0u;
	uint16 ChunkIndex = 0u;
	uint8 SampleIndex = 0u;
	bool bTeleport = false;
};

/** Client-only interpolation state. It intentionally contains no Mass movement fragments. */
// 每个 SoldierId 的客户端缓存，最多保留有限历史样本；AuthoritativeTransform 是样本求值结果，不是服务器实时对象。
struct FGuLiCommanderPresentedSoldier
{
	TArray<FGuLiCommanderBufferedSoldierPose> Samples;
	FTransform AuthoritativeTransform = FTransform::Identity;
	FTransform PresentedTransform = FTransform::Identity;
	EGuLiSoldierLifeState LastLifeState = EGuLiSoldierLifeState::Alive;
	double LastPoseReceiptLocalTimeSeconds = 0.0;
	double MaximumPoseReceiptGapSeconds = 0.0;
	FVector LastUntaggedHardSnapDelta = FVector::ZeroVector;
	FVector LastHardSnapPriorSampleDelta = FVector::ZeroVector;
	FVector LastHardSnapSampleVelocity = FVector::ZeroVector;
	FVector LastHardSnapCurrentAnchor = FVector::ZeroVector;
	FVector LastHardSnapCurrentRelative = FVector::ZeroVector;
	FVector LastHardSnapPreviousAnchor = FVector::ZeroVector;
	FVector LastHardSnapPreviousRelative = FVector::ZeroVector;
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
};

/** Non-authoritative diagnostics used by the development network acceptance gate. */
struct FGuLiCommanderSoldierPresentationDiagnostics
{
	double MaximumPoseReceiptGapSeconds = 0.0;
	FVector LastUntaggedHardSnapDelta = FVector::ZeroVector;
	FVector LastHardSnapPriorSampleDelta = FVector::ZeroVector;
	FVector LastHardSnapSampleVelocity = FVector::ZeroVector;
	FVector LastHardSnapCurrentAnchor = FVector::ZeroVector;
	FVector LastHardSnapCurrentRelative = FVector::ZeroVector;
	FVector LastHardSnapPreviousAnchor = FVector::ZeroVector;
	FVector LastHardSnapPreviousRelative = FVector::ZeroVector;
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

/**
 * Actor 本身复制以便客户端发现；单位表现由名册与拥有者姿态流在本地重建。
 * ISM 实例变换、样本缓存与预测偏移不做属性复制，客户端 Mass 镜像不承担权威模拟。
 */
UCLASS(Config = Game)
class GULISTRIKE_API AGuLiCommanderPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	AGuLiCommanderPresentationActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Commander|Presentation")
	UInstancedStaticMeshComponent* GetUnitInstances() const { return UnitInstances; }

	UFUNCTION(BlueprintPure, Category = "Commander|Presentation")
	UInstancedStaticMeshComponent* GetRingInstances() const { return RingInstances; }

	/** Returns the current interpolated/predicted visual transform without exposing authority writes. */
	// 本地只读查询；有效时写 OutTransform 并返回 true，供 HUD/诊断使用，不开放权威写入。
	bool TryGetPresentedSoldierTransform(FGuLiSoldierId SoldierId, FTransform& OutTransform) const;

	/** Starts a bounded visual prediction immediately before the caller sends the move RPC. */
	// 本地发送前调用；Selection 必须来自已确认选择，只对已存在且存活的士兵添加有限偏移。
	void BeginPredictedMove(
		const FGuLiCommanderSelectionState& Selection,
		const FVector& Target,
		uint32 ClientCommandId);

	/** Accepts or rejects only the matching prediction; authority state is never mutated here. */
	// 本地消费匹配移动 ACK；按控制组区分接受/拒绝，接受后等待对应权威命令样本收敛。
	void ResolvePredictedMove(const FGuLiCommandAck& Ack);

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
	void InitializePresentationPerformanceSettings();
	void ApplyPresentationPerformanceSettings();
	void ResolveSoftAssets();
	AGuLiSoldierStateReplicator* FindStateReplicator();
	AGuLiCommanderPlayerController* FindLocalController();
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
	void UpdateClientMirrorEntity(
		const FGuLiSoldierStateItem& ReliableState,
		const FTransform* PresentedTransform);
	void DestroyClientMirrorEntities();
	void ResetNetworkPresentationState();
	void EnsureStableInstancePool(const AGuLiSoldierStateReplicator& Replicator);
	void RebuildLocalInstances(float DeltaSeconds);
	FTransform BuildRingTransform(const FTransform& SoldierTransform) const;
	static bool IsAckResultAccepted(EGuLiCommandAckResult Result);

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> UnitInstances;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> RingInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UStaticMesh> UnitMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UStaticMesh> RingMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UMaterialInterface> UnitMaterialAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UMaterialInterface> RingMaterialAsset;

	// 默认渲染时间回退 0.1 秒以便插值；Config 可覆盖，不是网络发送延迟。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float InterpolationBackTimeSeconds = 0.1f;

	// 缺少新样本时最多外推的时间，超过后停在有界估计位置，避免持续漂移。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float MaximumExtrapolationSeconds = 0.1f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0", Units = "cm"))
	float HardSnapDistanceCentimeters = 1000.0f;

	// 本地预表现最长时间；再受距离上限约束，不能代替服务器寻路。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0"))
	float PredictionDurationSeconds = 0.25f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumPredictionDistanceCentimeters = 900.0f;

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
	TWeakObjectPtr<AGuLiCommanderPlayerController> LocalController;
	TWeakObjectPtr<UMassEntitySubsystem> ClientMirrorMassSubsystem;
	FMassArchetypeHandle ClientMirrorArchetype;
	// SoldierId 到本地实体句柄的映射；两端 FMassEntityHandle 不相同，不能当网络身份发送。
	TMap<FGuLiSoldierId, FMassEntityHandle> ClientMirrorEntities;
	TMap<FGuLiSoldierId, int32> SoldierInstanceIndices;
	TMap<FGuLiSoldierId, FGuLiCommanderPresentedSoldier> PresentedSoldiers;
	TMap<FGuLiSoldierId, FGuLiCommanderPredictedMove> PredictedMoves;
	TMap<FGuLiSoldierId, double> WreckExpireTimes;
	TArray<FTransform> CachedUnitTransforms;
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
	bool bServerClockInitialized = false;
	bool bLatestClockRoundTripFromConnectionStats = false;
	bool bLoggedInstancePoolFailure = false;
	bool bLoggedInvalidPerformanceConfig = false;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Mass/GuLiSoldierCombat.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "Commander/Mass/GuLiMassExternalControl.h"
#include "GuLiBattleAuthoritySubsystem.generated.h"

class AGuLiBattlePlayerState;
class ANavigationData;
struct FGuLiBattleAuthorityState;
class UGuLiCommanderSkillCatalog;
struct FGuLiActiveSkillUnitResult;
struct FGuLiActiveSkillRuntime;
struct FGuLiCommanderInitialSpawnSlot;
struct FGuLiSoldierDefinition;

/**
 * 将 PImpl 的 delete 放在 .cpp 中执行，那里能看到 FGuLiBattleAuthorityState 的完整定义。
 * 避免 UHT 生成代码在只看到前置声明时删除不完整类型。
 */
struct FGuLiBattleAuthorityStateDeleter
{
	void operator()(FGuLiBattleAuthorityState* State) const;
};

/** Development measurements of completed fixed steps; excludes snapshot/network publication and rendering. */
struct FGuLiAuthorityPerformanceCounters
{
	uint64 Steps = 0;
	uint64 Shots = 0;
	double SimulationMilliseconds = 0.0;
	double CombatMilliseconds = 0.0;
	double MaxSimulationMilliseconds = 0.0;
	double MaxCombatMilliseconds = 0.0;
};

/** Server-only navigation state. Arrived and Blocked are terminal for the current order. */
enum class EGuLiSoldierNavigationState : uint8
{
	Idle,
	Normal,
	CenterlineRecovery,
	PersonalPathRecovery,
	Arrived,
	Blocked
};

/** Stable reason retained after a Soldier leaves an order in the Blocked state. */
enum class EGuLiSoldierNavigationFailure : uint8
{
	None,
	NavigationUnavailable,
	SurfaceMoveFailed,
	ExcessiveHeightDelta,
	PersonalPathFailed,
	FinalSlotInvalidated
};

/** Read-only authoritative state for one Soldier; never enters the v5 snapshot wire format. */
struct FGuLiSoldierNavigationDebug
{
	FGuLiSoldierId SoldierId;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	EGuLiSoldierNavigationState State = EGuLiSoldierNavigationState::Idle;
	EGuLiSoldierNavigationFailure Failure = EGuLiSoldierNavigationFailure::None;
	uint32 ActiveOrderId = 0u;
	uint32 LastCompletedOrderId = 0u;
	uint32 LastFailedOrderId = 0u;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector LastValidNavLocation = FVector::ZeroVector;
	FVector FinalSlot = FVector::ZeroVector;
	FVector CurrentWaypoint = FVector::ZeroVector;
	float DistanceToFinalSlotCentimeters = 0.0f;
	float NoProgressSeconds = 0.0f;
	double FailureSimulationSeconds = 0.0;
	int32 PathPointIndex = 0;
	int32 ConsecutiveSurfaceFailures = 0;
	int32 TotalSurfaceFailures = 0;
	int32 PersonalPathRetries = 0;
	bool bHasFinalSlot = false;
	bool bMoving = false;
};

/** Aggregate fixed-step navigation diagnostics. Querying this does not mutate simulation state. */
struct FGuLiNavigationStats
{
	int32 Alive = 0;
	int32 Idle = 0;
	int32 Active = 0;
	int32 Arrived = 0;
	int32 CenterlineRecovery = 0;
	int32 PersonalPathRecovery = 0;
	int32 Blocked = 0;
	uint64 SurfaceMoveCalls = 0u;
	uint64 SurfaceMoveFailures = 0u;
	uint64 MovementUpdateCalls = 0u;
	uint64 ForcedMovementUpdateCalls = 0u;
	uint64 ManualAvoidanceRefreshes = 0u;
	uint64 ManualAvoidanceCandidatePairs = 0u;
	uint64 ManualAvoidanceOverlapPairs = 0u;
	int32 MaximumManualAvoidanceBucketOccupancy = 0;
	uint64 GroundMechYieldSteps = 0u;
	int32 GroundMechYieldingSoldiers = 0;
	uint64 PredictiveAvoidanceSolves = 0u;
	uint64 ForcedPredictiveAvoidanceSolves = 0u;
	uint64 PredictiveAvoidanceCandidates = 0u;
	uint64 PredictiveAvoidanceColliderEvaluations = 0u;
	int32 MaximumPredictiveAvoidanceBucketOccupancy = 0;
	uint64 PathQueries = 0u;
	uint64 PersonalPathQueries = 0u;
	double LastDestinationPlanningMilliseconds = 0.0;
	double MaximumDestinationPlanningMilliseconds = 0.0;
	int32 PendingMovePlanningTasks = 0;
	uint64 MoveCandidateProjectionQueries = 0u;
	uint64 MovePlanningPathQueries = 0u;
	uint64 PartiallyAcceptedMoveCommands = 0u;
	uint64 MovePlanningFailureCounts[10] = {};
	FGuLiSoldierId SlowestSoldierId;
	float SlowestNoProgressSeconds = 0.0f;
};

/** Finite stages used by the async free-destination planner and its GM summary. */
enum class EGuLiMovePlanFailureStage : uint8
{
	None = 0,
	MemberInvalid,
	StartInvalid,
	CandidateProjection,
	Separation,
	FriendlyReservation,
	RestoredReservation,
	SharedPath,
	Connector,
	PersonalPath,
	CandidatesExhausted,
	Count
};

/** Poll result for a move request that is prepared across world frames. */
enum class EGuLiMovePlanningStatus : uint8
{
	NotFound,
	Pending,
	Completed
};

/** Server snapshot converted by NetSync into the OwnerOnly endpoint FastArray. */
struct FGuLiMoveEndpointSnapshot
{
	FGuLiSoldierId SoldierId;
	uint32 ActiveOrderId = 0u;
	FVector CommandStart = FVector::ZeroVector;
	FVector FinalDestination = FVector::ZeroVector;
	uint32 Revision = 0u;
};

/** One completed request's bounded diagnostics; candidate failures are aggregated, never spammed. */
struct FGuLiMoveCohortPlanningDebug
{
	FGuLiControlCohortId CohortId;
	uint8 MemberCount = 0u;
	uint32 EligibleMemberMask = 0u;
	uint32 AcceptedMemberMask = 0u;
	TArray<FGuLiSoldierId> FailedSoldierIds;
};

struct FGuLiMovePlanningDebug
{
	uint32 ClientCommandId = 0u;
	uint32 BatchOrderId = 0u;
	FVector RequestedTarget = FVector::ZeroVector;
	int32 TheoreticalCandidates = 0;
	int32 ProjectedCandidates = 0;
	int32 LegalCandidates = 0;
	int32 DesiredLegalSlots = 0;
	int32 InitialProjectionLimit = 0;
	int32 FinalProjectionLimit = 0;
	int32 ProjectionExpansionCount = 0;
	int32 PlanningWorldFrames = 0;
	int32 CandidateProjectionQueries = 0;
	int32 PathQueries = 0;
	int32 RouteSplitCount = 0;
	int32 ReservationConflictCount = 0;
	int32 AcceptedMembers = 0;
	int32 FailedMembers = 0;
	float MaximumSearchRadiusCentimeters = 0.0f;
	double PlanningMilliseconds = 0.0;
	bool bEscalatedToFullCandidatePool = false;
	TArray<FGuLiSoldierId> FailedSoldierIds;
	TArray<FGuLiMoveCohortPlanningDebug> Cohorts;
	uint64 FailureCounts[static_cast<uint8>(EGuLiMovePlanFailureStage::Count)] = {};
};

/**
 * 当前 World 的战斗权威子系统：在单机或服务器维护 500 名独立士兵，并按 10 Hz 推进模拟。
 *
 * SoldierId 是跨网络使用的士兵身份；Mass Entity 句柄只用于服务端本地访问 Fragment。
 * ControlCohort 是选兵产生的临时控制组，OrderFormation 是执行一次移动的临时编队，
 * 出生时的 25 人方阵不构成永久编制。
 *
 * 下方玩法入口供 C++ 调用；客户端 RPC、限流与重复请求处理由 NetSyncComponent 接入。
 * 本类负责权威判定和数据生成，网络发送及客户端表现由外部系统处理。
 */
UCLASS(Config = Game)
class GULISTRIKE_API UGuLiBattleAuthoritySubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Pure deployment geometry; callable on the CDO without starting a game world. */
	bool BuildInitialArmySpawnLayout(const TArray<FGuLiSoldierDefinition>& Definitions,
		const FVector& RedAssembly, const FVector& BlueAssembly,
		TArray<FGuLiCommanderInitialSpawnSlot>& OutSlots, FString& OutError) const;

	/** 在 .cpp 中使用 = default，按默认规则构造父类和成员；世界相关初始化放在 Initialize。 */
	UGuLiBattleAuthoritySubsystem();

	/** 在 .cpp 中使用 = default，自动析构成员；部队清理由 OnWorldEndPlay / Deinitialize 生命周期处理。 */
	virtual ~UGuLiBattleAuthoritySubsystem() override;

	//~ USubsystem / UWorldSubsystem 生命周期
	/** 仅允许游戏世界中的单机或服务器创建；普通客户端不创建第二套权威模拟。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 初始化 Mass、运行时调参依赖，分配本世界的权威状态并读取士兵参数。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 清理剩余部队并释放 AuthorityState，可与 EndPlay 阶段的清理重复调用。 */
	virtual void Deinitialize() override;

	/** 订阅专用导航重建通知并尝试生成部队；导航未就绪时由后续 Tick 重试。 */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** 解绑导航通知并清理本世界的权威部队。 */
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	//~ 生命周期接口结束

	//~ FTickableGameObject 更新与性能统计
	/** 每世界帧处理流场预算，再按 1/10 秒固定步追赶模拟；累计时间最多保留 4 步。 */
	virtual void Tick(float DeltaTime) override;

	/** 为引擎 Tick 性能统计提供本子系统的标识。 */
	virtual TStatId GetStatId() const override;
	//~ 更新与统计接口结束

	/** 服务器本地模块生命周期入口，由唯一士兵发布组件调用；公共 Battle 世界默认不启动部队模拟。 */
	void SetSoldierSimulationEnabled(bool bEnabled);

	/**
	 * 按权威位置、兵种、阵营和存活状态解析点/框/范围/同兵种意图，精确成员按最多 25 人分组。
	 * 检查权限、请求结构和已知选择版本；接受后提交 InOutSelection，并通过 OutAck 返回结果。
	 * 返回 true 表示请求被接受，选择内容未变化时也可成功。
	 */
	bool ResolveSelection(
		const AGuLiBattlePlayerState& PlayerState,
		const FGuLiSelectionRequest& Request,
		FGuLiCommanderSelectionState& InOutSelection,
		FGuLiCommandAck& OutAck);

	/**
	 * Starts or attaches to a deterministic, frame-budgeted free-destination plan.
	 * Global validation errors are returned immediately; accepted work remains Pending until a fixed-step commit.
	 */
	bool BeginMovePlanning(
		const AGuLiBattlePlayerState& PlayerState,
		const FGuLiMoveRequest& Request,
		const FGuLiCommanderSelectionState& Selection,
		FGuLiCommandAck& OutImmediateAck);

	/** Returns and consumes a completed result; Pending and NotFound never mutate OutUpdatedSelection. */
	EGuLiMovePlanningStatus PollMovePlanning(
		const AGuLiBattlePlayerState& PlayerState,
		uint32 ClientCommandId,
		FGuLiCommandAck& OutAck,
		FGuLiCommanderSelectionState& OutUpdatedSelection,
		bool& bOutSelectionChanged);

	/** Cancels every unconsumed plan owned by this PlayerState; used by connection/match reset. */
	void CancelMovePlanning(const AGuLiBattlePlayerState& PlayerState);

	/** Rebuilds the currently active endpoint set for one team; arrival/failure/death are omitted. */
	void BuildActiveMoveEndpointSnapshot(
		EGuLiTeam Team,
		TArray<FGuLiMoveEndpointSnapshot>& OutEndpoints) const;

	/** Last completed move-plan summary for GM inspection; optional cohort filtering is applied by the caller. */
	bool TryGetLastMovePlanningDebug(FGuLiMovePlanningDebug& OutDebug) const;

	/**
	 * 服务端刷新选择：去掉无效、重复和异阵营成员，整组无人存活时移除该组。
	 * 部分阵亡组保留死亡成员 ID，只更新存活数和共同指令摘要。
	 * 返回 true 表示 InOutSelection 发生变化；false 也可能只是无需更新。
	 */
	bool RefreshSelection(EGuLiTeam Team, FGuLiCommanderSelectionState& InOutSelection) const;

	/**
	 * 服务端 C++ 扣血入口；未知/无效 SoldierId、已死亡士兵或零伤害返回 false。
	 * 此处不计算攻防公式；死亡时清除指令和速度，保留实体用于残骸窗口及后续状态同步。
	 */
	bool ApplyDamage(FGuLiSoldierId SoldierId, float Amount);
	/** Selection is the fixed owner's confirmed server state; no client-provided caster list. */
	void ExecuteSelectedUnitSkills(AGuLiBattlePlayerState& PlayerState, const FGuLiCommanderSelectionState& Selection,
		const UGuLiCommanderSkillCatalog& Catalog, FGuid RequestId, bool bHasGroundPoint, FVector GroundPoint,
		TArray<FGuLiActiveSkillUnitResult>& OutResults);
	bool QueryUnitSkillRuntime(FGuLiSoldierId SoldierId, const UGuLiCommanderSkillCatalog& Catalog, FGuLiActiveSkillRuntime& OutRuntime) const;
	void CollectExternalUnitsInDisc(EGuLiTeam Team, FVector Center, float Radius, TArray<FGuLiMassExternalUnit>& Out) const;
	/** Physical presence across all teams, including externally locked units, for placement clearance. */
	void CollectExternalUnitsForClearance(const FBox& Bounds, TArray<FGuLiMassExternalUnit>& Out) const;
	bool CanApplyExternalUnitState(TConstArrayView<FGuLiMassExternalUnit> Participants, FGuid Token) const;
	bool ApplyExternalUnitState(TConstArrayView<FGuLiMassExternalUnit> Soldiers, FGuid CastId,
		bool bPhased, bool bLocked, bool bRelocate);
	bool IsSoldierPhased(FGuLiSoldierId Id) const;
	bool IsSoldierExternallyLocked(FGuLiSoldierId Id) const;
	bool ProjectExternalUnitLocation(FVector Desired, FVector& OutLocation) const;
	/** Stable Damage Ledger identity for a Soldier in the current match. */
	FGuLiTargetHandle MakeSoldierTargetHandle(FGuLiSoldierId SoldierId) const;

	/** Read-only server diagnostics; a valid ID can describe a dead Soldier. */
	bool TryGetSoldierCombatDebug(FGuLiSoldierId SoldierId, FGuLiSoldierCombatDebug& OutDebug) const;
	bool TryGetSoldierWeaponDebug(FGuLiSoldierId SoldierId, FName SlotId, FGuLiSoldierCombatDebug& OutDebug) const;
	/** Read-only server navigation diagnostics; terminal failure data remains available after ActiveOrderId clears. */
	bool TryGetSoldierNavigationDebug(FGuLiSoldierId SoldierId, FGuLiSoldierNavigationDebug& OutDebug) const;
	/** Returns a fresh state census plus cumulative surface/path-query counters. */
	FGuLiNavigationStats GetNavigationStats() const;
	/** Extends the executor registry and the skill resolver together. Game-thread/server only. */
	bool RegisterCombatExecutor(FName ExecutorId, FGuLiCombatExecutorRegistry::FExecutor Executor);
	/** Non-shipping authoritative test spawn. Uses the same data and current profile as initial Soldiers. */
	bool SpawnDebugSoldier(EGuLiTeam Team, uint16 UnitTypeId, const FVector& Location, FGuLiSoldierId& OutId);
	/** Formal production boundary: reserve the available population before creating any entity. */
	int32 SpawnSoldierBatch(EGuLiTeam Team, uint16 UnitTypeId, TConstArrayView<FVector> Locations, TArray<FGuLiSoldierId>& OutIds);
	FIntPoint GetTeamPopulation(EGuLiTeam Team) const;
	int32 GetTeamUnitCap() const;
	void RegisterAutomaticAdvance(TConstArrayView<FGuLiSoldierId> Soldiers);
	bool IsAutomaticallyAdvancing(FGuLiSoldierId Soldier) const;
	bool IssueAttackMove(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers, const FVector& Destination);
	void StopAutomaticMove(TConstArrayView<FGuLiSoldierId> Soldiers);
	void StopTaskSoldiers(TConstArrayView<FGuLiSoldierId> Soldiers);
	/** Supersede only unfinished planning; keep committed routes, velocity and destination reservations. */
	void InvalidateTaskSoldierPlans(TConstArrayView<FGuLiSoldierId> Soldiers);
	bool GetTaskSoldierInfo(FGuLiSoldierId Id, EGuLiTeam& Team, uint16& UnitTypeId, FVector& Location) const;
	bool SetExplicitSelection(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers,
		TConstArrayView<FGuLiControllableActorId> Actors, FGuLiCommanderSelectionState& Selection);
	const FGuLiAuthorityPerformanceCounters& GetPerformanceCounters() const { return PerformanceCounters; }

	/**
	 * 校验并应用本 World 的士兵调参：普通属性立即同步，最大生命变化按比例保留当前生命。
	 * 速度先记录为待提交值，部队就绪后在下一个 10 Hz 模拟步开头更新 Mass 共享参数。
	 * 完成属性遍历后返回部队记录总数；校验失败或部队/Mass 未就绪时返回 0。
	 * 部队未生成时仍可保存有效参数；返回值不表示新速度已提交的实体数量。
	 */
	int32 ApplyRuntimeTuning(const FGuLiSoldierRuntimeTuningValues& Values);

	/** 返回初始化时读取的士兵调参基线。 */
	const FGuLiSoldierRuntimeTuningValues& GetBaselineRuntimeTuning() const
	{
		return BaselineRuntimeTuning;
	}

	/** 返回当前有效的调参目标值；其中的速度可能仍等待下一个固定步提交。 */
	const FGuLiSoldierRuntimeTuningValues& GetEffectiveRuntimeTuning() const
	{
		return EffectiveRuntimeTuning;
	}

	/** 当前模拟使用的移动速度；实体存在时对应已提交的 Mass 共享参数，单位 cm/s。 */
	float GetCommittedMovementSpeedCmPerSecond() const
	{
		return MovementSpeedCentimetersPerSecond;
	}

	/**
	 * 清空并填充供可靠复制使用的离散状态，包括身份、阵营、生命和当前指令，不含连续位置。
	 * 包含仍保留记录的死亡士兵；本函数仅生成数据，不自行发送网络消息。
	 */
	void BuildSoldierStateSnapshot(TArray<FGuLiSoldierStateItem>& OutStates) const;

	/**
	 * 清空并捕获一帧压缩姿态；每块最多 32 人，超出相对坐标编码范围时提前拆块。
	 * GameMode 按目标 10 Hz 调度并经不可靠通道发送；本函数不自行计时或发 RPC。
	 * AuthorityEpoch 是外部传入的战局代际，必须非零，用于区分不同战局的姿态帧。
	 */
	void CaptureSoldierPoseChunks(
		TArray<FGuLiSoldierPoseChunk>& OutChunks,
		uint32 AuthorityEpoch);

	/**
	 * 按 SoldierId 查询服务端记录的位置和朝向，成功时写入 OutTransform；不接收客户端位置。
	 * 返回 true 仅表示找到记录，不能据此判断士兵仍存活或残骸仍可见。
	 */
	bool TryGetSoldierTransform(FGuLiSoldierId SoldierId, FTransform& OutTransform) const;

	/** Reuses the caller-owned array and captures finite locations of living authoritative Soldiers. */
	void BuildLivingSoldierLocationSnapshot(TArray<FVector>& OutLocations) const;
	/** Unquantized server poses for passive local-avoidance bodies; no movement or replication side effects. */
	void BuildGroundAvoidanceSnapshot(TArray<struct FGuLiGroundAvoidanceBody>& OutBodies) const;
	/** Full data-only cylinders used by player ground-mech side collision and virtual support. */
	void BuildGroundCollisionSnapshot(TArray<struct FGuLiGroundMassBody>& OutBodies,
		uint32& OutEpoch, uint32& OutSequence, double& OutSimulationSeconds) const;
	float GetExternalUnitRadius() const { return MemberAgentRadiusCentimeters; }

	/** 当前保留的临时移动编队数量；同一 BatchOrderId 可包含多个编队。 */
	int32 GetActiveOrderFormationCount() const;

	/** 权威士兵记录总数，包含死亡和残骸已隐藏的成员，不等于存活人数。 */
	UFUNCTION(BlueprintPure, Category = "Commander|Authority")
	int32 GetAuthoritativeMemberCount() const;

	/** 当前权威固定模拟步序号，供网络捕获调度与时序标记使用。 */
	uint32 GetServerSimTick() const;
	/** 因世界帧积压超过四步上限而丢弃的固定步数；正式 S1 必须为零。 */
	uint64 GetDroppedFixedStepCount() const;

	/** 是否已提交完整初始部队（关卡部署点，或无部署点时的默认500人）。 */
	UFUNCTION(BlueprintPure, Category = "Commander|Authority")
	bool HasSpawnedAuthorityPopulation() const;

private:
	int32 ReservedRedPopulation = 0;
	int32 ReservedBluePopulation = 0;
	bool SpawnReservedSoldier(EGuLiTeam Team, uint16 UnitTypeId, const FVector& Location, FGuLiSoldierId& OutId);
	void RetireExpiredSoldiers();
	/** 检查 World 存在且不是普通客户端；生成部队还需满足世界和导航就绪条件。 */
	bool IsAuthorityWorld() const;

	/** 要求全部出生点投影成功后才提交；失败返回 false，后续 Tick 可重试。 */
	bool TrySpawnAuthorityPopulation();

	/** 在生命周期允许时销毁本地 Mass 实体，并清空索引、编队与部队生成标记。 */
	void DestroyAuthorityPopulation();

	/** 执行一个固定步：提交待生效速度、更新编队、积分士兵位移，再按批次收尾。 */
	void TickAuthority(float FixedDeltaSeconds);
	void CommitCombatProfiles();
	void TickSoldierCombat();
	void RegisterCombatLedgerTargets();
	bool RegisterCombatLedgerTarget(FGuLiSoldierId SoldierId);
	void UnregisterCombatLedgerTargets();

	/** 每世界帧按预算采样可走性、提交纯数据后台构建，并核对版本后接收流场结果。 */
	void TickLocalFlowFields();

	/** Advances candidate projection/routing budgets once per rendered world frame. */
	void TickMovePlanning(int32& RemainingProjectionBudget, int32& RemainingPathBudget);

	/** Advances a NavMesh-generation repair job without exceeding the shared per-frame query budgets. */
	void TickNavigationRepairs(int32& RemainingProjectionBudget, int32& RemainingPathBudget);

	/** Commits every ready plan at the start of one authoritative 10 Hz step. */
	void CommitReadyMovePlans();

	/** Applies one completed navigation repair at a 10 Hz boundary before movement reads its results. */
	void CommitReadyNavigationRepairs();

	/** 在固定步边界交替迁移 Even/Odd Archetype，以替换只读共享的移动参数。 */
	void ApplyPendingMovementSpeed();

	/** 向运行时调参子系统回报已提交的速度，以及实际完成迁移的实体数量。 */
	void NotifyMovementSpeedCommitted(int32 AppliedEntityCount) const;

	/** 专用 CommanderSoldier 导航重建的委托回调：重建路径、更新版本并使旧流场失效。 */
	UFUNCTION()
	void HandleNavigationGenerationFinished(ANavigationData* NavigationData);
	/** Generic obstacle registry notification; invalidates only the manual-avoidance cache. */
	void HandleDynamicObstaclesChanged(uint32 ObstacleRevision);

	// 配置声明中的初始值可被 Game 配置覆盖；移动速度还会在 Initialize 中读取运行时调参值。
	/** 红方出生布局中心，使用世界坐标（cm）；实际士兵出生位置还需投影到专用 NavMesh。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Spawn")
	FVector RedSpawnCenter = FVector(0.0, 180000.0, 0.0);

	/** 蓝方出生布局中心，使用世界坐标（cm）；与红方一样须满足导航就绪条件。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Spawn")
	FVector BlueSpawnCenter = FVector(0.0, -180000.0, 0.0);

	/** 出生方阵之间的间距（cm），仅用于初始部署，不表示移动指令中的编队间距。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Formation", meta = (ClampMin = "1000.0", Units = "cm"))
	float GroupSpacingCentimeters = 3000.0f;

	/** 方阵成员槽位间距（cm），用于出生布局与行进阶段的动态列宽、槽位计算。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Formation", meta = (ClampMin = "100.0", Units = "cm"))
	float MemberSpacingCentimeters = 360.0f;

	/** 士兵导航/分离半径（cm），参与路径游标与重叠分离；需与 CommanderSoldier 导航 Agent 匹配。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float MemberAgentRadiusCentimeters = 150.0f;

	/** 当前已提交的移动速度上限（cm/s）；调参时延迟到固定步边界更新。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Movement", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MovementSpeedCentimetersPerSecond = 720.0f;

	/** 士兵与编队引导方向的最大转向速率（度/秒），按固定步时长限制本步转角。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Movement", meta = (ClampMin = "1.0", Units = "deg/s"))
	float FacingRateDegreesPerSecond = 90.0f;

	/** 是否启用服务端局部流场；关闭或无可用流场时，行进方向继续使用共享 NavMesh 路径。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|FlowField")
	bool bEnableLocalFlowField = false;

	/** 每世界帧所有编队共用的流场格子扫描预算；每个编队本轮最多处理 128 格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|FlowField", meta = (ClampMin = "1"))
	int32 FlowFieldWalkabilitySamplesPerTick = 512;

	/** 共享路径采样走廊的半宽（cm）；走廊外格子跳过 NavMesh 查询。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|FlowField", meta = (ClampMin = "100.0", Units = "cm"))
	float FlowFieldCorridorHalfWidthCentimeters = 3000.0f;

	/** 死亡后本地残骸保留的模拟秒数；到期隐藏 Transform，但保留 Entity 与士兵身份。 */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Life", meta = (ClampMin = "0.0", Units = "s"))
	float WreckLifetimeSeconds = 5.0f;

	/** 初始化时读取的基线参数，保留用于与本会话的有效调参值区分。 */
	FGuLiSoldierRuntimeTuningValues BaselineRuntimeTuning;

	/** 本会话最新校验通过的调参值；速度尚未提交时可能与当前模拟速度不同。 */
	FGuLiSoldierRuntimeTuningValues EffectiveRuntimeTuning;

	/** 待下个固定步提交的速度；最后一次请求覆盖或取消旧值，未设置表示无待提交修改。 */
	TOptional<float> PendingMovementSpeedCmPerSecond;

	/** 独占本 World 的运行时状态；Initialize 分配、Deinitialize 释放，Mass 子系统只被弱引用。 */
	TUniquePtr<FGuLiBattleAuthorityState, FGuLiBattleAuthorityStateDeleter> AuthorityState;
	bool bSoldierSimulationEnabled = false;
	TArray<FGuLiTargetHandle> RegisteredCombatLedgerTargets;
	FGuLiAuthorityPerformanceCounters PerformanceCounters;
};

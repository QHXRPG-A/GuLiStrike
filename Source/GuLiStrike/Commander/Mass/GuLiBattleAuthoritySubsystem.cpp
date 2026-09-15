// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"

#include "Algo/MinElement.h"
#include "Avoidance/MassAvoidanceFragments.h"
#include "Async/Async.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderDeploymentPoint.h"
#include "Commander/Framework/GuLiCommanderResourceAdapter.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Mass/GuLiCommanderSelectionQuery.h"
#include "Commander/Mass/GuLiControlCohortBuilder.h"
#include "Commander/Mass/Navigation/GuLiCommanderDestinationPlanner.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Mass/Navigation/GuLiLocalFlowField.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"
#include "Gameplay/CommanderSkills/GuLiUnitSkillExecution.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "HAL/IConsoleManager.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCommanderMass, Log, All);
CSV_DEFINE_CATEGORY(GuLiCommanderAuthority, true);

// 本文件是 Commander 的服务端战斗权威入口：维护独立士兵、解析选兵/移动意图，
// 以 10 Hz 推进权威状态，每步求解全部士兵位置并生成网络快照。
// SoldierId 跨网络，Mass Entity 句柄只在本地使用。
// 建议阅读顺序：生命周期与生成 -> ResolveSelection -> BeginMovePlanning -> TickAuthority -> 快照输出。
namespace GuLiCommanderMassPrivate
{
	// 2 队 × 10 个出生方阵 × 25 人 = 500 人；出生方阵只是摆放规则，不是永久编制。
	constexpr int32 SpawnFormationsPerTeam = 10;
	constexpr int32 TeamCount = 2;
	constexpr int32 SoldierCountPerFormation = 25;
	constexpr int32 TotalSoldierCount = SpawnFormationsPerTeam * TeamCount * SoldierCountPerFormation;
	constexpr int32 FormationColumns = 5;
	constexpr int32 FormationRows = 5;
	// 模拟按固定步长推进；最多积累 4 步，卡顿时舍弃超额时间，避免单帧无限追赶。
	constexpr float FixedStepSeconds = GuLiCommanderSimulationTiming::StepSeconds;
	constexpr float MaxAccumulatedSeconds = FixedStepSeconds * 4.0f;
	// 此常量及下方 FRequestGate 当前未接入请求路径；实际网络限流由 NetSyncComponent 负责。
	constexpr int32 MaxRequestsPerSecond = 10;
	constexpr float SpatialCellSizeCentimeters = 10000.0f;
	constexpr float FormationGuideMaximumLeadCentimeters = 9000.0f;
	constexpr float FormationWaypointToleranceCentimeters = 1800.0f;
	constexpr float FormationArrivalToleranceCentimeters = 500.0f;
	constexpr float TravelWeight = 0.70f;
	constexpr float SlotCorrectionWeight = 0.30f;
	constexpr float ManualAvoidanceStrength = 0.25f;
	constexpr float MaximumSurfaceStepZCentimeters = 250.0f;
	constexpr float ProgressDistanceCentimeters = 30.0f;
	constexpr float CenterlineRecoverySeconds = 1.0f;
	constexpr float PersonalRecoveryRetrySeconds = 2.0f;
	constexpr float PersonalRecoveryBlockedSeconds = 4.0f;
	constexpr int32 SurfaceFailuresBeforeCenterline = 2;
	constexpr int32 SurfaceFailuresBeforePersonalPath = 6;
	constexpr int32 MaximumPersonalPathQueriesPerStep = 4;
	constexpr int32 ExpansionSuccessfulStepsRequired = GuLiCommanderNavigationPolicy::RequiredTransitExpansionSuccessSteps;
	constexpr float TransitReassignmentCooldownSeconds = 0.5f;
	constexpr float DestinationMinimumSeparationCentimeters = 1600.0f;
	constexpr float DestinationMaximumProjectionCorrectionCentimeters = 750.0f;
	constexpr float FreeDestinationMaximumRadiusCentimeters = 45000.0f;
	constexpr int32 MoveCandidateProjectionBudgetPerFrame = 64;
	constexpr int32 MovePathQueryBudgetPerFrame = 4;
	constexpr float MoveCandidateReserveFraction = 0.25f;
	constexpr int32 MoveCandidateMinimumReserveSlots = 8;
	constexpr int32 MoveCandidateMaximumReserveSlots = 32;
	constexpr int32 MoveCommitConnectionRetryLimit = 2;
	// Spawn centers are authored in XY with Z=0 while the terrain is far from world zero.
	// Keep the strict XY correction, but search the full map-height range vertically.
	constexpr float SpawnProjectionVerticalExtentCentimeters = 50000.0f;

	static_assert(FormationColumns * FormationRows == SoldierCountPerFormation);
	static_assert(SoldierCountPerFormation == static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));
	static_assert(FormationColumns == GuLiCommanderNavigationPolicy::MaximumFormationColumns);
	static_assert(SoldierCountPerFormation == GuLiCommanderNavigationPolicy::FormationMemberCapacity);
	static_assert(GuLiCommanderNavigationPolicy::MovementUpdateIntervalTicks == 1u);

	struct FSoldierWeaponRuntime
	{
		FName SlotId;
		int32 ProfileIndex = INDEX_NONE;
		FGuLiSoldierAttackState Attack;
	};

	// 每名士兵的权威运行时记录。位置/速度在本类推进，再同步到 Mass Fragment 供处理器读取。
	struct FSoldierRuntime
	{
		// Entity 是当前 EntityManager 的访问键；SoldierId 不随选择组、移动编队或 Archetype 改变。
		FMassEntityHandle Entity;
		FGuLiSoldierId SoldierId;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint16 UnitTypeId = GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		double LastMovementUpdateSimulationSeconds = 0.0;
		float FacingYawDegrees = 0.0f;
		float Health = 100.0f;
		float MaxHealth = 100.0f;
		float Defense = 0.0f;
		TSharedPtr<const TArray<FGuLiResolvedSkillProfile>> WeaponProfiles;
		TArray<FSoldierWeaponRuntime> Weapons;
		// Unique active skill is independent of automatic weapon slots and advances on SimulationSeconds.
		FGuLiActiveSkillRuntime ActiveSkill;
		// StateRevision 标识离散状态变化；ActiveOrderId 指向当前批次，0 表示无活动指令。
		uint32 StateRevision = 1u;
		uint32 ActiveOrderId = 0u;
		bool bAutomaticAdvance = false;
		bool bAttackMoveHolding = false;
		FNavLocation LastValidNavLocation;
		FNavLocation FinalDestination;
		FVector CurrentNavigationWaypoint = FVector::ZeroVector;
		EGuLiSoldierNavigationState NavigationState = EGuLiSoldierNavigationState::Idle;
		EGuLiSoldierNavigationFailure NavigationFailure = EGuLiSoldierNavigationFailure::None;
		uint32 LastCompletedOrderId = 0u;
		uint32 LastFailedOrderId = 0u;
		uint32 FinalDestinationNavigationGeneration = 0u;
		float NoProgressSeconds = 0.0f;
		double FailureSimulationSeconds = 0.0;
		float BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		int32 LastProgressPathPointIndex = INDEX_NONE;
		int32 ConsecutiveSurfaceFailures = 0;
		int32 TotalSurfaceFailures = 0;
		TArray<FVector> PersonalPathPoints;
		int32 PersonalPathPointIndex = 0;
		int32 PersonalPathRetries = 0;
		bool bHasFinalDestination = false;
		bool bForceMovementUpdate = false;
		FVector LastCapturedPoseLocation = FVector::ZeroVector;
		uint32 LastCapturedPoseFrameSequence = 0u;
		// The wreck window retains the identity; expiration retires both entity and replicated record.
		double DeathSimulationSeconds = -1.0;
		bool bWreckExpired = false;
		FGuid ExternalControlToken;
		bool bPhased = false;
		bool bExternalActionsLocked = false;
		uint32 DisplacementFrameFloor = 0;
		FVector DisplacementLocation = FVector::ZeroVector;
		double DisplacementSimulationTime = 0;
		double ExternalLockSimulationTime = 0;
		bool CanAct() const { return IsAlive() && !bExternalActionsLocked; }
		bool IsPresent() const { return IsAlive() && !bPhased; }

		bool IsAlive() const
		{
			return FMath::IsFinite(Health) && Health > 0.0f;
		}
	};

	void SynchronizeSoldierWeapons(FSoldierRuntime& Soldier,
		TSharedPtr<const TArray<FGuLiResolvedSkillProfile>> Profiles, const double NowSeconds, const bool bInitial = false)
	{
		if (Soldier.WeaponProfiles == Profiles) return;
		for (auto& Weapon : Soldier.Weapons) Weapon.ProfileIndex = INDEX_NONE;
		if (Profiles) for (int32 Index = 0; Index < Profiles->Num(); ++Index)
		{
			const auto& Next = (*Profiles)[Index];
			auto* Weapon = Soldier.Weapons.FindByPredicate([&](const auto& Entry) { return Entry.SlotId == Next.SlotId; });
			if (!Weapon)
			{
				Weapon = &Soldier.Weapons.AddDefaulted_GetRef();
				Weapon->SlotId = Next.SlotId;
			}
			const auto* Previous = Soldier.WeaponProfiles ? Soldier.WeaponProfiles->FindByPredicate(
				[&](const auto& Entry) { return Entry.SlotId == Next.SlotId; }) : nullptr;
			if (!bInitial && (!Previous || !Previous->HasSameConfiguration(Next)))
				GuLiSoldierCombat::ReplaceProfile(Previous ? *Previous : FGuLiResolvedSkillProfile(), Next, NowSeconds, Weapon->Attack);
			if (!Next.bEquipped) Weapon->Attack.TargetId = {};
			Weapon->ProfileIndex = Index;
		}
		// Unequipped/removed slots retain only their cooldown tombstone until this soldier's life ends.
		for (auto& Weapon : Soldier.Weapons)
			if (Weapon.ProfileIndex == INDEX_NONE) Weapon.Attack.TargetId = {};
		Soldier.WeaponProfiles = MoveTemp(Profiles);
	}

	void InitializeSoldierCombat(FSoldierRuntime& Soldier, const FGuLiSoldierDefinition& Definition,
		const FGuLiSoldierRuntimeTuningValues& Tuning, const UGuLiArmySkillSubsystem* Skills)
	{
		Soldier.UnitTypeId = Definition.UnitTypeId;
		Soldier.MaxHealth = Tuning.bOverrideMaxHealth ? Tuning.MaxHealth : Definition.MaxHealth;
		Soldier.Health = Soldier.MaxHealth;
		Soldier.Defense = Tuning.bOverrideDefense ? Tuning.Defense : Definition.Defense;
		if (Skills)
		{
			SynchronizeSoldierWeapons(Soldier, Skills->GetSharedUnitProfiles(Soldier.Team, Soldier.UnitTypeId), 0.0, true);
		}
	}

	// 一个合法 ControlCohort 在一次移动请求中对应一个临时编队。
	// 同一请求的编队共享 BatchOrderId、TargetAnchor 和到达域，但各自持有路径与引导点。
	struct FOrderFormationRuntime
	{
		uint32 FormationId = 0u;
		uint32 BatchOrderId = 0u;
		FGuLiControlCohortId SourceCohortId;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		TArray<FGuLiSoldierId> MemberIds;
		TMap<uint32, uint8> SlotBySoldierId;
		TMap<uint32, FNavLocation> CommandStartNavLocationBySoldierId;
		TMap<uint32, FNavLocation> FinalDestinationBySoldierId;
		// GuideAnchor 是沿共享路径推进的虚拟领队；TargetAnchor 是本批移动的共同终点。
		FVector GuideAnchor = FVector::ZeroVector;
		FVector TargetAnchor = FVector::ZeroVector;
		TArray<FVector> PathPoints;
		int32 PathPointIndex = 0;
		float TravelFacingYawDegrees = 0.0f;
		GuLiCommanderNavigationPolicy::FFinalPathFrame FinalPathFrame;
		uint32 PathRevision = 1u;
		// 主线程分帧采样可走性，线程池只计算脱离 UObject 的数据；版本键决定结果能否安装。
		TSharedPtr<const FGuLiLocalFlowField, ESPMode::ThreadSafe> FlowField;
		FGuLiLocalFlowFieldBuildData PendingFlowBuildData;
		int32 NextFlowWalkabilitySample = INDEX_NONE;
		TFuture<TSharedPtr<FGuLiLocalFlowField, ESPMode::ThreadSafe>> FlowBuildFuture;
		bool bFlowBuildInFlight = false;
		bool bPathValid = true;
		int32 TransitColumnCount = FormationColumns;
		int32 ConsecutiveExpansionFitSteps = 0;
		double LastTransitReassignmentSeconds = -TNumericLimits<double>::Max();
		bool bLastMovementStepSucceeded = true;
		float FinalApproachTriggerRadiusCentimeters = 0.0f;
		bool bFinalApproachStarted = false;
		// 成员分别维护共享路径游标；最终槽位在当前 OrderId 生命周期内不重排。
		TMap<uint32, int32> MemberPathPointIndexBySoldierId;
	};

	struct FRequestGate
	{
		double WindowStartSeconds = 0.0;
		int32 RequestCount = 0;
		uint32 LastMoveClientCommandId = 0u;
		FGuLiCommandAck LastMoveAck;
	};

	struct FMoveDestinationReservation
	{
		uint32 OwnerSoldierId = 0u;
		FVector Location = FVector::ZeroVector;
	};

	struct FMoveMemberPlan
	{
		FGuLiSoldierId SoldierId;
		FGuLiControlCohortId CohortId;
		int32 CohortMemberIndex = INDEX_NONE;
		FNavLocation CommandStart;
		FNavLocation DestinationNav;
		FVector OldReservation = FVector::ZeroVector;
		GuLiCommanderDestinationPlanner::FFreeDestinationSlot Destination;
		EGuLiMovePlanFailureStage FailureStage = EGuLiMovePlanFailureStage::None;
		int32 LastCandidateIndex = INDEX_NONE;
		int32 CommitConnectionRetryCount = 0;
		bool bEligible = false;
		bool bStartValid = false;
		bool bHasDestination = false;
		bool bAccepted = false;
	};

	struct FMoveCohortPlan
	{
		FGuLiControlCohortId CohortId;
		TArray<FGuLiSoldierId> FrozenMemberIds;
		TArray<int32> MemberPlanIndices;
	};

	struct FMoveRouteTask
	{
		FGuLiControlCohortId CohortId;
		TArray<int32> MemberPlanIndices;
	};

	enum class EMovePlanningStage : uint8
	{
		ValidateStarts,
		ProjectCandidates,
		AssignDestinations,
		Route,
		ReconcileReservations,
		ReadyToCommit,
		Completed
	};

	struct FMovePlanningJob
	{
		TWeakObjectPtr<const AGuLiBattlePlayerState> PlayerState;
		TWeakObjectPtr<const AController> OwningController;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		FGuLiMoveRequest Request;
		FGuLiCommanderSelectionState FrozenSelection;
		FGuLiCommanderSelectionState UpdatedSelection;
		FGuLiCommandAck Ack;
		GuLiCommanderDestinationPlanner::FRequest PlannerRequest;
		TArray<GuLiCommanderDestinationPlanner::FFreeDestinationCandidate> HexCandidates;
		TArray<GuLiCommanderDestinationPlanner::FFreeDestinationSlot> LegalSlots;
		TMap<int32, FNavLocation> ProjectedNavByCandidateIndex;
		TArray<FMoveDestinationReservation> HardReservations;
		TArray<FMoveMemberPlan> Members;
		TArray<FMoveCohortPlan> Cohorts;
		TArray<FMoveRouteTask> RouteTasks;
		TArray<FOrderFormationRuntime> PreparedFormations;
		TMap<int32, int32> CandidateOwnerMemberPlanIndex;
		TSet<uint64> RejectedCandidatePairs;
		TMap<FIntPoint, TArray<int32>> HardReservationBuckets;
		TMap<FIntPoint, TArray<int32>> LegalSlotBuckets;
		FGuLiMovePlanningDebug Debug;
		EMovePlanningStage Stage = EMovePlanningStage::ValidateStarts;
		uint32 NavigationGeneration = 0u;
		uint32 AuthorityEpoch = 0u;
		int32 NextStartValidationIndex = 0;
		int32 NextCandidateProjectionIndex = 0;
		int32 DesiredLegalSlotCount = 0;
		int32 CandidateProjectionLimit = 0;
		int32 ProjectionExpansionCount = 0;
		double PlanningStartedAt = 0.0;
		bool bSelectionChanged = false;
		bool bRequireOwningController = false;
		bool bEscalatedToFullCandidatePool = false;
	};

	enum class ENavigationRepairMemberStage : uint8
	{
		ProjectCurrent,
		ProjectFinal,
		ProjectCandidate,
		QueryCandidatePath,
		Ready,
		Failed,
		Discarded
	};

	struct FNavigationRepairMemberTask
	{
		FGuLiSoldierId SoldierId;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint32 FormationId = 0u;
		uint32 ExpectedOrderId = 0u;
		FVector PreviousCurrentLocation = FVector::ZeroVector;
		FVector PreviousFinalLocation = FVector::ZeroVector;
		FNavLocation RefreshedCurrentLocation;
		FNavLocation RepairedFinalLocation;
		FNavLocation PendingCandidate;
		ENavigationRepairMemberStage Stage = ENavigationRepairMemberStage::ProjectCurrent;
		int32 NextCandidateIndex = 0;
		bool bHadFinalDestination = false;
		bool bWasArrived = false;
	};

	enum class ENavigationRepairFormationStage : uint8
	{
		ProjectTarget,
		ProjectGuide,
		QueryPath,
		Ready,
		Discarded
	};

	struct FNavigationRepairFormationTask
	{
		uint32 FormationId = 0u;
		uint32 ExpectedOrderId = 0u;
		FVector PreviousGuideAnchor = FVector::ZeroVector;
		FVector PreviousTargetAnchor = FVector::ZeroVector;
		FNavLocation ProjectedGuideAnchor;
		FNavLocation ProjectedTargetAnchor;
		TArray<FVector> RebuiltPathPoints;
		ENavigationRepairFormationStage Stage = ENavigationRepairFormationStage::ProjectTarget;
		bool bPathValid = false;
	};

	struct FNavigationRepairJob
	{
		uint32 NavigationGeneration = 0u;
		uint32 AuthorityEpoch = 0u;
		TArray<FNavigationRepairMemberTask> Members;
		TArray<FNavigationRepairFormationTask> Formations;
		TSet<uint32> PendingActiveSoldierIds;
		int32 NextMemberIndex = 0;
		int32 NextFormationIndex = 0;
		bool bReadyToCommit = false;
	};

	bool IsMovePlanningOwnerCurrent(
		const FMovePlanningJob& Job,
		const AGuLiBattlePlayerState& PlayerState)
	{
		if (!Job.bRequireOwningController)
		{
			return true;
		}
		const AController* Controller = Job.OwningController.Get();
		return Controller
			&& Controller->GetPlayerState<AGuLiBattlePlayerState>() == &PlayerState;
	}

	int32 CountMovePlannerMembers(
		const GuLiCommanderDestinationPlanner::FRequest& PlannerRequest)
	{
		int32 MemberCount = 0;
		for (const GuLiCommanderDestinationPlanner::FCohortInput& Cohort
			: PlannerRequest.Cohorts)
		{
			MemberCount += Cohort.Members.Num();
		}
		return MemberCount;
	}

	void InitializeMoveCandidateProjectionWindow(FMovePlanningJob& Job)
	{
		const int32 RequestedMemberCount = CountMovePlannerMembers(Job.PlannerRequest);
		const int32 ReserveSlotCount = FMath::Clamp(
			FMath::CeilToInt(static_cast<float>(RequestedMemberCount)
				* MoveCandidateReserveFraction),
			MoveCandidateMinimumReserveSlots,
			MoveCandidateMaximumReserveSlots);
		Job.DesiredLegalSlotCount = FMath::Min(
			Job.HexCandidates.Num(),
			RequestedMemberCount + ReserveSlotCount);
		Job.CandidateProjectionLimit =
			GuLiCommanderDestinationPlanner::FindProjectionPrefixEnd(
				Job.HexCandidates,
				Job.DesiredLegalSlotCount);
		Job.ProjectionExpansionCount = 0;
		Job.bEscalatedToFullCandidatePool = false;
		Job.Debug.DesiredLegalSlots = Job.DesiredLegalSlotCount;
		Job.Debug.InitialProjectionLimit = Job.CandidateProjectionLimit;
		Job.Debug.FinalProjectionLimit = Job.CandidateProjectionLimit;
		Job.Debug.ProjectionExpansionCount = 0;
		Job.Debug.bEscalatedToFullCandidatePool = false;
	}

	bool ExpandMoveCandidateProjectionWindow(
		FMovePlanningJob& Job,
		const bool bUseCompletePool)
	{
		const int32 CandidateCount = Job.HexCandidates.Num();
		if (Job.CandidateProjectionLimit >= CandidateCount)
		{
			return false;
		}

		const int32 MinimumCandidateCount = bUseCompletePool
			? CandidateCount
			: FMath::Min(
				CandidateCount,
				Job.CandidateProjectionLimit + FMath::Max(
					MoveCandidateProjectionBudgetPerFrame,
					FMath::Max(1, Job.DesiredLegalSlotCount - Job.LegalSlots.Num())));
		const int32 ExpandedLimit =
			GuLiCommanderDestinationPlanner::FindProjectionPrefixEnd(
				Job.HexCandidates,
				MinimumCandidateCount);
		if (ExpandedLimit <= Job.CandidateProjectionLimit)
		{
			return false;
		}

		Job.CandidateProjectionLimit = ExpandedLimit;
		++Job.ProjectionExpansionCount;
		Job.bEscalatedToFullCandidatePool |= ExpandedLimit >= CandidateCount;
		Job.Debug.FinalProjectionLimit = ExpandedLimit;
		Job.Debug.ProjectionExpansionCount = Job.ProjectionExpansionCount;
		Job.Debug.bEscalatedToFullCandidatePool = Job.bEscalatedToFullCandidatePool;
		return true;
	}

	bool RestartMovePlanningWithCompleteCandidatePool(FMovePlanningJob& Job)
	{
		if (Job.bEscalatedToFullCandidatePool
			|| Job.NextCandidateProjectionIndex >= Job.HexCandidates.Num()
			|| !ExpandMoveCandidateProjectionWindow(Job, true))
		{
			return false;
		}

		Job.CandidateOwnerMemberPlanIndex.Reset();
		// The entire uncommitted assignment transaction is rebuilt below. Clear
		// pair-local rejection history so Hungarian can reproduce the stable first
		// assignment; the now-complete legal pool supplies the subsequent fallback.
		Job.RejectedCandidatePairs.Reset();
		Job.RouteTasks.Reset();
		Job.PreparedFormations.Reset();
		for (FMoveMemberPlan& Member : Job.Members)
		{
			Member.bHasDestination = false;
			Member.bAccepted = false;
			Member.LastCandidateIndex = INDEX_NONE;
			Member.CommitConnectionRetryCount = 0;
			if (Member.bStartValid)
			{
				Member.FailureStage = EGuLiMovePlanFailureStage::None;
			}
		}
		Job.Stage = EMovePlanningStage::ProjectCandidates;
		return true;
	}

	uint64 MakeMoveCandidatePairKey(const FGuLiSoldierId SoldierId, const int32 CandidateIndex)
	{
		return (static_cast<uint64>(SoldierId.Value) << 32u)
			| static_cast<uint64>(static_cast<uint32>(CandidateIndex));
	}

	FIntPoint MakeMoveDestinationBucket(const FVector& Location)
	{
		return FIntPoint(
			FMath::FloorToInt(Location.X / DestinationMinimumSeparationCentimeters),
			FMath::FloorToInt(Location.Y / DestinationMinimumSeparationCentimeters));
	}

	void AddHardReservationToMoveJob(
		FMovePlanningJob& Job,
		const FMoveDestinationReservation& Reservation)
	{
		const int32 ReservationIndex = Job.HardReservations.Add(Reservation);
		Job.HardReservationBuckets.FindOrAdd(
			MakeMoveDestinationBucket(Reservation.Location)).Add(ReservationIndex);
	}

	bool IsMoveCandidateBlockedByHardReservation(
		const FMovePlanningJob& Job,
		const FVector& CandidateLocation)
	{
		const FIntPoint CenterBucket = MakeMoveDestinationBucket(CandidateLocation);
		const double MinimumSeparationSquared =
			FMath::Square(static_cast<double>(DestinationMinimumSeparationCentimeters));
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				const TArray<int32>* ReservationIndices = Job.HardReservationBuckets.Find(
					CenterBucket + FIntPoint(OffsetX, OffsetY));
				if (!ReservationIndices)
				{
					continue;
				}
				for (const int32 ReservationIndex : *ReservationIndices)
				{
					if (Job.HardReservations.IsValidIndex(ReservationIndex)
						&& FVector::DistSquared2D(
							Job.HardReservations[ReservationIndex].Location,
							CandidateLocation) < MinimumSeparationSquared)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	bool IsMoveCandidateTooCloseToLegalSlot(
		const FMovePlanningJob& Job,
		const FVector& CandidateLocation)
	{
		const FIntPoint CenterBucket = MakeMoveDestinationBucket(CandidateLocation);
		const double MinimumSeparationSquared =
			FMath::Square(static_cast<double>(DestinationMinimumSeparationCentimeters));
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				const TArray<int32>* SlotIndices = Job.LegalSlotBuckets.Find(
					CenterBucket + FIntPoint(OffsetX, OffsetY));
				if (!SlotIndices)
				{
					continue;
				}
				for (const int32 SlotIndex : *SlotIndices)
				{
					if (Job.LegalSlots.IsValidIndex(SlotIndex)
						&& FVector::DistSquared2D(
							Job.LegalSlots[SlotIndex].WorldDestination,
							CandidateLocation) < MinimumSeparationSquared)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	void ReleaseMoveMemberDestination(
		FMovePlanningJob& Job,
		const int32 MemberPlanIndex,
		const bool bRejectPair)
	{
		if (!Job.Members.IsValidIndex(MemberPlanIndex))
		{
			return;
		}
		FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
		if (Member.LastCandidateIndex != INDEX_NONE)
		{
			if (bRejectPair)
			{
				Job.RejectedCandidatePairs.Add(
					MakeMoveCandidatePairKey(Member.SoldierId, Member.LastCandidateIndex));
			}
			if (const int32* Owner =
				Job.CandidateOwnerMemberPlanIndex.Find(Member.LastCandidateIndex);
				Owner && *Owner == MemberPlanIndex)
			{
				Job.CandidateOwnerMemberPlanIndex.Remove(Member.LastCandidateIndex);
			}
		}
		Member.bHasDestination = false;
		Member.bAccepted = false;
	}

	bool IsMoveCandidateAvailableForMember(
		const FMovePlanningJob& Job,
		const FMoveMemberPlan& Member,
		const int32 CandidateIndex)
	{
		return !Job.CandidateOwnerMemberPlanIndex.Contains(CandidateIndex)
			&& !Job.RejectedCandidatePairs.Contains(
				MakeMoveCandidatePairKey(Member.SoldierId, CandidateIndex));
	}

	void ClaimMoveCandidate(
		FMovePlanningJob& Job,
		const int32 MemberPlanIndex,
		const GuLiCommanderDestinationPlanner::FFreeDestinationSlot& Slot,
		const FNavLocation& DestinationNav)
	{
		check(Job.Members.IsValidIndex(MemberPlanIndex));
		FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
		Member.Destination = Slot;
		Member.DestinationNav = DestinationNav;
		Member.LastCandidateIndex = Slot.CandidateIndex;
		Member.bHasDestination = true;
		Member.FailureStage = EGuLiMovePlanFailureStage::None;
		Job.CandidateOwnerMemberPlanIndex.Add(Slot.CandidateIndex, MemberPlanIndex);
	}

	void RemoveMoveMemberFromPreparedFormations(
		FMovePlanningJob& Job,
		const FGuLiSoldierId SoldierId)
	{
		for (FOrderFormationRuntime& Formation : Job.PreparedFormations)
		{
			Formation.MemberIds.Remove(SoldierId);
			Formation.CommandStartNavLocationBySoldierId.Remove(SoldierId.Value);
			Formation.FinalDestinationBySoldierId.Remove(SoldierId.Value);
		}
	}

	void CompleteMovePlanningJobWithSystemFailure(
		FMovePlanningJob& Job,
		const EGuLiCommandAckResult Result = EGuLiCommandAckResult::PathFailed)
	{
		Job.Ack.Result = Result;
		Job.Ack.BatchOrderId = 0u;
		for (FGuLiCohortCommandAck& CohortAck : Job.Ack.CohortResults)
		{
			CohortAck.AcceptedMemberMask = 0u;
			CohortAck.Result = CohortAck.EligibleMemberMask != 0u
				? Result
				: EGuLiCommandAckResult::NoSelection;
		}
		Job.UpdatedSelection = Job.FrozenSelection;
		Job.bSelectionChanged = false;
		Job.Ack.ServerSelectionRevision = Job.FrozenSelection.SelectionRevision;
		Job.Ack.Sanitize();
		Job.Stage = EMovePlanningStage::Completed;
	}

	// 跳过协议保留的 0；这里只分配非零序号，不保证 uint32 回绕后仍全局唯一。
	uint32 AllocateNonZero(uint32& Counter)
	{
		const uint32 Result = Counter++;
		if (Counter == 0u)
		{
			Counter = 1u;
		}
		return Result == 0u ? Counter++ : Result;
	}

	FVector MakeSpawnFormationOffset(const int32 FormationIndex, const float Spacing)
	{
		const int32 Column = FormationIndex % FormationColumns;
		const int32 Row = FormationIndex / FormationColumns;
		return FVector(
			(static_cast<float>(Column) - 2.0f) * Spacing,
			(static_cast<float>(Row) - 0.5f) * Spacing,
			0.0f);
	}

	FVector MakeFormationSlotOffset(
		const int32 SlotIndex,
		const float Spacing,
		const int32 RequestedColumnCount = FormationColumns)
	{
		return GuLiCommanderNavigationPolicy::MakeFormationSlotOffset(
			SlotIndex,
			Spacing,
			RequestedColumnCount);
	}

	// 只接受 CommanderSoldier 专用导航数据；不回退到默认 Agent，以免通行半径不匹配。
	ANavigationData* GetCommanderNavigationData(UNavigationSystemV1& NavigationSystem)
	{
		return GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(NavigationSystem);
	}

	bool ProjectPointToCommanderNavigation(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FVector& Point,
		const FVector& Extent,
		FNavLocation& OutLocation)
	{
		return NavigationSystem.ProjectPointToNavigation(
			Point,
			OutLocation,
			Extent,
			&NavigationData);
	}

	// 沿横向逐列投影探测当前引导点能容纳的宽度；这是局部探测，不是整条路径的宽度证明。
	bool CanFitFormationColumns(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FVector& Anchor,
		const float FacingYawDegrees,
		const float Spacing,
		const int32 ColumnCount)
	{
		const FRotator FacingRotation(0.0f, FacingYawDegrees, 0.0f);
		for (int32 ProbeIndex = 0; ProbeIndex < ColumnCount; ++ProbeIndex)
		{
			FVector LocalOffset = FVector::ZeroVector;
			LocalOffset.Y = (static_cast<float>(ProbeIndex) - static_cast<float>(ColumnCount - 1) * 0.5f)
				* Spacing;
			const FVector RequestedPoint = Anchor + FacingRotation.RotateVector(LocalOffset);
			FNavLocation ProjectedPoint;
			const bool bProbeWalkable = ProjectPointToCommanderNavigation(
				NavigationSystem,
				NavigationData,
				RequestedPoint,
				FVector(250.0f, 250.0f, 5000.0f),
				ProjectedPoint)
				&& FVector::DistSquared2D(RequestedPoint, ProjectedPoint.Location)
					<= FMath::Square(500.0f);
			if (!bProbeWalkable)
			{
				return false;
			}
		}
		return true;
	}

	// 从 5 列向 1 列收窄；没有成功探测时返回单列，路径有效性仍由独立检查决定。
	int32 DetermineTransitFormationColumns(
		UNavigationSystemV1* NavigationSystem,
		const ANavigationData* NavigationData,
		const FVector& GuideAnchor,
		const float FacingYawDegrees,
		const float Spacing)
	{
		if (!NavigationSystem || !NavigationData)
		{
			return 1;
		}
		TArray<uint8, TInlineAllocator<FormationColumns>> FitsByColumnCount;
		FitsByColumnCount.Init(0u, FormationColumns);
		for (int32 ColumnCount = FormationColumns; ColumnCount >= 1; --ColumnCount)
		{
			if (CanFitFormationColumns(
				*NavigationSystem,
				*NavigationData,
				GuideAnchor,
				FacingYawDegrees,
				Spacing,
				ColumnCount))
			{
				FitsByColumnCount[ColumnCount - 1] = 1u;
				break;
			}
		}
		return GuLiCommanderNavigationPolicy::SelectTransitColumnCount(
			FitsByColumnCount);
	}

	// 同速士兵共享只读移动/避让参数，避免每个实体存一份。
	// bIsCodeDrivenMovement 表示由项目代码推进位移；项目预测处理器只读取 CPA 参数并直接写输出。
	FMassArchetypeSharedFragmentValues MakeAuthoritySharedFragmentValues(
		FMassEntityManager& EntityManager,
		const float MovementSpeedCentimetersPerSecond,
		const float AgentRadiusCentimeters)
	{
		FMassMovementParameters MovementParameters;
		MovementParameters.MaxSpeed = MovementSpeedCentimetersPerSecond;
		MovementParameters.DefaultDesiredSpeed = MovementSpeedCentimetersPerSecond;
		MovementParameters.DefaultDesiredSpeedVariance = 0.0f;
		MovementParameters.MaxAcceleration = MovementSpeedCentimetersPerSecond * 4.0f;
		MovementParameters.bIsCodeDrivenMovement = true;
		MovementParameters.Update();

		FMassMovingAvoidanceParameters AvoidanceParameters;
		AvoidanceParameters.ObstacleDetectionDistance = AgentRadiusCentimeters * 8.0f;
		AvoidanceParameters.SeparationRadiusScale = 0.95f;
		AvoidanceParameters.ObstacleSeparationDistance = AgentRadiusCentimeters * 0.35f;
		AvoidanceParameters.PredictiveAvoidanceDistance = AgentRadiusCentimeters * 0.35f;

		FMassArchetypeSharedFragmentValues SharedValues;
		SharedValues.Add(EntityManager.GetOrCreateConstSharedFragment(
			MovementParameters.GetValidated()));
		SharedValues.Add(EntityManager.GetOrCreateConstSharedFragment(
			AvoidanceParameters.GetValidated()));
		SharedValues.Sort();
		return SharedValues;
	}

	FIntPoint MakeSpatialCell(const FVector& Location)
	{
		return FIntPoint(
			FMath::FloorToInt(Location.X / SpatialCellSizeCentimeters),
			FMath::FloorToInt(Location.Y / SpatialCellSizeCentimeters));
	}

	// 只投影一次公共终点；允许修正高度，但 XY 偏移不得超过一个 Agent 半径。
	bool ResolveSharedMoveTarget(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FVector& RequestedTarget,
		const float AgentRadiusCentimeters,
		FVector& OutTarget)
	{
		FNavLocation ProjectedTarget;
		const FVector ProjectionExtent(
			AgentRadiusCentimeters,
			AgentRadiusCentimeters,
			5000.0f);
		if (!ProjectPointToCommanderNavigation(
			NavigationSystem,
			NavigationData,
			RequestedTarget,
			ProjectionExtent,
			ProjectedTarget)
			|| !GuLiCommanderNavigationPolicy::IsProjectedTargetAcceptable(
				RequestedTarget,
				ProjectedTarget.Location,
				AgentRadiusCentimeters))
		{
			return false;
		}
		OutTarget = ProjectedTarget.Location;
		return true;
	}

	bool HasCompletePath(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FVector& Start,
		const FVector& Target)
	{
		FPathFindingQuery Query(nullptr, NavigationData, Start, Target);
		const FPathFindingResult Result = NavigationSystem.FindPathSync(MoveTemp(Query));
		return Result.IsSuccessful()
			&& Result.Path.IsValid()
			&& !Result.Path->IsPartial();
	}

	bool HasReachableSurfaceSegment(
		const ANavigationData& NavigationData,
		const FNavLocation& Start,
		const FVector& Target,
		const float TargetToleranceCentimeters = 100.0f,
		uint64* const InOutFallbackPathQueryCount = nullptr)
	{
		FNavLocation ReachedLocation;
		if (NavigationData.FindMoveAlongSurface(
				Start,
				Target,
				ReachedLocation,
				nullptr,
				nullptr)
			&& FVector::DistSquared(ReachedLocation.Location, Target)
				<= FMath::Square(static_cast<double>(TargetToleranceCentimeters)))
		{
			return true;
		}
		if (InOutFallbackPathQueryCount)
		{
			++(*InOutFallbackPathQueryCount);
		}
		FPathFindingQuery Query(nullptr, NavigationData, Start.Location, Target);
		return NavigationData.TestPath(NavigationData.GetConfig(), Query, nullptr);
	}

	// 每个临时编队从成员质心寻路一次，只接受至少两个点的完整路径，拒绝 partial path。
	bool BuildSharedPath(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FVector& Start,
		const FVector& SharedTarget,
		TArray<FVector>& OutPathPoints)
	{
		OutPathPoints.Reset();
		FNavLocation ProjectedStart;
		const FVector StartProjectionExtent(
			DestinationMaximumProjectionCorrectionCentimeters,
			DestinationMaximumProjectionCorrectionCentimeters,
			5000.0f);
		if (!ProjectPointToCommanderNavigation(
			NavigationSystem,
			NavigationData,
			Start,
			StartProjectionExtent,
			ProjectedStart)
			|| FVector::DistSquared2D(Start, ProjectedStart.Location)
				> FMath::Square(DestinationMaximumProjectionCorrectionCentimeters))
		{
			UE_LOG(
				LogGuLiCommanderMass,
				Warning,
				TEXT("Shared path start projection failed: nav=%s start=%s target=%s."),
				*GetNameSafe(&NavigationData),
				*Start.ToCompactString(),
				*SharedTarget.ToCompactString());
			return false;
		}

		FPathFindingQuery Query(
			nullptr,
			NavigationData,
			ProjectedStart.Location,
			SharedTarget);
		const FPathFindingResult Result = NavigationSystem.FindPathSync(MoveTemp(Query));
		if (Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial())
		{
			const TArray<FNavPathPoint>& PathPoints = Result.Path->GetPathPoints();
			if (PathPoints.Num() >= 1)
			{
				OutPathPoints.Reserve(FMath::Max(2, PathPoints.Num()));
				for (const FNavPathPoint& PathPoint : PathPoints)
				{
					OutPathPoints.Add(PathPoint.Location);
				}
				if (OutPathPoints.Num() == 1)
				{
					OutPathPoints.Add(SharedTarget);
				}
				return true;
			}
		}
		UE_LOG(
			LogGuLiCommanderMass,
			Warning,
			TEXT("Shared path query failed: nav=%s result=%u path=%d partial=%d points=%d start=%s target=%s."),
			*GetNameSafe(&NavigationData),
			static_cast<uint8>(Result.Result),
			Result.Path.IsValid() ? 1 : 0,
			Result.Path.IsValid() && Result.Path->IsPartial() ? 1 : 0,
			Result.Path.IsValid() ? Result.Path->GetPathPoints().Num() : 0,
			*ProjectedStart.Location.ToCompactString(),
			*SharedTarget.ToCompactString());
		return false;
	}

	// Move-plan retries are expected while splitting groups, so this variant records no per-attempt log.
	bool BuildCompletePathQuiet(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FNavLocation& Start,
		const FVector& Target,
		TArray<FVector>& OutPathPoints)
	{
		OutPathPoints.Reset();
		FPathFindingQuery Query(nullptr, NavigationData, Start.Location, Target);
		const FPathFindingResult Result = NavigationSystem.FindPathSync(MoveTemp(Query));
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
		{
			return false;
		}
		for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
		{
			OutPathPoints.Add(Point.Location);
		}
		if (OutPathPoints.IsEmpty())
		{
			return false;
		}
		if (OutPathPoints.Num() == 1)
		{
			OutPathPoints.Add(Target);
		}
		return true;
	}

	bool HasDirectSurfaceConnection(
		const ANavigationData& NavigationData,
		const FNavLocation& Start,
		const FVector& Target,
		const float ToleranceCentimeters = 100.0f)
	{
		FNavLocation Reached;
		return NavigationData.FindMoveAlongSurface(Start, Target, Reached, nullptr, nullptr)
			&& FVector::DistSquared(Reached.Location, Target)
				<= FMath::Square(static_cast<double>(ToleranceCentimeters));
	}

	// 只平均存活且仍服从指定指令的成员；RequiredOrderId 为 0 时不限制指令归属。
	FVector ComputeCentroid(
		TConstArrayView<FGuLiSoldierId> MemberIds,
		const TArray<FSoldierRuntime>& Soldiers,
		const TMap<uint32, int32>& SoldierIndexById,
		const uint32 RequiredOrderId = 0u)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const FGuLiSoldierId SoldierId : MemberIds)
		{
			const int32* SoldierIndex = SoldierIndexById.Find(SoldierId.Value);
			if (!SoldierIndex || !Soldiers.IsValidIndex(*SoldierIndex))
			{
				continue;
			}
			const FSoldierRuntime& Soldier = Soldiers[*SoldierIndex];
			if (!Soldier.IsAlive()
				|| (RequiredOrderId != 0u && Soldier.ActiveOrderId != RequiredOrderId))
			{
				continue;
			}
			Sum += Soldier.Location;
			++Count;
		}
		return Count > 0 ? Sum / static_cast<double>(Count) : FVector::ZeroVector;
	}

	// 匈牙利算法：给 Count 个成员与 Count 个候选槽位做一对一最小总代价匹配。
	// 内部使用 1-based 索引，右侧 0 是增广路径的哨兵；输出恢复为 0-based 槽位索引。
	void SolveMinimumCostAssignment(
		const int32 Count,
		const TFunctionRef<double(int32, int32)>& Cost,
		TArray<int32>& OutRightIndexByLeftIndex)
	{
		OutRightIndexByLeftIndex.Init(INDEX_NONE, Count);
		if (Count <= 0)
		{
			return;
		}

		// 左/右势用于计算约化代价；PreviousRight 记录增广路径，最终重接匹配关系。
		TArray<double> LeftPotential;
		TArray<double> RightPotential;
		TArray<int32> MatchedLeftByRight;
		TArray<int32> PreviousRight;
		LeftPotential.Init(0.0, Count + 1);
		RightPotential.Init(0.0, Count + 1);
		MatchedLeftByRight.Init(0, Count + 1);
		PreviousRight.Init(0, Count + 1);

		for (int32 Left = 1; Left <= Count; ++Left)
		{
			MatchedLeftByRight[0] = Left;
			int32 CurrentRight = 0;
			TArray<double> MinimumReducedCost;
			TArray<bool> bUsedRight;
			MinimumReducedCost.Init(TNumericLimits<double>::Max(), Count + 1);
			bUsedRight.Init(false, Count + 1);
			do
			{
				bUsedRight[CurrentRight] = true;
				const int32 CurrentLeft = MatchedLeftByRight[CurrentRight];
				double Delta = TNumericLimits<double>::Max();
				int32 NextRight = 0;
				for (int32 Right = 1; Right <= Count; ++Right)
				{
					if (bUsedRight[Right])
					{
						continue;
					}
					const double ReducedCost = Cost(CurrentLeft - 1, Right - 1)
						- LeftPotential[CurrentLeft] - RightPotential[Right];
					if (ReducedCost < MinimumReducedCost[Right])
					{
						MinimumReducedCost[Right] = ReducedCost;
						PreviousRight[Right] = CurrentRight;
					}
					if (MinimumReducedCost[Right] < Delta
						|| (FMath::IsNearlyEqual(MinimumReducedCost[Right], Delta)
							&& (NextRight == 0 || Right < NextRight)))
					{
						Delta = MinimumReducedCost[Right];
						NextRight = Right;
					}
				}

				for (int32 Right = 0; Right <= Count; ++Right)
				{
					if (bUsedRight[Right])
					{
						LeftPotential[MatchedLeftByRight[Right]] += Delta;
						RightPotential[Right] -= Delta;
					}
					else
					{
						MinimumReducedCost[Right] -= Delta;
					}
				}
				CurrentRight = NextRight;
			}
			while (MatchedLeftByRight[CurrentRight] != 0);

			do
			{
				const int32 Previous = PreviousRight[CurrentRight];
				MatchedLeftByRight[CurrentRight] = MatchedLeftByRight[Previous];
				CurrentRight = Previous;
			}
			while (CurrentRight != 0);
		}

		for (int32 Right = 1; Right <= Count; ++Right)
		{
			if (MatchedLeftByRight[Right] > 0)
			{
				OutRightIndexByLeftIndex[MatchedLeftByRight[Right] - 1] = Right - 1;
			}
		}
	}

	// 只给仍执行当前指令的有效成员分配途中槽位；最终槽位由独立规划器固定。
	// 先取靠近中心的 N 个槽位，再以 XY 距离平方为代价匹配，减少成员互相穿越。
	void AssignFormationSlots(
		FOrderFormationRuntime& Formation,
		const TArray<FSoldierRuntime>& Soldiers,
		const TMap<uint32, int32>& SoldierIndexById,
		const float SlotSpacing,
		const uint32 RequiredOrderId,
		const int32 ColumnCount = FormationColumns)
	{
		Formation.SlotBySoldierId.Reset();
		TArray<FGuLiSoldierId> ValidMembers;
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* Index = SoldierIndexById.Find(SoldierId.Value);
			if (Index && Soldiers.IsValidIndex(*Index) && Soldiers[*Index].IsAlive()
				&& (RequiredOrderId == 0u || Soldiers[*Index].ActiveOrderId == RequiredOrderId))
			{
				ValidMembers.Add(SoldierId);
			}
		}
		ValidMembers.Sort([](const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs)
		{
			return Lhs.Value < Rhs.Value;
		});

		TArray<int32> AvailableSlots;
		for (int32 SlotIndex = 0; SlotIndex < SoldierCountPerFormation; ++SlotIndex)
		{
			AvailableSlots.Add(SlotIndex);
		}
		AvailableSlots.Sort([SlotSpacing, ColumnCount](const int32 Lhs, const int32 Rhs)
		{
			const double LhsDistance = MakeFormationSlotOffset(
				Lhs,
				SlotSpacing,
				ColumnCount).SizeSquared2D();
			const double RhsDistance = MakeFormationSlotOffset(
				Rhs,
				SlotSpacing,
				ColumnCount).SizeSquared2D();
			return !FMath::IsNearlyEqual(LhsDistance, RhsDistance)
				? LhsDistance < RhsDistance
				: Lhs < Rhs;
		});
		AvailableSlots.SetNum(ValidMembers.Num(), EAllowShrinking::No);

		TArray<int32> AssignedSlotEntryByMember;
		SolveMinimumCostAssignment(
			ValidMembers.Num(),
			[&Formation, &ValidMembers, &AvailableSlots, &Soldiers, &SoldierIndexById, SlotSpacing, ColumnCount](
				const int32 MemberIndex,
				const int32 SlotEntryIndex)
			{
				const int32* SoldierIndex = SoldierIndexById.Find(ValidMembers[MemberIndex].Value);
				if (!SoldierIndex)
				{
					return TNumericLimits<double>::Max() * 0.25;
				}
				const FVector SlotLocation = Formation.GuideAnchor
					+ FRotator(0.0f, Formation.TravelFacingYawDegrees, 0.0f).RotateVector(
						MakeFormationSlotOffset(
							AvailableSlots[SlotEntryIndex],
							SlotSpacing,
							ColumnCount));
				return FVector::DistSquared2D(Soldiers[*SoldierIndex].Location, SlotLocation);
			},
			AssignedSlotEntryByMember);
		for (int32 MemberIndex = 0; MemberIndex < ValidMembers.Num(); ++MemberIndex)
		{
			const int32 SlotEntryIndex = AssignedSlotEntryByMember[MemberIndex];
			if (AvailableSlots.IsValidIndex(SlotEntryIndex))
			{
				Formation.SlotBySoldierId.Add(
					ValidMembers[MemberIndex].Value,
					static_cast<uint8>(AvailableSlots[SlotEntryIndex]));
			}
		}
	}

	FIntPoint MakeFlowFieldTileCoordinate(const FVector& WorldLocation)
	{
		const double TileWorldSize = static_cast<double>(FGuLiLocalFlowField::GridSize)
			* FGuLiLocalFlowField::DefaultCellSizeCentimeters;
		return FIntPoint(
			FMath::FloorToInt(WorldLocation.X / TileWorldSize),
			FMath::FloorToInt(WorldLocation.Y / TileWorldSize));
	}

	double DistanceSquaredToSegment2D(
		const FVector& Point,
		const FVector& SegmentStart,
		const FVector& SegmentEnd)
	{
		const FVector2D Point2D(Point.X, Point.Y);
		const FVector2D Start2D(SegmentStart.X, SegmentStart.Y);
		const FVector2D Segment = FVector2D(SegmentEnd.X, SegmentEnd.Y) - Start2D;
		const double SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::DistSquared(Point2D, Start2D);
		}
		const double Alpha = FMath::Clamp(
			FVector2D::DotProduct(Point2D - Start2D, Segment) / SegmentLengthSquared,
			0.0,
			1.0);
		return FVector2D::DistSquared(Point2D, Start2D + Segment * Alpha);
	}

	bool IsInsideFormationCorridor(
		const FVector& Point,
		const FOrderFormationRuntime& Formation,
		const float CorridorHalfWidthCentimeters)
	{
		if (Formation.PathPoints.Num() < 2)
		{
			return FVector::DistSquared2D(Point, Formation.TargetAnchor)
				<= FMath::Square(CorridorHalfWidthCentimeters);
		}
		const double MaximumDistanceSquared = FMath::Square(
			static_cast<double>(CorridorHalfWidthCentimeters));
		for (int32 PointIndex = 1; PointIndex < Formation.PathPoints.Num(); ++PointIndex)
		{
			if (DistanceSquaredToSegment2D(
				Point,
				Formation.PathPoints[PointIndex - 1],
				Formation.PathPoints[PointIndex]) <= MaximumDistanceSquared)
			{
				return true;
			}
		}
		return false;
	}

	// 构造引导点所在的局部瓦片；目标不在瓦片内时，取朝目标方向与瓦片内边界的交点。
	void PrepareFlowFieldBuild(
		FOrderFormationRuntime& Formation,
		const uint32 NavigationGeneration)
	{
		const FIntPoint TileCoordinate = MakeFlowFieldTileCoordinate(Formation.GuideAnchor);
		const float CellSize = FGuLiLocalFlowField::DefaultCellSizeCentimeters;
		const float TileWorldSize = CellSize * FGuLiLocalFlowField::GridSize;
		Formation.PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
		Formation.PendingFlowBuildData.Key.OrderId = Formation.BatchOrderId;
		Formation.PendingFlowBuildData.Key.NavigationGeneration = NavigationGeneration;
		Formation.PendingFlowBuildData.Key.PathRevision = Formation.PathRevision;
		Formation.PendingFlowBuildData.Key.PathPointIndex = Formation.PathPointIndex;
		Formation.PendingFlowBuildData.Key.TileCoordinate = TileCoordinate;
		Formation.PendingFlowBuildData.WorldMin = FVector2D(
			static_cast<double>(TileCoordinate.X) * TileWorldSize,
			static_cast<double>(TileCoordinate.Y) * TileWorldSize);
		Formation.PendingFlowBuildData.CellSizeCentimeters = CellSize;

		FVector RequestedGoal = Formation.TargetAnchor;
		if (Formation.PathPoints.IsValidIndex(Formation.PathPointIndex))
		{
			RequestedGoal = Formation.PathPoints[Formation.PathPointIndex];
		}
		const FVector2D InnerMinimum = Formation.PendingFlowBuildData.WorldMin
			+ FVector2D(CellSize * 0.5f, CellSize * 0.5f);
		const FVector2D InnerMaximum = Formation.PendingFlowBuildData.WorldMin
			+ FVector2D(TileWorldSize - CellSize * 0.5f, TileWorldSize - CellSize * 0.5f);
		const FVector2D StartInsideTile(
			FMath::Clamp(static_cast<double>(Formation.GuideAnchor.X), InnerMinimum.X, InnerMaximum.X),
			FMath::Clamp(static_cast<double>(Formation.GuideAnchor.Y), InnerMinimum.Y, InnerMaximum.Y));
		const FVector2D Goal2D(RequestedGoal.X, RequestedGoal.Y);
		const FVector2D GoalDelta = Goal2D - StartInsideTile;
		double ExitAlpha = 1.0;
		if (GoalDelta.X > UE_DOUBLE_SMALL_NUMBER)
		{
			ExitAlpha = FMath::Min(ExitAlpha, (InnerMaximum.X - StartInsideTile.X) / GoalDelta.X);
		}
		else if (GoalDelta.X < -UE_DOUBLE_SMALL_NUMBER)
		{
			ExitAlpha = FMath::Min(ExitAlpha, (InnerMinimum.X - StartInsideTile.X) / GoalDelta.X);
		}
		if (GoalDelta.Y > UE_DOUBLE_SMALL_NUMBER)
		{
			ExitAlpha = FMath::Min(ExitAlpha, (InnerMaximum.Y - StartInsideTile.Y) / GoalDelta.Y);
		}
		else if (GoalDelta.Y < -UE_DOUBLE_SMALL_NUMBER)
		{
			ExitAlpha = FMath::Min(ExitAlpha, (InnerMinimum.Y - StartInsideTile.Y) / GoalDelta.Y);
		}
		const FVector2D ClampedGoal = StartInsideTile
			+ GoalDelta * FMath::Clamp(ExitAlpha, 0.0, 1.0);
		Formation.PendingFlowBuildData.RequestedGoalCell = FIntPoint(
			FMath::Clamp(
				FMath::FloorToInt(
					(ClampedGoal.X - Formation.PendingFlowBuildData.WorldMin.X) / CellSize),
				0,
				FGuLiLocalFlowField::GridSize - 1),
			FMath::Clamp(
				FMath::FloorToInt(
					(ClampedGoal.Y - Formation.PendingFlowBuildData.WorldMin.Y) / CellSize),
				0,
				FGuLiLocalFlowField::GridSize - 1));
		Formation.PendingFlowBuildData.Walkable.Init(false, FGuLiLocalFlowField::CellCount);
		Formation.PendingFlowBuildData.TraversalCosts.Reset();
		Formation.NextFlowWalkabilitySample = 0;
	}

	bool AreSelectionsEqual(
		const FGuLiCommanderSelectionState& Lhs,
		const FGuLiCommanderSelectionState& Rhs)
	{
		if (Lhs.Cohorts.Num() != Rhs.Cohorts.Num() || Lhs.ActorIds != Rhs.ActorIds)
		{
			return false;
		}
		for (int32 CohortIndex = 0; CohortIndex < Lhs.Cohorts.Num(); ++CohortIndex)
		{
			const FGuLiControlCohortDescriptor& LhsCohort = Lhs.Cohorts[CohortIndex];
			const FGuLiControlCohortDescriptor& RhsCohort = Rhs.Cohorts[CohortIndex];
			if (LhsCohort.CohortId != RhsCohort.CohortId
				|| LhsCohort.MemberIds != RhsCohort.MemberIds
				|| LhsCohort.AliveCount != RhsCohort.AliveCount
				|| LhsCohort.ActiveOrderId != RhsCohort.ActiveOrderId)
			{
				return false;
			}
		}
		return true;
	}
}

// PImpl：头文件只暴露不完整类型，实体管理器、索引、路径与异步任务留在实现文件中。
// 自定义 Deleter 在此处看到完整定义后再 delete，避免生成代码对不完整类型执行删除。
struct FGuLiBattleAuthorityState
{
	TWeakObjectPtr<UMassEntitySubsystem> MassEntitySubsystem;
	FMassArchetypeHandle AuthorityArchetype;
	FMassArchetypeHandle RuntimeTuningEvenBaseArchetype;
	FMassArchetypeHandle RuntimeTuningOddBaseArchetype;
	TArray<GuLiCommanderMassPrivate::FSoldierRuntime> Soldiers;
	TMap<uint32, int32> SoldierIndexById;
	TArray<GuLiCommanderMassPrivate::FOrderFormationRuntime> OrderFormations;
	TArray<TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>> MovePlanningJobs;
	TUniquePtr<GuLiCommanderMassPrivate::FNavigationRepairJob> NavigationRepairJob;
	TMap<FIntPoint, TArray<int32>> SpatialGrid;
	GuLiCommanderNavigationPolicy::FManualAvoidanceSpatialGrid ManualAvoidanceSpatialGrid;
	TArray<GuLiCommanderNavigationPolicy::FManualAvoidanceAgent> ManualAvoidanceAgents;
	TArray<FVector> CachedManualAvoidanceVelocities;
	FGuLiCombatExecutorRegistry CombatExecutors;
	TArray<FGuLiCombatSample> CombatSamples;
	TArray<FGuLiCombatSample> CombatChannels;
	TArray<FGuLiCombatDamageEvent> PendingDamage;
	uint32 CommittedCombatRevision = 0;
	TMap<FObjectKey, GuLiCommanderMassPrivate::FRequestGate> RequestGates;
	double FixedStepAccumulator = 0.0;
	uint64 DroppedFixedStepCount = 0u;
	double SimulationSeconds = 0.0;
	uint32 ServerSimTick = 0u;
	uint32 NextSoldierId = 1u;
	uint32 NextControlCohortId = 1u;
	uint32 NextBatchOrderId = 1u;
	uint32 NextFormationId = 1u;
	uint32 NextPoseFrameSequence = 1u;
	uint32 AuthorityEpoch = 1u;
	uint32 NavigationGeneration = 1u;
	uint64 SurfaceMoveCalls = 0u;
	uint64 SurfaceMoveFailures = 0u;
	uint64 MovementUpdateCalls = 0u;
	uint64 ForcedMovementUpdateCalls = 0u;
	uint64 ManualAvoidanceRefreshes = 0u;
	uint64 ManualAvoidanceCandidatePairs = 0u;
	uint64 ManualAvoidanceOverlapPairs = 0u;
	int32 MaximumManualAvoidanceBucketOccupancy = 0;
	uint64 PathQueries = 0u;
	uint64 PersonalPathQueries = 0u;
	uint64 MoveCandidateProjectionQueries = 0u;
	uint64 MovePlanningPathQueries = 0u;
	uint64 PartiallyAcceptedMoveCommands = 0u;
	uint64 MovePlanningFailureCounts[static_cast<uint8>(EGuLiMovePlanFailureStage::Count)] = {};
	double LastDestinationPlanningMilliseconds = 0.0;
	double MaximumDestinationPlanningMilliseconds = 0.0;
	FGuLiMovePlanningDebug LastMovePlanningDebug;
	bool bHasLastMovePlanningDebug = false;
	bool bPopulationSpawned = false;
	bool bLoggedNavigationFallback = false;
	bool bLoggedSpawnValidationFailure = false;
	bool bUsingRuntimeTuningEvenArchetype = true;
	bool bForceManualAvoidanceRefresh = false;
};

void FGuLiBattleAuthorityStateDeleter::operator()(FGuLiBattleAuthorityState* State) const
{
	delete State;
}

UGuLiBattleAuthoritySubsystem::UGuLiBattleAuthoritySubsystem() = default;
UGuLiBattleAuthoritySubsystem::~UGuLiBattleAuthoritySubsystem() = default;

// 仅在游戏世界的服务器或单机创建；普通客户端不运行第二套权威模拟。
bool UGuLiBattleAuthoritySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& World != nullptr
		&& World->IsGameWorld()
		&& World->GetNetMode() != NM_Client;
}

// 先初始化 Mass 与运行时调参依赖，再建立本世界独占的权威记录。
void UGuLiBattleAuthoritySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMassEntitySubsystem>();
	Collection.InitializeDependency<UGuLiRuntimeTuningSubsystem>();
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	Collection.InitializeDependency<UGuLiArmySkillSubsystem>();
	Collection.InitializeDependency<UGuLiCommanderResourceAdapter>();
	Collection.InitializeDependency<UGuLiDynamicObstacleRegistrySubsystem>();
	AuthorityState.Reset(new FGuLiBattleAuthorityState());
	if (UWorld* World = GetWorld())
	{
		AuthorityState->MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (const UGuLiRuntimeTuningSubsystem* RuntimeTuning = World->GetSubsystem<UGuLiRuntimeTuningSubsystem>())
		{
			BaselineRuntimeTuning = RuntimeTuning->GetBaselineSoldierValues();
			EffectiveRuntimeTuning = RuntimeTuning->GetEffectiveSoldierValues();
			MovementSpeedCentimetersPerSecond = EffectiveRuntimeTuning.MovementSpeedCmPerSecond;
		}
		UGuLiDynamicObstacleRegistrySubsystem* Obstacles =
			World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
		check(Obstacles);
		Obstacles->OnObstaclesChanged().AddUObject(
			this, &ThisClass::HandleDynamicObstaclesChanged);
	}
}

void UGuLiBattleAuthoritySubsystem::Deinitialize()
{
	UGuLiDynamicObstacleRegistrySubsystem* Obstacles =
		GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	check(Obstacles);
	Obstacles->OnObstaclesChanged().RemoveAll(this);
	DestroyAuthorityPopulation();
	AuthorityState.Reset();
	Super::Deinitialize();
}

void UGuLiBattleAuthoritySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
	{
		NavigationSystem->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(
			this,
			&UGuLiBattleAuthoritySubsystem::HandleNavigationGenerationFinished);
	}
	TrySpawnAuthorityPopulation();
}

void UGuLiBattleAuthoritySubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
	{
		NavigationSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(
			this,
			&UGuLiBattleAuthoritySubsystem::HandleNavigationGenerationFinished);
	}
	DestroyAuthorityPopulation();
	Super::OnWorldEndPlay(InWorld);
}

void UGuLiBattleAuthoritySubsystem::Tick(const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT(GuLiCommanderAuthority, WorldTick);
	if (!bSoldierSimulationEnabled || !AuthorityState || !IsAuthorityWorld())
	{
		return;
	}
	if (!AuthorityState->bPopulationSpawned && !TrySpawnAuthorityPopulation())
	{
		return;
	}
	// 可走性采样按世界帧分配预算，放在固定步循环外，避免补帧时成倍增加 NavMesh 查询。
	int32 RemainingProjectionBudget = GuLiCommanderMassPrivate::MoveCandidateProjectionBudgetPerFrame;
	int32 RemainingPathBudget = GuLiCommanderMassPrivate::MovePathQueryBudgetPerFrame;
	TickNavigationRepairs(RemainingProjectionBudget, RemainingPathBudget);
	TickLocalFlowFields();
	TickMovePlanning(RemainingProjectionBudget, RemainingPathBudget);

	// 负 DeltaTime 按 0 处理；每次消耗 1/30 秒，累计上限把本帧模拟工作限制在最多 4 步。
	const double UnclampedAccumulator = AuthorityState->FixedStepAccumulator
		+ static_cast<double>(FMath::Max(0.0f, DeltaTime));
	const double MaximumAccumulator =
		static_cast<double>(GuLiCommanderMassPrivate::MaxAccumulatedSeconds);
	if (UnclampedAccumulator > MaximumAccumulator)
	{
		const uint64 DroppedSteps = static_cast<uint64>(FMath::Max<int64>(1,
			FMath::CeilToInt64((UnclampedAccumulator - MaximumAccumulator)
				/ GuLiCommanderMassPrivate::FixedStepSeconds)));
		AuthorityState->DroppedFixedStepCount += DroppedSteps;
		FGuLiWingmanQAInvariantRegistry::Add(TEXT("COMMANDER_FIXED_STEP_DROP"),
			static_cast<int64>(DroppedSteps));
	}
	AuthorityState->FixedStepAccumulator = FMath::Min(UnclampedAccumulator, MaximumAccumulator);
	while (AuthorityState->FixedStepAccumulator >= GuLiCommanderMassPrivate::FixedStepSeconds)
	{
		TickAuthority(GuLiCommanderMassPrivate::FixedStepSeconds);
		AuthorityState->FixedStepAccumulator -= GuLiCommanderMassPrivate::FixedStepSeconds;
	}
}

TStatId UGuLiBattleAuthoritySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiBattleAuthoritySubsystem, STATGROUP_Tickables);
}

bool UGuLiBattleAuthoritySubsystem::IsAuthorityWorld() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetNetMode() != NM_Client;
}

void UGuLiBattleAuthoritySubsystem::SetSoldierSimulationEnabled(const bool bEnabled)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || bSoldierSimulationEnabled == bEnabled)
	{
		return;
	}
	bSoldierSimulationEnabled = bEnabled;
	if (bEnabled)
	{
		// WorldSubsystem 的 BeginPlay 可以先于组件；启用时再尝试生成，导航未就绪则按原 Tick 路径重试。
		TrySpawnAuthorityPopulation();
	}
	else
	{
		DestroyAuthorityPopulation();
	}
}

// 导航或世界尚未准备好时由 Tick 重试；关卡部署点优先，无部署点时保留默认500人布局。
bool UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation()
{
	using namespace GuLiCommanderMassPrivate;
	if (!bSoldierSimulationEnabled || !AuthorityState || AuthorityState->bPopulationSpawned || !IsAuthorityWorld())
	{
		return AuthorityState && AuthorityState->bPopulationSpawned;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	const UGuLiCommanderDataSubsystem* SoldierData = World ? World->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	if (!World || !World->HasBegunPlay() || !MassSubsystem || !SoldierData)
	{
		return false;
	}
	const UGuLiCommanderResourceAdapter* ResourceAdapter =
		World->GetSubsystem<UGuLiCommanderResourceAdapter>();
	check(ResourceAdapter);
	if (!ResourceAdapter->IsCommandRuntimeReady()
		|| UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
	{
		return false;
	}
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!NavigationSystem || !CommanderNavigationData)
	{
		return false;
	}

	// Validate the complete deployment before creating a single Mass entity. A failed slot must
	// never fall back to its requested world position because that can start an agent off NavMesh.
	struct FValidatedSpawnSlot
	{
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		int32 SlotIndex = 0;
		float FacingYawDegrees = 0.0f;
		const FGuLiSoldierDefinition* Definition = nullptr;
		FNavLocation NavigationLocation;
	};

	TArray<AGuLiCommanderDeploymentPoint*> Deployments;
	int32 InitialSoldierCount = 0;
	for (TActorIterator<AGuLiCommanderDeploymentPoint> It(World); It; ++It)
	{
		Deployments.Add(*It);
		InitialSoldierCount += It->Rows * It->Columns;
	}
	Deployments.Sort([](const AGuLiCommanderDeploymentPoint& A, const AGuLiCommanderDeploymentPoint& B)
	{
		return A.GetFName().LexicalLess(B.GetFName());
	});
	if (Deployments.IsEmpty()) InitialSoldierCount = TotalSoldierCount;
	const TArray<FGuLiSoldierDefinition>& Definitions = SoldierData->GetSoldierDefinitions();
	TArray<FValidatedSpawnSlot> ValidatedSpawnSlots;
	ValidatedSpawnSlots.Reserve(InitialSoldierCount);
	bool bSpawnValidationSucceeded = true;
	FString SpawnValidationFailure;
	for (AGuLiCommanderDeploymentPoint* Deployment : Deployments)
	{
		const FGuLiSoldierDefinition* Definition = SoldierData->FindSoldierDefinition(Deployment->UnitTypeId);
		if (!Definition)
		{
			bSpawnValidationSucceeded = false;
			SpawnValidationFailure = FString::Printf(TEXT("deployment=%s unknown unit type=%d"),
				*Deployment->GetName(), Deployment->UnitTypeId);
			break;
		}
		for (int32 SlotIndex = 0; SlotIndex < Deployment->Rows * Deployment->Columns; ++SlotIndex)
		{
			const FVector RequestedLocation = Deployment->GetSlotLocation(SlotIndex);
			FNavLocation ProjectedLocation;
			if (!ProjectPointToCommanderNavigation(*NavigationSystem, *CommanderNavigationData,
				RequestedLocation, FVector(DestinationMaximumProjectionCorrectionCentimeters,
					DestinationMaximumProjectionCorrectionCentimeters, SpawnProjectionVerticalExtentCentimeters),
				ProjectedLocation)
				|| FVector::Dist2D(RequestedLocation, ProjectedLocation.Location)
					> DestinationMaximumProjectionCorrectionCentimeters)
			{
				bSpawnValidationSucceeded = false;
				SpawnValidationFailure = FString::Printf(TEXT("deployment=%s slot=%d requested=%s is off navigation"),
					*Deployment->GetName(), SlotIndex, *RequestedLocation.ToCompactString());
				break;
			}
			FValidatedSpawnSlot& Slot = ValidatedSpawnSlots.AddDefaulted_GetRef();
			Slot.Team = Deployment->Team;
			Slot.SlotIndex = SlotIndex;
			Slot.FacingYawDegrees = Deployment->GetActorRotation().Yaw;
			Slot.Definition = Definition;
			Slot.NavigationLocation = ProjectedLocation;
		}
		if (!bSpawnValidationSucceeded) break;
	}
	for (int32 TeamIndex = 0; Deployments.IsEmpty() && TeamIndex < TeamCount && bSpawnValidationSucceeded; ++TeamIndex)
	{
		const EGuLiTeam Team = TeamIndex == 0 ? EGuLiTeam::Red : EGuLiTeam::Blue;
		const FVector TeamCenter = Team == EGuLiTeam::Red ? RedSpawnCenter : BlueSpawnCenter;
		const FVector OpposingCenter = Team == EGuLiTeam::Red ? BlueSpawnCenter : RedSpawnCenter;
		for (int32 FormationIndex = 0;
			FormationIndex < SpawnFormationsPerTeam && bSpawnValidationSucceeded;
			++FormationIndex)
		{
			const FVector FormationAnchor = TeamCenter
				+ MakeSpawnFormationOffset(FormationIndex, GroupSpacingCentimeters);
			const float FacingYawDegrees = (OpposingCenter - FormationAnchor)
				.GetSafeNormal2D().Rotation().Yaw;
			for (int32 SlotIndex = 0; SlotIndex < SoldierCountPerFormation; ++SlotIndex)
			{
				const FVector RequestedLocation = FormationAnchor
					+ FRotator(0.0f, FacingYawDegrees, 0.0f).RotateVector(
						MakeFormationSlotOffset(SlotIndex, MemberSpacingCentimeters));
				FNavLocation ProjectedLocation;
				const bool bProjected = ProjectPointToCommanderNavigation(
						*NavigationSystem,
						*CommanderNavigationData,
						RequestedLocation,
						FVector(
							DestinationMaximumProjectionCorrectionCentimeters,
							DestinationMaximumProjectionCorrectionCentimeters,
							SpawnProjectionVerticalExtentCentimeters),
						ProjectedLocation);
				const float ProjectionCorrection = bProjected
					? FVector::Dist2D(RequestedLocation, ProjectedLocation.Location)
					: TNumericLimits<float>::Max();
				if (!bProjected
					|| ProjectionCorrection > DestinationMaximumProjectionCorrectionCentimeters)
				{
					bSpawnValidationSucceeded = false;
					SpawnValidationFailure = FString::Printf(
						TEXT("team=%u formation=%d slot=%d requested=%s projected=%d correction=%.1fcm"),
						static_cast<uint8>(Team),
						FormationIndex,
						SlotIndex,
						*RequestedLocation.ToCompactString(),
						bProjected ? 1 : 0,
						ProjectionCorrection);
					break;
				}
				FValidatedSpawnSlot& Slot = ValidatedSpawnSlots.AddDefaulted_GetRef();
				Slot.Team = Team;
				Slot.SlotIndex = SlotIndex;
				Slot.FacingYawDegrees = FacingYawDegrees;
				Slot.Definition = Definitions.IsEmpty() ? &SoldierData->GetDefaultSoldierDefinition()
					: &Definitions[FormationIndex % FMath::Min(2, Definitions.Num())];
				Slot.NavigationLocation = ProjectedLocation;
			}
		}
	}
	if (bSpawnValidationSucceeded)
	{
		const float MinimumDistanceSquared = FMath::Square(DestinationMinimumSeparationCentimeters);
		for (int32 Left = 0; Left < ValidatedSpawnSlots.Num() && bSpawnValidationSucceeded; ++Left)
		{
			for (int32 Right = Left + 1; Right < ValidatedSpawnSlots.Num(); ++Right)
			{
				if (FVector::DistSquared2D(
						ValidatedSpawnSlots[Left].NavigationLocation.Location,
						ValidatedSpawnSlots[Right].NavigationLocation.Location)
					< MinimumDistanceSquared)
				{
					bSpawnValidationSucceeded = false;
					SpawnValidationFailure = FString::Printf(
						TEXT("slots=%d/%d distance=%.1fcm left=%s right=%s"),
						Left,
						Right,
						FVector::Dist2D(
							ValidatedSpawnSlots[Left].NavigationLocation.Location,
							ValidatedSpawnSlots[Right].NavigationLocation.Location),
						*ValidatedSpawnSlots[Left].NavigationLocation.Location.ToCompactString(),
						*ValidatedSpawnSlots[Right].NavigationLocation.Location.ToCompactString());
					break;
				}
			}
		}
	}
	if (!bSpawnValidationSucceeded || ValidatedSpawnSlots.Num() != InitialSoldierCount)
	{
		if (!AuthorityState->bLoggedSpawnValidationFailure)
		{
			UE_LOG(
				LogGuLiCommanderMass,
				Error,
				TEXT("Deferred complete Soldier population: deployment validation produced %d/%d unique CommanderSoldier slots with <=%.0fcm correction and >=%.0fcm separation; reason=%s."),
				ValidatedSpawnSlots.Num(),
				InitialSoldierCount,
				DestinationMaximumProjectionCorrectionCentimeters,
				DestinationMinimumSeparationCentimeters,
				SpawnValidationFailure.IsEmpty() ? TEXT("count mismatch") : *SpawnValidationFailure);
			AuthorityState->bLoggedSpawnValidationFailure = true;
		}
		return false;
	}
	AuthorityState->bLoggedSpawnValidationFailure = false;
	int32 InitialRed = 0, InitialBlue = 0;
	ValidatedSpawnSlots.RemoveAll([&](const FValidatedSpawnSlot& Slot)
	{
		int32& Count = Slot.Team == EGuLiTeam::Red ? InitialRed : InitialBlue;
		return Count++ >= GetTeamUnitCap();
	});
	InitialSoldierCount = ValidatedSpawnSlots.Num();

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	TArray<const UScriptStruct*> FragmentAndTagTypes = {
		FTransformFragment::StaticStruct(),
		FAgentRadiusFragment::StaticStruct(),
		FMassVelocityFragment::StaticStruct(),
		FMassForceFragment::StaticStruct(),
		FMassMoveTargetFragment::StaticStruct(),
		FMassNavigationObstacleGridCellLocationFragment::StaticStruct(),
		FGuLiMassIdentityFragment::StaticStruct(),
		FGuLiMassHealthFragment::StaticStruct(),
		FGuLiMassSoldierStatsFragment::StaticStruct(),
		FGuLiMassOrderFragment::StaticStruct(),
		FGuLiMassSlotTargetFragment::StaticStruct(),
		FGuLiMassAvoidanceOutputFragment::StaticStruct(),
		FGuLiMassAvoidanceStateFragment::StaticStruct(),
		FGuLiMassAvoidanceParticipantTag::StaticStruct(),
		FGuLiServerAuthorityMassTag::StaticStruct(),
		FGuLiMassRuntimeTuningEvenTag::StaticStruct()
	};

	FMassArchetypeCreationParams ArchetypeParams;
	ArchetypeParams.DebugName = TEXT("GuLiServerAuthority500DynamicSoldiers");
	const FMassArchetypeHandle BaseAuthorityArchetype = EntityManager.CreateArchetype(FragmentAndTagTypes, ArchetypeParams);
	if (!BaseAuthorityArchetype.IsValid())
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Failed to create Soldier authority archetype."));
		return false;
	}
	// 预建仅 Even/Odd 标签不同的两套基础组合，供后续替换只读共享参数时交替迁移。
	FragmentAndTagTypes.RemoveSingle(FGuLiMassRuntimeTuningEvenTag::StaticStruct());
	FragmentAndTagTypes.Add(FGuLiMassRuntimeTuningOddTag::StaticStruct());
	FMassArchetypeCreationParams OddArchetypeParams;
	OddArchetypeParams.DebugName = TEXT("GuLiServerAuthority500DynamicSoldiers_RuntimeTuningOdd");
	const FMassArchetypeHandle OddBaseAuthorityArchetype = EntityManager.CreateArchetype(FragmentAndTagTypes, OddArchetypeParams);
	if (!OddBaseAuthorityArchetype.IsValid())
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Failed to create alternate Soldier tuning archetype."));
		return false;
	}

	AuthorityState->RuntimeTuningEvenBaseArchetype = BaseAuthorityArchetype;
	AuthorityState->RuntimeTuningOddBaseArchetype = OddBaseAuthorityArchetype;
	AuthorityState->bUsingRuntimeTuningEvenArchetype = true;
	FMassArchetypeSharedFragmentValues SharedValues = MakeAuthoritySharedFragmentValues(
		EntityManager,
		MovementSpeedCentimetersPerSecond,
		MemberAgentRadiusCentimeters);
	AuthorityState->AuthorityArchetype = EntityManager.GetOrCreateSuitableArchetype(
		BaseAuthorityArchetype,
		SharedValues.GetSharedFragmentBitSet(),
		SharedValues.GetConstSharedFragmentBitSet());

	TArray<FMassEntityHandle> EntityHandles;
	EntityHandles.Reserve(InitialSoldierCount);
	// 保持创建上下文存活到初始化结束，使创建通知发出时 Fragment 已填好业务数据。
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.BatchCreateEntities(
		AuthorityState->AuthorityArchetype,
		SharedValues,
		InitialSoldierCount,
		EntityHandles);
	if (EntityHandles.Num() != InitialSoldierCount)
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Expected %d Soldiers but Mass created %d."), InitialSoldierCount, EntityHandles.Num());
		EntityManager.BatchDestroyEntities(EntityHandles);
		return false;
	}

	AuthorityState->Soldiers.Reset(InitialSoldierCount);
	AuthorityState->SoldierIndexById.Reset();
	const UGuLiArmySkillSubsystem* Skills = World->GetSubsystem<UGuLiArmySkillSubsystem>();
	int32 EntityIndex = 0;
	int32 NavigationProjectionCount = 0;
	for (const FValidatedSpawnSlot& ValidatedSlot : ValidatedSpawnSlots)
	{
				const EGuLiTeam Team = ValidatedSlot.Team;
				const float FacingYaw = ValidatedSlot.FacingYawDegrees;
				FSoldierRuntime& Soldier = AuthorityState->Soldiers.AddDefaulted_GetRef();
				Soldier.Entity = EntityHandles[EntityIndex++];
				Soldier.SoldierId = FGuLiSoldierId(AllocateNonZero(AuthorityState->NextSoldierId));
				Soldier.Team = Team;
				Soldier.FacingYawDegrees = FacingYaw;
				InitializeSoldierCombat(Soldier, *ValidatedSlot.Definition, EffectiveRuntimeTuning, Skills);
				Soldier.LastValidNavLocation = ValidatedSlot.NavigationLocation;
				Soldier.Location = Soldier.LastValidNavLocation.Location;
				++NavigationProjectionCount;

				const int32 SoldierIndex = AuthorityState->Soldiers.Num() - 1;
				AuthorityState->SoldierIndexById.Add(Soldier.SoldierId.Value, SoldierIndex);

				FTransformFragment& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
				Transform.SetTransform(FTransform(FRotator(0.0f, FacingYaw, 0.0f), Soldier.Location));
				EntityManager.GetFragmentDataChecked<FAgentRadiusFragment>(Soldier.Entity).Radius = MemberAgentRadiusCentimeters;
				EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
				EntityManager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
				EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity).Value = FVector::ZeroVector;

				FMassMoveTargetFragment& MoveTarget = EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
				MoveTarget.Center = Soldier.Location;
				MoveTarget.Forward = FRotator(0.0f, FacingYaw, 0.0f).Vector();

				FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(Soldier.Entity);
				Identity.SoldierId = Soldier.SoldierId;
				Identity.Team = Team;

				FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
				Health.Health = Soldier.Health;
				Health.bDead = false;
				Health.WreckSecondsRemaining = 0.0f;
				FGuLiMassSoldierStatsFragment& Stats = EntityManager
					.GetFragmentDataChecked<FGuLiMassSoldierStatsFragment>(Soldier.Entity);
				Stats.MaxHealth = Soldier.MaxHealth;
				Stats.Defense = Soldier.Defense;
				EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(Soldier.Entity).WorldTarget = Soldier.Location;
	}
	// The preflight is atomic; this guard catches only an unexpected initialization mismatch.
	if (NavigationProjectionCount != InitialSoldierCount)
	{
		UE_LOG(
			LogGuLiCommanderMass,
			Warning,
			TEXT("Deferred Soldier population: CommanderSoldier NavMesh projected %d/%d deployment points."),
			NavigationProjectionCount,
			InitialSoldierCount);
		EntityManager.BatchDestroyEntities(EntityHandles);
		AuthorityState->Soldiers.Reset();
		AuthorityState->SoldierIndexById.Reset();
		return false;
	}

	AuthorityState->SpatialGrid.Reset();
	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (Soldier.IsAlive())
		{
			AuthorityState->SpatialGrid.FindOrAdd(MakeSpatialCell(Soldier.Location)).Add(SoldierIndex);
		}
	}
	AuthorityState->bPopulationSpawned = true;
	RegisterCombatLedgerTargets();
	UE_LOG(
		LogGuLiCommanderMass,
		Display,
		TEXT("Spawned %d independent server-authoritative Mass Soldiers (%d CommanderSoldier NavMesh projections); no permanent 25-Soldier groups."),
		AuthorityState->Soldiers.Num(),
		NavigationProjectionCount);
	return true;
}

// 生命周期收尾：世界仍处于 BeginPlay 时显式销毁有效实体，随后清空本地运行时记录。
void UGuLiBattleAuthoritySubsystem::DestroyAuthorityPopulation()
{
	if (!AuthorityState || !AuthorityState->bPopulationSpawned)
	{
		return;
	}

	UnregisterCombatLedgerTargets();
	if (UWorld* World = GetWorld(); World && World->HasBegunPlay())
	{
		if (UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get())
		{
			FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
			TArray<FMassEntityHandle> Entities;
			Entities.Reserve(AuthorityState->Soldiers.Num());
			for (const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
			{
				if (EntityManager.IsEntityValid(Soldier.Entity))
				{
					Entities.Add(Soldier.Entity);
				}
			}
			if (!Entities.IsEmpty())
			{
				EntityManager.BatchDestroyEntities(Entities);
			}
		}
	}

	AuthorityState->Soldiers.Reset();
	AuthorityState->SoldierIndexById.Reset();
	AuthorityState->OrderFormations.Reset();
	AuthorityState->MovePlanningJobs.Reset();
	AuthorityState->NavigationRepairJob.Reset();
	AuthorityState->SpatialGrid.Reset();
	AuthorityState->ManualAvoidanceSpatialGrid.Reset();
	AuthorityState->ManualAvoidanceAgents.Reset();
	AuthorityState->CachedManualAvoidanceVelocities.Reset();
	AuthorityState->RequestGates.Reset();
	AuthorityState->CombatSamples.Reset();
	AuthorityState->CombatChannels.Reset();
	AuthorityState->PendingDamage.Reset();
	AuthorityState->CommittedCombatRevision = 0;
	AuthorityState->SurfaceMoveCalls = 0u;
	AuthorityState->SurfaceMoveFailures = 0u;
	AuthorityState->MovementUpdateCalls = 0u;
	AuthorityState->ForcedMovementUpdateCalls = 0u;
	AuthorityState->ManualAvoidanceRefreshes = 0u;
	AuthorityState->ManualAvoidanceCandidatePairs = 0u;
	AuthorityState->ManualAvoidanceOverlapPairs = 0u;
	AuthorityState->MaximumManualAvoidanceBucketOccupancy = 0;
	AuthorityState->PathQueries = 0u;
	AuthorityState->PersonalPathQueries = 0u;
	AuthorityState->MoveCandidateProjectionQueries = 0u;
	AuthorityState->MovePlanningPathQueries = 0u;
	AuthorityState->PartiallyAcceptedMoveCommands = 0u;
	FMemory::Memzero(AuthorityState->MovePlanningFailureCounts);
	AuthorityState->LastDestinationPlanningMilliseconds = 0.0;
	AuthorityState->MaximumDestinationPlanningMilliseconds = 0.0;
	AuthorityState->LastMovePlanningDebug = FGuLiMovePlanningDebug{};
	AuthorityState->bHasLastMovePlanningDebug = false;
	AuthorityState->AuthorityArchetype = FMassArchetypeHandle();
	AuthorityState->RuntimeTuningEvenBaseArchetype = FMassArchetypeHandle();
	AuthorityState->RuntimeTuningOddBaseArchetype = FMassArchetypeHandle();
	AuthorityState->bUsingRuntimeTuningEvenArchetype = true;
	AuthorityState->bForceManualAvoidanceRefresh = false;
	AuthorityState->bPopulationSpawned = false;
}

void UGuLiBattleAuthoritySubsystem::TickMovePlanning(
	int32& RemainingProjectionBudget,
	int32& RemainingPathBudget)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning);
	using namespace GuLiCommanderMassPrivate;
	using namespace GuLiCommanderDestinationPlanner;
	if (!AuthorityState || !AuthorityState->bPopulationSpawned)
	{
		return;
	}
	// A destroyed owner can never poll its terminal result. Remove both terminal and
	// in-flight work immediately so reconnects do not inherit an unreachable job.
	AuthorityState->MovePlanningJobs.RemoveAll(
		[](const TUniquePtr<FMovePlanningJob>& Job)
		{
			return !Job || !Job->PlayerState.IsValid();
		});

	UWorld* World = GetWorld();
	const AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	const uint32 CurrentAuthorityEpoch = BattleGameState
		? BattleGameState->GetMatchEpoch()
		: 0u;
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	ANavigationData* NavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!World || !NavigationSystem || !NavigationData || CurrentAuthorityEpoch == 0u)
	{
		for (const TUniquePtr<FMovePlanningJob>& Job : AuthorityState->MovePlanningJobs)
		{
			if (Job && Job->Stage != EMovePlanningStage::Completed)
			{
				CompleteMovePlanningJobWithSystemFailure(*Job);
			}
		}
		return;
	}
	const UGuLiCommanderLandscapeQuerySubsystem* LandscapeQuery =
		World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>();

	auto RebuildHardReservations = [this](FMovePlanningJob& Job)
	{
		TSet<uint32> ReleasedIds;
		for (FMoveMemberPlan& Member : Job.Members)
		{
			Member.bStartValid = false;
			Member.bHasDestination = false;
			Member.bAccepted = false;
			Member.LastCandidateIndex = INDEX_NONE;
			if (Member.bEligible)
			{
				ReleasedIds.Add(Member.SoldierId.Value);
				Member.FailureStage = EGuLiMovePlanFailureStage::None;
			}
		}
		Job.HardReservations.Reset();
		Job.HardReservationBuckets.Reset();
		for (const FSoldierRuntime& Soldier : AuthorityState->Soldiers)
		{
			if (!Soldier.CanAct() || Soldier.Team != Job.Team)
			{
				continue;
			}
			const FVector ReservationLocation = Soldier.bHasFinalDestination
				&& Soldier.ActiveOrderId != 0u
				? Soldier.FinalDestination.Location
				: Soldier.Location;
			if (ReleasedIds.Contains(Soldier.SoldierId.Value))
			{
				if (FMoveMemberPlan* Member = Job.Members.FindByPredicate(
					[&Soldier](const FMoveMemberPlan& Candidate)
					{
						return Candidate.SoldierId == Soldier.SoldierId;
					}))
				{
					Member->OldReservation = ReservationLocation;
				}
				continue;
			}
			FMoveDestinationReservation Reservation;
			Reservation.OwnerSoldierId = Soldier.SoldierId.Value;
			Reservation.Location = ReservationLocation;
			AddHardReservationToMoveJob(Job, Reservation);
		}
		Job.PlannerRequest = FRequest{};
		Job.PlannerRequest.TargetAnchor = FVector(Job.Request.Target);
		Job.PlannerRequest.MemberSpacingCentimeters = MemberSpacingCentimeters;
		Job.LegalSlots.Reset();
		Job.LegalSlotBuckets.Reset();
		Job.ProjectedNavByCandidateIndex.Reset();
		Job.CandidateOwnerMemberPlanIndex.Reset();
		Job.RejectedCandidatePairs.Reset();
		Job.RouteTasks.Reset();
		Job.PreparedFormations.Reset();
		Job.NextStartValidationIndex = 0;
		Job.NextCandidateProjectionIndex = 0;
		Job.DesiredLegalSlotCount = 0;
		Job.CandidateProjectionLimit = 0;
		Job.ProjectionExpansionCount = 0;
		Job.bEscalatedToFullCandidatePool = false;
		Job.Debug.ProjectedCandidates = 0;
		Job.Debug.LegalCandidates = 0;
		Job.Debug.DesiredLegalSlots = 0;
		Job.Debug.InitialProjectionLimit = 0;
		Job.Debug.FinalProjectionLimit = 0;
		Job.Debug.ProjectionExpansionCount = 0;
		Job.Debug.bEscalatedToFullCandidatePool = false;
		Job.Stage = EMovePlanningStage::ValidateStarts;
	};

	for (const TUniquePtr<FMovePlanningJob>& JobPointer : AuthorityState->MovePlanningJobs)
	{
		if (!JobPointer || JobPointer->Stage == EMovePlanningStage::Completed
			|| JobPointer->Stage == EMovePlanningStage::ReadyToCommit)
		{
			continue;
		}
		FMovePlanningJob& Job = *JobPointer;
		const AGuLiBattlePlayerState* PlayerState = Job.PlayerState.Get();
		if (!PlayerState || !IsMovePlanningOwnerCurrent(Job, *PlayerState)
			|| !PlayerState->IsCommander() || PlayerState->GetTeam() != Job.Team)
		{
			Job.Ack.Result = EGuLiCommandAckResult::Unauthorized;
			Job.Ack.BatchOrderId = 0u;
			Job.UpdatedSelection = Job.FrozenSelection;
			Job.bSelectionChanged = false;
			Job.Stage = EMovePlanningStage::Completed;
			continue;
		}
		if (Job.AuthorityEpoch != CurrentAuthorityEpoch)
		{
			Job.Ack.Result = EGuLiCommandAckResult::InvalidRequest;
			Job.Ack.BatchOrderId = 0u;
			Job.UpdatedSelection = Job.FrozenSelection;
			Job.bSelectionChanged = false;
			Job.Stage = EMovePlanningStage::Completed;
			continue;
		}
		++Job.Debug.PlanningWorldFrames;
		if (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
		{
			Job.NavigationGeneration = AuthorityState->NavigationGeneration;
			RebuildHardReservations(Job);
		}

		if (Job.Stage == EMovePlanningStage::ValidateStarts)
		{
			while (RemainingProjectionBudget > 0
				&& Job.NextStartValidationIndex < Job.Members.Num())
			{
				FMoveMemberPlan& Member = Job.Members[Job.NextStartValidationIndex++];
				if (!Member.bEligible)
				{
					continue;
				}
				--RemainingProjectionBudget;
				++AuthorityState->MoveCandidateProjectionQueries;
				++Job.Debug.CandidateProjectionQueries;
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
				if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
					continue;
				}
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				FNavLocation ProjectedStart;
				if (!Soldier.CanAct() || Soldier.Team != Job.Team
					|| !ProjectPointToCommanderNavigation(
						*NavigationSystem,
						*NavigationData,
						Soldier.Location,
						FVector(10.0f, 10.0f, 5000.0f),
						ProjectedStart)
					|| FVector::DistSquared2D(Soldier.Location, ProjectedStart.Location)
						> FMath::Square(10.0f))
				{
					Member.FailureStage = EGuLiMovePlanFailureStage::StartInvalid;
					FMoveDestinationReservation Restored;
					Restored.OwnerSoldierId = Member.SoldierId.Value;
					Restored.Location = Member.OldReservation;
					AddHardReservationToMoveJob(Job, Restored);
					continue;
				}
				ProjectedStart.Location.X = Soldier.Location.X;
				ProjectedStart.Location.Y = Soldier.Location.Y;
				Member.CommandStart = ProjectedStart;
				Member.bStartValid = true;
			}
			if (Job.NextStartValidationIndex >= Job.Members.Num())
			{
				Job.PlannerRequest = FRequest{};
				Job.PlannerRequest.TargetAnchor = FVector(Job.Request.Target);
				Job.PlannerRequest.MemberSpacingCentimeters = MemberSpacingCentimeters;
				for (const FMoveCohortPlan& Cohort : Job.Cohorts)
				{
					FCohortInput CohortInput;
					CohortInput.CohortId = Cohort.CohortId;
					for (const int32 MemberPlanIndex : Cohort.MemberPlanIndices)
					{
						if (!Job.Members.IsValidIndex(MemberPlanIndex)
							|| !Job.Members[MemberPlanIndex].bStartValid)
						{
							continue;
						}
						const FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
						FMemberInput& Input = CohortInput.Members.AddDefaulted_GetRef();
						Input.SoldierId = Member.SoldierId;
						Input.Location = Member.CommandStart.Location;
						const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
						Input.Facing = SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
							? FRotator(0.0f, AuthorityState->Soldiers[*SoldierIndex].FacingYawDegrees, 0.0f).Vector()
							: FVector::ForwardVector;
					}
					if (!CohortInput.Members.IsEmpty())
					{
						Job.PlannerRequest.Cohorts.Add(MoveTemp(CohortInput));
					}
				}
				if (Job.PlannerRequest.Cohorts.IsEmpty())
				{
					Job.Stage = EMovePlanningStage::ReadyToCommit;
				}
				else
				{
					InitializeMoveCandidateProjectionWindow(Job);
					Job.Stage = EMovePlanningStage::ProjectCandidates;
				}
			}
		}

		if (Job.Stage == EMovePlanningStage::ProjectCandidates)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning_ProjectCandidates);
			while (Job.Stage == EMovePlanningStage::ProjectCandidates)
			{
				while (RemainingProjectionBudget > 0
					&& Job.NextCandidateProjectionIndex < Job.CandidateProjectionLimit)
				{
					const FFreeDestinationCandidate& Candidate =
						Job.HexCandidates[Job.NextCandidateProjectionIndex++];
					--RemainingProjectionBudget;
					++AuthorityState->MoveCandidateProjectionQueries;
					++Job.Debug.CandidateProjectionQueries;
					++Job.Debug.ProjectedCandidates;
					FVector Seed = Candidate.WorldCandidate;
					float LandscapeHeight = 0.0f;
					if (!LandscapeQuery
						|| !LandscapeQuery->TryGetLandscapeHeight(
							FVector2D(Seed.X, Seed.Y), LandscapeHeight))
					{
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::CandidateProjection)];
						continue;
					}
					Seed.Z = LandscapeHeight;
					FNavLocation Projected;
					if (!ProjectPointToCommanderNavigation(
							*NavigationSystem,
							*NavigationData,
							Seed,
							FVector(
								DestinationMaximumProjectionCorrectionCentimeters,
								DestinationMaximumProjectionCorrectionCentimeters,
								5000.0f),
							Projected)
						|| FVector::DistSquared2D(Seed, Projected.Location)
							> FMath::Square(DestinationMaximumProjectionCorrectionCentimeters)
						|| FVector::DistSquared2D(FVector(Job.Request.Target), Projected.Location)
							> FMath::Square(FreeDestinationMaximumRadiusCentimeters))
					{
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::CandidateProjection)];
						continue;
					}

					if (IsMoveCandidateBlockedByHardReservation(Job, Projected.Location))
					{
						++Job.Debug.ReservationConflictCount;
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::FriendlyReservation)];
						continue;
					}
					if (IsMoveCandidateTooCloseToLegalSlot(Job, Projected.Location))
					{
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::Separation)];
						continue;
					}
					FFreeDestinationSlot& Slot = Job.LegalSlots.AddDefaulted_GetRef();
					Slot.CandidateIndex = Candidate.CandidateIndex;
					Slot.AxialQ = Candidate.AxialQ;
					Slot.AxialR = Candidate.AxialR;
					Slot.OriginalWorldCandidate = Seed;
					Slot.WorldDestination = Projected.Location;
					Job.ProjectedNavByCandidateIndex.Add(Candidate.CandidateIndex, Projected);
					Job.LegalSlotBuckets.FindOrAdd(
						MakeMoveDestinationBucket(Projected.Location)).Add(Job.LegalSlots.Num() - 1);
				}

				Job.Debug.LegalCandidates = Job.LegalSlots.Num();
				if (Job.NextCandidateProjectionIndex < Job.CandidateProjectionLimit)
				{
					break;
				}
				if (Job.LegalSlots.Num() >= Job.DesiredLegalSlotCount
					|| Job.NextCandidateProjectionIndex >= Job.HexCandidates.Num()
					|| !ExpandMoveCandidateProjectionWindow(Job, false))
				{
					Job.Stage = EMovePlanningStage::AssignDestinations;
					break;
				}
				if (RemainingProjectionBudget <= 0)
				{
					break;
				}
			}
		}

		if (Job.Stage == EMovePlanningStage::AssignDestinations)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning_AssignDestinations);
			FFreeAssignmentPlan Assignment;
			if (!AssignFreeDestinations(
					Job.PlannerRequest,
					Job.LegalSlots,
					DefaultSoftAnchorPitchCentimeters,
					Assignment))
			{
				for (FMoveMemberPlan& Member : Job.Members)
				{
					if (Member.bStartValid)
					{
						Member.FailureStage = EGuLiMovePlanFailureStage::CandidatesExhausted;
					}
				}
				Job.Stage = EMovePlanningStage::ReadyToCommit;
				continue;
			}
			for (const FFreeCohortAssignment& CohortAssignment : Assignment.Cohorts)
			{
				FMoveRouteTask RouteTask;
				RouteTask.CohortId = CohortAssignment.CohortId;
				for (const FFreeMemberAssignment& Assigned : CohortAssignment.Members)
				{
					FMoveMemberPlan* Member = Job.Members.FindByPredicate(
						[&Assigned](const FMoveMemberPlan& Candidate)
						{
							return Candidate.SoldierId == Assigned.SoldierId;
						});
					const FFreeDestinationSlot* Slot = Job.LegalSlots.FindByPredicate(
						[&Assigned](const FFreeDestinationSlot& Candidate)
						{
							return Candidate.CandidateIndex == Assigned.CandidateIndex;
						});
					const FNavLocation* DestinationNav = Slot
						? Job.ProjectedNavByCandidateIndex.Find(Slot->CandidateIndex)
						: nullptr;
					if (!Member || !Slot || !DestinationNav)
					{
						continue;
					}
					const int32 MemberPlanIndex =
						static_cast<int32>(Member - Job.Members.GetData());
					if (!IsMoveCandidateAvailableForMember(Job, *Member, Slot->CandidateIndex))
					{
						continue;
					}
					ClaimMoveCandidate(Job, MemberPlanIndex, *Slot, *DestinationNav);
					RouteTask.MemberPlanIndices.Add(MemberPlanIndex);
				}
				if (!RouteTask.MemberPlanIndices.IsEmpty())
				{
					Job.RouteTasks.Add(MoveTemp(RouteTask));
				}
			}
			for (FMoveMemberPlan& Member : Job.Members)
			{
				if (Member.bStartValid && !Member.bHasDestination)
				{
					Member.FailureStage = EGuLiMovePlanFailureStage::CandidatesExhausted;
				}
			}
			Job.Stage = EMovePlanningStage::Route;
		}

		if (Job.Stage == EMovePlanningStage::Route)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning_Route);
			while (RemainingPathBudget > 0 && !Job.RouteTasks.IsEmpty())
			{
				FMoveRouteTask Task = MoveTemp(Job.RouteTasks[0]);
				Job.RouteTasks.RemoveAt(0, 1, EAllowShrinking::No);
				Task.MemberPlanIndices.RemoveAll([this, &Job](const int32 MemberPlanIndex)
				{
					if (!Job.Members.IsValidIndex(MemberPlanIndex))
					{
						return true;
					}
					FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
					const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
					if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
						|| !AuthorityState->Soldiers[*SoldierIndex].CanAct()
						|| AuthorityState->Soldiers[*SoldierIndex].Team != Job.Team)
					{
						Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
						ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
						return true;
					}
					Member.CommandStart = AuthorityState->Soldiers[*SoldierIndex].LastValidNavLocation;
					return !Member.bHasDestination;
				});
				if (Task.MemberPlanIndices.IsEmpty())
				{
					continue;
				}

				FVector StartCentroid = FVector::ZeroVector;
				FVector DestinationCentroid = FVector::ZeroVector;
				for (const int32 MemberPlanIndex : Task.MemberPlanIndices)
				{
					StartCentroid += Job.Members[MemberPlanIndex].CommandStart.Location;
					DestinationCentroid += Job.Members[MemberPlanIndex].Destination.WorldDestination;
				}
				StartCentroid /= static_cast<double>(Task.MemberPlanIndices.Num());
				DestinationCentroid /= static_cast<double>(Task.MemberPlanIndices.Num());
				const int32 StartMedoidIndex = *Algo::MinElementBy(
					Task.MemberPlanIndices,
					[&Job, &StartCentroid](const int32 Index)
					{
						return FVector::DistSquared2D(
							Job.Members[Index].CommandStart.Location,
							StartCentroid);
					});
				const int32 DestinationMedoidIndex = *Algo::MinElementBy(
					Task.MemberPlanIndices,
					[&Job, &DestinationCentroid](const int32 Index)
					{
						return FVector::DistSquared2D(
							Job.Members[Index].Destination.WorldDestination,
							DestinationCentroid);
					});
				FMoveMemberPlan& StartMedoid = Job.Members[StartMedoidIndex];
				FMoveMemberPlan& DestinationMedoid = Job.Members[DestinationMedoidIndex];
				TArray<FVector> PathPoints;
				--RemainingPathBudget;
				++AuthorityState->PathQueries;
				++AuthorityState->MovePlanningPathQueries;
				++Job.Debug.PathQueries;
				bool bRouteValid = BuildCompletePathQuiet(
					*NavigationSystem,
					*NavigationData,
					StartMedoid.CommandStart,
					DestinationMedoid.Destination.WorldDestination,
					PathPoints);
				EGuLiMovePlanFailureStage RouteFailure = EGuLiMovePlanFailureStage::SharedPath;
				if (bRouteValid)
				{
					for (const int32 MemberPlanIndex : Task.MemberPlanIndices)
					{
						const FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
						if (!HasDirectSurfaceConnection(
								*NavigationData,
								Member.CommandStart,
								PathPoints[0])
							|| !HasDirectSurfaceConnection(
								*NavigationData,
								DestinationMedoid.DestinationNav,
								Member.Destination.WorldDestination))
						{
							bRouteValid = false;
							RouteFailure = EGuLiMovePlanFailureStage::Connector;
							break;
						}
					}
				}

				if (!bRouteValid && Task.MemberPlanIndices.Num() > 1)
				{
					FBox2D DestinationBounds(ForceInit);
					for (const int32 Index : Task.MemberPlanIndices)
					{
						const FVector& Destination = Job.Members[Index].Destination.WorldDestination;
						DestinationBounds += FVector2D(Destination.X, Destination.Y);
					}
					const bool bSplitX = DestinationBounds.GetSize().X >= DestinationBounds.GetSize().Y;
					Task.MemberPlanIndices.Sort([&Job, bSplitX](const int32 Lhs, const int32 Rhs)
					{
						const FVector& A = Job.Members[Lhs].Destination.WorldDestination;
						const FVector& B = Job.Members[Rhs].Destination.WorldDestination;
						const double PrimaryA = bSplitX ? A.X : A.Y;
						const double PrimaryB = bSplitX ? B.X : B.Y;
						if (PrimaryA < PrimaryB)
						{
							return true;
						}
						if (PrimaryB < PrimaryA)
						{
							return false;
						}
						const double SecondaryA = bSplitX ? A.Y : A.X;
						const double SecondaryB = bSplitX ? B.Y : B.X;
						if (SecondaryA < SecondaryB)
						{
							return true;
						}
						if (SecondaryB < SecondaryA)
						{
							return false;
						}
						return Job.Members[Lhs].SoldierId.Value
							< Job.Members[Rhs].SoldierId.Value;
					});
					const int32 Mid = Task.MemberPlanIndices.Num() / 2;
					FMoveRouteTask Left;
					Left.CohortId = Task.CohortId;
					Left.MemberPlanIndices.Append(Task.MemberPlanIndices.GetData(), Mid);
					FMoveRouteTask Right;
					Right.CohortId = Task.CohortId;
					Right.MemberPlanIndices.Append(
						Task.MemberPlanIndices.GetData() + Mid,
						Task.MemberPlanIndices.Num() - Mid);
					Job.RouteTasks.Insert(MoveTemp(Right), 0);
					Job.RouteTasks.Insert(MoveTemp(Left), 0);
					++Job.Debug.RouteSplitCount;
					++Job.Debug.FailureCounts[static_cast<uint8>(RouteFailure)];
					continue;
				}

				if (!bRouteValid)
				{
					const int32 MemberPlanIndex = Task.MemberPlanIndices[0];
					FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
					const int32 FailedCandidateIndex = Member.LastCandidateIndex;
					ReleaseMoveMemberDestination(Job, MemberPlanIndex, true);
					const FFreeDestinationSlot* Fallback = Job.LegalSlots.FindByPredicate(
						[&Job, &Member, FailedCandidateIndex](const FFreeDestinationSlot& Candidate)
						{
							return Candidate.CandidateIndex > FailedCandidateIndex
								&& IsMoveCandidateAvailableForMember(
									Job,
									Member,
									Candidate.CandidateIndex)
								&& Job.ProjectedNavByCandidateIndex.Contains(Candidate.CandidateIndex);
						});
					if (Fallback)
					{
						ClaimMoveCandidate(
							Job,
							MemberPlanIndex,
							*Fallback,
							Job.ProjectedNavByCandidateIndex.FindChecked(Fallback->CandidateIndex));
						Job.RouteTasks.Add(MoveTemp(Task));
					}
					else if (RestartMovePlanningWithCompleteCandidatePool(Job))
					{
						// The first local window was sufficient for assignment but not routing.
						// Preserve projected slots, finish the hard-cap pool, then re-run the
						// uncommitted transaction once with the existing deterministic order.
						break;
					}
					else
					{
						Member.FailureStage = EGuLiMovePlanFailureStage::CandidatesExhausted;
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::PersonalPath)];
					}
					continue;
				}

				FOrderFormationRuntime Formation;
				Formation.SourceCohortId = Task.CohortId;
				Formation.Team = Job.Team;
				Formation.GuideAnchor = PathPoints[0];
				Formation.TargetAnchor = DestinationMedoid.Destination.WorldDestination;
				Formation.PathPoints = MoveTemp(PathPoints);
				Formation.PathPointIndex = Formation.PathPoints.Num() > 1 ? 1 : 0;
				Formation.FinalPathFrame = GuLiCommanderNavigationPolicy::ResolveFinalPathFrame(
					Formation.PathPoints,
					Formation.GuideAnchor,
					Formation.TargetAnchor);
				const FVector InitialTravelDirection =
					Formation.PathPoints[Formation.PathPointIndex] - Formation.GuideAnchor;
				Formation.TravelFacingYawDegrees = InitialTravelDirection.IsNearlyZero()
					? 0.0f
					: InitialTravelDirection.GetSafeNormal2D().Rotation().Yaw;
				const float ArrivalSnapDistance = FMath::Max(
					100.0f,
					MovementSpeedCentimetersPerSecond * FixedStepSeconds);
				for (const int32 MemberPlanIndex : Task.MemberPlanIndices)
				{
					FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
					Member.bAccepted = true;
					Formation.MemberIds.Add(Member.SoldierId);
					Formation.CommandStartNavLocationBySoldierId.Add(
						Member.SoldierId.Value,
						Member.CommandStart);
					Formation.FinalDestinationBySoldierId.Add(
						Member.SoldierId.Value,
						Member.DestinationNav);
					Formation.FinalApproachTriggerRadiusCentimeters = FMath::Max(
						Formation.FinalApproachTriggerRadiusCentimeters,
						FVector::Dist2D(Formation.TargetAnchor, Member.DestinationNav.Location)
							+ ArrivalSnapDistance);
				}
				Job.PreparedFormations.Add(MoveTemp(Formation));
			}
			if (Job.Stage == EMovePlanningStage::Route && Job.RouteTasks.IsEmpty())
			{
				Job.Stage = EMovePlanningStage::ReconcileReservations;
			}
		}

		if (Job.Stage == EMovePlanningStage::ReconcileReservations)
		{
			TArray<FVector> RestoredReservations;
			for (FMoveMemberPlan& Member : Job.Members)
			{
				if (!Member.bEligible)
				{
					continue;
				}
				// Pending units continue their old command, so the reservation captured at
				// request time may no longer describe the endpoint they currently own.
				bool bHasLiveReservation = false;
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(
					Member.SoldierId.Value);
				if (SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
					if (Soldier.CanAct() && Soldier.Team == Job.Team)
					{
						bHasLiveReservation = true;
						Member.OldReservation = Soldier.bHasFinalDestination
							&& Soldier.ActiveOrderId != 0u
							&& Soldier.FinalDestinationNavigationGeneration
								== AuthorityState->NavigationGeneration
							? Soldier.FinalDestination.Location
							: Soldier.LastValidNavLocation.Location;
					}
				}
				if (!Member.bAccepted && bHasLiveReservation)
				{
					RestoredReservations.Add(Member.OldReservation);
				}
			}
			bool bQueuedRepair = false;
			bool bRestoredReservationSetChanged = false;
			bool bRestartedWithCompleteCandidatePool = false;
			for (int32 MemberPlanIndex = 0; MemberPlanIndex < Job.Members.Num(); ++MemberPlanIndex)
			{
				FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
				if (!Member.bAccepted)
				{
					continue;
				}
				const bool bConflicts = RestoredReservations.ContainsByPredicate(
					[&Member](const FVector& Restored)
					{
						return FVector::DistSquared2D(Restored, Member.Destination.WorldDestination)
							< FMath::Square(DestinationMinimumSeparationCentimeters);
					});
				if (!bConflicts)
				{
					continue;
				}
				++Job.Debug.ReservationConflictCount;
				RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
				const int32 ConflictingCandidateIndex = Member.LastCandidateIndex;
				ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
				const FFreeDestinationSlot* Fallback = Job.LegalSlots.FindByPredicate(
					[&Job, &Member, &RestoredReservations, ConflictingCandidateIndex](
						const FFreeDestinationSlot& Candidate)
					{
						return Candidate.CandidateIndex > ConflictingCandidateIndex
							&& IsMoveCandidateAvailableForMember(
								Job,
								Member,
								Candidate.CandidateIndex)
							&& Job.ProjectedNavByCandidateIndex.Contains(Candidate.CandidateIndex)
							&& !RestoredReservations.ContainsByPredicate(
								[&Candidate](const FVector& Restored)
								{
									return FVector::DistSquared2D(Restored, Candidate.WorldDestination)
										< FMath::Square(DestinationMinimumSeparationCentimeters);
								});
					});
				if (!Fallback)
				{
					if (RestartMovePlanningWithCompleteCandidatePool(Job))
					{
						bRestartedWithCompleteCandidatePool = true;
						break;
					}
					Member.FailureStage = EGuLiMovePlanFailureStage::RestoredReservation;
					RestoredReservations.AddUnique(Member.OldReservation);
					bRestoredReservationSetChanged = true;
					continue;
				}
				ClaimMoveCandidate(
					Job,
					MemberPlanIndex,
					*Fallback,
					Job.ProjectedNavByCandidateIndex.FindChecked(Fallback->CandidateIndex));
				FMoveRouteTask Retry;
				Retry.CohortId = Member.CohortId;
				Retry.MemberPlanIndices.Add(MemberPlanIndex);
				Job.RouteTasks.Add(MoveTemp(Retry));
				bQueuedRepair = true;
			}
			if (bRestartedWithCompleteCandidatePool)
			{
				continue;
			}
			Job.PreparedFormations.RemoveAll([](const FOrderFormationRuntime& Formation)
			{
				return Formation.MemberIds.IsEmpty();
			});
			Job.Stage = bQueuedRepair
				? EMovePlanningStage::Route
				: bRestoredReservationSetChanged
					? EMovePlanningStage::ReconcileReservations
					: EMovePlanningStage::ReadyToCommit;
		}
	}
}

void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->bPopulationSpawned)
	{
		return;
	}
	UWorld* World = GetWorld();
	const AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	const uint32 CurrentAuthorityEpoch = BattleGameState
		? BattleGameState->GetMatchEpoch()
		: 0u;
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	ANavigationData* NavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!World || !NavigationSystem || !NavigationData || !MassSubsystem
		|| CurrentAuthorityEpoch == 0u)
	{
		for (const TUniquePtr<FMovePlanningJob>& Job : AuthorityState->MovePlanningJobs)
		{
			if (Job && Job->Stage == EMovePlanningStage::ReadyToCommit)
			{
				CompleteMovePlanningJobWithSystemFailure(*Job);
			}
		}
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

	for (const TUniquePtr<FMovePlanningJob>& JobPointer : AuthorityState->MovePlanningJobs)
	{
		if (!JobPointer || JobPointer->Stage != EMovePlanningStage::ReadyToCommit)
		{
			continue;
		}
		FMovePlanningJob& Job = *JobPointer;
		const AGuLiBattlePlayerState* PlayerState = Job.PlayerState.Get();
		if (!PlayerState || !IsMovePlanningOwnerCurrent(Job, *PlayerState)
			|| !PlayerState->IsCommander() || PlayerState->GetTeam() != Job.Team)
		{
			Job.Ack.Result = EGuLiCommandAckResult::Unauthorized;
			Job.Ack.BatchOrderId = 0u;
			Job.UpdatedSelection = Job.FrozenSelection;
			Job.bSelectionChanged = false;
			Job.Stage = EMovePlanningStage::Completed;
			continue;
		}
		if (Job.AuthorityEpoch != CurrentAuthorityEpoch)
		{
			Job.Ack.Result = EGuLiCommandAckResult::InvalidRequest;
			Job.Ack.BatchOrderId = 0u;
			Job.UpdatedSelection = Job.FrozenSelection;
			Job.bSelectionChanged = false;
			Job.Stage = EMovePlanningStage::Completed;
			continue;
		}
		if (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
		{
			Job.LegalSlots.Reset();
			Job.LegalSlotBuckets.Reset();
			Job.ProjectedNavByCandidateIndex.Reset();
			Job.CandidateOwnerMemberPlanIndex.Reset();
			Job.RejectedCandidatePairs.Reset();
			Job.RouteTasks.Reset();
			Job.PreparedFormations.Reset();
			Job.NextStartValidationIndex = 0;
			Job.NextCandidateProjectionIndex = 0;
			Job.DesiredLegalSlotCount = 0;
			Job.CandidateProjectionLimit = 0;
			Job.ProjectionExpansionCount = 0;
			Job.bEscalatedToFullCandidatePool = false;
			Job.Debug.ProjectedCandidates = 0;
			Job.Debug.LegalCandidates = 0;
			Job.Debug.DesiredLegalSlots = 0;
			Job.Debug.InitialProjectionLimit = 0;
			Job.Debug.FinalProjectionLimit = 0;
			Job.Debug.ProjectionExpansionCount = 0;
			Job.Debug.bEscalatedToFullCandidatePool = false;
			for (FMoveMemberPlan& Member : Job.Members)
			{
				Member.bStartValid = false;
				Member.bHasDestination = false;
				Member.bAccepted = false;
				Member.CommitConnectionRetryCount = 0;
				if (Member.bEligible)
				{
					Member.FailureStage = EGuLiMovePlanFailureStage::None;
				}
			}
			Job.Stage = EMovePlanningStage::ValidateStarts;
			continue;
		}

		bool bNeedsReservationReconcile = false;
		for (FOrderFormationRuntime& Formation : Job.PreparedFormations)
		{
			FMoveRouteTask RetryTask;
			RetryTask.CohortId = Formation.SourceCohortId;
			const TArray<FGuLiSoldierId> MembersToValidate = Formation.MemberIds;
			for (const FGuLiSoldierId SoldierId : MembersToValidate)
			{
				const int32 MemberPlanIndex = Job.Members.IndexOfByPredicate(
					[SoldierId](const FMoveMemberPlan& Candidate)
					{
						return Candidate.SoldierId == SoldierId;
					});
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
				if (!Job.Members.IsValidIndex(MemberPlanIndex) || !SoldierIndex
					|| !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					if (Job.Members.IsValidIndex(MemberPlanIndex))
					{
						FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
						Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
						ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
					}
					RemoveMoveMemberFromPreparedFormations(Job, SoldierId);
					bNeedsReservationReconcile = true;
					continue;
				}
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
				if (!Soldier.CanAct() || Soldier.Team != Job.Team
					|| !EntityManager.IsEntityValid(Soldier.Entity))
				{
					Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
					ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
					RemoveMoveMemberFromPreparedFormations(Job, SoldierId);
					bNeedsReservationReconcile = true;
					continue;
				}
				if (Formation.PathPoints.IsEmpty()
					|| !HasDirectSurfaceConnection(
						*NavigationData,
						Soldier.LastValidNavLocation,
						Formation.PathPoints[0],
						MovementSpeedCentimetersPerSecond * FixedStepSeconds + 250.0f))
				{
					Member.bAccepted = false;
					RemoveMoveMemberFromPreparedFormations(Job, SoldierId);
					if (Member.CommitConnectionRetryCount < MoveCommitConnectionRetryLimit)
					{
						++Member.CommitConnectionRetryCount;
						Member.CommandStart = Soldier.LastValidNavLocation;
						RetryTask.MemberPlanIndices.Add(MemberPlanIndex);
					}
					else
					{
						Member.FailureStage = EGuLiMovePlanFailureStage::Connector;
						ReleaseMoveMemberDestination(Job, MemberPlanIndex, true);
						bNeedsReservationReconcile = true;
					}
					continue;
				}
				Formation.CommandStartNavLocationBySoldierId.Add(
					SoldierId.Value,
					Soldier.LastValidNavLocation);
			}
			if (!RetryTask.MemberPlanIndices.IsEmpty())
			{
				Job.RouteTasks.Add(MoveTemp(RetryTask));
			}
		}
		Job.PreparedFormations.RemoveAll([](const FOrderFormationRuntime& Formation)
		{
			return Formation.MemberIds.IsEmpty();
		});
		if (!Job.RouteTasks.IsEmpty())
		{
			Job.Stage = EMovePlanningStage::Route;
			continue;
		}
		if (bNeedsReservationReconcile)
		{
			Job.Stage = EMovePlanningStage::ReconcileReservations;
			continue;
		}

		// The route pass and fixed-step commit are separated by at least one tick.
		// Clear any member that became invalid before deriving the batch id or masks.
		TSet<uint32> PreparedMemberIds;
		for (const FOrderFormationRuntime& Formation : Job.PreparedFormations)
		{
			for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
			{
				if (Formation.CommandStartNavLocationBySoldierId.Contains(SoldierId.Value)
					&& Formation.FinalDestinationBySoldierId.Contains(SoldierId.Value))
				{
					PreparedMemberIds.Add(SoldierId.Value);
				}
			}
		}
		bool bFinalMembershipChanged = false;
		for (int32 MemberPlanIndex = 0; MemberPlanIndex < Job.Members.Num(); ++MemberPlanIndex)
		{
			FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
			if (!Member.bAccepted)
			{
				continue;
			}
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
			const bool bValidAtCommit = Member.bHasDestination
				&& PreparedMemberIds.Contains(Member.SoldierId.Value)
				&& SoldierIndex
				&& AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
				&& AuthorityState->Soldiers[*SoldierIndex].CanAct()
				&& AuthorityState->Soldiers[*SoldierIndex].Team == Job.Team
				&& EntityManager.IsEntityValid(AuthorityState->Soldiers[*SoldierIndex].Entity);
			if (bValidAtCommit)
			{
				continue;
			}
			Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
			ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
			RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
			bFinalMembershipChanged = true;
		}
		Job.PreparedFormations.RemoveAll([](const FOrderFormationRuntime& Formation)
		{
			return Formation.MemberIds.IsEmpty();
		});
		if (bFinalMembershipChanged)
		{
			Job.Stage = EMovePlanningStage::ReconcileReservations;
			continue;
		}

		int32 AcceptedMembers = 0;
		for (const FMoveMemberPlan& Member : Job.Members)
		{
			AcceptedMembers += Member.bAccepted ? 1 : 0;
		}
		const uint32 BatchOrderId = AcceptedMembers > 0
			? AllocateNonZero(AuthorityState->NextBatchOrderId)
			: 0u;

		for (FOrderFormationRuntime& Formation : Job.PreparedFormations)
		{
			Formation.MemberIds.RemoveAll([this, &Job](const FGuLiSoldierId SoldierId)
			{
				const FMoveMemberPlan* Member = Job.Members.FindByPredicate(
					[SoldierId](const FMoveMemberPlan& Candidate)
					{
						return Candidate.SoldierId == SoldierId;
					});
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
				return !Member || !Member->bAccepted || !SoldierIndex
					|| !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
					|| !AuthorityState->Soldiers[*SoldierIndex].CanAct();
			});
			if (Formation.MemberIds.IsEmpty())
			{
				continue;
			}
			Formation.FormationId = AllocateNonZero(AuthorityState->NextFormationId);
			Formation.BatchOrderId = BatchOrderId;
			Formation.MemberPathPointIndexBySoldierId.Reset();
			for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
			{
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
				FNavLocation* CommandStart = Formation.CommandStartNavLocationBySoldierId.Find(SoldierId.Value);
				const FNavLocation* FinalDestination = Formation.FinalDestinationBySoldierId.Find(SoldierId.Value);
				if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
					|| !CommandStart || !FinalDestination)
				{
					continue;
				}
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				*CommandStart = Soldier.LastValidNavLocation;
				Soldier.ActiveOrderId = BatchOrderId;
				Soldier.bAutomaticAdvance = false;
				Soldier.bAttackMoveHolding = false;
				Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
				Soldier.bForceMovementUpdate = true;
				AuthorityState->bForceManualAvoidanceRefresh = true;
				Soldier.FinalDestination = *FinalDestination;
				Soldier.FinalDestinationNavigationGeneration = AuthorityState->NavigationGeneration;
				Soldier.bHasFinalDestination = true;
				Soldier.NavigationState = EGuLiSoldierNavigationState::Normal;
				Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
				Soldier.FailureSimulationSeconds = 0.0;
				Soldier.NoProgressSeconds = 0.0f;
				Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
				Soldier.LastProgressPathPointIndex = Formation.PathPoints.Num() > 1 ? 1 : 0;
				Soldier.ConsecutiveSurfaceFailures = 0;
				Soldier.TotalSurfaceFailures = 0;
				Soldier.PersonalPathPoints.Reset();
				Soldier.PersonalPathPointIndex = 0;
				Soldier.PersonalPathRetries = 0;
				++Soldier.StateRevision;
				Formation.MemberPathPointIndexBySoldierId.Add(
					SoldierId.Value,
					Formation.PathPoints.Num() > 1 ? 1 : 0);

				FGuLiMassOrderFragment& Order = EntityManager
					.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
				Order.ActiveOrderId = BatchOrderId;
				Order.OrderRevision = Soldier.StateRevision;
				Order.FormationTarget = FinalDestination->Location;
				Order.bHasMoveTarget = true;
				FMassMoveTargetFragment& MoveTarget = EntityManager
					.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				MoveTarget.CreateNewAction(EMassMovementAction::Move, *World);
				EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(
					Soldier.Entity).Value = FVector::ZeroVector;
				MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
				MoveTarget.Center = FinalDestination->Location;
				MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
			}
			AssignFormationSlots(
				Formation,
				AuthorityState->Soldiers,
				AuthorityState->SoldierIndexById,
				MemberSpacingCentimeters,
				BatchOrderId,
				Formation.TransitColumnCount);
			AuthorityState->OrderFormations.Add(MoveTemp(Formation));
		}

		Job.Ack.BatchOrderId = BatchOrderId;
		int32 EligibleMembers = 0;
		for (FGuLiCohortCommandAck& CohortAck : Job.Ack.CohortResults)
		{
			EligibleMembers += FPlatformMath::CountBits(CohortAck.EligibleMemberMask);
			CohortAck.AcceptedMemberMask = 0u;
			for (const FMoveMemberPlan& Member : Job.Members)
			{
				if (Member.CohortId == CohortAck.CohortId && Member.bAccepted
					&& Member.CohortMemberIndex >= 0 && Member.CohortMemberIndex < 32)
				{
					CohortAck.AcceptedMemberMask |= 1u << Member.CohortMemberIndex;
				}
			}
			if (CohortAck.EligibleMemberMask == 0u)
			{
				CohortAck.Result = EGuLiCommandAckResult::NoSelection;
			}
			else if (CohortAck.AcceptedMemberMask == CohortAck.EligibleMemberMask)
			{
				CohortAck.Result = EGuLiCommandAckResult::Accepted;
			}
			else if (CohortAck.AcceptedMemberMask != 0u)
			{
				CohortAck.Result = EGuLiCommandAckResult::PartiallyAccepted;
			}
			else
			{
				CohortAck.Result = EGuLiCommandAckResult::PathFailed;
			}
		}
		Job.Ack.Result = AcceptedMembers == 0
			? EligibleMembers > 0
				? EGuLiCommandAckResult::PathFailed
				: EGuLiCommandAckResult::NoSelection
			: AcceptedMembers == EligibleMembers
				? EGuLiCommandAckResult::Accepted
				: EGuLiCommandAckResult::PartiallyAccepted;
		if (!Job.FrozenSelection.ActorIds.IsEmpty())
		{
			FGuLiMiningCommand ActorCommand;
			ActorCommand.RequestId = Job.Request.ClientCommandId;
			ActorCommand.Type = EGuLiMiningOrderType::Move;
			ActorCommand.Target = Job.Request.Target;
			ActorCommand.SelectionRevision = Job.Request.SelectionRevision;
			const UGuLiCommanderResourceAdapter* ResourceAdapter =
				GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
			check(ResourceAdapter);
			const bool bActorAccepted = ResourceAdapter->IssueMiningCommand(
				*PlayerState, Job.FrozenSelection.ActorIds, ActorCommand);
			if (bActorAccepted && EligibleMembers == 0)
			{
				Job.Ack.Result = EGuLiCommandAckResult::Accepted;
			}
			else if (bActorAccepted && Job.Ack.Result != EGuLiCommandAckResult::Accepted)
			{
				Job.Ack.Result = EGuLiCommandAckResult::PartiallyAccepted;
			}
			else if (!bActorAccepted && AcceptedMembers > 0)
			{
				Job.Ack.Result = EGuLiCommandAckResult::PartiallyAccepted;
			}
		}
		if (Job.Ack.Result == EGuLiCommandAckResult::PartiallyAccepted)
		{
			++AuthorityState->PartiallyAcceptedMoveCommands;
		}

		Job.UpdatedSelection = Job.FrozenSelection;
		Job.UpdatedSelection.Cohorts.Reset();
		for (const FGuLiControlCohortDescriptor& FrozenCohort : Job.FrozenSelection.Cohorts)
		{
			FGuLiControlCohortDescriptor UpdatedCohort;
			UpdatedCohort.CohortId = FrozenCohort.CohortId;
			for (const FGuLiSoldierId SoldierId : FrozenCohort.MemberIds)
			{
				const FMoveMemberPlan* Member = Job.Members.FindByPredicate(
					[SoldierId, &FrozenCohort](const FMoveMemberPlan& Candidate)
					{
						return Candidate.CohortId == FrozenCohort.CohortId
							&& Candidate.SoldierId == SoldierId;
					});
				if (Member && Member->bAccepted)
				{
					UpdatedCohort.MemberIds.Add(SoldierId);
				}
			}
			if (!UpdatedCohort.MemberIds.IsEmpty())
			{
				UpdatedCohort.AliveCount = static_cast<uint8>(UpdatedCohort.MemberIds.Num());
				UpdatedCohort.ActiveOrderId = BatchOrderId;
				Job.UpdatedSelection.Cohorts.Add(MoveTemp(UpdatedCohort));
			}
		}
		bool bMembershipPruned =
			Job.FrozenSelection.Cohorts.Num() != Job.UpdatedSelection.Cohorts.Num();
		if (!bMembershipPruned)
		{
			for (int32 CohortIndex = 0;
				CohortIndex < Job.FrozenSelection.Cohorts.Num();
				++CohortIndex)
			{
				const FGuLiControlCohortDescriptor& Before =
					Job.FrozenSelection.Cohorts[CohortIndex];
				const FGuLiControlCohortDescriptor& After =
					Job.UpdatedSelection.Cohorts[CohortIndex];
				if (Before.CohortId != After.CohortId || Before.MemberIds != After.MemberIds)
				{
					bMembershipPruned = true;
					break;
				}
			}
		}
		Job.bSelectionChanged = !AreSelectionsEqual(Job.FrozenSelection, Job.UpdatedSelection);
		if (bMembershipPruned)
		{
			++Job.UpdatedSelection.SelectionRevision;
			if (Job.UpdatedSelection.SelectionRevision == 0u)
			{
				++Job.UpdatedSelection.SelectionRevision;
			}
		}
		Job.UpdatedSelection.Sanitize();
		Job.Ack.ServerSelectionRevision = Job.UpdatedSelection.SelectionRevision;
		Job.Ack.Sanitize();

		Job.Debug.BatchOrderId = BatchOrderId;
		Job.Debug.AcceptedMembers = AcceptedMembers;
		Job.Debug.FailedMembers = Job.Members.Num() - AcceptedMembers;
		Job.Debug.FailedSoldierIds.Reset();
		Job.Debug.Cohorts.Reset();
		for (const FGuLiCohortCommandAck& CohortAck : Job.Ack.CohortResults)
		{
			FGuLiMoveCohortPlanningDebug& CohortDebug =
				Job.Debug.Cohorts.AddDefaulted_GetRef();
			CohortDebug.CohortId = CohortAck.CohortId;
			CohortDebug.MemberCount = CohortAck.MemberCount;
			CohortDebug.EligibleMemberMask = CohortAck.EligibleMemberMask;
			CohortDebug.AcceptedMemberMask = CohortAck.AcceptedMemberMask;
			const FMoveCohortPlan* CohortPlan = Job.Cohorts.FindByPredicate(
				[&CohortAck](const FMoveCohortPlan& Candidate)
				{
					return Candidate.CohortId == CohortAck.CohortId;
				});
			if (CohortPlan)
			{
				for (int32 MemberIndex = 0;
					MemberIndex < CohortPlan->FrozenMemberIds.Num() && MemberIndex < 32;
					++MemberIndex)
				{
					if ((CohortAck.AcceptedMemberMask & (1u << MemberIndex)) == 0u)
					{
						CohortDebug.FailedSoldierIds.Add(CohortPlan->FrozenMemberIds[MemberIndex]);
					}
				}
			}
		}
		for (const FMoveMemberPlan& Member : Job.Members)
		{
			if (Member.bAccepted)
			{
				continue;
			}
			Job.Debug.FailedSoldierIds.Add(Member.SoldierId);
			const EGuLiMovePlanFailureStage Stage = Member.FailureStage == EGuLiMovePlanFailureStage::None
				? EGuLiMovePlanFailureStage::CandidatesExhausted
				: Member.FailureStage;
			++AuthorityState->MovePlanningFailureCounts[static_cast<uint8>(Stage)];
		}
		Job.Debug.PlanningMilliseconds =
			(FPlatformTime::Seconds() - Job.PlanningStartedAt) * 1000.0;
		AuthorityState->LastDestinationPlanningMilliseconds = Job.Debug.PlanningMilliseconds;
		AuthorityState->MaximumDestinationPlanningMilliseconds = FMath::Max(
			AuthorityState->MaximumDestinationPlanningMilliseconds,
			Job.Debug.PlanningMilliseconds);
		AuthorityState->LastMovePlanningDebug = Job.Debug;
		AuthorityState->bHasLastMovePlanningDebug = true;
		UE_LOG(
			LogGuLiCommanderMass,
			Display,
			TEXT("Move plan command=%u batch=%u target=%s candidates=%d/%d desiredLegal=%d prefix=%d->%d expansions=%d fullPool=%s frames=%d accepted=%d failed=%d projections=%d paths=%d splits=%d reservationConflicts=%d planningMs=%.3f."),
			Job.Request.ClientCommandId,
			BatchOrderId,
			*FVector(Job.Request.Target).ToCompactString(),
			Job.Debug.LegalCandidates,
			Job.Debug.TheoreticalCandidates,
			Job.Debug.DesiredLegalSlots,
			Job.Debug.InitialProjectionLimit,
			Job.Debug.FinalProjectionLimit,
			Job.Debug.ProjectionExpansionCount,
			Job.Debug.bEscalatedToFullCandidatePool ? TEXT("true") : TEXT("false"),
			Job.Debug.PlanningWorldFrames,
			AcceptedMembers,
			Job.Debug.FailedMembers,
			Job.Debug.CandidateProjectionQueries,
			Job.Debug.PathQueries,
			Job.Debug.RouteSplitCount,
			Job.Debug.ReservationConflictCount,
			Job.Debug.PlanningMilliseconds);
		Job.Stage = EMovePlanningStage::Completed;
	}
}

EGuLiMovePlanningStatus UGuLiBattleAuthoritySubsystem::PollMovePlanning(
	const AGuLiBattlePlayerState& PlayerState,
	const uint32 ClientCommandId,
	FGuLiCommandAck& OutAck,
	FGuLiCommanderSelectionState& OutUpdatedSelection,
	bool& bOutSelectionChanged)
{
	bOutSelectionChanged = false;
	if (!AuthorityState)
	{
		return EGuLiMovePlanningStatus::NotFound;
	}
	for (int32 JobIndex = 0; JobIndex < AuthorityState->MovePlanningJobs.Num(); ++JobIndex)
	{
		const TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>& Job =
			AuthorityState->MovePlanningJobs[JobIndex];
		if (!Job || Job->PlayerState.Get() != &PlayerState
			|| Job->Request.ClientCommandId != ClientCommandId)
		{
			continue;
		}
		if (Job->Stage != GuLiCommanderMassPrivate::EMovePlanningStage::Completed)
		{
			return EGuLiMovePlanningStatus::Pending;
		}
		OutAck = Job->Ack;
		OutUpdatedSelection = Job->UpdatedSelection;
		bOutSelectionChanged = Job->bSelectionChanged;
		AuthorityState->MovePlanningJobs.RemoveAt(JobIndex);
		return EGuLiMovePlanningStatus::Completed;
	}
	return EGuLiMovePlanningStatus::NotFound;
}

void UGuLiBattleAuthoritySubsystem::CancelMovePlanning(
	const AGuLiBattlePlayerState& PlayerState)
{
	if (!AuthorityState)
	{
		return;
	}
	AuthorityState->MovePlanningJobs.RemoveAll(
		[&PlayerState](const TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>& Job)
		{
			return Job && Job->PlayerState.Get() == &PlayerState;
		});
}

void UGuLiBattleAuthoritySubsystem::BuildActiveMoveEndpointSnapshot(
	const EGuLiTeam Team,
	TArray<FGuLiMoveEndpointSnapshot>& OutEndpoints) const
{
	using namespace GuLiCommanderMassPrivate;
	OutEndpoints.Reset();
	if (!AuthorityState || !GuLiCommanderProtocol::IsPlayableTeam(Team))
	{
		return;
	}
	TSet<uint32> AddedSoldierIds;
	for (const FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		if (Formation.Team != Team || Formation.BatchOrderId == 0u)
		{
			continue;
		}
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			if (AddedSoldierIds.Contains(SoldierId.Value))
			{
				continue;
			}
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			const FNavLocation* CommandStart =
				Formation.CommandStartNavLocationBySoldierId.Find(SoldierId.Value);
			const FNavLocation* FinalDestination =
				Formation.FinalDestinationBySoldierId.Find(SoldierId.Value);
			if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
				|| !CommandStart || !FinalDestination)
			{
				continue;
			}
			const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
			if (!Soldier.IsAlive() || Soldier.Team != Team
				|| Soldier.ActiveOrderId != Formation.BatchOrderId)
			{
				continue;
			}
			FGuLiMoveEndpointSnapshot& Endpoint = OutEndpoints.AddDefaulted_GetRef();
			Endpoint.SoldierId = SoldierId;
			Endpoint.ActiveOrderId = Formation.BatchOrderId;
			Endpoint.CommandStart = CommandStart->Location;
			Endpoint.FinalDestination = FinalDestination->Location;
			Endpoint.Revision = Soldier.StateRevision;
			AddedSoldierIds.Add(SoldierId.Value);
		}
	}
	OutEndpoints.Sort([](const FGuLiMoveEndpointSnapshot& Lhs, const FGuLiMoveEndpointSnapshot& Rhs)
	{
		return Lhs.SoldierId.Value < Rhs.SoldierId.Value;
	});
}

bool UGuLiBattleAuthoritySubsystem::TryGetLastMovePlanningDebug(
	FGuLiMovePlanningDebug& OutDebug) const
{
	if (!AuthorityState || !AuthorityState->bHasLastMovePlanningDebug)
	{
		return false;
	}
	OutDebug = AuthorityState->LastMovePlanningDebug;
	return true;
}

// 可选方向引导层：没有可用流场时，行进分支继续使用已验证的共享 NavMesh 路径方向。
void UGuLiBattleAuthoritySubsystem::TickLocalFlowFields()
{
	using namespace GuLiCommanderMassPrivate;
	if (!bEnableLocalFlowField || !AuthorityState || !GetWorld())
	{
		return;
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!NavigationSystem || !CommanderNavigationData)
	{
		return;
	}

	// 全部编队共用本帧预算；每个编队本轮最多检查 128 格，避免一次扫描完整 64×64 瓦片。
	int32 RemainingSampleBudget = FMath::Max(1, FlowFieldWalkabilitySamplesPerTick);
	for (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		if (AuthorityState->NavigationRepairJob
			&& AuthorityState->NavigationRepairJob->Formations.ContainsByPredicate(
				[&Formation](const FNavigationRepairFormationTask& Repair)
				{
					return Repair.FormationId == Formation.FormationId
						&& Repair.ExpectedOrderId == Formation.BatchOrderId;
				}))
		{
			continue;
		}
		if (Formation.bFlowBuildInFlight && Formation.FlowBuildFuture.IsReady())
		{
			TSharedPtr<FGuLiLocalFlowField, ESPMode::ThreadSafe> BuiltField = Formation.FlowBuildFuture.Get();
			Formation.bFlowBuildInFlight = false;
			const FIntPoint CurrentTile = MakeFlowFieldTileCoordinate(Formation.GuideAnchor);
			// 指令、导航代际、路径版本、路径点和瓦片必须全匹配，异步旧结果不能覆盖当前状态。
			if (BuiltField.IsValid())
			{
				const FGuLiFlowFieldBuildKey& Key = BuiltField->GetBuildKey();
				if (Key.OrderId == Formation.BatchOrderId
					&& Key.NavigationGeneration == AuthorityState->NavigationGeneration
					&& Key.PathRevision == Formation.PathRevision
					&& Key.PathPointIndex == Formation.PathPointIndex
					&& Key.TileCoordinate == CurrentTile)
				{
					Formation.FlowField = MoveTemp(BuiltField);
				}
			}
		}

		const FIntPoint CurrentTile = MakeFlowFieldTileCoordinate(Formation.GuideAnchor);
		if (Formation.FlowField.IsValid())
		{
			const FGuLiFlowFieldBuildKey& Key = Formation.FlowField->GetBuildKey();
			if (Key.OrderId != Formation.BatchOrderId
				|| Key.NavigationGeneration != AuthorityState->NavigationGeneration
				|| Key.PathRevision != Formation.PathRevision
				|| Key.PathPointIndex != Formation.PathPointIndex
				|| Key.TileCoordinate != CurrentTile)
			{
				Formation.FlowField.Reset();
			}
		}

		if (Formation.NextFlowWalkabilitySample != INDEX_NONE)
		{
			const FGuLiFlowFieldBuildKey& PendingKey = Formation.PendingFlowBuildData.Key;
			if (PendingKey.OrderId != Formation.BatchOrderId
				|| PendingKey.NavigationGeneration != AuthorityState->NavigationGeneration
				|| PendingKey.PathRevision != Formation.PathRevision
				|| PendingKey.PathPointIndex != Formation.PathPointIndex
				|| PendingKey.TileCoordinate != CurrentTile)
			{
				Formation.NextFlowWalkabilitySample = INDEX_NONE;
				Formation.PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
			}
		}

		if (!Formation.FlowField.IsValid() && !Formation.bFlowBuildInFlight
			&& Formation.NextFlowWalkabilitySample == INDEX_NONE)
		{
			PrepareFlowFieldBuild(Formation, AuthorityState->NavigationGeneration);
		}

		if (Formation.NextFlowWalkabilitySample == INDEX_NONE || RemainingSampleBudget <= 0)
		{
			continue;
		}

		const int32 PerFormationBudget = FMath::Min(RemainingSampleBudget, 128);
		int32 SamplesThisFormation = 0;
		while (Formation.NextFlowWalkabilitySample < FGuLiLocalFlowField::CellCount
			&& SamplesThisFormation < PerFormationBudget)
		{
			const int32 CellIndex = Formation.NextFlowWalkabilitySample++;
			++SamplesThisFormation;
			--RemainingSampleBudget;
			const FIntPoint Cell = FGuLiLocalFlowField::IndexToCell(CellIndex);
			const float CellSize = Formation.PendingFlowBuildData.CellSizeCentimeters;
			const FVector CellCenter(
				Formation.PendingFlowBuildData.WorldMin.X
					+ (static_cast<double>(Cell.X) + 0.5) * CellSize,
				Formation.PendingFlowBuildData.WorldMin.Y
					+ (static_cast<double>(Cell.Y) + 0.5) * CellSize,
				Formation.GuideAnchor.Z);
			if (!IsInsideFormationCorridor(
				CellCenter,
				Formation,
				FlowFieldCorridorHalfWidthCentimeters))
			{
				continue;
			}

			FNavLocation ProjectedLocation;
			const FVector ProjectionExtent(CellSize * 0.45f, CellSize * 0.45f, 5000.0f);
			if (ProjectPointToCommanderNavigation(
				*NavigationSystem,
				*CommanderNavigationData,
				CellCenter,
				ProjectionExtent,
				ProjectedLocation)
				&& FVector::DistSquared2D(CellCenter, ProjectedLocation.Location)
					<= FMath::Square(CellSize))
			{
				Formation.PendingFlowBuildData.Walkable[CellIndex] = true;
			}
		}

		if (Formation.NextFlowWalkabilitySample >= FGuLiLocalFlowField::CellCount)
		{
			FGuLiLocalFlowFieldBuildData DetachedBuildData = MoveTemp(Formation.PendingFlowBuildData);
			Formation.NextFlowWalkabilitySample = INDEX_NONE;
			Formation.bFlowBuildInFlight = true;
			// Lambda 只按值接管 BuildData，不捕获 this/Formation/NavData，避免工作线程访问世界对象。
			Formation.FlowBuildFuture = Async(
				EAsyncExecution::ThreadPool,
				[BuildData = MoveTemp(DetachedBuildData)]() mutable
				{
					TSharedPtr<FGuLiLocalFlowField, ESPMode::ThreadSafe> Field = MakeShared<FGuLiLocalFlowField, ESPMode::ThreadSafe>();
					if (!Field->Build(BuildData))
					{
						Field.Reset();
					}
					return Field;
				});
		}
	}
}

// 专用导航重建后重新寻路并递增版本；旧流场失效，尚未到达成员的过弯进度重新建立。
void UGuLiBattleAuthoritySubsystem::HandleNavigationGenerationFinished(
	ANavigationData* NavigationData)
{
	using namespace GuLiCommanderMassPrivate;
	UWorld* World = GetWorld();
	if (!AuthorityState || !World || !NavigationData
		|| NavigationData->GetConfig().Name
			!= GuLiCommanderNavigationPolicy::GetRequiredAgentName())
	{
		return;
	}

	++AuthorityState->NavigationGeneration;
	if (AuthorityState->NavigationGeneration == 0u)
	{
		++AuthorityState->NavigationGeneration;
	}

	// A resource cluster disappearing opens space. Existing paths remain legal and must not
	// be stopped merely because unrelated Recast tiles changed. Only derived local-flow caches
	// are invalidated; ordinary surface projection failures already enqueue the affected member
	// in the bounded centerline/personal-path recovery path below.
	AuthorityState->NavigationRepairJob.Reset();
	for (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		Formation.FlowField.Reset();
		Formation.NextFlowWalkabilitySample = INDEX_NONE;
		Formation.PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
	}
	AuthorityState->bForceManualAvoidanceRefresh = true;
}

void UGuLiBattleAuthoritySubsystem::HandleDynamicObstaclesChanged(const uint32 ObstacleRevision)
{
	(void)ObstacleRevision;
	check(AuthorityState);
	AuthorityState->bForceManualAvoidanceRefresh = true;
}

void UGuLiBattleAuthoritySubsystem::TickNavigationRepairs(
	int32& RemainingProjectionBudget,
	int32& RemainingPathBudget)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->NavigationRepairJob
		|| AuthorityState->NavigationRepairJob->bReadyToCommit)
	{
		return;
	}
	FNavigationRepairJob& Job = *AuthorityState->NavigationRepairJob;
	UWorld* World = GetWorld();
	const AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	const uint32 CurrentAuthorityEpoch = BattleGameState
		? BattleGameState->GetMatchEpoch()
		: 0u;
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	ANavigationData* NavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (Job.AuthorityEpoch == 0u && CurrentAuthorityEpoch != 0u)
	{
		Job.AuthorityEpoch = CurrentAuthorityEpoch;
	}
	const bool bRepairBecameStale =
		Job.NavigationGeneration != AuthorityState->NavigationGeneration
		|| (Job.AuthorityEpoch != 0u && CurrentAuthorityEpoch != 0u
			&& Job.AuthorityEpoch != CurrentAuthorityEpoch);
	if (bRepairBecameStale)
	{
		UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
		if (World && MassSubsystem)
		{
			FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
			for (const FNavigationRepairMemberTask& Task : Job.Members)
			{
				if (Task.ExpectedOrderId == 0u)
				{
					continue;
				}
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Task.SoldierId.Value);
				if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					continue;
				}
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				if (!Soldier.CanAct() || Soldier.ActiveOrderId != Task.ExpectedOrderId
					|| !EntityManager.IsEntityValid(Soldier.Entity))
				{
					continue;
				}
				FMassMoveTargetFragment& MoveTarget = EntityManager
					.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				MoveTarget.CreateNewAction(EMassMovementAction::Move, *World);
				MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
				MoveTarget.Center = Soldier.bHasFinalDestination
					? Soldier.FinalDestination.Location
					: Soldier.Location;
				MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
			}
		}
		AuthorityState->NavigationRepairJob.Reset();
		return;
	}
	if (!World || !NavigationSystem || !NavigationData
		|| !BattleGameState || CurrentAuthorityEpoch == 0u)
	{
		return;
	}

	for (FNavigationRepairMemberTask& Task : Job.Members)
	{
		if (Task.ExpectedOrderId == 0u)
		{
			continue;
		}
		const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Task.SoldierId.Value);
		if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
			|| !AuthorityState->Soldiers[*SoldierIndex].CanAct()
			|| AuthorityState->Soldiers[*SoldierIndex].ActiveOrderId != Task.ExpectedOrderId)
		{
			Task.Stage = ENavigationRepairMemberStage::Discarded;
			Job.PendingActiveSoldierIds.Remove(Task.SoldierId.Value);
		}
	}

	auto MakeRepairCandidate = [](const FVector& Center, const int32 CandidateIndex)
	{
		if (CandidateIndex <= 0)
		{
			return Center;
		}
		const int32 RingIndex = CandidateIndex - 1;
		const float Radius = 250.0f * static_cast<float>(RingIndex / 8 + 1);
		const int32 DirectionIndex = RingIndex % 8;
		const float AngleRadians = UE_TWO_PI * static_cast<float>(DirectionIndex) / 8.0f;
		return Center + FVector(
			FMath::Cos(AngleRadians) * Radius,
			FMath::Sin(AngleRadians) * Radius,
			0.0f);
	};
	auto IsCandidateClear = [this, &Job](const FNavigationRepairMemberTask& Task,
		const FVector& CandidateLocation)
	{
		const double MinimumDistanceSquared = FMath::Square(
			static_cast<double>(DestinationMinimumSeparationCentimeters));
		for (const FSoldierRuntime& Other : AuthorityState->Soldiers)
		{
			if (!Other.IsAlive() || Other.Team != Task.Team || Other.SoldierId == Task.SoldierId)
			{
				continue;
			}
			const bool bOtherReservesFinal = Other.bHasFinalDestination
				&& (Other.ActiveOrderId != 0u
					|| Other.NavigationState == EGuLiSoldierNavigationState::Arrived);
			const FVector& Reservation = bOtherReservesFinal
				? Other.FinalDestination.Location
				: Other.Location;
			if (FVector::DistSquared2D(CandidateLocation, Reservation) < MinimumDistanceSquared)
			{
				return false;
			}
		}
		for (const FNavigationRepairMemberTask& OtherTask : Job.Members)
		{
			if (OtherTask.SoldierId == Task.SoldierId || OtherTask.Team != Task.Team
				|| OtherTask.Stage != ENavigationRepairMemberStage::Ready
				|| !OtherTask.bHadFinalDestination)
			{
				continue;
			}
			if (FVector::DistSquared2D(
					CandidateLocation,
					OtherTask.RepairedFinalLocation.Location) < MinimumDistanceSquared)
			{
				return false;
			}
		}
		return true;
	};

	while (Job.NextMemberIndex < Job.Members.Num())
	{
		FNavigationRepairMemberTask& Task = Job.Members[Job.NextMemberIndex];
		if (Task.Stage == ENavigationRepairMemberStage::Ready
			|| Task.Stage == ENavigationRepairMemberStage::Failed
			|| Task.Stage == ENavigationRepairMemberStage::Discarded)
		{
			++Job.NextMemberIndex;
			continue;
		}
		const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Task.SoldierId.Value);
		if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
		{
			Task.Stage = ENavigationRepairMemberStage::Discarded;
			continue;
		}
		if (Task.Stage == ENavigationRepairMemberStage::ProjectCurrent)
		{
			if (RemainingProjectionBudget <= 0)
			{
				break;
			}
			--RemainingProjectionBudget;
			FNavLocation Projected;
			if (!ProjectPointToCommanderNavigation(*NavigationSystem, *NavigationData,
					Task.PreviousCurrentLocation, FVector(10.0f, 10.0f, 5000.0f), Projected)
				|| FVector::DistSquared2D(Task.PreviousCurrentLocation, Projected.Location)
					> FMath::Square(10.0f))
			{
				Task.Stage = ENavigationRepairMemberStage::Failed;
				continue;
			}
			Projected.Location.X = Task.PreviousCurrentLocation.X;
			Projected.Location.Y = Task.PreviousCurrentLocation.Y;
			Task.RefreshedCurrentLocation = Projected;
			Task.Stage = Task.bHadFinalDestination
				? ENavigationRepairMemberStage::ProjectFinal
				: ENavigationRepairMemberStage::Ready;
			continue;
		}
		if (Task.Stage == ENavigationRepairMemberStage::ProjectFinal)
		{
			if (RemainingProjectionBudget <= 0)
			{
				break;
			}
			--RemainingProjectionBudget;
			FNavLocation Projected;
			if (ProjectPointToCommanderNavigation(*NavigationSystem, *NavigationData,
					Task.PreviousFinalLocation, FVector(10.0f, 10.0f, 5000.0f), Projected)
				&& FVector::DistSquared2D(Task.PreviousFinalLocation, Projected.Location)
					<= FMath::Square(10.0f))
			{
				Projected.Location.X = Task.PreviousFinalLocation.X;
				Projected.Location.Y = Task.PreviousFinalLocation.Y;
				Task.RepairedFinalLocation = Projected;
				Task.Stage = ENavigationRepairMemberStage::Ready;
			}
			else
			{
				Task.Stage = Task.ExpectedOrderId != 0u
					? ENavigationRepairMemberStage::ProjectCandidate
					: ENavigationRepairMemberStage::Failed;
			}
			continue;
		}
		if (Task.Stage == ENavigationRepairMemberStage::ProjectCandidate)
		{
			if (Task.NextCandidateIndex >= 25)
			{
				Task.Stage = ENavigationRepairMemberStage::Failed;
				continue;
			}
			if (RemainingProjectionBudget <= 0)
			{
				break;
			}
			--RemainingProjectionBudget;
			const FVector Requested = MakeRepairCandidate(
				Task.PreviousFinalLocation, Task.NextCandidateIndex);
			FNavLocation Projected;
			if (ProjectPointToCommanderNavigation(*NavigationSystem, *NavigationData,
					Requested, FVector(100.0f, 100.0f, 5000.0f), Projected)
				&& FVector::DistSquared2D(Task.PreviousFinalLocation, Projected.Location)
					<= FMath::Square(DestinationMaximumProjectionCorrectionCentimeters)
				&& IsCandidateClear(Task, Projected.Location))
			{
				Task.PendingCandidate = Projected;
				Task.Stage = ENavigationRepairMemberStage::QueryCandidatePath;
			}
			else
			{
				++Task.NextCandidateIndex;
			}
			continue;
		}
		if (RemainingPathBudget <= 0)
		{
			break;
		}
		--RemainingPathBudget;
		++AuthorityState->PathQueries;
		if (HasCompletePath(*NavigationSystem, *NavigationData,
				Task.RefreshedCurrentLocation.Location, Task.PendingCandidate.Location))
		{
			Task.RepairedFinalLocation = Task.PendingCandidate;
			Task.Stage = ENavigationRepairMemberStage::Ready;
		}
		else
		{
			++Task.NextCandidateIndex;
			Task.Stage = ENavigationRepairMemberStage::ProjectCandidate;
		}
	}
	if (Job.NextMemberIndex < Job.Members.Num())
	{
		return;
	}

	while (Job.NextFormationIndex < Job.Formations.Num())
	{
		FNavigationRepairFormationTask& Task = Job.Formations[Job.NextFormationIndex];
		if (Task.Stage == ENavigationRepairFormationStage::Ready
			|| Task.Stage == ENavigationRepairFormationStage::Discarded)
		{
			++Job.NextFormationIndex;
			continue;
		}
		FOrderFormationRuntime* Formation = AuthorityState->OrderFormations.FindByPredicate(
			[&Task](const FOrderFormationRuntime& Candidate)
			{
				return Candidate.FormationId == Task.FormationId
					&& Candidate.BatchOrderId == Task.ExpectedOrderId;
			});
		if (!Formation)
		{
			Task.Stage = ENavigationRepairFormationStage::Discarded;
			continue;
		}
		const bool bHasRepairableActiveMember = Job.Members.ContainsByPredicate(
			[this, &Task](const FNavigationRepairMemberTask& MemberTask)
			{
				if (MemberTask.FormationId != Task.FormationId
					|| MemberTask.ExpectedOrderId != Task.ExpectedOrderId
					|| MemberTask.Stage != ENavigationRepairMemberStage::Ready)
				{
					return false;
				}
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(
					MemberTask.SoldierId.Value);
				return SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
					&& AuthorityState->Soldiers[*SoldierIndex].CanAct()
					&& AuthorityState->Soldiers[*SoldierIndex].ActiveOrderId
						== Task.ExpectedOrderId;
			});
		if (!bHasRepairableActiveMember)
		{
			Task.bPathValid = false;
			Task.Stage = ENavigationRepairFormationStage::Ready;
			continue;
		}
		if (Task.Stage == ENavigationRepairFormationStage::ProjectTarget)
		{
			if (RemainingProjectionBudget <= 0)
			{
				break;
			}
			--RemainingProjectionBudget;
			if (!ProjectPointToCommanderNavigation(*NavigationSystem, *NavigationData,
					Task.PreviousTargetAnchor,
					FVector(MemberAgentRadiusCentimeters, MemberAgentRadiusCentimeters, 5000.0f),
					Task.ProjectedTargetAnchor)
				|| !GuLiCommanderNavigationPolicy::IsProjectedTargetAcceptable(
					Task.PreviousTargetAnchor, Task.ProjectedTargetAnchor.Location,
					MemberAgentRadiusCentimeters))
			{
				Task.bPathValid = false;
				Task.Stage = ENavigationRepairFormationStage::Ready;
			}
			else
			{
				Task.Stage = ENavigationRepairFormationStage::ProjectGuide;
			}
			continue;
		}
		if (Task.Stage == ENavigationRepairFormationStage::ProjectGuide)
		{
			if (RemainingProjectionBudget <= 0)
			{
				break;
			}
			--RemainingProjectionBudget;
			if (!ProjectPointToCommanderNavigation(*NavigationSystem, *NavigationData,
					Task.PreviousGuideAnchor,
					FVector(DestinationMaximumProjectionCorrectionCentimeters,
						DestinationMaximumProjectionCorrectionCentimeters, 5000.0f),
					Task.ProjectedGuideAnchor)
				|| FVector::DistSquared2D(
						Task.PreviousGuideAnchor, Task.ProjectedGuideAnchor.Location)
					> FMath::Square(DestinationMaximumProjectionCorrectionCentimeters))
			{
				Task.bPathValid = false;
				Task.Stage = ENavigationRepairFormationStage::Ready;
			}
			else
			{
				Task.Stage = ENavigationRepairFormationStage::QueryPath;
			}
			continue;
		}
		if (RemainingPathBudget <= 0)
		{
			break;
		}
		--RemainingPathBudget;
		++AuthorityState->PathQueries;
		Task.bPathValid = BuildCompletePathQuiet(*NavigationSystem, *NavigationData,
			Task.ProjectedGuideAnchor, Task.ProjectedTargetAnchor.Location,
			Task.RebuiltPathPoints);
		Task.Stage = ENavigationRepairFormationStage::Ready;
	}
	Job.bReadyToCommit = Job.NextFormationIndex >= Job.Formations.Num();
}

void UGuLiBattleAuthoritySubsystem::CommitReadyNavigationRepairs()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->NavigationRepairJob
		|| !AuthorityState->NavigationRepairJob->bReadyToCommit)
	{
		return;
	}
	FNavigationRepairJob& Job = *AuthorityState->NavigationRepairJob;
	UWorld* World = GetWorld();
	const AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!World || !BattleGameState || !MassSubsystem
		|| Job.NavigationGeneration != AuthorityState->NavigationGeneration
		|| Job.AuthorityEpoch == 0u || Job.AuthorityEpoch != BattleGameState->GetMatchEpoch())
	{
		if (World && MassSubsystem)
		{
			FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
			for (const FNavigationRepairMemberTask& Task : Job.Members)
			{
				const int32* SoldierIndex = Task.ExpectedOrderId != 0u
					? AuthorityState->SoldierIndexById.Find(Task.SoldierId.Value)
					: nullptr;
				if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					continue;
				}
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				if (!Soldier.CanAct() || Soldier.ActiveOrderId != Task.ExpectedOrderId
					|| !EntityManager.IsEntityValid(Soldier.Entity))
				{
					continue;
				}
				FMassMoveTargetFragment& MoveTarget = EntityManager
					.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				MoveTarget.CreateNewAction(EMassMovementAction::Move, *World);
				MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
				MoveTarget.Center = Soldier.bHasFinalDestination
					? Soldier.FinalDestination.Location
					: Soldier.Location;
				MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
			}
		}
		AuthorityState->NavigationRepairJob.Reset();
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

	auto BlockInvalidated = [this, World, &EntityManager](FSoldierRuntime& Soldier,
		const uint32 FailedOrderId)
	{
		Soldier.Location = Soldier.LastValidNavLocation.Location;
		Soldier.Velocity = FVector::ZeroVector;
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Soldier.bForceMovementUpdate = false;
		Soldier.ActiveOrderId = 0u;
		Soldier.LastFailedOrderId = FailedOrderId;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Blocked;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::FinalSlotInvalidated;
		Soldier.FailureSimulationSeconds = AuthorityState->SimulationSeconds;
		++Soldier.StateRevision;
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			return;
		}
		FMassMoveTargetFragment& MoveTarget = EntityManager
			.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
		MoveTarget.Center = Soldier.Location;
		MoveTarget.DesiredSpeed = FMassInt16Real(0.0f);
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		FGuLiMassOrderFragment& Order = EntityManager
			.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = 0u;
		Order.OrderRevision = Soldier.StateRevision;
		Order.bHasMoveTarget = false;
	};

	for (const FNavigationRepairMemberTask& Task : Job.Members)
	{
		const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Task.SoldierId.Value);
		if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
		{
			continue;
		}
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
		if (!Soldier.CanAct() || Soldier.ActiveOrderId != Task.ExpectedOrderId)
		{
			continue;
		}
		FOrderFormationRuntime* Formation = Task.FormationId != 0u
			? AuthorityState->OrderFormations.FindByPredicate(
				[&Task](const FOrderFormationRuntime& Candidate)
				{
					return Candidate.FormationId == Task.FormationId
						&& Candidate.BatchOrderId == Task.ExpectedOrderId;
				})
			: nullptr;
		if (Task.Stage != ENavigationRepairMemberStage::Ready)
		{
			const uint32 FailedOrderId = Task.bWasArrived
				? Soldier.LastCompletedOrderId
				: Soldier.ActiveOrderId;
			BlockInvalidated(Soldier, FailedOrderId);
			if (Formation)
			{
				Formation->FinalDestinationBySoldierId.Remove(Task.SoldierId.Value);
			}
			continue;
		}

		Soldier.LastValidNavLocation = Task.RefreshedCurrentLocation;
		Soldier.Location = Task.RefreshedCurrentLocation.Location;
		if (Task.bHadFinalDestination)
		{
			Soldier.FinalDestination = Task.RepairedFinalLocation;
			Soldier.FinalDestinationNavigationGeneration = AuthorityState->NavigationGeneration;
			if (Formation)
			{
				Formation->FinalDestinationBySoldierId.Add(
					Task.SoldierId.Value, Task.RepairedFinalLocation);
			}
		}
		if (Task.bWasArrived && Task.bHadFinalDestination)
		{
			Soldier.LastValidNavLocation = Task.RepairedFinalLocation;
			Soldier.Location = Task.RepairedFinalLocation.Location;
		}
		if (Task.ExpectedOrderId == 0u)
		{
			continue;
		}
		Soldier.PersonalPathPoints.Reset();
		Soldier.PersonalPathPointIndex = 0;
		Soldier.PersonalPathRetries = 0;
		Soldier.NoProgressSeconds = 0.0f;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		Soldier.LastProgressPathPointIndex = INDEX_NONE;
		Soldier.ConsecutiveSurfaceFailures = 0;
		Soldier.TotalSurfaceFailures = 0;
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Soldier.bForceMovementUpdate = true;
		AuthorityState->bForceManualAvoidanceRefresh = true;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Normal;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
		Soldier.FailureSimulationSeconds = 0.0;
		++Soldier.StateRevision;
		if (EntityManager.IsEntityValid(Soldier.Entity))
		{
			FMassMoveTargetFragment& MoveTarget = EntityManager
				.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
			MoveTarget.CreateNewAction(EMassMovementAction::Move, *World);
			MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
			MoveTarget.Center = Soldier.FinalDestination.Location;
			MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
			FGuLiMassOrderFragment& Order = EntityManager
				.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
			Order.ActiveOrderId = Soldier.ActiveOrderId;
			Order.OrderRevision = Soldier.StateRevision;
			Order.FormationTarget = Soldier.FinalDestination.Location;
			Order.bHasMoveTarget = true;
		}
	}

	for (const FNavigationRepairFormationTask& Task : Job.Formations)
	{
		FOrderFormationRuntime* Formation = AuthorityState->OrderFormations.FindByPredicate(
			[&Task](const FOrderFormationRuntime& Candidate)
			{
				return Candidate.FormationId == Task.FormationId
					&& Candidate.BatchOrderId == Task.ExpectedOrderId;
			});
		if (!Formation || Task.Stage == ENavigationRepairFormationStage::Discarded)
		{
			continue;
		}
		Formation->bPathValid = Task.bPathValid;
		Formation->FlowField.Reset();
		Formation->NextFlowWalkabilitySample = INDEX_NONE;
		Formation->PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
		if (!Task.bPathValid)
		{
			Formation->PathPoints.Reset();
			Formation->PathPointIndex = 0;
			continue;
		}
		Formation->GuideAnchor = Task.ProjectedGuideAnchor.Location;
		Formation->TargetAnchor = Task.ProjectedTargetAnchor.Location;
		Formation->PathPoints = Task.RebuiltPathPoints;
		Formation->PathPointIndex = Formation->PathPoints.Num() > 1 ? 1 : 0;
		Formation->FinalPathFrame = GuLiCommanderNavigationPolicy::ResolveFinalPathFrame(
			Formation->PathPoints, Formation->GuideAnchor, Formation->TargetAnchor);
		Formation->bFinalApproachStarted = false;
		Formation->MemberPathPointIndexBySoldierId.Reset();
		for (const FGuLiSoldierId SoldierId : Formation->MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
				&& AuthorityState->Soldiers[*SoldierIndex].CanAct()
				&& AuthorityState->Soldiers[*SoldierIndex].ActiveOrderId == Formation->BatchOrderId)
			{
				Formation->MemberPathPointIndexBySoldierId.Add(
					SoldierId.Value, Formation->PathPointIndex);
			}
		}
	}
	AuthorityState->NavigationRepairJob.Reset();
}
// 固定步：提交配置 -> 编队速度 -> 10 Hz 避让缓存/全员位移 -> 按批次收尾 -> 统一攻击/伤害。
// 实际位置由这里积分并写回 Fragment；导航策略函数负责判定，不直接改世界状态。
void UGuLiBattleAuthoritySubsystem::TickAuthority(const float FixedDeltaSeconds)
{
	CSV_SCOPED_TIMING_STAT(GuLiCommanderAuthority, FixedStep);
	using namespace GuLiCommanderMassPrivate;
	check(AuthorityState);
#if !UE_BUILD_SHIPPING
	const double StepStartedAt = FPlatformTime::Seconds();
#endif
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	UWorld* World = GetWorld();
	if (!MassSubsystem || !World)
	{
		return;
	}
	CommitReadyMovePlans();
	CommitReadyNavigationRepairs();

	CommitCombatProfiles();
	ApplyPendingMovementSpeed();
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	AuthorityState->SimulationSeconds += FixedDeltaSeconds;
	++AuthorityState->ServerSimTick;

	auto SetTerminalNavigationState = [this, World, &EntityManager](
		FSoldierRuntime& Soldier,
		const EGuLiSoldierNavigationState TerminalState,
		const EGuLiSoldierNavigationFailure Failure)
	{
		const uint32 FinishedOrderId = Soldier.ActiveOrderId;
		if (TerminalState == EGuLiSoldierNavigationState::Arrived)
		{
			Soldier.Location = Soldier.FinalDestination.Location;
			Soldier.LastValidNavLocation = Soldier.FinalDestination;
			Soldier.LastCompletedOrderId = FinishedOrderId;
			Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
			Soldier.FailureSimulationSeconds = 0.0;
		}
		else
		{
			Soldier.Location = Soldier.LastValidNavLocation.Location;
			Soldier.LastFailedOrderId = FinishedOrderId;
			Soldier.NavigationFailure = Failure;
			Soldier.FailureSimulationSeconds = AuthorityState->SimulationSeconds;
		}
		Soldier.NavigationState = TerminalState;
		Soldier.ActiveOrderId = 0u;
		Soldier.Velocity = FVector::ZeroVector;
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Soldier.bForceMovementUpdate = false;
		Soldier.CurrentNavigationWaypoint = Soldier.Location;
		++Soldier.StateRevision;

		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			return;
		}
		FMassMoveTargetFragment& MoveTarget = EntityManager
			.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
		MoveTarget.Center = Soldier.Location;
		MoveTarget.DesiredSpeed = FMassInt16Real(0.0f);
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		FGuLiMassOrderFragment& Order = EntityManager
			.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = 0u;
		Order.OrderRevision = Soldier.StateRevision;
		Order.bHasMoveTarget = false;
	};

	auto EnterCenterlineRecovery = [](FSoldierRuntime& Soldier)
	{
		Soldier.NavigationState = EGuLiSoldierNavigationState::CenterlineRecovery;
		Soldier.NoProgressSeconds = 0.0f;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
	};
	auto EnterPersonalPathRecovery = [](FSoldierRuntime& Soldier)
	{
		Soldier.NavigationState = EGuLiSoldierNavigationState::PersonalPathRecovery;
		Soldier.NoProgressSeconds = 0.0f;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		Soldier.PersonalPathPoints.Reset();
		Soldier.PersonalPathPointIndex = 0;
		Soldier.PersonalPathRetries = 0;
	};

	const float AvoidanceAgentHeightCentimeters = CommanderNavigationData
		? FMath::Max(1.0f, CommanderNavigationData->GetConfig().AgentHeight)
		: 144.0f;

	TArray<FVector> DesiredVelocities;
	DesiredVelocities.Init(FVector::ZeroVector, AuthorityState->Soldiers.Num());
	TBitArray<> bReceivesAvoidance;
	bReceivesAvoidance.Init(false, AuthorityState->Soldiers.Num());
	TBitArray<> bRunsMovementUpdate;
	bRunsMovementUpdate.Init(false, AuthorityState->Soldiers.Num());
	TBitArray<> bForcedMovementUpdate;
	bForcedMovementUpdate.Init(false, AuthorityState->Soldiers.Num());
	TArray<float> MovementUpdateDeltaSeconds;
	MovementUpdateDeltaSeconds.Init(0.0f, AuthorityState->Soldiers.Num());
	TBitArray<> bSuppressMovementForStep;
	bSuppressMovementForStep.Init(false, AuthorityState->Soldiers.Num());
	TMap<uint32, FOrderFormationRuntime*> FormationBySoldierId;
	FormationBySoldierId.Reserve(AuthorityState->Soldiers.Num());

	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		const bool bHasMovingOrder = Soldier.CanAct()
			&& !Soldier.bAttackMoveHolding
			&& Soldier.ActiveOrderId != 0u
			&& (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
				|| Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
				|| Soldier.NavigationState == EGuLiSoldierNavigationState::PersonalPathRecovery);
		if (!bHasMovingOrder)
		{
			Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
			Soldier.bForceMovementUpdate = false;
			continue;
		}

		const bool bWasForced = Soldier.bForceMovementUpdate;
		if (!GuLiCommanderNavigationPolicy::ShouldRunMovementUpdate(
			AuthorityState->ServerSimTick,
			Soldier.SoldierId.Value,
			bWasForced))
		{
			continue;
		}

		bRunsMovementUpdate[SoldierIndex] = true;
		bForcedMovementUpdate[SoldierIndex] = bWasForced;
		MovementUpdateDeltaSeconds[SoldierIndex] =
			GuLiCommanderNavigationPolicy::ResolveMovementUpdateDeltaSeconds(
				AuthorityState->SimulationSeconds,
				Soldier.LastMovementUpdateSimulationSeconds,
				FixedDeltaSeconds);
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Soldier.bForceMovementUpdate = false;
	}

	for (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		TArray<int32, TInlineAllocator<SoldierCountPerFormation>> ActiveIndices;
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
			{
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				if (Soldier.IsAlive() && Soldier.ActiveOrderId == Formation.BatchOrderId)
				{
					ActiveIndices.Add(*SoldierIndex);
				}
			}
		}
		if (ActiveIndices.IsEmpty())
		{
			continue;
		}
		const bool bFormationPendingNavigationRepair = AuthorityState->NavigationRepairJob
			&& ActiveIndices.ContainsByPredicate([this](const int32 SoldierIndex)
			{
				return AuthorityState->Soldiers.IsValidIndex(SoldierIndex)
					&& AuthorityState->NavigationRepairJob->PendingActiveSoldierIds.Contains(
						AuthorityState->Soldiers[SoldierIndex].SoldierId.Value);
			});
		if (bFormationPendingNavigationRepair)
		{
			Formation.bLastMovementStepSucceeded = false;
			for (const int32 SoldierIndex : ActiveIndices)
			{
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
				Soldier.Location = Soldier.LastValidNavLocation.Location;
				Soldier.Velocity = FVector::ZeroVector;
				Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
				Soldier.bForceMovementUpdate = false;
				bSuppressMovementForStep[SoldierIndex] = true;
			}
			continue;
		}
		if (!NavigationSystem || !CommanderNavigationData)
		{
			Formation.bLastMovementStepSucceeded = false;
			for (const int32 SoldierIndex : ActiveIndices)
			{
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
				Soldier.Location = Soldier.LastValidNavLocation.Location;
				Soldier.Velocity = FVector::ZeroVector;
				Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
				Soldier.bForceMovementUpdate = false;
				Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::NavigationUnavailable;
				bSuppressMovementForStep[SoldierIndex] = true;
			}
			continue;
		}
		if (!Formation.bPathValid || Formation.PathPoints.Num() < 2)
		{
			Formation.bLastMovementStepSucceeded = false;
			Formation.PathPoints.Reset(2);
			Formation.PathPoints.Add(Formation.GuideAnchor);
			Formation.PathPoints.Add(Formation.TargetAnchor);
			Formation.PathPointIndex = 1;
			for (const int32 SoldierIndex : ActiveIndices)
			{
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
				if (Soldier.NavigationState != EGuLiSoldierNavigationState::PersonalPathRecovery)
				{
					EnterPersonalPathRecovery(Soldier);
				}
				Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::NavigationUnavailable;
				Formation.MemberPathPointIndexBySoldierId.Add(Soldier.SoldierId.Value, 1);
			}
		}

		if (!Formation.bFinalApproachStarted
			&& Formation.SlotBySoldierId.Num() != ActiveIndices.Num())
		{
			AssignFormationSlots(
				Formation,
				AuthorityState->Soldiers,
				AuthorityState->SoldierIndexById,
				MemberSpacingCentimeters,
				Formation.BatchOrderId,
				Formation.TransitColumnCount);
			Formation.LastTransitReassignmentSeconds = AuthorityState->SimulationSeconds;
			for (const int32 SoldierIndex : ActiveIndices)
			{
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
				Soldier.NoProgressSeconds = 0.0f;
				Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
				Soldier.LastProgressPathPointIndex = INDEX_NONE;
			}
		}

		const FVector ActiveCentroid = ComputeCentroid(
			Formation.MemberIds,
			AuthorityState->Soldiers,
			AuthorityState->SoldierIndexById,
			Formation.BatchOrderId);
		Formation.PathPointIndex = FMath::Clamp(
			Formation.PathPointIndex,
			1,
			Formation.PathPoints.Num() - 1);
		const int32 PreviousGuidePathIndex = Formation.PathPointIndex;
		while (Formation.PathPointIndex < Formation.PathPoints.Num() - 1
			&& FVector::DistSquared2D(
				Formation.GuideAnchor,
				Formation.PathPoints[Formation.PathPointIndex])
				<= FMath::Square(FormationWaypointToleranceCentimeters))
		{
			++Formation.PathPointIndex;
		}
		if (Formation.PathPointIndex != PreviousGuidePathIndex)
		{
			Formation.FlowField.Reset();
			Formation.NextFlowWalkabilitySample = INDEX_NONE;
			Formation.PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
		}

		const float MaximumStep = MovementSpeedCentimetersPerSecond * FixedDeltaSeconds;
		FVector GuideDelta = Formation.PathPoints[Formation.PathPointIndex] - Formation.GuideAnchor;
		const float GuideDistance = GuideDelta.Size();
		const bool bGuideMayAdvance = Formation.bFinalApproachStarted
			|| FVector::DistSquared2D(ActiveCentroid, Formation.GuideAnchor)
				<= FMath::Square(FormationGuideMaximumLeadCentimeters);
		if (bGuideMayAdvance && GuideDistance > UE_KINDA_SMALL_NUMBER)
		{
			const FVector GuideDirection = GuideDelta / GuideDistance;
			Formation.GuideAnchor += GuideDirection * FMath::Min(MaximumStep, GuideDistance);
			Formation.TravelFacingYawDegrees = FMath::FixedTurn(
				Formation.TravelFacingYawDegrees,
				GuideDirection.GetSafeNormal2D().Rotation().Yaw,
				FacingRateDegreesPerSecond * FixedDeltaSeconds);
		}
		Formation.bFinalApproachStarted |= Formation.PathPointIndex == Formation.PathPoints.Num() - 1
			&& FVector::DistSquared2D(Formation.GuideAnchor, Formation.TargetAnchor)
				<= FMath::Square(Formation.FinalApproachTriggerRadiusCentimeters);

		if (!Formation.bFinalApproachStarted)
		{
			const int32 DesiredColumnCount = DetermineTransitFormationColumns(
				NavigationSystem,
				CommanderNavigationData,
				Formation.GuideAnchor,
				Formation.TravelFacingYawDegrees,
				MemberSpacingCentimeters);
			GuLiCommanderNavigationPolicy::FTransitColumnHysteresisState PreviousColumnState;
			PreviousColumnState.ColumnCount = Formation.TransitColumnCount;
			PreviousColumnState.ConsecutiveExpansionSuccessSteps =
				Formation.ConsecutiveExpansionFitSteps;
			PreviousColumnState.LastRearrangementTimeSeconds =
				Formation.LastTransitReassignmentSeconds;
			const GuLiCommanderNavigationPolicy::FTransitColumnHysteresisState NextColumnState =
				GuLiCommanderNavigationPolicy::UpdateTransitColumnHysteresis(
					PreviousColumnState,
					DesiredColumnCount,
					Formation.bLastMovementStepSucceeded,
					AuthorityState->SimulationSeconds,
					ExpansionSuccessfulStepsRequired,
					TransitReassignmentCooldownSeconds);
			const bool bReassignTransitSlots =
				NextColumnState.ColumnCount != Formation.TransitColumnCount;
			Formation.TransitColumnCount = NextColumnState.ColumnCount;
			Formation.ConsecutiveExpansionFitSteps =
				NextColumnState.ConsecutiveExpansionSuccessSteps;
			Formation.LastTransitReassignmentSeconds =
				NextColumnState.LastRearrangementTimeSeconds;
			if (bReassignTransitSlots)
			{
				AssignFormationSlots(
					Formation,
					AuthorityState->Soldiers,
					AuthorityState->SoldierIndexById,
					MemberSpacingCentimeters,
					Formation.BatchOrderId,
					Formation.TransitColumnCount);
				for (const int32 SoldierIndex : ActiveIndices)
				{
					FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
					Soldier.NoProgressSeconds = 0.0f;
					Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
					Soldier.LastProgressPathPointIndex = INDEX_NONE;
				}
			}
		}
		Formation.bLastMovementStepSucceeded = true;

		for (const int32 SoldierIndex : ActiveIndices)
		{
			FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
			FormationBySoldierId.Add(Soldier.SoldierId.Value, &Formation);
			if (Soldier.NavigationState != EGuLiSoldierNavigationState::Normal)
			{
				Formation.bLastMovementStepSucceeded = false;
			}
			const FNavLocation* FinalDestination = Formation.FinalDestinationBySoldierId.Find(
				Soldier.SoldierId.Value);
			if (!FinalDestination)
			{
				SetTerminalNavigationState(
					Soldier,
					EGuLiSoldierNavigationState::Blocked,
					EGuLiSoldierNavigationFailure::FinalSlotInvalidated);
				continue;
			}
			if (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
				&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(
					Soldier.ConsecutiveSurfaceFailures,
					Soldier.NoProgressSeconds,
					SurfaceFailuresBeforeCenterline,
					CenterlineRecoverySeconds))
			{
				EnterCenterlineRecovery(Soldier);
			}
			else if (Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
				&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(
					Soldier.TotalSurfaceFailures,
					Soldier.NoProgressSeconds,
					SurfaceFailuresBeforePersonalPath,
					CenterlineRecoverySeconds))
			{
				EnterPersonalPathRecovery(Soldier);
			}
			if (!bRunsMovementUpdate[SoldierIndex])
			{
				bReceivesAvoidance[SoldierIndex] = Soldier.NavigationState
					== EGuLiSoldierNavigationState::Normal;
				continue;
			}
			++AuthorityState->MovementUpdateCalls;
			AuthorityState->ForcedMovementUpdateCalls +=
				bForcedMovementUpdate[SoldierIndex] ? 1u : 0u;

			int32* MemberPathPointIndex = Formation.MemberPathPointIndexBySoldierId.Find(
				Soldier.SoldierId.Value);
			if (!MemberPathPointIndex)
			{
				MemberPathPointIndex = &Formation.MemberPathPointIndexBySoldierId.Add(
					Soldier.SoldierId.Value,
					Formation.PathPoints.Num() > 1 ? 1 : 0);
			}
			*MemberPathPointIndex = GuLiCommanderNavigationPolicy::AdvanceMemberPathPointIndex(
				Formation.PathPoints,
				*MemberPathPointIndex,
				Soldier.Location,
				MemberAgentRadiusCentimeters,
				0.5f * static_cast<float>(Formation.TransitColumnCount - 1)
					* MemberSpacingCentimeters + MemberAgentRadiusCentimeters);

			FVector WorldTarget = Soldier.Location;
			FVector DesiredVelocity = FVector::ZeroVector;
			int32 ProgressPathIndex = *MemberPathPointIndex;
			const bool bSharedPathAtLastPoint = *MemberPathPointIndex
				>= Formation.PathPoints.Num() - 1;
			bool bCanApproachFinalSlot = Formation.bFinalApproachStarted
				&& bSharedPathAtLastPoint;

			if (Soldier.NavigationState == EGuLiSoldierNavigationState::PersonalPathRecovery)
			{
				if (!Soldier.PersonalPathPoints.IsEmpty())
				{
					Soldier.PersonalPathPointIndex = FMath::Clamp(
						Soldier.PersonalPathPointIndex,
						0,
						Soldier.PersonalPathPoints.Num() - 1);
					while (Soldier.PersonalPathPointIndex < Soldier.PersonalPathPoints.Num() - 1
						&& FVector::DistSquared2D(
							Soldier.Location,
							Soldier.PersonalPathPoints[Soldier.PersonalPathPointIndex])
							<= FMath::Square(FormationWaypointToleranceCentimeters))
					{
						++Soldier.PersonalPathPointIndex;
					}
					WorldTarget = Soldier.PersonalPathPoints[Soldier.PersonalPathPointIndex];
					DesiredVelocity = (WorldTarget - Soldier.Location).GetSafeNormal()
						* MovementSpeedCentimetersPerSecond;
					ProgressPathIndex = 100000 + Soldier.PersonalPathPointIndex;
					bCanApproachFinalSlot = Soldier.PersonalPathPointIndex
						>= Soldier.PersonalPathPoints.Num() - 1;
				}
				else
				{
					WorldTarget = FinalDestination->Location;
					ProgressPathIndex = 100000;
				}
			}
			else if (Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery)
			{
				WorldTarget = Formation.PathPoints[*MemberPathPointIndex];
				DesiredVelocity = (WorldTarget - Soldier.Location).GetSafeNormal()
					* MovementSpeedCentimetersPerSecond;
			}
			else if (bCanApproachFinalSlot)
			{
				WorldTarget = FinalDestination->Location;
				DesiredVelocity = (WorldTarget - Soldier.Location).GetSafeNormal()
					* MovementSpeedCentimetersPerSecond;
			}
			else
			{
				const uint8* TransitSlotIndex = Formation.SlotBySoldierId.Find(
					Soldier.SoldierId.Value);
				const float LaneOffset = TransitSlotIndex
					? MakeFormationSlotOffset(
						static_cast<int32>(*TransitSlotIndex),
						MemberSpacingCentimeters,
						Formation.TransitColumnCount).Y
					: 0.0f;
				const FVector LaneWaypoint = GuLiCommanderNavigationPolicy::CalculatePathLaneWaypoint(
					Formation.PathPoints,
					FMath::Clamp(*MemberPathPointIndex, 1, Formation.PathPoints.Num() - 1),
					LaneOffset);
				FVector TravelDirection = (LaneWaypoint - Soldier.Location).GetSafeNormal();
				if (bEnableLocalFlowField && Formation.FlowField.IsValid())
				{
					FVector FlowDirection;
					if (Formation.FlowField->SampleDirection(Soldier.Location, FlowDirection)
						&& !FlowDirection.IsNearlyZero())
					{
						TravelDirection = FlowDirection;
					}
				}
				FVector SlotCorrectionTarget = Formation.GuideAnchor;
				FVector LocalSlotOffset = FVector::ZeroVector;
				if (TransitSlotIndex)
				{
					LocalSlotOffset = MakeFormationSlotOffset(
						static_cast<int32>(*TransitSlotIndex),
						MemberSpacingCentimeters,
						Formation.TransitColumnCount);
					SlotCorrectionTarget += FRotator(
						0.0f,
						Formation.TravelFacingYawDegrees,
						0.0f).RotateVector(LocalSlotOffset);
				}
				const FVector SlotVelocity = (SlotCorrectionTarget - Soldier.Location)
					.GetClampedToMaxSize(MovementSpeedCentimetersPerSecond * SlotCorrectionWeight);
				DesiredVelocity = (TravelDirection
						* MovementSpeedCentimetersPerSecond * TravelWeight
						+ SlotVelocity)
					.GetClampedToMaxSize(MovementSpeedCentimetersPerSecond);
				WorldTarget = LaneWaypoint;
				if (EntityManager.IsEntityValid(Soldier.Entity))
				{
					FGuLiMassSlotTargetFragment& SlotTarget = EntityManager
						.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(Soldier.Entity);
					SlotTarget.LocalOffset = LocalSlotOffset;
					SlotTarget.WorldTarget = SlotCorrectionTarget;
				}
			}

			const float ArrivalSnapDistance = FMath::Max(
				100.0f,
				MovementSpeedCentimetersPerSecond
					* MovementUpdateDeltaSeconds[SoldierIndex]);
			if (bRunsMovementUpdate[SoldierIndex]
				&& bCanApproachFinalSlot
				&& FVector::Dist2D(Soldier.Location, FinalDestination->Location)
					<= ArrivalSnapDistance)
			{
				FNavLocation SurfaceDestination;
				++AuthorityState->SurfaceMoveCalls;
				const bool bFinalSurfaceMoveSucceeded = CommanderNavigationData->FindMoveAlongSurface(
					Soldier.LastValidNavLocation,
					FinalDestination->Location,
					SurfaceDestination,
					nullptr,
					this);
				const bool bFinalSurfaceMoveAccepted =
					GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
						bFinalSurfaceMoveSucceeded,
						Soldier.LastValidNavLocation.Location,
						SurfaceDestination.Location,
						MaximumSurfaceStepZCentimeters);
				const bool bFinalSegmentValid = bFinalSurfaceMoveAccepted
					&& FVector::DistSquared2D(
						SurfaceDestination.Location,
						FinalDestination->Location) <= FMath::Square(10.0f)
					&& FMath::Abs(
						SurfaceDestination.Location.Z - FinalDestination->Location.Z) <= 10.0f;
				if (bFinalSegmentValid)
				{
					SurfaceDestination.Location.X = FinalDestination->Location.X;
					SurfaceDestination.Location.Y = FinalDestination->Location.Y;
					Soldier.FinalDestination = SurfaceDestination;
					Formation.FinalDestinationBySoldierId.Add(
						Soldier.SoldierId.Value,
						SurfaceDestination);
					SetTerminalNavigationState(
						Soldier,
						EGuLiSoldierNavigationState::Arrived,
						EGuLiSoldierNavigationFailure::None);
					continue;
				}
				++AuthorityState->SurfaceMoveFailures;
				++Soldier.ConsecutiveSurfaceFailures;
				++Soldier.TotalSurfaceFailures;
				Formation.bLastMovementStepSucceeded = false;
				Soldier.NavigationFailure = bFinalSurfaceMoveSucceeded
					&& !bFinalSurfaceMoveAccepted
					? EGuLiSoldierNavigationFailure::ExcessiveHeightDelta
					: EGuLiSoldierNavigationFailure::SurfaceMoveFailed;
				DesiredVelocity = FVector::ZeroVector;
				Soldier.Velocity = FVector::ZeroVector;
				bSuppressMovementForStep[SoldierIndex] = true;
				if (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
					&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(
						Soldier.ConsecutiveSurfaceFailures,
						Soldier.NoProgressSeconds,
						SurfaceFailuresBeforeCenterline,
						CenterlineRecoverySeconds))
				{
					EnterCenterlineRecovery(Soldier);
				}
				else if (Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
					&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(
						Soldier.TotalSurfaceFailures,
						Soldier.NoProgressSeconds,
						SurfaceFailuresBeforePersonalPath,
						CenterlineRecoverySeconds))
				{
					EnterPersonalPathRecovery(Soldier);
				}
			}

			Soldier.CurrentNavigationWaypoint = WorldTarget;
			if (bRunsMovementUpdate[SoldierIndex])
			{
				const float DistanceToWaypoint = FVector::Dist(Soldier.Location, WorldTarget);
				const bool bHasProgressBaseline =
					FMath::IsFinite(Soldier.BestWaypointDistanceCentimeters);
				const bool bMadeMeaningfulProgress = bHasProgressBaseline
					&& GuLiCommanderNavigationPolicy::HasMeaningfulNavigationProgress(
						Soldier.LastProgressPathPointIndex,
						ProgressPathIndex,
						Soldier.BestWaypointDistanceCentimeters,
						DistanceToWaypoint,
						ProgressDistanceCentimeters);
				if (!bHasProgressBaseline || bMadeMeaningfulProgress)
				{
					Soldier.NoProgressSeconds = 0.0f;
					Soldier.BestWaypointDistanceCentimeters = DistanceToWaypoint;
					Soldier.LastProgressPathPointIndex = ProgressPathIndex;
				}
				else
				{
					Soldier.NoProgressSeconds += MovementUpdateDeltaSeconds[SoldierIndex];
					Soldier.BestWaypointDistanceCentimeters = FMath::Min(
						Soldier.BestWaypointDistanceCentimeters,
						DistanceToWaypoint);
				}
			}

			DesiredVelocities[SoldierIndex] = DesiredVelocity;
			bReceivesAvoidance[SoldierIndex] = Soldier.NavigationState
				== EGuLiSoldierNavigationState::Normal;
			if (EntityManager.IsEntityValid(Soldier.Entity))
			{
				FMassMoveTargetFragment& MoveTarget = EntityManager
					.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				MoveTarget.Center = WorldTarget;
				MoveTarget.Forward = DesiredVelocity.GetSafeNormal2D();
				MoveTarget.DistanceToGoal = FVector::Dist2D(Soldier.Location, FinalDestination->Location);
				MoveTarget.DesiredSpeed = FMassInt16Real(
					DesiredVelocity.IsNearlyZero(1.0f)
						? 0.0f
						: MovementSpeedCentimetersPerSecond);
			}
		}
	}

	const bool bPeriodicManualAvoidanceRefresh =
		AuthorityState->ServerSimTick
			% GuLiCommanderNavigationPolicy::MovementUpdateIntervalTicks == 0u;
	const bool bManualAvoidanceStorageChanged =
		AuthorityState->CachedManualAvoidanceVelocities.Num()
			< AuthorityState->Soldiers.Num();
	if (bPeriodicManualAvoidanceRefresh
		|| AuthorityState->bForceManualAvoidanceRefresh
		|| bManualAvoidanceStorageChanged)
	{
		const UGuLiDynamicObstacleRegistrySubsystem* ObstacleRegistry =
			GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
		check(ObstacleRegistry);
		const TConstArrayView<FGuLiDynamicObstacle> DynamicObstacles =
			ObstacleRegistry->GetObstacles();
		AuthorityState->ManualAvoidanceAgents.SetNum(
			AuthorityState->Soldiers.Num() + DynamicObstacles.Num());
		bool bAnySoldierReceivesAvoidance = false;
		for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
		{
			const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
			GuLiCommanderNavigationPolicy::FManualAvoidanceAgent& Agent =
				AuthorityState->ManualAvoidanceAgents[SoldierIndex];
			Agent.StableSoldierId = Soldier.SoldierId.Value;
			Agent.Location = Soldier.Location;
			Agent.bParticipates = Soldier.IsPresent() && !Soldier.Location.ContainsNaN();
			Agent.bReceivesAvoidance = Agent.bParticipates
				&& bReceivesAvoidance[SoldierIndex];
			Agent.RadiusCentimeters = MemberAgentRadiusCentimeters;
			bAnySoldierReceivesAvoidance |= Agent.bReceivesAvoidance;
		}
		float AvoidanceCellSizeCentimeters = MemberAgentRadiusCentimeters * 2.0f;
		for (int32 ObstacleIndex = 0; ObstacleIndex < DynamicObstacles.Num(); ++ObstacleIndex)
		{
			const FGuLiDynamicObstacle& Obstacle = DynamicObstacles[ObstacleIndex];
			GuLiCommanderNavigationPolicy::FManualAvoidanceAgent& Agent =
				AuthorityState->ManualAvoidanceAgents[AuthorityState->Soldiers.Num() + ObstacleIndex];
			Agent.StableSoldierId = 0x80000000u | Obstacle.Handle.Value;
			Agent.Location = Obstacle.Location;
			Agent.bParticipates = true;
			Agent.bReceivesAvoidance = false;
			Agent.RadiusCentimeters = Obstacle.RadiusCentimeters;
			AvoidanceCellSizeCentimeters = FMath::Max(
				AvoidanceCellSizeCentimeters,
				MemberAgentRadiusCentimeters + Obstacle.RadiusCentimeters);
		}

		if (bAnySoldierReceivesAvoidance)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_ManualAvoidanceRefresh);
			const float MinimumAvoidanceDistanceCentimeters = FMath::Max(
				1.0f,
				MemberAgentRadiusCentimeters * 2.0f);
			const GuLiCommanderNavigationPolicy::FManualAvoidanceMetrics Metrics =
				GuLiCommanderNavigationPolicy::BuildManualAvoidanceVelocities(
					AuthorityState->ManualAvoidanceAgents,
					AvoidanceCellSizeCentimeters,
					MinimumAvoidanceDistanceCentimeters,
					AvoidanceAgentHeightCentimeters,
					MovementSpeedCentimetersPerSecond,
					ManualAvoidanceStrength,
					AuthorityState->ManualAvoidanceSpatialGrid,
					AuthorityState->CachedManualAvoidanceVelocities);
			++AuthorityState->ManualAvoidanceRefreshes;
			AuthorityState->ManualAvoidanceCandidatePairs += Metrics.CandidatePairs;
			AuthorityState->ManualAvoidanceOverlapPairs += Metrics.OverlapPairs;
			AuthorityState->MaximumManualAvoidanceBucketOccupancy = FMath::Max(
				AuthorityState->MaximumManualAvoidanceBucketOccupancy,
				Metrics.MaximumBucketOccupancy);
		}
		else
		{
			AuthorityState->ManualAvoidanceSpatialGrid.Reset();
			AuthorityState->CachedManualAvoidanceVelocities.Init(
				FVector::ZeroVector,
				AuthorityState->Soldiers.Num());
		}
		AuthorityState->bForceManualAvoidanceRefresh = false;
	}

	TArray<int32> PersonalPathQueryCandidates;
	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (!Soldier.IsAlive() || Soldier.ActiveOrderId == 0u
			|| Soldier.NavigationState != EGuLiSoldierNavigationState::PersonalPathRecovery)
		{
			continue;
		}
		if (GuLiCommanderNavigationPolicy::ShouldBlockPersonalPathRecovery(
				Soldier.PersonalPathRetries,
				Soldier.NoProgressSeconds,
				2,
				PersonalRecoveryRetrySeconds))
		{
			SetTerminalNavigationState(
				Soldier,
				EGuLiSoldierNavigationState::Blocked,
				EGuLiSoldierNavigationFailure::PersonalPathFailed);
			continue;
		}
		const bool bNeedsInitialQuery = Soldier.PersonalPathRetries == 0;
		const bool bNeedsSingleRetry = Soldier.PersonalPathRetries == 1
			&& Soldier.NoProgressSeconds >= PersonalRecoveryRetrySeconds;
		if (bNeedsInitialQuery || bNeedsSingleRetry)
		{
			PersonalPathQueryCandidates.Add(SoldierIndex);
		}
	}
	PersonalPathQueryCandidates.Sort([this](const int32 Left, const int32 Right)
	{
		return AuthorityState->Soldiers[Left].SoldierId
			< AuthorityState->Soldiers[Right].SoldierId;
	});
	const int32 PathQueryCount = FMath::Min(
		MaximumPersonalPathQueriesPerStep,
		PersonalPathQueryCandidates.Num());
	for (int32 QueryIndex = 0; QueryIndex < PathQueryCount; ++QueryIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[PersonalPathQueryCandidates[QueryIndex]];
		if (!NavigationSystem || !CommanderNavigationData || !Soldier.bHasFinalDestination)
		{
			continue;
		}
		Soldier.PersonalPathPoints.Reset();
		Soldier.PersonalPathPointIndex = 0;
		Soldier.NoProgressSeconds = 0.0f;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		++Soldier.PersonalPathRetries;
		++AuthorityState->PathQueries;
		++AuthorityState->PersonalPathQueries;
		FPathFindingQuery Query(
			nullptr,
			*CommanderNavigationData,
			Soldier.LastValidNavLocation.Location,
			Soldier.FinalDestination.Location);
		const FPathFindingResult Result = NavigationSystem->FindPathSync(MoveTemp(Query));
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
		{
			continue;
		}
		for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
		{
			Soldier.PersonalPathPoints.Add(Point.Location);
		}
		if (!Soldier.PersonalPathPoints.IsEmpty())
		{
			Soldier.PersonalPathPointIndex = Soldier.PersonalPathPoints.Num() > 1 ? 1 : 0;
		}
	}

	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}

		if (Soldier.bExternalActionsLocked && Soldier.IsAlive()) continue;
		FGuLiMassHealthFragment& Health = EntityManager
			.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
		if (!Soldier.IsAlive())
		{
			Health.WreckSecondsRemaining = FMath::Max(
				0.0f,
				static_cast<float>(Soldier.DeathSimulationSeconds + WreckLifetimeSeconds
					- AuthorityState->SimulationSeconds));
			if (!Soldier.bWreckExpired && Health.WreckSecondsRemaining <= 0.0f)
			{
				FTransformFragment& Transform = EntityManager
					.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
				FTransform HiddenTransform = Transform.GetTransform();
				HiddenTransform.SetScale3D(FVector::ZeroVector);
				Transform.SetTransform(HiddenTransform);
				Soldier.bWreckExpired = true;
			}
			continue;
		}

		const bool bHasMovingOrder = Soldier.ActiveOrderId != 0u
			&& (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
				|| Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
				|| Soldier.NavigationState == EGuLiSoldierNavigationState::PersonalPathRecovery);
		bSuppressMovementForStep[SoldierIndex] = bSuppressMovementForStep[SoldierIndex] || Soldier.bAttackMoveHolding;
		if (bHasMovingOrder
			&& bRunsMovementUpdate[SoldierIndex]
			&& !bSuppressMovementForStep[SoldierIndex])
		{
			const float MovementDeltaSeconds = MovementUpdateDeltaSeconds[SoldierIndex];
			FVector AvoidanceVelocity = FVector::ZeroVector;
			if (bReceivesAvoidance[SoldierIndex])
			{
				if (AuthorityState->CachedManualAvoidanceVelocities.IsValidIndex(SoldierIndex))
				{
					AvoidanceVelocity +=
						AuthorityState->CachedManualAvoidanceVelocities[SoldierIndex];
				}
				const FGuLiMassAvoidanceOutputFragment& AvoidanceOutput = EntityManager
					.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity);
				AvoidanceVelocity += AvoidanceOutput.Value.GetClampedToMaxSize(
					MovementSpeedCentimetersPerSecond * 4.0f) * MovementDeltaSeconds;
			}

			const FVector TargetVelocity = (DesiredVelocities[SoldierIndex] + AvoidanceVelocity)
				.GetClampedToMaxSize(MovementSpeedCentimetersPerSecond);
			Soldier.Velocity = TargetVelocity.IsNearlyZero(1.0f)
				? FVector::ZeroVector
				: FMath::VInterpTo(
					Soldier.Velocity,
					TargetVelocity,
					MovementDeltaSeconds,
					8.0f);

			if (!Soldier.Velocity.IsNearlyZero(1.0f))
			{
				const FVector CandidateLocation = Soldier.LastValidNavLocation.Location
					+ Soldier.Velocity * MovementDeltaSeconds;
				FNavLocation SurfaceLocation;
				++AuthorityState->SurfaceMoveCalls;
				const bool bSurfaceMoveSucceeded = CommanderNavigationData
					&& CommanderNavigationData->FindMoveAlongSurface(
						Soldier.LastValidNavLocation,
						CandidateLocation,
						SurfaceLocation,
						nullptr,
						this);
				const bool bSurfaceMoveAccepted =
					GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
						bSurfaceMoveSucceeded,
						Soldier.LastValidNavLocation.Location,
						SurfaceLocation.Location,
						MaximumSurfaceStepZCentimeters);
				if (bSurfaceMoveAccepted)
				{
					Soldier.Location = SurfaceLocation.Location;
					Soldier.LastValidNavLocation = SurfaceLocation;
					Soldier.ConsecutiveSurfaceFailures = 0;
					Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
					if (!Soldier.Velocity.IsNearlyZero(1.0f))
					{
						Soldier.FacingYawDegrees = FMath::FixedTurn(
							Soldier.FacingYawDegrees,
							Soldier.Velocity.GetSafeNormal2D().Rotation().Yaw,
							FacingRateDegreesPerSecond * MovementDeltaSeconds);
					}
				}
				else
				{
					Soldier.Location = Soldier.LastValidNavLocation.Location;
					Soldier.Velocity = FVector::ZeroVector;
					++AuthorityState->SurfaceMoveFailures;
					++Soldier.ConsecutiveSurfaceFailures;
					++Soldier.TotalSurfaceFailures;
					if (FOrderFormationRuntime* const* Formation =
						FormationBySoldierId.Find(Soldier.SoldierId.Value))
					{
						(*Formation)->bLastMovementStepSucceeded = false;
					}
					Soldier.NavigationFailure = bSurfaceMoveSucceeded
						? EGuLiSoldierNavigationFailure::ExcessiveHeightDelta
						: EGuLiSoldierNavigationFailure::SurfaceMoveFailed;
					if (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
						&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(
							Soldier.ConsecutiveSurfaceFailures,
							Soldier.NoProgressSeconds,
							SurfaceFailuresBeforeCenterline,
							CenterlineRecoverySeconds))
					{
						EnterCenterlineRecovery(Soldier);
					}
					else if (Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
						&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(
							Soldier.TotalSurfaceFailures,
							Soldier.NoProgressSeconds,
							SurfaceFailuresBeforePersonalPath,
							CenterlineRecoverySeconds))
					{
						EnterPersonalPathRecovery(Soldier);
					}
				}
			}
		}
		else if (!bHasMovingOrder || bSuppressMovementForStep[SoldierIndex])
		{
			Soldier.Velocity = FVector::ZeroVector;
		}

		FTransformFragment& Transform = EntityManager
			.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
		Transform.SetTransform(FTransform(
			FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f),
			Soldier.Location));
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = Soldier.Velocity;
		EntityManager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		FGuLiMassOrderFragment& Order = EntityManager
			.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = Soldier.ActiveOrderId;
		Order.OrderRevision = Soldier.StateRevision;
		Order.bHasMoveTarget = Soldier.ActiveOrderId != 0u;
	}

	for (int32 FormationIndex = AuthorityState->OrderFormations.Num() - 1;
		FormationIndex >= 0;
		--FormationIndex)
	{
		const FOrderFormationRuntime& Formation = AuthorityState->OrderFormations[FormationIndex];
		const bool bAnyMemberStillExecuting = Formation.MemberIds.ContainsByPredicate(
			[this, &Formation](const FGuLiSoldierId SoldierId)
			{
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
				return SoldierIndex
					&& AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
					&& AuthorityState->Soldiers[*SoldierIndex].CanAct()
					&& AuthorityState->Soldiers[*SoldierIndex].ActiveOrderId
						== Formation.BatchOrderId;
			});
		if (!bAnyMemberStillExecuting)
		{
			AuthorityState->OrderFormations.RemoveAtSwap(
				FormationIndex,
				1,
				EAllowShrinking::No);
		}
	}

	// Movement, arrival and recovery state are authoritative before combat samples positions.
	TickSoldierCombat();
	RetireExpiredSoldiers();
#if !UE_BUILD_SHIPPING
	const double StepMilliseconds = (FPlatformTime::Seconds() - StepStartedAt) * 1000.0;
	++PerformanceCounters.Steps;
	PerformanceCounters.SimulationMilliseconds += StepMilliseconds;
	PerformanceCounters.MaxSimulationMilliseconds = FMath::Max(
		PerformanceCounters.MaxSimulationMilliseconds,
		StepMilliseconds);
#endif
}
// 解析点/框/范围/同兵种意图；最终成员的位置、阵营、兵种和存活均以权威记录为准。
// 请求限流、去重与 ACK 重放在 NetSyncComponent；这里再次检查权限、结构与选择版本。
bool UGuLiBattleAuthoritySubsystem::ResolveSelection(
	const AGuLiBattlePlayerState& PlayerState,
	const FGuLiSelectionRequest& Request,
	FGuLiCommanderSelectionState& InOutSelection,
	FGuLiCommandAck& OutAck)
{
	using namespace GuLiCommanderMassPrivate;

	OutAck.ClientCommandId = Request.ClientRequestId;
	OutAck.ServerSelectionRevision = InOutSelection.SelectionRevision;
	OutAck.CohortResults.Reset();
	if (!AuthorityState || !AuthorityState->bPopulationSpawned || !IsAuthorityWorld()
		|| !PlayerState.IsCommander() || !GuLiCommanderProtocol::IsPlayableTeam(PlayerState.GetTeam()))
	{
		OutAck.Result = EGuLiCommandAckResult::Unauthorized;
		return false;
	}
	if (!Request.IsWellFormed())
	{
		OutAck.Result = EGuLiCommandAckResult::InvalidRequest;
		return false;
	}
	if (Request.KnownSelectionRevision != InOutSelection.SelectionRevision)
	{
		OutAck.Result = EGuLiCommandAckResult::StaleSelectionRevision;
		return false;
	}

	// 在副本上刷新和编组，最后统一提交选择结果；成员变动才推进 SelectionRevision。
	FGuLiCommanderSelectionState WorkingSelection = InOutSelection;
	RefreshSelection(PlayerState.GetTeam(), WorkingSelection);
	const TArray<FGuLiControlCohortDescriptor> PreviousCohorts = WorkingSelection.Cohorts;
	const TArray<FGuLiControllableActorId> PreviousActorIds = WorkingSelection.ActorIds;

	TArray<GuLiCommanderSelectionQuery::FCandidate> Population;
	Population.Reserve(AuthorityState->Soldiers.Num());
	for (const FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		Population.Add({Soldier.SoldierId, Soldier.Location, Soldier.Velocity,
			Soldier.Team, Soldier.UnitTypeId, Soldier.CanAct()});
	}
	TArray<FGuLiSoldierId> HitIds;
	if (!GuLiCommanderSelectionQuery::ResolveCandidates(Request, PlayerState.GetTeam(), Population, HitIds))
	{
		OutAck.Result = EGuLiCommandAckResult::InvalidTarget;
		return false;
	}
	const UGuLiCommanderResourceAdapter* ResourceAdapter =
		GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
	check(ResourceAdapter);
	TArray<FGuLiControllableActorId> NewActorIds;
	if (!ResourceAdapter->ResolveActorSelection(
		Request, PlayerState.GetTeam(), WorkingSelection.ActorIds, NewActorIds))
	{
		OutAck.Result = EGuLiCommandAckResult::InvalidTarget;
		return false;
	}
	WorkingSelection.ActorIds = MoveTemp(NewActorIds);

	TArray<FGuLiSoldierId> ExistingIds;
	for (const FGuLiControlCohortDescriptor& Cohort : WorkingSelection.Cohorts)
	{
		for (const FGuLiSoldierId Id : Cohort.MemberIds)
		{
			const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
			if (Index && AuthorityState->Soldiers.IsValidIndex(*Index)
				&& AuthorityState->Soldiers[*Index].CanAct())
			{
				ExistingIds.Add(Id);
			}
		}
	}
	ExistingIds.Sort();
	// Legacy native QA can still request Toggle. Player Shift sends Add and never enters this branch.
	if (Request.Modifier == EGuLiSelectionModifier::Toggle)
	{
		const TSet<FGuLiSoldierId> OriginalHits(HitIds);
		for (const FGuLiControlCohortDescriptor& Cohort : WorkingSelection.Cohorts)
		{
			if (Cohort.MemberIds.ContainsByPredicate([&OriginalHits](const FGuLiSoldierId Id)
				{ return OriginalHits.Contains(Id); }))
			{
				for (const FGuLiSoldierId Id : Cohort.MemberIds)
				{
					if (ExistingIds.Contains(Id))
					{
						HitIds.AddUnique(Id);
					}
				}
			}
		}
	}
	TArray<FGuLiSoldierId> NewIds;
	GuLiCommanderSelectionQuery::CombineMembership(ExistingIds, HitIds, Request.Modifier, NewIds);
	// Membership equality preserves cohort IDs/revision for repeated Shift hits. Changed unions
	// are regrouped together so hundreds of one-person additions cannot consume 400 cohort slots.
	if (NewIds != ExistingIds)
	{
		WorkingSelection.Cohorts.Reset();
		TArray<GuLiControlCohortBuilder::FCandidate> SelectedCandidates;
		SelectedCandidates.Reserve(NewIds.Num());
		for (const FGuLiSoldierId Id : NewIds)
		{
			const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
			if (Index && AuthorityState->Soldiers.IsValidIndex(*Index))
			{
				SelectedCandidates.Add({Id, AuthorityState->Soldiers[*Index].Location});
			}
		}
		TArray<TArray<FGuLiSoldierId>> BuiltCohorts;
		// All shapes select exact hits. Cohort partitioning never adds unselected neighbours.
		GuLiControlCohortBuilder::Build(SelectedCandidates, {}, 0.0f, BuiltCohorts);
		for (TArray<FGuLiSoldierId>& MemberIds : BuiltCohorts)
		{
			FGuLiControlCohortDescriptor& Descriptor = WorkingSelection.Cohorts.AddDefaulted_GetRef();
			Descriptor.CohortId = FGuLiControlCohortId(AllocateNonZero(AuthorityState->NextControlCohortId));
			Descriptor.MemberIds = MoveTemp(MemberIds);
			Descriptor.AliveCount = static_cast<uint8>(Descriptor.MemberIds.Num());
			TArray<uint32, TInlineAllocator<GuLiCommanderMassPrivate::SoldierCountPerFormation>> ActiveOrderIds;
			for (const FGuLiSoldierId Id : Descriptor.MemberIds)
			{
				const uint32 ActiveOrder = AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)].ActiveOrderId;
				ActiveOrderIds.Add(ActiveOrder);
			}
			Descriptor.ActiveOrderId =
				GuLiCommanderNavigationPolicy::ResolveCommonActiveOrderId(ActiveOrderIds);
		}
	}

	WorkingSelection.Sanitize();
	bool bMembershipChanged = PreviousCohorts.Num() != WorkingSelection.Cohorts.Num()
		|| PreviousActorIds != WorkingSelection.ActorIds;
	if (!bMembershipChanged)
	{
		for (int32 Index = 0; Index < PreviousCohorts.Num(); ++Index)
		{
			if (PreviousCohorts[Index].CohortId != WorkingSelection.Cohorts[Index].CohortId
				|| PreviousCohorts[Index].MemberIds != WorkingSelection.Cohorts[Index].MemberIds)
			{
				bMembershipChanged = true;
				break;
			}
		}
	}
	if (bMembershipChanged)
	{
		++WorkingSelection.SelectionRevision;
		if (WorkingSelection.SelectionRevision == 0u)
		{
			++WorkingSelection.SelectionRevision;
		}
	}
	WorkingSelection.AcceptedClientRequestId = Request.ClientRequestId;
	InOutSelection = MoveTemp(WorkingSelection);
	OutAck.ServerSelectionRevision = InOutSelection.SelectionRevision;
	OutAck.Result = EGuLiCommandAckResult::Accepted;
	return true;
}

// 保持已有控制组身份并刷新摘要：去掉无效/重复/异阵营成员，整组无人存活才移除。
// 部分死亡成员仍留在 MemberIds；AliveCount 与共同 ActiveOrderId 是独立的动态摘要。
bool UGuLiBattleAuthoritySubsystem::RefreshSelection(
	const EGuLiTeam Team,
	FGuLiCommanderSelectionState& InOutSelection) const
{
	if (!AuthorityState || !GuLiCommanderProtocol::IsPlayableTeam(Team))
	{
		return false;
	}

	FGuLiCommanderSelectionState Refreshed = InOutSelection;
	Refreshed.Cohorts.Reset();
	TSet<uint32> SeenSoldiers;
	bool bMembershipChanged = false;
	for (const FGuLiControlCohortDescriptor& Existing : InOutSelection.Cohorts)
	{
		FGuLiControlCohortDescriptor Descriptor;
		Descriptor.CohortId = Existing.CohortId;
		TArray<uint32, TInlineAllocator<GuLiCommanderMassPrivate::SoldierCountPerFormation>> ActiveOrderIds;
		for (const FGuLiSoldierId SoldierId : Existing.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
				|| SeenSoldiers.Contains(SoldierId.Value))
			{
				bMembershipChanged = true;
				continue;
			}
			const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
			if (Soldier.Team != Team)
			{
				bMembershipChanged = true;
				continue;
			}
			SeenSoldiers.Add(SoldierId.Value);
			Descriptor.MemberIds.Add(SoldierId);
			if (Soldier.IsAlive())
			{
				++Descriptor.AliveCount;
				ActiveOrderIds.Add(Soldier.ActiveOrderId);
			}
		}

		if (Descriptor.AliveCount == 0u || Descriptor.MemberIds.IsEmpty())
		{
			bMembershipChanged = true;
			continue;
		}
		Descriptor.ActiveOrderId =
			GuLiCommanderNavigationPolicy::ResolveCommonActiveOrderId(ActiveOrderIds);
		Refreshed.Cohorts.Add(MoveTemp(Descriptor));
	}

	if (bMembershipChanged)
	{
		++Refreshed.SelectionRevision;
		if (Refreshed.SelectionRevision == 0u)
		{
			++Refreshed.SelectionRevision;
		}
	}
	Refreshed.Sanitize();
	const bool bChanged = !GuLiCommanderMassPrivate::AreSelectionsEqual(InOutSelection, Refreshed)
		|| InOutSelection.SelectionRevision != Refreshed.SelectionRevision;
	if (bChanged)
	{
		InOutSelection = MoveTemp(Refreshed);
	}
	return bChanged;
}

bool UGuLiBattleAuthoritySubsystem::BeginMovePlanning(
	const AGuLiBattlePlayerState& PlayerState,
	const FGuLiMoveRequest& Request,
	const FGuLiCommanderSelectionState& Selection,
	FGuLiCommandAck& OutImmediateAck)
{
	using namespace GuLiCommanderMassPrivate;
	using namespace GuLiCommanderDestinationPlanner;

	OutImmediateAck = FGuLiCommandAck{};
	OutImmediateAck.CommandKind = EGuLiCommandKind::Move;
	OutImmediateAck.ClientCommandId = Request.ClientCommandId;
	OutImmediateAck.ServerSelectionRevision = Selection.SelectionRevision;
	if (!AuthorityState || !AuthorityState->bPopulationSpawned || !IsAuthorityWorld()
		|| !PlayerState.IsCommander() || !GuLiCommanderProtocol::IsPlayableTeam(PlayerState.GetTeam()))
	{
		OutImmediateAck.Result = EGuLiCommandAckResult::Unauthorized;
		return false;
	}
	if (!Request.IsWellFormed())
	{
		OutImmediateAck.Result = EGuLiCommandAckResult::InvalidRequest;
		return false;
	}
	if (Request.SelectionRevision != Selection.SelectionRevision)
	{
		OutImmediateAck.Result = EGuLiCommandAckResult::StaleSelectionRevision;
		return false;
	}
	if (Selection.Cohorts.IsEmpty() && Selection.ActorIds.IsEmpty())
	{
		OutImmediateAck.Result = EGuLiCommandAckResult::NoSelection;
		return false;
	}
	if (!Request.TargetTerritoryId.IsNone())
	{
		FGuLiStrongholdTransitOrder Order;
		Order.RequestId = int32(Request.ClientCommandId); Order.SelectionRevision = int32(Request.SelectionRevision);
		Order.TerritoryId = Request.TargetTerritoryId; Order.ClickLocation = Request.Target;
		GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>()->IssueStrongholdTransit(
			PlayerState,Selection.ActorIds,Order,OutImmediateAck);
		return false; // Immediate actor-only acknowledgement; Mass keeps its orders.
	}
	if (Request.MiningOrderType != EGuLiMiningOrderType::Move)
	{
		if (Selection.ActorIds.IsEmpty())
		{
			OutImmediateAck.Result = EGuLiCommandAckResult::NoSelection;
			return false;
		}
		FGuLiMiningCommand Command;
		Command.RequestId = Request.ClientCommandId;
		Command.Type = Request.MiningOrderType;
		Command.Target = Request.Target;
		Command.ClusterId = Request.TargetClusterId;
		Command.SelectionRevision = Request.SelectionRevision;
		const UGuLiCommanderResourceAdapter* ResourceAdapter =
			GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
		check(ResourceAdapter);
		OutImmediateAck.Result = ResourceAdapter->IssueMiningCommand(
			PlayerState, Selection.ActorIds, Command)
			? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::InvalidTarget;
		return false;
	}
	if (Selection.Cohorts.IsEmpty())
	{
		FGuLiMiningCommand Command;
		Command.RequestId = Request.ClientCommandId;
		Command.Type = EGuLiMiningOrderType::Move;
		Command.Target = Request.Target;
		Command.SelectionRevision = Request.SelectionRevision;
		const UGuLiCommanderResourceAdapter* ResourceAdapter =
			GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
		check(ResourceAdapter);
		OutImmediateAck.Result = ResourceAdapter->IssueMiningCommand(
			PlayerState, Selection.ActorIds, Command)
			? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::PathFailed;
		return false;
	}

	for (const TUniquePtr<FMovePlanningJob>& Existing : AuthorityState->MovePlanningJobs)
	{
		if (!Existing || Existing->PlayerState.Get() != &PlayerState
			|| Existing->Request.ClientCommandId != Request.ClientCommandId)
		{
			continue;
		}
		if (Existing->Request.SelectionRevision != Request.SelectionRevision
			|| Existing->Request.MiningOrderType != Request.MiningOrderType
			|| Existing->Request.TargetClusterId != Request.TargetClusterId
			|| Existing->Request.TargetTerritoryId != Request.TargetTerritoryId
			|| !FVector(Existing->Request.Target).Equals(FVector(Request.Target), 0.01))
		{
			OutImmediateAck.Result = EGuLiCommandAckResult::InvalidRequest;
			return false;
		}
		OutImmediateAck = Existing->Ack;
		return true;
	}
	// Destination reservations are built in an isolated scratch table. Keep one in-flight
	// planner per team so two authority owners can never commit overlapping scratch results.
	// Red and Blue remain independent and may plan concurrently.
	for (const TUniquePtr<FMovePlanningJob>& Existing : AuthorityState->MovePlanningJobs)
	{
		if (Existing && Existing->Stage != EMovePlanningStage::Completed
			&& Existing->Team == PlayerState.GetTeam())
		{
			OutImmediateAck.Result = EGuLiCommandAckResult::RateLimited;
			return false;
		}
	}

	UWorld* World = GetWorld();
	const AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	const uint32 CurrentAuthorityEpoch = BattleGameState
		? BattleGameState->GetMatchEpoch()
		: 0u;
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	ANavigationData* NavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!World || !NavigationSystem || !NavigationData || CurrentAuthorityEpoch == 0u)
	{
		OutImmediateAck.Result = EGuLiCommandAckResult::PathFailed;
		return false;
	}

	TUniquePtr<FMovePlanningJob> NewJob = MakeUnique<FMovePlanningJob>();
	FMovePlanningJob& Job = *NewJob;
	Job.PlayerState = &PlayerState;
	if (const AController* OwningController = Cast<AController>(PlayerState.GetOwner()))
	{
		Job.OwningController = OwningController;
		Job.bRequireOwningController = true;
	}
	Job.Team = PlayerState.GetTeam();
	Job.Request = Request;
	Job.FrozenSelection = Selection;
	Job.UpdatedSelection = Selection;
	Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.AuthorityEpoch = CurrentAuthorityEpoch;
	Job.PlanningStartedAt = FPlatformTime::Seconds();
	Job.Ack.CommandKind = EGuLiCommandKind::Move;
	Job.Ack.ClientCommandId = Request.ClientCommandId;
	Job.Ack.ServerSelectionRevision = Selection.SelectionRevision;
	Job.Ack.Result = EGuLiCommandAckResult::InvalidRequest;
	Job.Debug.ClientCommandId = Request.ClientCommandId;
	Job.Debug.RequestedTarget = FVector(Request.Target);
	Job.Debug.MaximumSearchRadiusCentimeters = FreeDestinationMaximumRadiusCentimeters;

	TSet<uint32> RequestedEligibleIds;
	for (const FGuLiControlCohortDescriptor& SourceCohort : Selection.Cohorts)
	{
		FMoveCohortPlan& Cohort = Job.Cohorts.AddDefaulted_GetRef();
		Cohort.CohortId = SourceCohort.CohortId;
		const int32 MemberCount = FMath::Min(
			SourceCohort.MemberIds.Num(),
			static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));
		Cohort.FrozenMemberIds.Append(SourceCohort.MemberIds.GetData(), MemberCount);

		FGuLiCohortCommandAck& CohortAck = Job.Ack.CohortResults.AddDefaulted_GetRef();
		CohortAck.CohortId = SourceCohort.CohortId;
		CohortAck.MemberCount = static_cast<uint8>(MemberCount);
		CohortAck.Result = EGuLiCommandAckResult::NoSelection;
		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			FMoveMemberPlan& Member = Job.Members.AddDefaulted_GetRef();
			Member.SoldierId = SourceCohort.MemberIds[MemberIndex];
			Member.CohortId = SourceCohort.CohortId;
			Member.CohortMemberIndex = MemberIndex;
			Cohort.MemberPlanIndices.Add(Job.Members.Num() - 1);
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
			if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
				|| RequestedEligibleIds.Contains(Member.SoldierId.Value))
			{
				Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
				continue;
			}
			const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
			if (!Soldier.CanAct() || Soldier.Team != Job.Team)
			{
				Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
				continue;
			}
			Member.bEligible = true;
			Member.OldReservation = Soldier.bHasFinalDestination
				&& Soldier.ActiveOrderId != 0u
				? Soldier.FinalDestination.Location
				: Soldier.Location;
			RequestedEligibleIds.Add(Member.SoldierId.Value);
			CohortAck.EligibleMemberMask |= 1u << MemberIndex;
		}
	}

	for (const FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		if (!Soldier.CanAct() || Soldier.Team != Job.Team
			|| RequestedEligibleIds.Contains(Soldier.SoldierId.Value))
		{
			continue;
		}
		FMoveDestinationReservation Reservation;
		Reservation.OwnerSoldierId = Soldier.SoldierId.Value;
		Reservation.Location = Soldier.bHasFinalDestination && Soldier.ActiveOrderId != 0u
			? Soldier.FinalDestination.Location
			: Soldier.Location;
		AddHardReservationToMoveJob(Job, Reservation);
	}

	FHexCandidateRequest CandidateRequest;
	CandidateRequest.TargetAnchor = FVector(Request.Target);
	CandidateRequest.CandidatePitchCentimeters = DefaultFreeCandidatePitchCentimeters;
	CandidateRequest.MaximumRadiusCentimeters = FreeDestinationMaximumRadiusCentimeters;
	if (!BuildHexCandidates(CandidateRequest, Job.HexCandidates))
	{
		OutImmediateAck.Result = EGuLiCommandAckResult::InvalidTarget;
		return false;
	}
	Job.Debug.TheoreticalCandidates = Job.HexCandidates.Num();
	OutImmediateAck = Job.Ack;
	AuthorityState->MovePlanningJobs.Add(MoveTemp(NewJob));
	return true;
}
// 普通数值立即同步；最大生命变化按比例保留当前生命，速度则排队到下一个固定步提交。
int32 UGuLiBattleAuthoritySubsystem::ApplyRuntimeTuning(
	const FGuLiSoldierRuntimeTuningValues& Values)
{
	const bool bValuesValid = FMath::IsFinite(Values.MovementSpeedCmPerSecond)
		&& Values.MovementSpeedCmPerSecond > 0.0f
		&& Values.MovementSpeedCmPerSecond <= static_cast<float>(MAX_int16)
		&& FMath::IsFinite(Values.MaxHealth) && Values.MaxHealth > 0.0f && Values.MaxHealth <= 1.e9f
		&& FMath::IsFinite(Values.Defense) && Values.Defense >= 0.0f;
	if (!bValuesValid || !IsAuthorityWorld() || !AuthorityState)
	{
		UE_LOG(
			LogGuLiCommanderMass,
			Warning,
			TEXT("Rejected invalid or non-authority Soldier runtime tuning request."));
		return 0;
	}

	EffectiveRuntimeTuning = Values;
	if (!FMath::IsNearlyEqual(
		MovementSpeedCentimetersPerSecond,
		Values.MovementSpeedCmPerSecond))
	{
		PendingMovementSpeedCmPerSecond = Values.MovementSpeedCmPerSecond;
	}
	else
	{
		// 最后一次请求生效：同一步前先 Set(new) 再 Reset(baseline)，必须取消尚未提交的旧 Set。
		PendingMovementSpeedCmPerSecond.Reset();
	}

	if (!AuthorityState->bPopulationSpawned)
	{
		return 0;
	}
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!MassSubsystem)
	{
		return 0;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	const UGuLiCommanderDataSubsystem* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	for (GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}

		const FGuLiSoldierDefinition* Definition = Data ? Data->FindSoldierDefinition(Soldier.UnitTypeId) : nullptr;
		const float NewMaxHealth = Values.bOverrideMaxHealth ? Values.MaxHealth
			: (Definition ? Definition->MaxHealth : BaselineRuntimeTuning.MaxHealth);
		const float PreviousHealth = Soldier.Health;
		const bool bMaximumHealthChanged = Soldier.MaxHealth != NewMaxHealth;
		if (bMaximumHealthChanged)
		{
			Soldier.Health = GuLiRuntimeTuning::ScaleHealthPreservingRatio(
				Soldier.Health,
				Soldier.MaxHealth,
				NewMaxHealth);
		}
		Soldier.MaxHealth = NewMaxHealth;
		Soldier.Defense = Values.bOverrideDefense ? Values.Defense
			: (Definition ? Definition->Defense : BaselineRuntimeTuning.Defense);

		FGuLiMassSoldierStatsFragment& Stats = EntityManager
			.GetFragmentDataChecked<FGuLiMassSoldierStatsFragment>(Soldier.Entity);
		Stats.MaxHealth = Soldier.MaxHealth;
		Stats.Defense = Soldier.Defense;

		if (Soldier.Health != PreviousHealth || bMaximumHealthChanged)
		{
			++Soldier.StateRevision;
			FGuLiMassHealthFragment& Health = EntityManager
				.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
			Health.Health = Soldier.Health;
			Health.bDead = Soldier.Health <= 0.0f;
		}
	}
	return AuthorityState->Soldiers.Num();
}

// 通过 Even/Odd Archetype 迁移替换 const-shared 参数，迁移后重新取 Fragment，避免持有旧引用。
void UGuLiBattleAuthoritySubsystem::ApplyPendingMovementSpeed()
{
	using namespace GuLiCommanderMassPrivate;
	if (!PendingMovementSpeedCmPerSecond.IsSet() || !AuthorityState)
	{
		return;
	}

	const float NewMovementSpeed = PendingMovementSpeedCmPerSecond.GetValue();
	if (FMath::IsNearlyEqual(MovementSpeedCentimetersPerSecond, NewMovementSpeed))
	{
		PendingMovementSpeedCmPerSecond.Reset();
		return;
	}
	if (!AuthorityState->bPopulationSpawned)
	{
		// 尚未生成完整部队时保留待提交值；生成成功后由固定步开头再次调用本函数提交。
		return;
	}

	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!MassSubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	const FMassArchetypeHandle TargetBaseArchetype = AuthorityState->bUsingRuntimeTuningEvenArchetype
		? AuthorityState->RuntimeTuningOddBaseArchetype
		: AuthorityState->RuntimeTuningEvenBaseArchetype;
	if (!TargetBaseArchetype.IsValid())
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Cannot apply Soldier movement tuning: alternate Mass archetype is invalid."));
		return;
	}

	FMassArchetypeSharedFragmentValues SharedValues = MakeAuthoritySharedFragmentValues(
		EntityManager,
		NewMovementSpeed,
		MemberAgentRadiusCentimeters);
	const FMassArchetypeHandle TargetArchetype = EntityManager.GetOrCreateSuitableArchetype(
		TargetBaseArchetype,
		SharedValues.GetSharedFragmentBitSet(),
		SharedValues.GetConstSharedFragmentBitSet());
	if (!TargetArchetype.IsValid())
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Cannot apply Soldier movement tuning: target Mass archetype is invalid."));
		return;
	}

	int32 UpdatedEntityCount = 0;
	for (FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}
		EntityManager.MoveEntityToAnotherArchetype(
			Soldier.Entity,
			TargetArchetype,
			&SharedValues);

		Soldier.Velocity = Soldier.Velocity.GetClampedToMaxSize(NewMovementSpeed);
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = Soldier.Velocity;
		if (Soldier.ActiveOrderId != 0u)
		{
			EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity)
				.DesiredSpeed = FMassInt16Real(NewMovementSpeed);
		}
		++UpdatedEntityCount;
	}

	AuthorityState->AuthorityArchetype = TargetArchetype;
	AuthorityState->bUsingRuntimeTuningEvenArchetype = !AuthorityState->bUsingRuntimeTuningEvenArchetype;
	MovementSpeedCentimetersPerSecond = NewMovementSpeed;
	AuthorityState->bForceManualAvoidanceRefresh = true;
	PendingMovementSpeedCmPerSecond.Reset();
	NotifyMovementSpeedCommitted(UpdatedEntityCount);
	UE_LOG(
		LogGuLiCommanderMass,
		Display,
		TEXT("Applied Soldier movement speed %.3f cm/s to %d Mass entities at the 10 Hz authority boundary."),
		MovementSpeedCentimetersPerSecond,
		UpdatedEntityCount);
}

void UGuLiBattleAuthoritySubsystem::NotifyMovementSpeedCommitted(
	const int32 AppliedEntityCount) const
{
	if (UWorld* World = GetWorld())
	{
		if (UGuLiRuntimeTuningSubsystem* RuntimeTuning = World->GetSubsystem<UGuLiRuntimeTuningSubsystem>())
		{
			RuntimeTuning->NotifySoldierMovementSpeedCommitted(
				MovementSpeedCentimetersPerSecond,
				AppliedEntityCount);
		}
	}
}

// 服务端 C++ 伤害入口只做扣血与死亡转换，未在这里执行攻击力/防御力结算。
// 死亡即清除指令、速度和导航障碍网格 Fragment，但不立刻销毁 Entity 或移除 SoldierId。
bool UGuLiBattleAuthoritySubsystem::ApplyDamage(const FGuLiSoldierId SoldierId, const float Amount)
{
	if (!IsAuthorityWorld() || !AuthorityState || !FMath::IsFinite(Amount) || Amount <= 0.0f || !SoldierId.IsValid())
	{
		return false;
	}
	const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
	if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
	{
		return false;
	}

	GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
	if (!Soldier.IsPresent())
	{
		return false;
	}
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!MassSubsystem) return false;
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Soldier.Entity)) return false;
	Soldier.Health = FMath::Max(0.0f, Soldier.Health - Amount);
	++Soldier.StateRevision;
	FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(
		Soldier.Entity);
	Health.Health = Soldier.Health;
	Health.bDead = Soldier.Health <= 0.0f;
	if (Soldier.Health <= 0.0f)
	{
		for (auto& Weapon : Soldier.Weapons)
		{
			Weapon.Attack.TargetId = FGuLiSoldierId();
			Weapon.Attack.StopReason = EGuLiCombatStopReason::Dead;
			Weapon.Attack.NextFireSeconds = 0.0;
		}
		Soldier.ActiveOrderId = 0u;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Idle;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
		Soldier.Velocity = FVector::ZeroVector;
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Soldier.bForceMovementUpdate = false;
		AuthorityState->bForceManualAvoidanceRefresh = true;
		Soldier.DeathSimulationSeconds = AuthorityState->SimulationSeconds;
		Health.WreckSecondsRemaining = WreckLifetimeSeconds;
		FGuLiMassOrderFragment& Order = EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(
			Soldier.Entity);
		Order.ActiveOrderId = 0u;
		Order.OrderRevision = Soldier.StateRevision;
		Order.bHasMoveTarget = false;
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(
			Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity).CreateNewAction(
			EMassMovementAction::Stand,
			*GetWorld());
		EntityManager.RemoveFragmentFromEntity(
			Soldier.Entity,
			FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
	}
	return true;
}

FGuLiTargetHandle UGuLiBattleAuthoritySubsystem::MakeSoldierTargetHandle(
	const FGuLiSoldierId SoldierId) const
{
	const AGuLiBattleGameState* BattleState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const uint32 MatchEpoch = BattleState ? BattleState->GetMatchEpoch() : 0u;
	return SoldierId.IsValid()
		? GuLiCombatTargets::MakeCommanderSoldierTargetHandle(MatchEpoch, SoldierId.Value)
		: FGuLiTargetHandle{};
}

void UGuLiBattleAuthoritySubsystem::RegisterCombatLedgerTargets()
{
	UnregisterCombatLedgerTargets();
	if (!IsAuthorityWorld() || !AuthorityState)
	{
		return;
	}
	if (AGuLiBattleGameState* BattleState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
	{
		BattleState->InitializeServerMatchState();
	}
	RegisteredCombatLedgerTargets.Reserve(AuthorityState->Soldiers.Num());
	for (const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		RegisterCombatLedgerTarget(Soldier.SoldierId);
	}
}

bool UGuLiBattleAuthoritySubsystem::RegisterCombatLedgerTarget(
	const FGuLiSoldierId SoldierId)
{
	UGuLiDamageLedgerSubsystem* Ledger = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>() : nullptr;
	const FGuLiTargetHandle Handle = MakeSoldierTargetHandle(SoldierId);
	if (!Ledger || !Handle.IsValid())
	{
		return false;
	}

	TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> WeakThis(this);
	FGuLiCombatTargetAdapter Adapter;
	Adapter.LifetimeOwner = this;
	Adapter.ReadSnapshot = [WeakThis, SoldierId, Handle](FGuLiCombatTargetSnapshot& OutSnapshot)
	{
		const UGuLiBattleAuthoritySubsystem* Authority = WeakThis.Get();
		FGuLiSoldierCombatDebug Debug;
		if (!Authority || Authority->IsSoldierPhased(SoldierId) || !Authority->TryGetSoldierCombatDebug(SoldierId, Debug))
		{
			return false;
		}
		OutSnapshot = FGuLiCombatTargetSnapshot{};
		OutSnapshot.Handle = Handle;
		OutSnapshot.Team = Debug.Team;
		OutSnapshot.Location = Debug.Location;
		OutSnapshot.CollisionRadius = Authority->MemberAgentRadiusCentimeters;
		OutSnapshot.Health = Debug.Health;
		OutSnapshot.bAlive = Debug.Health > 0.0f;
		return !OutSnapshot.Location.ContainsNaN();
	};
	Adapter.ApplyDamage = [WeakThis, SoldierId](
		const FGuLiDamageRequest& Request,
		FGuLiDamageCommitResult& OutResult)
	{
		UGuLiBattleAuthoritySubsystem* Authority = WeakThis.Get();
		FGuLiSoldierCombatDebug Before;
		if (!Authority || !Authority->TryGetSoldierCombatDebug(SoldierId, Before)
			|| Before.Health <= 0.0f || !Authority->ApplyDamage(SoldierId, Request.Damage))
		{
			return false;
		}
		FGuLiSoldierCombatDebug After;
		if (!Authority->TryGetSoldierCombatDebug(SoldierId, After))
		{
			return false;
		}
		OutResult.AppliedDamage = FMath::Max(0.0f, Before.Health - After.Health);
		OutResult.RemainingHealth = FMath::Max(0.0f, After.Health);
		OutResult.bKilled = Before.Health > 0.0f && After.Health <= 0.0f;
		return OutResult.AppliedDamage > 0.0f;
	};
	if (!Ledger->RegisterTarget(Handle, MoveTemp(Adapter)))
	{
		return false;
	}
	RegisteredCombatLedgerTargets.AddUnique(Handle);
	return true;
}

void UGuLiBattleAuthoritySubsystem::CollectExternalUnitsInDisc(const EGuLiTeam Team, const FVector Center,
	const float Radius, TArray<FGuLiMassExternalUnit>& Out) const
{
	Out.Reset();
	if (!AuthorityState) return;
	for (const auto& Soldier : AuthorityState->Soldiers)
	{
		if (Soldier.Team == Team && Soldier.CanAct() && (!Soldier.Location.ContainsNaN() && !Center.ContainsNaN() && FMath::IsFinite(Radius) && Radius > 0 && FVector::DistSquared2D(Soldier.Location,Center) <= FMath::Square(Radius)))
			Out.Add({Soldier.SoldierId, FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location), MemberAgentRadiusCentimeters});
	}
	Out.Sort([](const auto& A, const auto& B) { return A.Id.Value < B.Id.Value; });
}

bool UGuLiBattleAuthoritySubsystem::IsSoldierPhased(const FGuLiSoldierId Id) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	return Index && AuthorityState->Soldiers[*Index].bPhased;
}

bool UGuLiBattleAuthoritySubsystem::IsSoldierExternallyLocked(const FGuLiSoldierId Id) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	return Index && AuthorityState->Soldiers[*Index].bExternalActionsLocked;
}

bool UGuLiBattleAuthoritySubsystem::ProjectExternalUnitLocation(const FVector Desired, FVector& OutLocation) const
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	auto* Data = Nav ? GuLiCommanderMassPrivate::GetCommanderNavigationData(*Nav) : nullptr;
	FNavLocation Projected;
	if (!Data || !Nav->ProjectPointToNavigation(Desired, Projected, FVector(100,100,2000), Data)
		|| FVector::DistSquared2D(Desired, Projected.Location) > FMath::Square(100.0)) return false;
	OutLocation = Projected.Location;
	return true;
}

bool UGuLiBattleAuthoritySubsystem::CanApplyExternalUnitState(TConstArrayView<FGuLiMassExternalUnit> Participants, const FGuid CastId) const
{
	if (!IsAuthorityWorld() || !AuthorityState || !CastId.IsValid()) return false;
	auto* Mass = AuthorityState->MassEntitySubsystem.Get();
	if (!Mass) return false;
	const auto& Manager = Mass->GetEntityManager();
	if (Manager.IsProcessing()) return false;
	for (const auto& Entry : Participants)
	{
		const int32* Index = AuthorityState->SoldierIndexById.Find(Entry.Id.Value);
		if (!Index) continue;
		const auto& Soldier = AuthorityState->Soldiers[*Index];
		if (!Soldier.IsAlive()) continue;
		if (!Manager.IsEntityValid(Soldier.Entity) || Entry.Transform.ContainsNaN()
			|| (Soldier.ExternalControlToken.IsValid() && Soldier.ExternalControlToken != CastId)) return false;
	}
	return true;
}

bool UGuLiBattleAuthoritySubsystem::ApplyExternalUnitState(TConstArrayView<FGuLiMassExternalUnit> Participants,
	const FGuid CastId, const bool bPhased, const bool bLocked, const bool bRelocate)
{
	if (!IsAuthorityWorld() || !AuthorityState || !CastId.IsValid()) return false;
	auto* Mass = AuthorityState->MassEntitySubsystem.Get();
	if (!Mass) return false;
	auto& Manager = Mass->GetMutableEntityManager();
	// Validate the complete batch before modifying any surviving member.
	if (!CanApplyExternalUnitState(Participants,CastId)) return false;
	for (const auto& Entry : Participants)
	{
		const int32* Index = AuthorityState->SoldierIndexById.Find(Entry.Id.Value);
		if (!Index) continue;
		auto& Soldier = AuthorityState->Soldiers[*Index];
		if (!Soldier.IsAlive()) continue;
		if (bLocked && !Soldier.bExternalActionsLocked) Soldier.ExternalLockSimulationTime = AuthorityState->SimulationSeconds;
		if (!bLocked && Soldier.bExternalActionsLocked)
		{
			const double Paused = AuthorityState->SimulationSeconds - Soldier.ExternalLockSimulationTime;
			for (auto& Weapon : Soldier.Weapons) Weapon.Attack.NextFireSeconds += Paused;
		}
		Soldier.ExternalControlToken = bLocked ? CastId : FGuid();
		Soldier.bPhased = bPhased; Soldier.bExternalActionsLocked = bLocked;
		Soldier.ActiveOrderId = 0; Soldier.bHasFinalDestination = false;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Idle;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
		Soldier.PersonalPathPoints.Reset(); Soldier.NoProgressSeconds = 0;
		Soldier.bForceMovementUpdate = false; Soldier.Velocity = FVector::ZeroVector;
		for (auto& Weapon : Soldier.Weapons) Weapon.Attack.TargetId = {};
		if (bRelocate)
		{
			Soldier.Location = Entry.Transform.GetLocation();
			Soldier.FacingYawDegrees = Entry.Transform.Rotator().Yaw;
			Soldier.LastValidNavLocation = FNavLocation(Soldier.Location);
			auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			auto* Data = Nav ? GuLiCommanderMassPrivate::GetCommanderNavigationData(*Nav) : nullptr;
			if (Data) Nav->ProjectPointToNavigation(Soldier.Location, Soldier.LastValidNavLocation, FVector(100,100,300), Data);
			Soldier.DisplacementFrameFloor = AuthorityState->NextPoseFrameSequence;
			Soldier.DisplacementLocation = Soldier.Location;
			Soldier.DisplacementSimulationTime = AuthorityState->SimulationSeconds;
		}
		++Soldier.StateRevision;
		Soldier.CurrentNavigationWaypoint = Soldier.Location;
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Manager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity).SetTransform(
			FTransform(FRotator(0,Soldier.FacingYawDegrees,0), Soldier.Location, FVector::OneVector));
		Manager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity).bPhased = bPhased;
		auto& Order = Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = 0; Order.bHasMoveTarget = false; Order.OrderRevision = Soldier.StateRevision;
		Manager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		Manager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		Manager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		auto& Move = Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.CreateNewAction(EMassMovementAction::Stand, *GetWorld()); Move.Center = Soldier.Location;
		Move.DesiredSpeed = FMassInt16Real(0.f);
		if (bPhased && Manager.GetFragmentDataPtr<FMassNavigationObstacleGridCellLocationFragment>(Soldier.Entity))
			Manager.RemoveFragmentFromEntity(Soldier.Entity, FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
		else if (!bPhased && !Manager.GetFragmentDataPtr<FMassNavigationObstacleGridCellLocationFragment>(Soldier.Entity))
			Manager.AddFragmentToEntity(Soldier.Entity, FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
	}
	AuthorityState->bForceManualAvoidanceRefresh = true;
	return true;
}

void UGuLiBattleAuthoritySubsystem::UnregisterCombatLedgerTargets()
{
	if (UGuLiDamageLedgerSubsystem* Ledger = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>() : nullptr)
	{
		for (const FGuLiTargetHandle& Handle : RegisteredCombatLedgerTargets)
		{
			Ledger->UnregisterTarget(Handle, this);
		}
	}
	RegisteredCombatLedgerTargets.Reset();
}

// 构造可靠复制使用的离散状态：生命、阵营、指令等；连续位置走下面独立的姿态通道。
void UGuLiBattleAuthoritySubsystem::BuildSoldierStateSnapshot(
	TArray<FGuLiSoldierStateItem>& OutStates) const
{
	OutStates.Reset();
	if (!AuthorityState)
	{
		return;
	}
	OutStates.Reserve(AuthorityState->Soldiers.Num());
	for (const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		FGuLiSoldierStateItem& State = OutStates.AddDefaulted_GetRef();
		State.SoldierId = Soldier.SoldierId;
		State.Team = Soldier.Team;
		State.UnitTypeId = Soldier.UnitTypeId;
		State.LifeState = Soldier.IsAlive()
			? EGuLiSoldierLifeState::Alive
			: EGuLiSoldierLifeState::Destroyed;
		State.Health = Soldier.Health;
		State.MaxHealth = Soldier.MaxHealth;
		State.StateRevision = Soldier.StateRevision;
		State.ActiveOrderId = Soldier.ActiveOrderId;
		State.bPhased = Soldier.bPhased;
		State.bExternalActionsLocked = Soldier.bExternalActionsLocked;
		State.DisplacementFrameFloor = Soldier.DisplacementFrameFloor;
		State.DisplacementLocation = Soldier.DisplacementLocation;
		State.DisplacementYaw = Soldier.FacingYawDegrees;
		State.DisplacementSimulationTime = Soldier.DisplacementSimulationTime;
	}
}

// 这里只捕获/压缩一帧；WorldReplicationComponent 按 10 Hz 模拟每步调度一次。
// 同一帧各分块共享 FrameSequence、ServerSimTick 和 AuthorityEpoch，接收端据此识别时序与战局。
// 跨文件出口：这里只捕获、量化并分块；WorldReplicationComponent 调度，NetSync::SendPoseChunk 发 Client RPC。
void UGuLiBattleAuthoritySubsystem::CaptureSoldierPoseChunks(
	TArray<FGuLiSoldierPoseChunk>& OutChunks,
	const uint32 AuthorityEpoch)
{
	OutChunks.Reset();
	if (!AuthorityState || AuthorityState->Soldiers.IsEmpty() || AuthorityEpoch == 0u)
	{
		return;
	}
	AuthorityState->AuthorityEpoch = AuthorityEpoch;

	TArray<int32> SortedIndices;
	SortedIndices.Reserve(AuthorityState->Soldiers.Num());
	for (int32 Index = 0; Index < AuthorityState->Soldiers.Num(); ++Index)
	{
		SortedIndices.Add(Index);
	}
	static_assert(GuLiCommanderSimulationTiming::RateHz % GULI_POSE_CAPTURE_RATE_HZ == 0u);
	constexpr uint32 CapturePosePhaseCount = GULI_POSE_DISPATCH_PHASE_COUNT;
	// 10 Hz 捕获与三相发送分别调度；相位内再按阵营和空间装块。
	// SoldierId 的相位固定，位置变化不影响单兵每秒收到的样本数。
	SortedIndices.Sort([this](const int32 LhsIndex, const int32 RhsIndex)
	{
		const GuLiCommanderMassPrivate::FSoldierRuntime& Lhs = AuthorityState->Soldiers[LhsIndex];
		const GuLiCommanderMassPrivate::FSoldierRuntime& Rhs = AuthorityState->Soldiers[RhsIndex];
		const uint32 LhsPhase = Lhs.SoldierId.Value % CapturePosePhaseCount;
		const uint32 RhsPhase = Rhs.SoldierId.Value % CapturePosePhaseCount;
		if (LhsPhase != RhsPhase)
		{
			return LhsPhase < RhsPhase;
		}
		if (Lhs.Team != Rhs.Team)
		{
			return static_cast<uint8>(Lhs.Team) < static_cast<uint8>(Rhs.Team);
		}
		const FIntPoint LhsCell = GuLiCommanderMassPrivate::MakeSpatialCell(Lhs.Location);
		const FIntPoint RhsCell = GuLiCommanderMassPrivate::MakeSpatialCell(Rhs.Location);
		if (LhsCell.Y != RhsCell.Y)
		{
			return LhsCell.Y < RhsCell.Y;
		}
		if (LhsCell.X != RhsCell.X)
		{
			return LhsCell.X < RhsCell.X;
		}
		return Lhs.SoldierId.Value < Rhs.SoldierId.Value;
	});

	const int32 ChunkSize = static_cast<int32>(GULI_MAX_POSE_SAMPLES_PER_CHUNK);
	constexpr double MaximumEncodableRelativeCentimeters = static_cast<double>(MAX_int16 - 1) * GULI_POSE_QUANTIZATION_CENTIMETERS;
	TArray<TArray<int32>> ChunkSoldierIndices;
	TArray<FVector> ChunkLocationSums;
	ChunkSoldierIndices.Reserve(FMath::DivideAndRoundUp(SortedIndices.Num(), ChunkSize));
	ChunkLocationSums.Reserve(ChunkSoldierIndices.Max());
	for (const int32 SoldierIndex : SortedIndices)
	{
		const GuLiCommanderMassPrivate::FSoldierRuntime& CandidateSoldier =
			AuthorityState->Soldiers[SoldierIndex];
		const FVector CandidateLocation = CandidateSoldier.Location;
		bool bStartNewChunk = ChunkSoldierIndices.IsEmpty()
			|| ChunkSoldierIndices.Last().Num() >= ChunkSize;
		if (!bStartNewChunk)
		{
			const GuLiCommanderMassPrivate::FSoldierRuntime& FirstSoldier =
				AuthorityState->Soldiers[ChunkSoldierIndices.Last()[0]];
			bStartNewChunk = FirstSoldier.SoldierId.Value % CapturePosePhaseCount
				!= CandidateSoldier.SoldierId.Value % CapturePosePhaseCount;
		}
		// 最多 32 人之外还检查 int16 相对坐标范围；候选加入后锚点变化，已有成员也必须重新检查。
		if (!bStartNewChunk)
		{
			const TArray<int32>& CurrentChunk = ChunkSoldierIndices.Last();
			const FVector CandidateAnchor = (ChunkLocationSums.Last() + CandidateLocation)
				/ static_cast<double>(CurrentChunk.Num() + 1);
			auto FitsRelativeEncoding = [&CandidateAnchor](const FVector& Location)
			{
				const FVector Relative = Location - CandidateAnchor;
				return FMath::Abs(Relative.X) <= MaximumEncodableRelativeCentimeters
					&& FMath::Abs(Relative.Y) <= MaximumEncodableRelativeCentimeters
					&& FMath::Abs(Relative.Z) <= MaximumEncodableRelativeCentimeters;
			};
			bStartNewChunk = !FitsRelativeEncoding(CandidateLocation);
			for (int32 ExistingOffset = 0;
				!bStartNewChunk && ExistingOffset < CurrentChunk.Num();
				++ExistingOffset)
			{
				bStartNewChunk = !FitsRelativeEncoding(
					AuthorityState->Soldiers[CurrentChunk[ExistingOffset]].Location);
			}
		}

		if (bStartNewChunk)
		{
			ChunkSoldierIndices.AddDefaulted();
			ChunkSoldierIndices.Last().Reserve(ChunkSize);
			ChunkLocationSums.Add(FVector::ZeroVector);
		}
		ChunkSoldierIndices.Last().Add(SoldierIndex);
		ChunkLocationSums.Last() += CandidateLocation;
	}

	const int32 ChunkCount = ChunkSoldierIndices.Num();
	if (ChunkCount <= 0
		|| ChunkCount > static_cast<int32>(GULI_MAX_POSE_CHUNKS_PER_FRAME))
	{
		UE_LOG(
			LogGuLiCommanderMass,
			Error,
			TEXT("Pose chunk builder produced invalid chunk count %d for %d Soldiers."),
			ChunkCount,
			SortedIndices.Num());
		return;
	}
	const uint32 FrameSequence = GuLiCommanderMassPrivate::AllocateNonZero(
		AuthorityState->NextPoseFrameSequence);
	OutChunks.Reserve(ChunkCount);
	for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
	{
		const TArray<int32>& ChunkIndices = ChunkSoldierIndices[ChunkIndex];
		const int32 Count = ChunkIndices.Num();

		FGuLiSoldierPoseChunk& Chunk = OutChunks.AddDefaulted_GetRef();
		Chunk.ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;
		Chunk.AuthorityEpoch = AuthorityState->AuthorityEpoch;
		Chunk.FrameSequence = FrameSequence;
		Chunk.ServerSimTick = AuthorityState->ServerSimTick;
		Chunk.ServerTimeSeconds = static_cast<float>(AuthorityState->SimulationSeconds);
		Chunk.ChunkIndex = static_cast<uint16>(ChunkIndex);
		Chunk.ChunkCount = static_cast<uint16>(ChunkCount);
		Chunk.Anchor = ChunkLocationSums[ChunkIndex] / static_cast<double>(Count);
		Chunk.Samples.Reserve(Count);
		for (int32 Offset = 0; Offset < Count; ++Offset)
		{
			GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[ChunkIndices[Offset]];
			if (Soldier.LastCapturedPoseFrameSequence != 0u
				&& (Soldier.DisplacementFrameFloor == 0 || int32(Soldier.LastCapturedPoseFrameSequence - Soldier.DisplacementFrameFloor) >= 0))
			{
				const float CapturedStepCentimeters = FVector::Dist(
					Soldier.LastCapturedPoseLocation,
					Soldier.Location);
				if (CapturedStepCentimeters > 1000.0f)
				{
					UE_LOG(
						LogGuLiCommanderMass,
						Error,
						TEXT("Authority pose discontinuity: soldier=%u previous_frame=%u frame=%u step_cm=%.1f previous=(%.1f,%.1f,%.1f) current=(%.1f,%.1f,%.1f) velocity=(%.1f,%.1f,%.1f)."),
						Soldier.SoldierId.Value,
						Soldier.LastCapturedPoseFrameSequence,
						FrameSequence,
						CapturedStepCentimeters,
						Soldier.LastCapturedPoseLocation.X,
						Soldier.LastCapturedPoseLocation.Y,
						Soldier.LastCapturedPoseLocation.Z,
						Soldier.Location.X,
						Soldier.Location.Y,
						Soldier.Location.Z,
						Soldier.Velocity.X,
						Soldier.Velocity.Y,
						Soldier.Velocity.Z);
				}
			}
			Soldier.LastCapturedPoseLocation = Soldier.Location;
			Soldier.LastCapturedPoseFrameSequence = FrameSequence;
			FGuLiCompressedSoldierPose& Pose = Chunk.Samples.AddDefaulted_GetRef();
			Pose.SoldierId = Soldier.SoldierId;
			Pose.SetRelativeLocationCentimeters(Soldier.Location - FVector(Chunk.Anchor));
			// 用网络锚点的厘米舍入方式在本地重建位置，检测量化/溢出异常；日志不会自动修正位置。
			const FVector QuantizedAnchor(
				FMath::RoundToDouble(Chunk.Anchor.X),
				FMath::RoundToDouble(Chunk.Anchor.Y),
				FMath::RoundToDouble(Chunk.Anchor.Z));
			const FVector LocallyReconstructedLocation = QuantizedAnchor + Pose.GetRelativeLocationCentimeters();
			const float LocalCompressionErrorCentimeters = FVector::Dist(
				Soldier.Location,
				LocallyReconstructedLocation);
			if (LocalCompressionErrorCentimeters > 100.0f)
			{
				UE_LOG(
					LogGuLiCommanderMass,
					Error,
					TEXT("Authority pose compression error: soldier=%u frame=%u chunk=%d sample=%d error_cm=%.1f anchor=(%.1f,%.1f,%.1f) relative_dm=(%d,%d,%d) authority=(%.1f,%.1f,%.1f) reconstructed=(%.1f,%.1f,%.1f)."),
					Soldier.SoldierId.Value,
					FrameSequence,
					ChunkIndex,
					Offset,
					LocalCompressionErrorCentimeters,
					Chunk.Anchor.X,
					Chunk.Anchor.Y,
					Chunk.Anchor.Z,
					Pose.RelativeXDecimeters,
					Pose.RelativeYDecimeters,
					Pose.RelativeZDecimeters,
					Soldier.Location.X,
					Soldier.Location.Y,
					Soldier.Location.Z,
					LocallyReconstructedLocation.X,
					LocallyReconstructedLocation.Y,
					LocallyReconstructedLocation.Z);
			}
			Pose.SetVelocityCentimetersPerSecond(Soldier.Velocity);
			Pose.FacingYaw = GuLiCommanderProtocol::QuantizeYawDegrees(Soldier.FacingYawDegrees);
			Pose.ActiveOrderId = Soldier.ActiveOrderId;
			Pose.State = Soldier.IsAlive()
				? (Soldier.ActiveOrderId != 0u
					? EGuLiSoldierPoseState::Moving
					: EGuLiSoldierPoseState::Idle)
				: EGuLiSoldierPoseState::Destroyed;
			Pose.Flags = Soldier.DisplacementFrameFloor != 0 && int32(FrameSequence - Soldier.DisplacementFrameFloor) < 3
				? GULI_SOLDIER_POSE_FLAG_TELEPORT : 0u;
		}
		Chunk.Sanitize();
	}
}

bool UGuLiBattleAuthoritySubsystem::TryGetSoldierTransform(
	const FGuLiSoldierId SoldierId,
	FTransform& OutTransform) const
{
	if (!AuthorityState)
	{
		return false;
	}
	const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
	if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
	{
		return false;
	}
	const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
	OutTransform = FTransform(FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f), Soldier.Location);
	return true;
}

void UGuLiBattleAuthoritySubsystem::BuildLivingSoldierLocationSnapshot(
	TArray<FVector>& OutLocations) const
{
	OutLocations.Reset();
	if (!AuthorityState)
	{
		return;
	}

	OutLocations.Reserve(AuthorityState->Soldiers.Num());
	for (const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		if (Soldier.IsPresent() && !Soldier.Location.ContainsNaN())
		{
			OutLocations.Add(Soldier.Location);
		}
	}
}

int32 UGuLiBattleAuthoritySubsystem::GetActiveOrderFormationCount() const
{
	return AuthorityState ? AuthorityState->OrderFormations.Num() : 0;
}

// 返回保留的权威记录数（包含死亡/残骸已隐藏成员），不是当前存活人数。
int32 UGuLiBattleAuthoritySubsystem::GetAuthoritativeMemberCount() const
{
	return AuthorityState ? AuthorityState->Soldiers.Num() : 0;
}

uint32 UGuLiBattleAuthoritySubsystem::GetServerSimTick() const
{
	return AuthorityState ? AuthorityState->ServerSimTick : 0u;
}

uint64 UGuLiBattleAuthoritySubsystem::GetDroppedFixedStepCount() const
{
	return AuthorityState ? AuthorityState->DroppedFixedStepCount : 0u;
}

bool UGuLiBattleAuthoritySubsystem::HasSpawnedAuthorityPopulation() const
{
	return AuthorityState && AuthorityState->bPopulationSpawned;
}

void UGuLiBattleAuthoritySubsystem::CommitCombatProfiles()
{
	UGuLiArmySkillSubsystem* Skills = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	if (!Skills || !AuthorityState) return;
	Skills->CommitPendingChanges();
	const uint32 Revision = Skills->GetLoadoutRevision();
	if (Revision == AuthorityState->CommittedCombatRevision) return;
	for (GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		GuLiCommanderMassPrivate::SynchronizeSoldierWeapons(Soldier,
			Skills->GetSharedUnitProfiles(Soldier.Team, Soldier.UnitTypeId), AuthorityState->SimulationSeconds);
	}
	AuthorityState->CommittedCombatRevision = Revision;
}

void UGuLiBattleAuthoritySubsystem::TickSoldierCombat()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_SoldierCombat);
	if (!AuthorityState) return;
#if !UE_BUILD_SHIPPING
	const double CombatStartedAt = FPlatformTime::Seconds();
#endif
	AuthorityState->CombatSamples.Reset(AuthorityState->Soldiers.Num());
	AuthorityState->CombatChannels.Reset();
	for (GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		AuthorityState->CombatSamples.Add({Soldier.SoldierId, Soldier.Team, Soldier.Location,
			Soldier.IsPresent(), Soldier.ActiveOrderId != 0u, nullptr, nullptr});
		if (Soldier.CanAct() && Soldier.WeaponProfiles) for (auto& Weapon : Soldier.Weapons)
		{
			if (!Soldier.WeaponProfiles->IsValidIndex(Weapon.ProfileIndex)) continue;
			AuthorityState->CombatChannels.Add({Soldier.SoldierId, Soldier.Team, Soldier.Location,
				Soldier.IsAlive(), Soldier.ActiveOrderId != 0u, &(*Soldier.WeaponProfiles)[Weapon.ProfileIndex], &Weapon.Attack});
		}
	}
	// Combat keeps its independent 100 m range grid and rebuilds it from final movement positions.
	GuLiSoldierCombat::BuildSpatialGrid(AuthorityState->CombatSamples, AuthorityState->SpatialGrid);
	const FGuLiCombatStepMetrics Metrics = GuLiSoldierCombat::CollectChannelAttacks(AuthorityState->CombatChannels,
		AuthorityState->CombatSamples, AuthorityState->SoldierIndexById,
		AuthorityState->SpatialGrid, AuthorityState->ServerSimTick, AuthorityState->SimulationSeconds,
		AuthorityState->CombatExecutors, AuthorityState->PendingDamage);
	for (auto& Soldier : AuthorityState->Soldiers)
	{
		Soldier.bAttackMoveHolding = Soldier.bAutomaticAdvance && Soldier.Weapons.ContainsByPredicate([](const auto& Weapon)
		{
			return Weapon.Attack.TargetId.IsValid() && (Weapon.Attack.StopReason == EGuLiCombatStopReason::Fired
				|| Weapon.Attack.StopReason == EGuLiCombatStopReason::Cooldown);
		});
	}
	// Do not hold Mass fragment references here: death may change an entity's archetype.
	// All shots were accepted against one alive-state snapshot, permitting simultaneous kills.
	if (UGuLiCombatEffectRuntimeSubsystem* Effects = GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>())
	{
		const AGuLiBattleGameState* BattleState = GetWorld()->GetGameState<AGuLiBattleGameState>();
		const uint32 EffectEpoch = BattleState ? BattleState->GetMatchEpoch() : 0;
		TArray<FGuLiCombatAttackRequest> Requests;
		Requests.Reserve(AuthorityState->PendingDamage.Num());
		for (const FGuLiCombatDamageEvent& Event : AuthorityState->PendingDamage)
		{
			const int32* SourceIndex = AuthorityState->SoldierIndexById.Find(Event.SourceId.Value);
			const int32* TargetIndex = AuthorityState->SoldierIndexById.Find(Event.TargetId.Value);
			if (!SourceIndex || !TargetIndex) continue;
			const auto& Source = AuthorityState->Soldiers[*SourceIndex];
			const auto& Target = AuthorityState->Soldiers[*TargetIndex];
			FGuLiCombatAttackRequest& Request = Requests.AddDefaulted_GetRef();
			Request.Context.Source = MakeSoldierTargetHandle(Event.SourceId);
			Request.Context.Target = MakeSoldierTargetHandle(Event.TargetId);
			Request.Context.MatchEpoch = EffectEpoch;
			Request.Context.WeaponBinding = FGuLiWeaponBindingKey::Army(Request.Context.MatchEpoch, Source.Team, Event.SourceUnitTypeId, Event.SourceSlotId);
			Request.Context.SkillId = Event.SkillId; Request.Context.Damage = Event.Damage;
			if (const auto* Skill = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>()->FindSkillDefinition(Event.SkillId))
				Request.Context.EffectConfigId = Skill->EffectConfigId;
			Request.Context.ProfileRevision = Event.ProfileRevision;
			Request.ExecutorId = Event.ExecutorId; Request.UnitTypeId = Event.SourceUnitTypeId;
			Request.SourceTransform = FTransform(FRotator(0, Source.FacingYawDegrees, 0), Source.Location);
			Request.TargetLocation = Target.Location; Request.ShotOrdinal = Event.ShotOrdinal;
		}
		Effects->ExecuteAttackBatch(Requests);
	}
#if !UE_BUILD_SHIPPING
	const double CombatMilliseconds = (FPlatformTime::Seconds() - CombatStartedAt) * 1000.0;
	PerformanceCounters.Shots += Metrics.Shots;
	PerformanceCounters.CombatMilliseconds += CombatMilliseconds;
	PerformanceCounters.MaxCombatMilliseconds = FMath::Max(PerformanceCounters.MaxCombatMilliseconds, CombatMilliseconds);
#else
	(void)Metrics;
#endif
}

bool UGuLiBattleAuthoritySubsystem::TryGetSoldierCombatDebug(const FGuLiSoldierId SoldierId,
	FGuLiSoldierCombatDebug& OutDebug) const
{
	return TryGetSoldierWeaponDebug(SoldierId, TEXT("BasicAttack"), OutDebug);
}

bool UGuLiBattleAuthoritySubsystem::TryGetSoldierWeaponDebug(const FGuLiSoldierId SoldierId,
	const FName SlotId, FGuLiSoldierCombatDebug& OutDebug) const
{
	OutDebug = {};
	if (!AuthorityState) return false;
	const int32* Index = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
	if (!Index || !AuthorityState->Soldiers.IsValidIndex(*Index)) return false;
	const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[*Index];
	OutDebug.SoldierId = Soldier.SoldierId;
	OutDebug.Team = Soldier.Team;
	OutDebug.UnitTypeId = Soldier.UnitTypeId;
	OutDebug.SlotId = SlotId;
	const auto* Weapon = Soldier.Weapons.FindByPredicate([&](const auto& Entry) { return Entry.SlotId == SlotId; });
	const FGuLiResolvedSkillProfile* Profile = Weapon && Soldier.WeaponProfiles
		&& Soldier.WeaponProfiles->IsValidIndex(Weapon->ProfileIndex) ? &(*Soldier.WeaponProfiles)[Weapon->ProfileIndex] : nullptr;
	const FGuLiSoldierAttackState EmptyAttack;
	const FGuLiSoldierAttackState& Attack = Weapon ? Weapon->Attack : EmptyAttack;
	OutDebug.SkillId = Profile ? Profile->SkillId : NAME_None;
	OutDebug.ExecutorId = Profile ? Profile->ExecutorId : NAME_None;
	OutDebug.TargetId = Attack.TargetId;
	OutDebug.Location = Soldier.Location;
	OutDebug.Health = Soldier.Health;
	OutDebug.MaxHealth = Soldier.MaxHealth;
	OutDebug.Damage = Profile ? Profile->Damage : 0.0f;
	OutDebug.AttackRate = Profile ? Profile->AttackRatePerSecond : 0.0f;
	OutDebug.RangeCentimeters = Profile ? Profile->RangeCentimeters : 0.0f;
	OutDebug.CooldownRemaining = FMath::Max(0.0, Attack.NextFireSeconds - AuthorityState->SimulationSeconds);
	OutDebug.ProfileRevision = Profile ? Profile->Revision : 0u;
	OutDebug.ShotsFired = Attack.ShotsFired;
	OutDebug.StopReason = Attack.StopReason;
	return true;
}

bool UGuLiBattleAuthoritySubsystem::TryGetSoldierNavigationDebug(
	const FGuLiSoldierId SoldierId,
	FGuLiSoldierNavigationDebug& OutDebug) const
{
	OutDebug = {};
	if (!AuthorityState)
	{
		return false;
	}
	const int32* Index = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
	if (!Index || !AuthorityState->Soldiers.IsValidIndex(*Index))
	{
		return false;
	}
	const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[*Index];
	OutDebug.SoldierId = Soldier.SoldierId;
	OutDebug.Team = Soldier.Team;
	OutDebug.State = Soldier.NavigationState;
	OutDebug.Failure = Soldier.NavigationFailure;
	OutDebug.ActiveOrderId = Soldier.ActiveOrderId;
	OutDebug.LastCompletedOrderId = Soldier.LastCompletedOrderId;
	OutDebug.LastFailedOrderId = Soldier.LastFailedOrderId;
	OutDebug.Location = Soldier.Location;
	OutDebug.LastValidNavLocation = Soldier.LastValidNavLocation.Location;
	OutDebug.FinalSlot = Soldier.FinalDestination.Location;
	OutDebug.CurrentWaypoint = Soldier.CurrentNavigationWaypoint;
	OutDebug.DistanceToFinalSlotCentimeters = Soldier.bHasFinalDestination
		? FVector::Dist(Soldier.Location, Soldier.FinalDestination.Location)
		: 0.0f;
	OutDebug.NoProgressSeconds = Soldier.NoProgressSeconds;
	OutDebug.FailureSimulationSeconds = Soldier.FailureSimulationSeconds;
	OutDebug.PathPointIndex = Soldier.LastProgressPathPointIndex;
	OutDebug.ConsecutiveSurfaceFailures = Soldier.ConsecutiveSurfaceFailures;
	OutDebug.TotalSurfaceFailures = Soldier.TotalSurfaceFailures;
	OutDebug.PersonalPathRetries = Soldier.PersonalPathRetries;
	OutDebug.bHasFinalSlot = Soldier.bHasFinalDestination;
	OutDebug.bMoving = Soldier.ActiveOrderId != 0u
		&& (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
			|| Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
			|| Soldier.NavigationState == EGuLiSoldierNavigationState::PersonalPathRecovery);
	return true;
}

FGuLiNavigationStats UGuLiBattleAuthoritySubsystem::GetNavigationStats() const
{
	FGuLiNavigationStats Result;
	if (!AuthorityState)
	{
		return Result;
	}
	Result.SurfaceMoveCalls = AuthorityState->SurfaceMoveCalls;
	Result.SurfaceMoveFailures = AuthorityState->SurfaceMoveFailures;
	Result.MovementUpdateCalls = AuthorityState->MovementUpdateCalls;
	Result.ForcedMovementUpdateCalls = AuthorityState->ForcedMovementUpdateCalls;
	Result.ManualAvoidanceRefreshes = AuthorityState->ManualAvoidanceRefreshes;
	Result.ManualAvoidanceCandidatePairs = AuthorityState->ManualAvoidanceCandidatePairs;
	Result.ManualAvoidanceOverlapPairs = AuthorityState->ManualAvoidanceOverlapPairs;
	Result.MaximumManualAvoidanceBucketOccupancy =
		AuthorityState->MaximumManualAvoidanceBucketOccupancy;
	const UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	const FMassEntityManager* EntityManager = MassSubsystem
		? &MassSubsystem->GetEntityManager()
		: nullptr;
	Result.PathQueries = AuthorityState->PathQueries;
	Result.PersonalPathQueries = AuthorityState->PersonalPathQueries;
	for (const TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>& Job
		: AuthorityState->MovePlanningJobs)
	{
		Result.PendingMovePlanningTasks += Job
			&& Job->Stage != GuLiCommanderMassPrivate::EMovePlanningStage::Completed
			? 1
			: 0;
	}
	Result.MoveCandidateProjectionQueries = AuthorityState->MoveCandidateProjectionQueries;
	Result.MovePlanningPathQueries = AuthorityState->MovePlanningPathQueries;
	Result.PartiallyAcceptedMoveCommands = AuthorityState->PartiallyAcceptedMoveCommands;
	for (uint8 StageIndex = 1u;
		StageIndex < static_cast<uint8>(EGuLiMovePlanFailureStage::Count);
		++StageIndex)
	{
		Result.MovePlanningFailureCounts[StageIndex - 1u] =
			AuthorityState->MovePlanningFailureCounts[StageIndex];
	}
	Result.LastDestinationPlanningMilliseconds = AuthorityState->LastDestinationPlanningMilliseconds;
	Result.MaximumDestinationPlanningMilliseconds = AuthorityState->MaximumDestinationPlanningMilliseconds;
	for (const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		if (EntityManager && EntityManager->IsEntityValid(Soldier.Entity))
		{
			if (const FGuLiMassAvoidanceStateFragment* AvoidanceState =
				EntityManager->GetFragmentDataPtr<FGuLiMassAvoidanceStateFragment>(Soldier.Entity))
			{
				Result.PredictiveAvoidanceSolves += AvoidanceState->SolveCount;
				Result.ForcedPredictiveAvoidanceSolves += AvoidanceState->ForcedSolveCount;
				Result.PredictiveAvoidanceCandidates += AvoidanceState->CandidateCount;
				Result.PredictiveAvoidanceColliderEvaluations +=
					AvoidanceState->ColliderEvaluationCount;
				Result.MaximumPredictiveAvoidanceBucketOccupancy = FMath::Max(
					Result.MaximumPredictiveAvoidanceBucketOccupancy,
					AvoidanceState->MaximumObservedBucketOccupancy);
			}
		}
		if (!Soldier.IsAlive())
		{
			continue;
		}
		++Result.Alive;
		Result.Active += Soldier.ActiveOrderId != 0u ? 1 : 0;
		switch (Soldier.NavigationState)
		{
		case EGuLiSoldierNavigationState::Idle:
			++Result.Idle;
			break;
		case EGuLiSoldierNavigationState::Normal:
			break;
		case EGuLiSoldierNavigationState::CenterlineRecovery:
			++Result.CenterlineRecovery;
			break;
		case EGuLiSoldierNavigationState::PersonalPathRecovery:
			++Result.PersonalPathRecovery;
			break;
		case EGuLiSoldierNavigationState::Arrived:
			++Result.Arrived;
			break;
		case EGuLiSoldierNavigationState::Blocked:
			++Result.Blocked;
			break;
		default:
			break;
		}
		if (Soldier.ActiveOrderId != 0u
			&& Soldier.NoProgressSeconds > Result.SlowestNoProgressSeconds)
		{
			Result.SlowestNoProgressSeconds = Soldier.NoProgressSeconds;
			Result.SlowestSoldierId = Soldier.SoldierId;
		}
	}
	return Result;
}

bool UGuLiBattleAuthoritySubsystem::RegisterCombatExecutor(const FName ExecutorId,
	FGuLiCombatExecutorRegistry::FExecutor Executor)
{
	if (!IsInGameThread() || !IsAuthorityWorld() || !AuthorityState || !Executor) return false;
	UGuLiArmySkillSubsystem* Skills = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	if (!Skills || Skills->IsExecutorRegistered(ExecutorId)
		|| !AuthorityState->CombatExecutors.RegisterExecutor(ExecutorId, MoveTemp(Executor))) return false;
	return Skills->RegisterExecutor(ExecutorId);
}

bool UGuLiBattleAuthoritySubsystem::SpawnReservedSoldier(const EGuLiTeam Team, const uint16 UnitTypeId,
	const FVector& Location, FGuLiSoldierId& OutId)
{
	OutId = FGuLiSoldierId();
	using namespace GuLiCommanderMassPrivate;
	if (!IsInGameThread() || !IsAuthorityWorld() || !AuthorityState || !AuthorityState->bPopulationSpawned
		|| !GuLiCommanderProtocol::IsPlayableTeam(Team) || Location.ContainsNaN()
		|| AuthorityState->Soldiers.Num() >= 10000) return false;
	UWorld* World = GetWorld();
	const UGuLiCommanderDataSubsystem* Data = World->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const FGuLiSoldierDefinition* Definition = Data ? Data->FindSoldierDefinition(UnitTypeId) : nullptr;
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* NavData = Navigation ? GetCommanderNavigationData(*Navigation) : nullptr;
	if (!Definition || !Definition->UsesMass() || !MassSubsystem || !Navigation || !NavData) return false;
	FNavLocation Projected;
	if (!ProjectPointToCommanderNavigation(*Navigation, *NavData, Location,
		FVector(MemberAgentRadiusCentimeters, MemberAgentRadiusCentimeters, 5000.0f), Projected)
		|| FVector::DistSquared2D(Location, Projected.Location) > FMath::Square(MemberAgentRadiusCentimeters)) return false;
	if (AuthorityState->Soldiers.ContainsByPredicate([&](const FSoldierRuntime& Existing)
		{ return Existing.IsPresent() && FVector::DistSquared2D(Existing.Location, Projected.Location) < FMath::Square(MemberAgentRadiusCentimeters * 2); })) return false;
	if (World->OverlapBlockingTestByChannel(Projected.Location + FVector(0,0,MemberAgentRadiusCentimeters + 20),
		FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(MemberAgentRadiusCentimeters))) return false;
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	FMassArchetypeSharedFragmentValues SharedValues = MakeAuthoritySharedFragmentValues(
		EntityManager, MovementSpeedCentimetersPerSecond, MemberAgentRadiusCentimeters);
	TArray<FMassEntityHandle> Handles;
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.BatchCreateEntities(
		AuthorityState->AuthorityArchetype, SharedValues, 1, Handles);
	if (Handles.Num() != 1)
	{
		if (!Handles.IsEmpty()) EntityManager.BatchDestroyEntities(Handles);
		return false;
	}
	FSoldierRuntime& Soldier = AuthorityState->Soldiers.AddDefaulted_GetRef();
	Soldier.Entity = Handles[0];
	Soldier.SoldierId = FGuLiSoldierId(AllocateNonZero(AuthorityState->NextSoldierId));
	Soldier.Team = Team;
	Soldier.Location = Projected.Location;
	Soldier.LastValidNavLocation = Projected;
	InitializeSoldierCombat(Soldier, *Definition, EffectiveRuntimeTuning, World->GetSubsystem<UGuLiArmySkillSubsystem>());
	AuthorityState->SoldierIndexById.Add(Soldier.SoldierId.Value, AuthorityState->Soldiers.Num() - 1);
	EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity).SetTransform(FTransform(Soldier.Location));
	EntityManager.GetFragmentDataChecked<FAgentRadiusFragment>(Soldier.Entity).Radius = MemberAgentRadiusCentimeters;
	FMassMoveTargetFragment& MoveTarget = EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
	MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
	MoveTarget.Center = Soldier.Location;
	MoveTarget.Forward = FVector::ForwardVector;
	FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(Soldier.Entity);
	Identity.SoldierId = Soldier.SoldierId;
	Identity.Team = Team;
	FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
	Health.Health = Soldier.Health;
	Health.bDead = false;
	FGuLiMassSoldierStatsFragment& Stats = EntityManager.GetFragmentDataChecked<FGuLiMassSoldierStatsFragment>(Soldier.Entity);
	Stats.MaxHealth = Soldier.MaxHealth;
	Stats.Defense = Soldier.Defense;
	EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(Soldier.Entity).WorldTarget = Soldier.Location;
	OutId = Soldier.SoldierId;
	RegisterCombatLedgerTarget(OutId);
	return true;
}

namespace
{
	TAutoConsoleVariable<int32> CVarStrongholdTeamUnitCap(TEXT("guli.stronghold.TeamUnitCap"), 300,
		TEXT("Alive Mass combat units plus reserved spawns per team; lowering never deletes existing units."));
}
int32 UGuLiBattleAuthoritySubsystem::GetTeamUnitCap() const
{
	const int32 Cap = CVarStrongholdTeamUnitCap.GetValueOnGameThread();
	checkf(Cap > 0, TEXT("Team unit cap must be positive."));
	return Cap;
}
FIntPoint UGuLiBattleAuthoritySubsystem::GetTeamPopulation(EGuLiTeam Team) const
{
	FIntPoint Result(0, Team == EGuLiTeam::Red ? ReservedRedPopulation : ReservedBluePopulation);
	if (AuthorityState) for (const auto& Soldier : AuthorityState->Soldiers)
		if (Soldier.Team == Team && Soldier.IsAlive()) ++Result.X;
	return Result;
}
int32 UGuLiBattleAuthoritySubsystem::SpawnSoldierBatch(EGuLiTeam Team, uint16 UnitTypeId,
	TConstArrayView<FVector> Locations, TArray<FGuLiSoldierId>& OutIds)
{
	OutIds.Reset();
	if (!IsAuthorityWorld() || !GuLiCommanderProtocol::IsPlayableTeam(Team) || !AuthorityState || !AuthorityState->bPopulationSpawned) return 0;
	const FIntPoint Population = GetTeamPopulation(Team);
	const int32 Count = FMath::Min(Locations.Num(), FMath::Max(0, GetTeamUnitCap() - Population.X - Population.Y));
	int32& Reserved = Team == EGuLiTeam::Red ? ReservedRedPopulation : ReservedBluePopulation;
	Reserved += Count;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FGuLiSoldierId Id;
		if (SpawnReservedSoldier(Team, UnitTypeId, Locations[Index], Id)) OutIds.Add(Id);
		--Reserved; // A failed spawn releases its reservation as well.
	}
	return OutIds.Num();
}
bool UGuLiBattleAuthoritySubsystem::SpawnDebugSoldier(EGuLiTeam Team, uint16 UnitTypeId,
	const FVector& Location, FGuLiSoldierId& OutId)
{
	OutId = {};
#if UE_BUILD_SHIPPING
	return false;
#else
	TArray<FGuLiSoldierId> Spawned;
	SpawnSoldierBatch(Team, UnitTypeId, MakeArrayView(&Location,1), Spawned);
	if (Spawned.IsEmpty()) return false;
	OutId = Spawned[0]; return true;
#endif
}
void UGuLiBattleAuthoritySubsystem::RetireExpiredSoldiers()
{
	auto& Manager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	auto& Ledger = *GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	bool bRemoved = false;
	for (int32 Index = AuthorityState->Soldiers.Num() - 1; Index >= 0; --Index)
	{
		const auto& Soldier = AuthorityState->Soldiers[Index];
		if (!Soldier.bWreckExpired) continue;
		const auto Handle = MakeSoldierTargetHandle(Soldier.SoldierId);
		Ledger.UnregisterTarget(Handle, this); RegisteredCombatLedgerTargets.Remove(Handle);
		Manager.DestroyEntity(Soldier.Entity);
		AuthorityState->Soldiers.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		bRemoved = true;
	}
	if (bRemoved)
	{
		AuthorityState->SoldierIndexById.Reset();
		for (int32 Index = 0; Index < AuthorityState->Soldiers.Num(); ++Index)
			AuthorityState->SoldierIndexById.Add(AuthorityState->Soldiers[Index].SoldierId.Value, Index);
		AuthorityState->CachedManualAvoidanceVelocities.Reset();
		AuthorityState->bForceManualAvoidanceRefresh = true;
	}
}
void UGuLiBattleAuthoritySubsystem::RegisterAutomaticAdvance(TConstArrayView<FGuLiSoldierId> Soldiers)
{
	check(IsAuthorityWorld());
	for (auto Id : Soldiers)
		AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)].bAutomaticAdvance = true;
}
bool UGuLiBattleAuthoritySubsystem::IsAutomaticallyAdvancing(FGuLiSoldierId Id) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	return Index && AuthorityState->Soldiers[*Index].IsAlive() && AuthorityState->Soldiers[*Index].bAutomaticAdvance;
}
void UGuLiBattleAuthoritySubsystem::StopAutomaticMove(TConstArrayView<FGuLiSoldierId> Soldiers)
{
	check(IsAuthorityWorld());
	auto& Manager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	for (auto Id : Soldiers)
	{
		if (!IsAutomaticallyAdvancing(Id)) continue;
		auto& Soldier = AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)];
		Soldier.ActiveOrderId = 0; Soldier.bHasFinalDestination = false;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Idle;
		Soldier.Velocity = FVector::ZeroVector; ++Soldier.StateRevision;
		auto& Order = Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = 0; Order.bHasMoveTarget = false; Order.OrderRevision = Soldier.StateRevision;
		auto& Move = Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.CreateNewAction(EMassMovementAction::Stand, *GetWorld()); Move.Center = Soldier.Location;
		Move.DesiredSpeed = FMassInt16Real(0.f);
	}
}
bool UGuLiBattleAuthoritySubsystem::IssueAttackMove(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers,
	const FVector& Destination)
{
	using namespace GuLiCommanderMassPrivate;
	if (!IsAuthorityWorld() || !AuthorityState || !AuthorityState->bPopulationSpawned || Destination.ContainsNaN()
		|| Soldiers.IsEmpty() || Soldiers.Num() > SoldierCountPerFormation) return false;
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	auto* NavData = Navigation ? GetCommanderNavigationData(*Navigation) : nullptr;
	if (!NavData || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return false;
	FOrderFormationRuntime Formation;
	Formation.Team = Team;
	FNavLocation Target;
	if (!ProjectPointToCommanderNavigation(*Navigation, *NavData, Destination,
		FVector(MemberAgentRadiusCentimeters,MemberAgentRadiusCentimeters,5000), Target)) return false;
	for (int32 Index = 0; Index < Soldiers.Num(); ++Index)
	{
		const auto Id = Soldiers[Index];
		const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Id.Value);
		if (!SoldierIndex) continue;
		const auto& Soldier = AuthorityState->Soldiers[*SoldierIndex];
		if (!Soldier.CanAct() || !Soldier.bAutomaticAdvance || Soldier.Team != Team) continue;
		const FVector Offset((Index % 5 - 2) * DestinationMinimumSeparationCentimeters,
			(Index / 5 - 2) * DestinationMinimumSeparationCentimeters, 0);
		FNavLocation End;
		if (!ProjectPointToCommanderNavigation(*Navigation, *NavData, Target.Location + Offset,
			FVector(MemberAgentRadiusCentimeters,MemberAgentRadiusCentimeters,5000), End)) continue;
		if (FVector::DistSquared2D(End.Location, Target.Location + Offset) > FMath::Square(MemberAgentRadiusCentimeters)) continue;
		if (GetWorld()->OverlapBlockingTestByChannel(End.Location + FVector(0,0,MemberAgentRadiusCentimeters+20),
			FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(MemberAgentRadiusCentimeters))) continue;
		Formation.MemberIds.Add(Id);
		Formation.CommandStartNavLocationBySoldierId.Add(Id.Value, Soldier.LastValidNavLocation);
		Formation.FinalDestinationBySoldierId.Add(Id.Value, End);
	}
	if (Formation.MemberIds.Num() != Soldiers.Num()) return false;
	const FVector Start = Formation.CommandStartNavLocationBySoldierId.FindChecked(Formation.MemberIds[0].Value).Location;
	FPathFindingQuery Query(this, *NavData, Start, Target.Location);
	const FPathFindingResult Path = Navigation->FindPathSync(Query);
	if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial()) return false;
	for (const FNavPathPoint& Point : Path.Path->GetPathPoints()) Formation.PathPoints.Add(Point.Location);
	if (Formation.PathPoints.Num() < 2) return false;
	Formation.FormationId = AllocateNonZero(AuthorityState->NextFormationId);
	Formation.BatchOrderId = AllocateNonZero(AuthorityState->NextBatchOrderId);
	Formation.GuideAnchor = Start; Formation.TargetAnchor = Target.Location; Formation.PathPointIndex = 1;
	Formation.FinalPathFrame = GuLiCommanderNavigationPolicy::ResolveFinalPathFrame(Formation.PathPoints, Start, Target.Location);
	Formation.TravelFacingYawDegrees = (Formation.PathPoints[1] - Start).Rotation().Yaw;
	Formation.FinalApproachTriggerRadiusCentimeters = DestinationMinimumSeparationCentimeters * 4;
	auto& Manager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	for (auto Id : Formation.MemberIds)
	{
		auto& Soldier = AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)];
		Soldier.ActiveOrderId = Formation.BatchOrderId;
		Soldier.FinalDestination = Formation.FinalDestinationBySoldierId.FindChecked(Id.Value);
		Soldier.FinalDestinationNavigationGeneration = AuthorityState->NavigationGeneration;
		Soldier.bHasFinalDestination = true; Soldier.bForceMovementUpdate = true;
		Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Normal;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
		Soldier.NoProgressSeconds = 0; Soldier.FailureSimulationSeconds = 0;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		Soldier.LastProgressPathPointIndex = 1;
		Soldier.ConsecutiveSurfaceFailures = 0; Soldier.TotalSurfaceFailures = 0;
		Soldier.PersonalPathPoints.Reset(); Soldier.PersonalPathPointIndex = 0; Soldier.PersonalPathRetries = 0;
		++Soldier.StateRevision;
		Formation.MemberPathPointIndexBySoldierId.Add(Id.Value, 1);
		auto& Order = Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = Formation.BatchOrderId; Order.OrderRevision = Soldier.StateRevision;
		Order.FormationTarget = Soldier.FinalDestination.Location; Order.bHasMoveTarget = true;
		auto& Move = Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.CreateNewAction(EMassMovementAction::Move, *GetWorld());
		Move.IntentAtGoal = EMassMovementAction::Stand; Move.Center = Soldier.FinalDestination.Location;
		Move.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
		Manager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity).Value = FVector::ZeroVector;
	}
	AssignFormationSlots(Formation, AuthorityState->Soldiers, AuthorityState->SoldierIndexById,
		MemberSpacingCentimeters, Formation.BatchOrderId, Formation.TransitColumnCount);
	AuthorityState->OrderFormations.Add(MoveTemp(Formation));
	AuthorityState->bForceManualAvoidanceRefresh = true;
	return true;
}


void UGuLiBattleAuthoritySubsystem::ExecuteSelectedUnitSkills(AGuLiBattlePlayerState& PlayerState,
	const FGuLiCommanderSelectionState& Selection, const UGuLiCommanderSkillCatalog& Catalog,
	FGuid RequestId, bool bHasGroundPoint, FVector GroundPoint, TArray<FGuLiActiveSkillUnitResult>& OutResults)
{
	OutResults.Reset();
	if (!IsAuthorityWorld() || !AuthorityState || !PlayerState.IsCommander() || !RequestId.IsValid()) return;
	TSet<uint32> Visited;
	const double Now = AuthorityState->SimulationSeconds;
	for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds)
	{
		if (Visited.Contains(Id.Value)) continue;
		Visited.Add(Id.Value);
		const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
		if (!Index)
		{
			auto& Result = OutResults.AddDefaulted_GetRef(); Result.SoldierId = Id;
			Result.Code = EGuLiActiveSkillResultCode::Ineligible; continue;
		}
		auto& Soldier = AuthorityState->Soldiers[*Index];
		const auto* Definition = Catalog.FindUnitSkill(Soldier.UnitTypeId);
		FGuLiUnitSkillCaster Caster;
		Caster.bEligible = Soldier.Team == PlayerState.GetTeam() && Soldier.CanAct() && Soldier.IsPresent();
		Caster.bHasGroundPoint = bHasGroundPoint;
		auto& Context = Caster.Context;
		Context.Commander = &PlayerState; Context.SoldierId = Id; Context.Source = MakeSoldierTargetHandle(Id);
		Context.SourceTransform = FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location);
		Context.GroundPoint = GroundPoint; Context.RequestId = RequestId; Context.SkillId = Definition ? Definition->SkillId : NAME_None;
		Context.Level = Soldier.ActiveSkill.Level;
		OutResults.Add(GuLiUnitSkillExecution::Execute(Definition, Caster, Soldier.ActiveSkill, Now, GetWorld()->GetTimeSeconds(),
			[Definition](const FGuLiActiveSkillExecutionContext& Cast, const UDataAsset* Configuration)
			{ return Definition->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->Execute(Cast, Configuration); }));
	}
}

bool UGuLiBattleAuthoritySubsystem::QueryUnitSkillRuntime(FGuLiSoldierId SoldierId, const UGuLiCommanderSkillCatalog& Catalog, FGuLiActiveSkillRuntime& OutRuntime) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(SoldierId.Value) : nullptr;
	if (!Index) return false;
	const auto& Soldier = AuthorityState->Soldiers[*Index];
	const auto* Definition = Catalog.FindUnitSkill(Soldier.UnitTypeId);
	if (!Definition) return false;
	OutRuntime = Soldier.ActiveSkill;
	OutRuntime.SkillId = Definition->SkillId;
	OutRuntime.ReadyAt = GetWorld()->GetTimeSeconds() + FMath::Max(0., OutRuntime.ReadyAt - AuthorityState->SimulationSeconds);
	return true;
}

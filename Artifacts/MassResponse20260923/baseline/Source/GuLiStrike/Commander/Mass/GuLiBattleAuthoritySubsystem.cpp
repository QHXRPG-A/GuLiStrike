// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/GuLiCommanderSpawnLayout.h"
#include "Commander/Behavior/GuLiCommanderMassStateTreeProcessor.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Units/GuLiGroundCrowdManager.h"
#include "Commander/Network/GuLiCommanderPoseCodec.h"

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
#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"
#include "Containers/Queue.h"
#include "Commander/Mass/Navigation/GuLiIncrementalAssignment.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"
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
	constexpr int32 TotalSoldierCount = GuLiCommanderInitialSpawn::Population;
	constexpr int32 FormationColumns = 5;
	constexpr int32 FormationRows = 5;
	// 模拟按固定步长推进；最多积累 4 步，卡顿时舍弃超额时间，避免单帧无限追赶。
	constexpr float FixedStepSeconds = GuLiCommanderSimulationTiming::StepSeconds;
	constexpr float MaxAccumulatedSeconds = FixedStepSeconds * 4.0f;
	// 此常量及下方 FRequestGate 当前未接入请求路径；实际网络限流由 NetSyncComponent 负责。
	constexpr int32 MaxRequestsPerSecond = 10;
	constexpr float SpatialCellSizeCentimeters = 2000.0f;
	constexpr float FormationGuideMaximumLeadCentimeters = 1800.0f;
	constexpr float FormationWaypointToleranceCentimeters = 360.0f;
	constexpr float FormationArrivalToleranceCentimeters = 100.0f;
	constexpr float TravelWeight = 0.70f;
	constexpr float SlotCorrectionWeight = 0.30f;
	constexpr float ManualAvoidanceStrength = 0.25f;
	constexpr int32 MaximumFriendlyYieldUnitsPerMech = 16;
	constexpr float GroundMechYieldQueryCellSizeCentimeters = 1000.0f;
	constexpr float GroundMechYieldActivationPaddingCentimeters = 100.0f;
	constexpr float GroundMechYieldClearancePaddingCentimeters = 50.0f;
	constexpr float GroundMechYieldSpeedFraction = 0.25f;
	constexpr float GroundMechYieldMaximumAnchorOffsetCentimeters = 1250.0f;
	constexpr double GroundMechYieldReturnDelaySeconds = 0.5;
	constexpr float GroundMechYieldArrivalToleranceCentimeters = 5.0f;
	constexpr float MaximumSurfaceStepZCentimeters = 50.0f;
	constexpr float ProgressDistanceCentimeters = 6.0f;
	constexpr float CenterlineRecoverySeconds = 1.0f;
	constexpr float PersonalRecoveryRetrySeconds = 2.0f;
	constexpr float PersonalRecoveryBlockedSeconds = 4.0f;
	constexpr int32 SurfaceFailuresBeforeCenterline = 2;
	constexpr int32 SurfaceFailuresBeforePersonalPath = 6;
	constexpr int32 MaximumPersonalPathQueriesPerStep = 4;
	constexpr int32 ExpansionSuccessfulStepsRequired = GuLiCommanderNavigationPolicy::RequiredTransitExpansionSuccessSteps;
	constexpr float TransitReassignmentCooldownSeconds = 0.5f;
	constexpr float DestinationMinimumSeparationCentimeters = GuLiCommanderInitialSpawn::MinimumSeparation;
	constexpr float DestinationMaximumProjectionCorrectionCentimeters = GuLiCommanderInitialSpawn::MaximumProjectionCorrection;
	constexpr float FreeDestinationMaximumRadiusCentimeters = 9000.0f;
	constexpr int32 MoveCandidateProjectionBudgetPerFrame = 64;
	constexpr int32 MovePathQueryBudgetPerFrame = 4;
	constexpr float MoveCandidateReserveFraction = 0.25f;
	constexpr int32 MoveCandidateMinimumReserveSlots = 8;
	constexpr int32 MoveCandidateMaximumReserveSlots = 32;
	constexpr int32 MoveCommitConnectionRetryLimit = 2;
	// Spawn centers are authored in XY with Z=0 while the terrain is far from world zero.
	// Keep the strict XY correction, but search the full map-height range vertically.
	constexpr float SpawnProjectionVerticalExtentCentimeters = GuLiCommanderInitialSpawn::ProjectionVerticalExtent;

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
		float AvoidanceRadiusCentimeters = 150.0f;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		double LastMovementUpdateSimulationSeconds = 0.0;
		float FacingYawDegrees = 0.0f;
		float Health = 100.0f;
		float MaxHealth = 100.0f;
		float Defense = 0.0f;
		TSharedPtr<const TArray<FGuLiResolvedSkillProfile>> WeaponProfiles;
		TArray<FSoldierWeaponRuntime> Weapons;
		bool bAllowAutomaticFire = true;
		// Unique active skill is independent of automatic weapon slots and advances on SimulationSeconds.
		FGuLiActiveSkillRuntime ActiveSkill;
		// StateRevision 标识离散状态变化；ActiveOrderId 指向当前批次，0 表示无活动指令。
		uint32 StateRevision = 1u;
		uint32 ActiveOrderId = 0u;
		uint64 TaskGeneration = 0;
		bool bAutomaticAdvance = false;
		bool bAttackMoveHolding = false;
		FNavLocation LastValidNavLocation;
		FNavLocation FinalDestination;
		FVector CommandStartLocation = FVector::ZeroVector;
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
		double LastCapturedPoseSimulationSeconds = 0.0;
		float LastCapturedMovementSpeed = 0.0f;
		// The wreck window retains the identity; expiration retires both entity and replicated record.
		double DeathSimulationSeconds = -1.0;
		bool bWreckExpired = false;
		FGuid ExternalControlToken;
		bool bPhased = false;
		bool bExternalActionsLocked = false;
		uint32 ContactObstacle=0;
		float BypassSide=0, ContactClearSeconds=0, FormationCorrectionAlpha=1.f, EnvironmentSpeedScale=1.f;
		bool bGroundMechYielding = false;
		FVector GroundMechYieldAnchor = FVector::ZeroVector;
		FVector GroundMechYieldTarget = FVector::ZeroVector;
		double GroundMechLastPressureSimulationSeconds = -1.0;
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

	FVector ConstrainEnvironmentVelocity(FSoldierRuntime& Soldier,const FGuLiDynamicObstacleSnapshot& Snapshot,FVector Velocity,float Dt,float Speed)
	{
		TArray<int32> Nearby; const float Reach=FMath::Max(80.f,Speed*.5f);
		Snapshot.Query(Soldier.Location,Soldier.AvoidanceRadiusCentimeters+Reach+100.f,Nearby);
		const FGuLiDynamicObstacle* Chosen=nullptr; double Best=TNumericLimits<double>::Max();
		if (const auto* I=Snapshot.ByHandle.Find(Soldier.ContactObstacle))
		{
			const auto& O=Snapshot.Obstacles[*I];
			if (FVector::Dist2D(Soldier.Location,O.Location)<O.RadiusCentimeters+Soldier.AvoidanceRadiusCentimeters+Reach+100.f
				&& FMath::Abs(Soldier.Location.Z-O.Location.Z)<=300.f) Chosen=&O;
		}
		for (int32 I : Nearby)
		{
			const auto& O=Snapshot.Obstacles[I]; if (FMath::Abs(Soldier.Location.Z-O.Location.Z)>300.f) continue;
			const double Gap=FVector::Dist2D(Soldier.Location,O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
			if (!Chosen && Gap<Best && Gap<Reach) { Best=Gap; }
		}
		if (!Chosen && !Soldier.ContactObstacle)
			for (int32 I : Nearby)
			{
				const auto& O=Snapshot.Obstacles[I];
				const double Gap=FVector::Dist2D(Soldier.Location,O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
				if (FMath::Abs(Soldier.Location.Z-O.Location.Z)<=300.f && Gap<=Best && Gap<Reach)
				{ Chosen=&O; Best=Gap; }
			}
		float Pressure=0;
		if (Chosen)
		{
			FVector Normal=(Soldier.Location-Chosen->Location).GetSafeNormal2D();
			if (Normal.IsNearlyZero()) Normal=FVector(1,0,0);
			const FVector Left(-Normal.Y,Normal.X,0);
			if (!Soldier.ContactObstacle)
			{
				Soldier.ContactObstacle=Chosen->Handle.Value;
				const double Side=FVector::DotProduct(Velocity,Left);
				Soldier.BypassSide=FMath::Abs(Side)>1. ? (Side>0 ? 1.f : -1.f) : ((Soldier.SoldierId.Value&1) ? 1.f : -1.f);
			}
			const double Gap=FVector::Dist2D(Soldier.Location,Chosen->Location)-Chosen->RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
			Pressure=FMath::Clamp(1.f-static_cast<float>(Gap)/Reach,0.f,1.f);
			if (Gap<Reach) Soldier.ContactClearSeconds=0; else Soldier.ContactClearSeconds+=Dt;
			const double Inward=FVector::DotProduct(Velocity,Normal);
			if (Inward<0 && Pressure>0)
			{
				// At contact project onto the tangent, never apply alternating radial pushback.
				Velocity-=Normal*Inward*Pressure;
				Velocity+=Left*Soldier.BypassSide*Speed*.5f*Pressure;
			}
			if (Gap<2.f) { Velocity-=Normal*FMath::Min(0.,FVector::DotProduct(Velocity,Normal)); Velocity+=Normal*FMath::Min(Speed*.25f,static_cast<float>(2.-Gap)/FMath::Max(Dt,.001f)); }
		}
		else Soldier.ContactClearSeconds+=Dt;
		if (Soldier.ContactClearSeconds>=.6f) { Soldier.ContactObstacle=0; Soldier.BypassSide=0; }
		Soldier.EnvironmentSpeedScale=FMath::FInterpTo(Soldier.EnvironmentSpeedScale,1.f-.5f*Pressure,Dt,6.f);
		Soldier.FormationCorrectionAlpha=FMath::FInterpTo(Soldier.FormationCorrectionAlpha,
			Soldier.ConsecutiveSurfaceFailures ? 0.f : 1.f-Pressure,Dt,Pressure>0 ? 8.f : 2.f);
		// Constrain every nearby obstacle, including those other than the latched steering obstacle.
		for (int32 I : Nearby)
		{
			const auto& O=Snapshot.Obstacles[I]; if (FMath::Abs(Soldier.Location.Z-O.Location.Z)>300.f) continue;
			const FVector N=(Soldier.Location-O.Location).GetSafeNormal2D();
			const double Gap=FVector::Dist2D(Soldier.Location,O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
			const double Into=FVector::DotProduct(Velocity,N);
			const double Limit=-FMath::Max(0.,Gap-1.)/FMath::Max(.001f,Dt);
			if (Into<Limit) Velocity+=N*(Limit-Into);
		}
		return Velocity.GetClampedToMaxSize(Speed*Soldier.EnvironmentSpeedScale);
	}

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
		float MemberSpacingCentimeters = 360.0f;
		float MaximumMemberRadiusCentimeters = 150.0f;
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

	struct FSteeringValidation
	{
		FVector RequestedLane=FVector::ZeroVector, RequestedSlot=FVector::ZeroVector;
		FNavLocation Origin, Lane, Slot;
		uint32 Order=0, Path=0, Nav=0, Obstacles=0;
		uint8 Stage=0;
		bool bLaneValid=false, bSlotValid=false;
	};

	struct FMoveDestinationReservation
	{
		uint32 OwnerSoldierId = 0u;
		FVector Location = FVector::ZeroVector;
		float RadiusCentimeters = 150.0f;
	};

	/** Live team reservations. Pending batches only release their own current members. */
	struct FMoveReservationLedger
	{
		struct FEntry : FMoveDestinationReservation { EGuLiTeam Team = EGuLiTeam::Unassigned; };
		static constexpr float CellSize = 600.0f;
		TMap<uint32, FEntry> Entries;
		TMap<FIntPoint, TSet<uint32>> Cells;
		float MaximumRadius = 150.0f;
		static FIntPoint Cell(const FVector& P)
		{ return FIntPoint(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize)); }
		void Remove(uint32 Id)
		{
			if (const FEntry* E = Entries.Find(Id))
			{
				const FIntPoint Key = Cell(E->Location);
				if (TSet<uint32>* Bucket = Cells.Find(Key))
				{ Bucket->Remove(Id); if (Bucket->IsEmpty()) Cells.Remove(Key); }
				Entries.Remove(Id);
			}
		}
		void Update(const FSoldierRuntime& Soldier)
		{
			const uint32 Id = Soldier.SoldierId.Value;
			if (!Soldier.IsPresent()) { Remove(Id); return; }
			const FVector Location = Soldier.bHasFinalDestination && Soldier.ActiveOrderId
				? Soldier.FinalDestination.Location : Soldier.LastValidNavLocation.Location;
			const FEntry* Previous = Entries.Find(Id);
			if (Previous && Previous->Location.Equals(Location, .01)
				&& Previous->Team == Soldier.Team && Previous->RadiusCentimeters == Soldier.AvoidanceRadiusCentimeters) return;
			Remove(Id);
			FEntry E; E.OwnerSoldierId = Id; E.Location = Location;
			E.Team = Soldier.Team; E.RadiusCentimeters = Soldier.AvoidanceRadiusCentimeters;
			Entries.Add(Id, E); Cells.FindOrAdd(Cell(Location)).Add(Id);
			MaximumRadius = FMath::Max(MaximumRadius, E.RadiusCentimeters);
		}
	};

	struct FMoveMemberPlan
	{
		uint64 TaskGeneration = 0;
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
		TArray<FVector> CachedPath;
		int32 NextConnector = 0;
		bool bPathQueried = false;
		bool bPathValid = false;
		bool bNeedsCandidate=false;
		int32 NextFallbackSlot=0;
	};

	enum class EMovePlanningStage : uint8
	{
		PrepareBatch,
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
		FGuLiIncrementalAssignment Matching;
		TArray<int32> MatchingMembers;
		bool bMatchingStarted=false;
		FGuLiMovePlanHandle Handle;
		FGuLiMovePlanProgress Progress;
		FGuLiCommanderSelectionState FullSelection;
		FGuLiCommandAck AggregateAck;
		TMap<uint32, uint64> FrozenTaskGenerations;
		TSet<uint32> ReleasedReservationIds;
		const FMoveReservationLedger* Reservations = nullptr;
		TMap<uint32, int32> MemberIndexById;
		TMap<int32, int32> LegalSlotIndexByCandidate;
		int32 NextCohort = 0;
		int32 CandidateRow = INDEX_NONE;
		bool bCandidatesGenerated = false;
		TSet<uint32> FinishedIds;
		int32 TotalAccepted = 0;
		int32 TotalEligible = 0;
		uint32 SharedBatchOrderId = 0;
		double LastProgressAt = 0;
		double FirstCommitAt = 0;
		bool bAutomatic = false;
		bool bActorsSubmitted = false;
		bool bActorsAccepted = false;
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
		TMap<int32, FNavLocation> ProjectionCache;
		TSet<int32> InvalidProjectionCache;
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
		float MaximumMemberRadiusCentimeters = 150.0f;
		float MinimumSlotSpacingCentimeters = DestinationMinimumSeparationCentimeters;
		float DestinationBucketSizeCentimeters = DestinationMinimumSeparationCentimeters;
		EMovePlanningStage Stage = EMovePlanningStage::PrepareBatch;
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

	struct FNavigationRecoveryWork
	{
		FGuLiSoldierId Id;
		uint64 TaskVersion=0;
		uint32 Order=0, Nav=0, Epoch=0;
		FNavLocation Start, Final;
		FVector RequestedFinal=FVector::ZeroVector;
		TArray<FVector> Path;
		int32 Stage=0, Candidate=0;
		double Started=0, Progress=0;
		bool bReady=false, bValid=false, bHadFinal=false;
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
		Job.bMatchingStarted=false; Job.MatchingMembers.Reset();
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

	FIntPoint MakeMoveDestinationBucket(const FVector& Location, const FMovePlanningJob& Job)
	{
		return FIntPoint(
			FMath::FloorToInt(Location.X / Job.DestinationBucketSizeCentimeters),
			FMath::FloorToInt(Location.Y / Job.DestinationBucketSizeCentimeters));
	}

	void AddHardReservationToMoveJob(
		FMovePlanningJob& Job,
		const FMoveDestinationReservation& Reservation)
	{
		const int32 ReservationIndex = Job.HardReservations.Add(Reservation);
		Job.HardReservationBuckets.FindOrAdd(
			MakeMoveDestinationBucket(Reservation.Location, Job)).Add(ReservationIndex);
	}

	bool IsMoveCandidateBlockedByHardReservation(
		const FMovePlanningJob& Job,
		const FVector& CandidateLocation)
	{
		if (Job.Reservations)
		{
			const auto& Ledger = *Job.Reservations;
			const auto Cell = FMoveReservationLedger::Cell(CandidateLocation);
			const int32 Range = FMath::CeilToInt(FMath::Max(DestinationMinimumSeparationCentimeters,
				Job.MaximumMemberRadiusCentimeters + Ledger.MaximumRadius) / FMoveReservationLedger::CellSize);
			for (int32 X = -Range; X <= Range; ++X) for (int32 Y = -Range; Y <= Range; ++Y)
				if (const auto* Ids = Ledger.Cells.Find(Cell + FIntPoint(X,Y))) for (const uint32 Id : *Ids)
				{
					if (Job.ReleasedReservationIds.Contains(Id)) continue;
					const auto* E = Ledger.Entries.Find(Id);
					if (E && E->Team == Job.Team && FVector::DistSquared2D(CandidateLocation, E->Location)
						< FMath::Square(FMath::Max(DestinationMinimumSeparationCentimeters, Job.MaximumMemberRadiusCentimeters + E->RadiusCentimeters))) return true;
				}
		}
		const FIntPoint CenterBucket = MakeMoveDestinationBucket(CandidateLocation, Job);
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
							CandidateLocation) < FMath::Square(FMath::Max(DestinationMinimumSeparationCentimeters,
							Job.MaximumMemberRadiusCentimeters + Job.HardReservations[ReservationIndex].RadiusCentimeters)))
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
		const FIntPoint CenterBucket = MakeMoveDestinationBucket(CandidateLocation, Job);
		const double MinimumSeparationSquared =
			FMath::Square(static_cast<double>(Job.MinimumSlotSpacingCentimeters));
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
		if (Result != EGuLiCommandAckResult::Cancelled)
		{
			const TCHAR* StageName = TEXT("Unknown");
			switch (Job.Stage)
			{
			case EMovePlanningStage::ValidateStarts: StageName = TEXT("ValidateStarts"); break;
			case EMovePlanningStage::ProjectCandidates: StageName = TEXT("ProjectCandidates"); break;
			case EMovePlanningStage::AssignDestinations: StageName = TEXT("AssignDestinations"); break;
			case EMovePlanningStage::Route: StageName = TEXT("Route"); break;
			case EMovePlanningStage::ReconcileReservations: StageName = TEXT("ReconcileReservations"); break;
			case EMovePlanningStage::ReadyToCommit: StageName = TEXT("ReadyToCommit"); break;
			case EMovePlanningStage::Completed: StageName = TEXT("Completed"); break;
			}
			UE_LOG(LogGuLiCommanderMass, Warning,
				TEXT("Move planning terminated command=%u team=%u ack=%u stage=%s elapsed=%.3fs frames=%d starts=%d/%d legal=%d projections=%d paths=%d pending_routes=%d connector_failures=%llu."),
				Job.Request.ClientCommandId, static_cast<uint8>(Job.Team), static_cast<uint8>(Result),
				StageName, FPlatformTime::Seconds() - Job.PlanningStartedAt, Job.Debug.PlanningWorldFrames,
				Job.NextStartValidationIndex, Job.Members.Num(), Job.LegalSlots.Num(),
				Job.Debug.CandidateProjectionQueries, Job.Debug.PathQueries, Job.RouteTasks.Num(),
				Job.Debug.FailureCounts[static_cast<uint8>(EGuLiMovePlanFailureStage::Connector)]);
		}
		for (const auto& Cohort : Job.FullSelection.Cohorts)
		{
			if (Job.AggregateAck.CohortResults.ContainsByPredicate([&](const auto& R) { return R.CohortId == Cohort.CohortId; })) continue;
			auto Receipt = decltype(Job.Ack.CohortResults)::ElementType{};
			Receipt.CohortId = Cohort.CohortId; Receipt.MemberCount = static_cast<uint8>(Cohort.MemberIds.Num()); Receipt.Result = Result;
			for (auto Id : Cohort.MemberIds) if (!Job.FinishedIds.Contains(Id.Value))
			{ Job.Progress.Failed.Add(Id); Job.FinishedIds.Add(Id.Value); }
			Job.AggregateAck.CohortResults.Add(Receipt);
		}
		Job.Ack = Job.AggregateAck;
		Job.Ack.BatchOrderId = Job.SharedBatchOrderId;
		Job.Ack.Result = Job.TotalAccepted == Job.FinishedIds.Num() && Job.TotalAccepted > 0
			? EGuLiCommandAckResult::Accepted : Job.TotalAccepted || Job.bActorsAccepted
				? EGuLiCommandAckResult::PartiallyAccepted : Result;
		Job.FrozenTaskGenerations.Reset();
		Job.Ack.ServerSelectionRevision = Job.FullSelection.SelectionRevision;
		Job.Ack.Sanitize();
		Job.Progress.bComplete = true;
		Job.Progress.BatchOrderId = Job.SharedBatchOrderId;
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
		const int32 ColumnCount, FGuLiNavigationWorkBudget& Budget)
	{
		const FRotator FacingRotation(0.0f, FacingYawDegrees, 0.0f);
		for (int32 ProbeIndex = 0; ProbeIndex < ColumnCount; ++ProbeIndex)
		{
			FVector LocalOffset = FVector::ZeroVector;
			LocalOffset.Y = (static_cast<float>(ProbeIndex) - static_cast<float>(ColumnCount - 1) * 0.5f)
				* Spacing;
			const FVector RequestedPoint = Anchor + FacingRotation.RotateVector(LocalOffset);
			FNavLocation ProjectedPoint;
			if (!Budget.TakeProjection()) return false;
			FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
			const bool bProbeWalkable = ProjectPointToCommanderNavigation(
				NavigationSystem,
				NavigationData,
				RequestedPoint,
				FVector(50.0f, 50.0f, 5000.0f),
				ProjectedPoint)
				&& FVector::DistSquared2D(RequestedPoint, ProjectedPoint.Location)
					<= FMath::Square(100.0f);
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
		const float Spacing, FGuLiNavigationWorkBudget& Budget, const int32 PreviousColumns)
	{
		FGuLiNavigationWorkBudget::FScope Scope(Budget);
		if (Budget.Projections<15 || !Budget.CanWork()) return PreviousColumns;
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
				ColumnCount,Budget))
			{
				FitsByColumnCount[ColumnCount - 1] = 1u;
				break;
			}
			if (!Budget.CanWork()) return PreviousColumns;
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
		AvoidanceParameters.ObstaclePredictiveAvoidanceStiffness = 140.0f;

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

	bool FindMoveAlongCurrentNavigationSurface(
		const ANavigationData& NavigationData,
		const FNavLocation& Start,
		const FVector& Target,
		FNavLocation& OutLocation,
		FGuLiNavigationWorkBudget& Budget,
		const UObject* Querier = nullptr)
	{
		FNavLocation CurrentStart = Start;
		if (!NavigationData.IsNodeRefValid(CurrentStart.NodeRef))
		{
			// Dynamic tile rebuilds invalidate polygon references even when the same
			// ground remains walkable. Refresh only the reference at this position;
			// never turn a stale reference into a teleport past a new obstacle.
			FGuLiNavigationWorkBudget::FScope Scope(Budget);
			if (!Budget.TakeProjection()) return false;
			FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
			if (!NavigationData.ProjectPoint(Start.Location, CurrentStart,
					FVector(10.0f, 10.0f, 5000.0f), nullptr, Querier)
				|| FVector::DistSquared2D(Start.Location, CurrentStart.Location) > FMath::Square(10.0f)
				|| FMath::Abs(Start.Location.Z - CurrentStart.Location.Z) > MaximumSurfaceStepZCentimeters)
			{
				return false;
			}
			CurrentStart.Location.X = Start.Location.X;
			CurrentStart.Location.Y = Start.Location.Y;
		}
		return NavigationData.FindMoveAlongSurface(CurrentStart, Target, OutLocation, nullptr, Querier);
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
		const float ToleranceCentimeters = 20.0f)
	{
		if (!Start.NodeRef || Start.Location.ContainsNaN() || Target.ContainsNaN()) return false;
		FVector Hit;
		return !NavigationData.Raycast(Start.Location,Target,Hit,NavigationData.GetDefaultQueryFilter())
			&& FVector::DistSquared2D(Hit,Target)<=FMath::Square(static_cast<double>(ToleranceCentimeters));
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
		float SlotSpacing,
		const uint32 RequiredOrderId,
		const int32 ColumnCount = FormationColumns)
	{
		Formation.SlotBySoldierId.Reset();
		Formation.MaximumMemberRadiusCentimeters = 0.0f;
		TArray<FGuLiSoldierId> ValidMembers;
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* Index = SoldierIndexById.Find(SoldierId.Value);
			if (Index && Soldiers.IsValidIndex(*Index) && Soldiers[*Index].IsAlive()
				&& (RequiredOrderId == 0u || Soldiers[*Index].ActiveOrderId == RequiredOrderId))
			{
				ValidMembers.Add(SoldierId);
				Formation.MaximumMemberRadiusCentimeters = FMath::Max(
					Formation.MaximumMemberRadiusCentimeters, Soldiers[*Index].AvoidanceRadiusCentimeters);
			}
		}
		SlotSpacing = FMath::Max(SlotSpacing, Formation.MaximumMemberRadiusCentimeters * 2.0f);
		Formation.MemberSpacingCentimeters = SlotSpacing;
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
	GuLiCommanderMassPrivate::FMoveReservationLedger MoveReservations;
	FGuLiNavigationWorkBudget PlanningBudget;
	TMap<uint32,GuLiCommanderMassPrivate::FSteeringValidation> Steering;
	TQueue<uint32> SteeringQueue;
	TSet<uint32> SteeringQueued;
	FGuLiNavigationWorkBudget CommitBudget;
	uint64 NextPlanId = 1;
	uint32 PlanningCursor = 0;
	int32 ReservationBootstrapCursor = 0;
	TMap<uint32, FGuLiMoveEndpointSnapshot> ActiveEndpoints;
	TSet<uint32> DirtyEndpointIds;
	TSet<uint32> RemovedEndpointIds;
	TMap<uint32,GuLiCommanderMassPrivate::FNavigationRecoveryWork> RecoveryWork;
	TQueue<uint32> RecoveryQueue, ReadyRecoveryQueue;
	uint32 RecoveryScanCursor=0;
	uint64 RecoveryQueries=0, RecoveryFailures=0;
	double NextPlanningDiagnostic=0;
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
	uint64 GroundMechYieldSteps = 0u;
	int32 GroundMechYieldingSoldiers = 0;
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
	AuthorityState->PlanningBudget.Reset(MovePlanningMilliseconds, MovePositionQueriesPerFrame, MovePathQueriesPerFrame);
	AuthorityState->CommitBudget.Reset(MoveCommitMilliseconds, 0, 0, FMath::Max(25, MoveCommitMembersPerFrame));
	{
		FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
		int32 BootstrapCount = 0;
		while (AuthorityState->ReservationBootstrapCursor < AuthorityState->Soldiers.Num()
			&& BootstrapCount++ < 256 && AuthorityState->PlanningBudget.CanWork())
			RefreshSoldierNavigationState(AuthorityState->Soldiers[AuthorityState->ReservationBootstrapCursor++].SoldierId);
	}
	// Rotate priority, so a continuous stream of repairs cannot starve new orders.
	if ((GFrameCounter & 1u) == 0)
		TickNavigationRepairs(AuthorityState->PlanningBudget.Projections, AuthorityState->PlanningBudget.Paths);
	if (GFrameCounter % 3 == 0) TickSteeringValidation();
	TickMovePlanning(AuthorityState->PlanningBudget.Projections, AuthorityState->PlanningBudget.Paths);
	if (GFrameCounter % 3 != 0) TickSteeringValidation();
	if ((GFrameCounter & 1u) != 0)
		TickNavigationRepairs(AuthorityState->PlanningBudget.Projections, AuthorityState->PlanningBudget.Paths);
	TickLocalFlowFields();

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
	PublishMoveEndpointChanges();
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, PlanningMs, AuthorityState->PlanningBudget.Elapsed()*1000., ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, PositionChecks, MovePositionQueriesPerFrame-AuthorityState->PlanningBudget.Projections, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, PlanningPaths, MovePathQueriesPerFrame-AuthorityState->PlanningBudget.Paths, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, CommitMs, AuthorityState->CommitBudget.Elapsed()*1000., ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, CommittedMembers, FMath::Max(25,MoveCommitMembersPerFrame)-AuthorityState->CommitBudget.Members, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, RecoveryPending, AuthorityState->RecoveryWork.Num(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, QueryOverruns, static_cast<int32>(AuthorityState->PlanningBudget.FrameQueryOverruns), ECsvCustomStatOp::Set);
	if (FPlatformTime::Seconds()>=AuthorityState->NextPlanningDiagnostic)
	{
		AuthorityState->NextPlanningDiagnostic=FPlatformTime::Seconds()+1.;
		UE_LOG(LogGuLiCommanderMass,Verbose,TEXT("MassPlanning ms=%.3f positions=%d paths=%d commitMs=%.3f members=%d recoveryPending=%d recoveryQueries=%llu recoveryFailures=%llu overruns=%llu maxQueryMs=%.3f"),
			AuthorityState->PlanningBudget.Elapsed()*1000., MovePositionQueriesPerFrame-AuthorityState->PlanningBudget.Projections,
			MovePathQueriesPerFrame-AuthorityState->PlanningBudget.Paths, AuthorityState->CommitBudget.Elapsed()*1000.,
			FMath::Max(25,MoveCommitMembersPerFrame)-AuthorityState->CommitBudget.Members, AuthorityState->RecoveryWork.Num(),
			AuthorityState->RecoveryQueries,AuthorityState->RecoveryFailures,AuthorityState->PlanningBudget.QueryOverruns,AuthorityState->PlanningBudget.MaximumQuerySeconds*1000.);
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
	// StateTree is a required part of the initial entity composition; never spawn a fallback without it.
	if (!World->GetSubsystem<UGuLiUnitDataSubsystem>()->IsCatalogValid()) return false;
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
		bool bAllowAutomaticFire = true;
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
			const float SpacingScale = FMath::Max(1.0f,
				(Definition->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters) * 2.0f + 20.0f)
					/ FMath::Max(1.0f, Deployment->SpacingCentimeters));
			const FVector RequestedLocation = Deployment->GetActorLocation()
				+ (Deployment->GetSlotLocation(SlotIndex) - Deployment->GetActorLocation()) * SpacingScale;
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
			Slot.bAllowAutomaticFire = Deployment->bAllowAutomaticFire;
			Slot.SlotIndex = SlotIndex;
			Slot.FacingYawDegrees = Deployment->GetActorRotation().Yaw;
			Slot.Definition = Definition;
			Slot.NavigationLocation = ProjectedLocation;
		}
		if (!bSpawnValidationSucceeded) break;
	}
	FVector RedAssembly = RedSpawnCenter;
	FVector BlueAssembly = BlueSpawnCenter;
	float InitialArmyInwardInset = 0.0f;
	ResourceAdapter->GetInitialArmySpawnAnchor(EGuLiTeam::Red, RedAssembly, &InitialArmyInwardInset);
	ResourceAdapter->GetInitialArmySpawnAnchor(EGuLiTeam::Blue, BlueAssembly);
	TArray<FGuLiCommanderInitialSpawnSlot> PlannedSlots;
	if (Deployments.IsEmpty() && bSpawnValidationSucceeded)
	{
		bSpawnValidationSucceeded = BuildInitialArmySpawnLayout(
			Definitions, RedAssembly, BlueAssembly, PlannedSlots, SpawnValidationFailure, InitialArmyInwardInset);
	}
	for (const FGuLiCommanderInitialSpawnSlot& Planned : PlannedSlots)
	{
		FNavLocation Projected;
		const bool bProjected = ProjectPointToCommanderNavigation(*NavigationSystem,
			*CommanderNavigationData, Planned.Location,
			FVector(DestinationMaximumProjectionCorrectionCentimeters,
				DestinationMaximumProjectionCorrectionCentimeters, SpawnProjectionVerticalExtentCentimeters), Projected);
		const float Correction = bProjected ? FVector::Dist2D(Planned.Location, Projected.Location)
			: TNumericLimits<float>::Max();
		if (!bProjected || Correction > DestinationMaximumProjectionCorrectionCentimeters)
		{
			bSpawnValidationSucceeded = false;
			SpawnValidationFailure = FString::Printf(
				TEXT("team=%u formation=%d slot=%d requested=%s projected=%d correction=%.1fcm"),
				static_cast<uint8>(Planned.Team), Planned.FormationIndex, Planned.SlotIndex,
				*Planned.Location.ToCompactString(), bProjected ? 1 : 0, Correction);
			break;
		}
		FValidatedSpawnSlot& Slot = ValidatedSpawnSlots.AddDefaulted_GetRef();
		Slot.Team = Planned.Team;
		Slot.SlotIndex = Planned.SlotIndex;
		Slot.FacingYawDegrees = Planned.FacingYawDegrees;
		Slot.Definition = SoldierData->FindSoldierDefinition(Planned.UnitTypeId);
		check(Slot.Definition);
		Slot.NavigationLocation = Projected;
	}

	if (bSpawnValidationSucceeded)
	{
		for (int32 Left = 0; Left < ValidatedSpawnSlots.Num() && bSpawnValidationSucceeded; ++Left)
		{
			for (int32 Right = Left + 1; Right < ValidatedSpawnSlots.Num(); ++Right)
			{
				const float MinimumDistanceSquared = FMath::Square(FMath::Max(DestinationMinimumSeparationCentimeters,
					ValidatedSpawnSlots[Left].Definition->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters)
						+ ValidatedSpawnSlots[Right].Definition->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters)));
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
		FGuLiCommanderStateTreeFragment::StaticStruct(),
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
	// 两套基础组合均包含 Commander StateTree fragment；调参迁移复制原实例句柄，不重启树。
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
				Soldier.bAllowAutomaticFire = ValidatedSlot.bAllowAutomaticFire;
				Soldier.FacingYawDegrees = FacingYaw;
				InitializeSoldierCombat(Soldier, *ValidatedSlot.Definition, EffectiveRuntimeTuning, Skills);
				EntityManager.GetFragmentDataChecked<FGuLiCommanderStateTreeFragment>(Soldier.Entity).Tree = ValidatedSlot.Definition->StateTreeAsset;
				Soldier.AvoidanceRadiusCentimeters = ValidatedSlot.Definition->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters);
				Soldier.LastValidNavLocation = ValidatedSlot.NavigationLocation;
				Soldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;
				Soldier.Location = Soldier.LastValidNavLocation.Location;
				++NavigationProjectionCount;

				const int32 SoldierIndex = AuthorityState->Soldiers.Num() - 1;
				AuthorityState->SoldierIndexById.Add(Soldier.SoldierId.Value, SoldierIndex);

				FTransformFragment& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
				Transform.SetTransform(FTransform(FRotator(0.0f, FacingYaw, 0.0f), Soldier.Location));
				EntityManager.GetFragmentDataChecked<FAgentRadiusFragment>(Soldier.Entity).Radius = Soldier.AvoidanceRadiusCentimeters;
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
		AuthorityState->ActiveEndpoints.Reset(); AuthorityState->DirtyEndpointIds.Reset(); AuthorityState->RemovedEndpointIds.Reset();
		AuthorityState->Steering.Reset(); AuthorityState->SteeringQueue.Empty(); AuthorityState->SteeringQueued.Reset();
		AuthorityState->MoveReservations = {}; AuthorityState->ReservationBootstrapCursor = 0;
		FGuLiMoveEndpointDelta ResetEndpoints; ResetEndpoints.bReset = true; OnMoveEndpointsChanged.Broadcast(ResetEndpoints);
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
	if (auto* Tasks = GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>())
	{
		for (const auto Team : { EGuLiTeam::Red, EGuLiTeam::Blue })
		{
			TArray<FGuLiSoldierId> Ids;
			for (const auto& Soldier : AuthorityState->Soldiers) if (Soldier.Team == Team) Ids.Add(Soldier.SoldierId);
			Tasks->RegisterSoldiers(Team, Ids);
		}
	}
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
	AuthorityState->ActiveEndpoints.Reset(); AuthorityState->DirtyEndpointIds.Reset(); AuthorityState->RemovedEndpointIds.Reset();
	AuthorityState->Steering.Reset(); AuthorityState->SteeringQueue.Empty(); AuthorityState->SteeringQueued.Reset();
	AuthorityState->MoveReservations = {}; AuthorityState->ReservationBootstrapCursor = 0;
	FGuLiMoveEndpointDelta ResetEndpoints; ResetEndpoints.bReset = true; OnMoveEndpointsChanged.Broadcast(ResetEndpoints);
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
	AuthorityState->RecoveryWork.Reset(); AuthorityState->RecoveryQueue.Empty(); AuthorityState->ReadyRecoveryQueue.Empty(); AuthorityState->RecoveryScanCursor=0;
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

void UGuLiBattleAuthoritySubsystem::PrepareNextMoveBatch(const uint64 PlanId)
{
	using namespace GuLiCommanderMassPrivate;
	using namespace GuLiCommanderDestinationPlanner;
	auto* Pointer = AuthorityState->MovePlanningJobs.FindByPredicate([PlanId](const auto& J) { return J && J->Handle.Value == PlanId; });
	if (!Pointer) return;
	auto& Job = **Pointer;
	if (Job.NextCohort >= Job.FullSelection.Cohorts.Num())
	{
		Job.Ack = Job.AggregateAck;
		Job.Ack.BatchOrderId = Job.SharedBatchOrderId;
		Job.Ack.Result = Job.TotalAccepted == 0 ? EGuLiCommandAckResult::PathFailed
			: Job.TotalAccepted == Job.TotalEligible ? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::PartiallyAccepted;
		if (!Job.FullSelection.ActorIds.IsEmpty())
		{
			if (Job.bActorsAccepted && !Job.TotalEligible) Job.Ack.Result=EGuLiCommandAckResult::Accepted;
			else if (Job.bActorsAccepted || Job.TotalAccepted)
				if (!Job.bActorsAccepted || Job.Ack.Result!=EGuLiCommandAckResult::Accepted) Job.Ack.Result=EGuLiCommandAckResult::PartiallyAccepted;
		}
		Job.bSelectionChanged = !AreSelectionsEqual(Job.FullSelection, Job.UpdatedSelection);
		Job.UpdatedSelection.SelectionRevision=Job.FullSelection.SelectionRevision+(Job.bSelectionChanged ? 1u : 0u);
		if (!Job.UpdatedSelection.SelectionRevision) Job.UpdatedSelection.SelectionRevision=1;
		Job.Ack.ServerSelectionRevision = Job.UpdatedSelection.SelectionRevision;
		Job.Ack.Sanitize();
		Job.Progress.bComplete = true; Job.Progress.BatchOrderId = Job.SharedBatchOrderId;
		Job.Debug.AcceptedMembers = Job.TotalAccepted;
		Job.Debug.FailedMembers = Job.FinishedIds.Num() - Job.TotalAccepted;
		Job.Debug.BatchOrderId = Job.SharedBatchOrderId;
		Job.Debug.PlanningMilliseconds = (FPlatformTime::Seconds() - Job.PlanningStartedAt) * 1000;
		AuthorityState->LastDestinationPlanningMilliseconds=Job.Debug.PlanningMilliseconds;
		AuthorityState->MaximumDestinationPlanningMilliseconds=FMath::Max(AuthorityState->MaximumDestinationPlanningMilliseconds,Job.Debug.PlanningMilliseconds);
		AuthorityState->PartiallyAcceptedMoveCommands+=Job.Ack.Result==EGuLiCommandAckResult::PartiallyAccepted ? 1u : 0u;
		for (uint8 Stage=1; Stage<static_cast<uint8>(EGuLiMovePlanFailureStage::Count); ++Stage)
			AuthorityState->MovePlanningFailureCounts[Stage]+=Job.Debug.FailureCounts[Stage];
		AuthorityState->LastMovePlanningDebug = Job.Debug; AuthorityState->bHasLastMovePlanningDebug = true;
		UE_LOG(LogGuLiCommanderMass, Display, TEXT("Incremental move plan=%llu batch=%u accepted=%d failed=%d firstBatchMs=%.2f totalMs=%.2f queryOverruns=%llu maxQueryMs=%.3f"),
			Job.Handle.Value, Job.SharedBatchOrderId, Job.TotalAccepted, Job.Debug.FailedMembers,
			Job.FirstCommitAt > 0 ? (Job.FirstCommitAt - Job.PlanningStartedAt) * 1000 : -1,
			Job.Debug.PlanningMilliseconds, AuthorityState->PlanningBudget.QueryOverruns, AuthorityState->PlanningBudget.MaximumQuerySeconds * 1000);
		Job.Stage = EMovePlanningStage::Completed;
		return;
	}
	Job.Members.Reset(); Job.Cohorts.Reset(); Job.MemberIndexById.Reset();
	Job.ReleasedReservationIds.Reset(); Job.PlannerRequest = FRequest{};
	Job.HardReservations.Reset(); Job.HardReservationBuckets.Reset();
	Job.LegalSlots.Reset(); Job.LegalSlotBuckets.Reset(); Job.LegalSlotIndexByCandidate.Reset();
	Job.ProjectedNavByCandidateIndex.Reset(); Job.CandidateOwnerMemberPlanIndex.Reset();
	Job.RejectedCandidatePairs.Reset(); Job.bMatchingStarted=false; Job.MatchingMembers.Reset();
		Job.RouteTasks.Reset(); Job.PreparedFormations.Reset();
	Job.NextStartValidationIndex = Job.NextCandidateProjectionIndex = 0;
	Job.CandidateProjectionLimit = Job.DesiredLegalSlotCount = 0;
	Job.bEscalatedToFullCandidatePool = false;
	Job.FrozenSelection = FGuLiCommanderSelectionState{};
	Job.FrozenSelection.SelectionRevision = Job.FullSelection.SelectionRevision;
	const auto& Source = Job.FullSelection.Cohorts[Job.NextCohort++];
	Job.FrozenSelection.Cohorts.Add(Source);
	Job.Ack = FGuLiCommandAck{};
	auto& Receipt = Job.Ack.CohortResults.AddDefaulted_GetRef();
	Receipt.CohortId = Source.CohortId; Receipt.MemberCount = static_cast<uint8>(Source.MemberIds.Num());
	auto& Cohort = Job.Cohorts.AddDefaulted_GetRef(); Cohort.CohortId = Source.CohortId; Cohort.FrozenMemberIds = Source.MemberIds;
	for (int32 M = 0; M < Source.MemberIds.Num(); ++M)
	{
		auto& Member = Job.Members.AddDefaulted_GetRef();
		Member.SoldierId = Source.MemberIds[M]; Member.CohortId = Source.CohortId; Member.CohortMemberIndex = M;
		Cohort.MemberPlanIndices.Add(M); Job.MemberIndexById.Add(Member.SoldierId.Value, M);
		const auto* Index = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
		const auto* Version = Job.FrozenTaskGenerations.Find(Member.SoldierId.Value);
		if (!Index || !Version) { Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid; continue; }
		const auto& Soldier = AuthorityState->Soldiers[*Index];
		Member.TaskGeneration = *Version;
		if (!Soldier.CanAct() || Soldier.TaskGeneration != *Version || Soldier.Team != Job.Team
			|| (Job.bAutomatic && !Soldier.bAutomaticAdvance)) { Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid; continue; }
		Member.bEligible = true; Receipt.EligibleMemberMask |= 1u << M;
		Member.OldReservation = Soldier.bHasFinalDestination && Soldier.ActiveOrderId ? Soldier.FinalDestination.Location : Soldier.LastValidNavLocation.Location;
		Job.ReleasedReservationIds.Add(Member.SoldierId.Value);
	}
	Job.TotalEligible += FPlatformMath::CountBits(Receipt.EligibleMemberMask);
	if (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
	{ Job.ProjectionCache.Reset(); Job.InvalidProjectionCache.Reset(); }
	Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.LastProgressAt = FPlatformTime::Seconds();
	Job.Stage = EMovePlanningStage::ValidateStarts;
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
	FGuLiNavigationWorkBudget::FScope BudgetScope(AuthorityState->PlanningBudget);
	if (AuthorityState->ReservationBootstrapCursor < AuthorityState->Soldiers.Num()) return;
	// A destroyed owner can never poll its terminal result. Remove both terminal and
	// in-flight work immediately so reconnects do not inherit an unreachable job.
	AuthorityState->MovePlanningJobs.RemoveAll(
		[](const TUniquePtr<FMovePlanningJob>& Job)
		{
			return !Job || (!Job->bAutomatic && !Job->PlayerState.IsValid())
				|| (Job->bAutomatic && Job->Stage==EMovePlanningStage::Completed && FPlatformTime::Seconds()-Job->PlanningStartedAt>35.);
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
		Job.ReleasedReservationIds = ReleasedIds;
		for (auto& Member : Job.Members) if (const auto* Index = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value))
		{
			const auto& Soldier = AuthorityState->Soldiers[*Index];
			Member.OldReservation = Soldier.bHasFinalDestination && Soldier.ActiveOrderId ? Soldier.FinalDestination.Location : Soldier.LastValidNavLocation.Location;
		}
		Job.PlannerRequest = FRequest{};
		Job.PlannerRequest.TargetAnchor = FVector(Job.Request.Target);
		Job.PlannerRequest.MemberSpacingCentimeters = FMath::Max(MemberSpacingCentimeters, Job.MinimumSlotSpacingCentimeters);
		Job.LegalSlots.Reset();
		Job.LegalSlotBuckets.Reset();
		Job.LegalSlotIndexByCandidate.Reset();
		Job.ProjectedNavByCandidateIndex.Reset();
		Job.CandidateOwnerMemberPlanIndex.Reset();
		Job.RejectedCandidatePairs.Reset();
		Job.bMatchingStarted=false; Job.MatchingMembers.Reset();
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

	for (int32 Visit = 0, Count = AuthorityState->MovePlanningJobs.Num(); Visit < Count; ++Visit)
	{
		const auto& JobPointer = AuthorityState->MovePlanningJobs[(AuthorityState->PlanningCursor + Visit) % AuthorityState->MovePlanningJobs.Num()];
		if (!JobPointer || JobPointer->Stage == EMovePlanningStage::Completed
			|| JobPointer->Stage == EMovePlanningStage::ReadyToCommit)
		{
			continue;
		}
		FMovePlanningJob& Job = *JobPointer;
		if (FPlatformTime::Seconds() - Job.PlanningStartedAt >= 30.0
			|| FPlatformTime::Seconds() - Job.LastProgressAt >= 5.0)
		{
			CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::TimedOut);
			continue;
		}
		const AGuLiBattlePlayerState* PlayerState = Job.PlayerState.Get();
		if (!Job.bAutomatic && (!PlayerState || !IsMovePlanningOwnerCurrent(Job, *PlayerState)
			|| !PlayerState->IsCommander() || PlayerState->GetTeam() != Job.Team))
		{
			CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::Unauthorized);
			continue;
		}
		if (Job.AuthorityEpoch != CurrentAuthorityEpoch)
		{
			CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::InvalidRequest);
			continue;
		}
		if (!AuthorityState->PlanningBudget.CanWork()) continue;
		if (!Job.bCandidatesGenerated)
		{
			// Generate one lattice row per work unit. The final bounded (2263-point) sort
			// preserves the existing distance/q/r ordering and capacity exactly.
			const double Pitch = DefaultFreeCandidatePitchCentimeters + Job.MinimumSlotSpacingCentimeters - DestinationMinimumSeparationCentimeters;
			const double Radius = FreeDestinationMaximumRadiusCentimeters * Pitch / DefaultFreeCandidatePitchCentimeters;
			const int32 Extent = FMath::CeilToInt(2 * Radius / (FMath::Sqrt(3.0) * Pitch)) + 1;
			if (Job.CandidateRow == INDEX_NONE) Job.CandidateRow = 0;
			while (Job.CandidateRow <= Extent * 2 && AuthorityState->PlanningBudget.CanWork())
			{
				const int32 Q = Job.CandidateRow++ - Extent;
				for (int32 R = -Extent; R <= Extent; ++R)
				{
					const double D = double(Q*Q + Q*R + R*R) * Pitch * Pitch;
					if (D > Radius*Radius + FMath::Max(1.e-6, Radius*Radius*1.e-12)) continue;
					auto& C = Job.HexCandidates.AddDefaulted_GetRef(); C.AxialQ=Q; C.AxialR=R;
					C.AnchorLocalOffset=FVector(Pitch*(Q+R*.5), Pitch*FMath::Sqrt(3.0)*.5*R,0);
					C.WorldCandidate=FVector(Job.Request.Target)+C.AnchorLocalOffset; C.DistanceSquaredFromAnchor=D;
				}
				Job.LastProgressAt = FPlatformTime::Seconds();
			}
			if (Job.CandidateRow <= Extent*2 || !AuthorityState->PlanningBudget.CanWork()) continue;
			Job.HexCandidates.Sort([](const auto& A,const auto& B) { return A.DistanceSquaredFromAnchor != B.DistanceSquaredFromAnchor
				? A.DistanceSquaredFromAnchor < B.DistanceSquaredFromAnchor : A.AxialQ != B.AxialQ ? A.AxialQ < B.AxialQ : A.AxialR < B.AxialR; });
			for (int32 I=0; I<Job.HexCandidates.Num(); ++I) Job.HexCandidates[I].CandidateIndex=I;
			Job.Debug.MaximumSearchRadiusCentimeters=Radius; Job.Debug.TheoreticalCandidates=Job.HexCandidates.Num();
			Job.bCandidatesGenerated=true;
		}
		if (Job.Stage == EMovePlanningStage::PrepareBatch)
		{
			PrepareNextMoveBatch(Job.Handle.Value);
			if (Job.Stage == EMovePlanningStage::Completed || !AuthorityState->PlanningBudget.CanWork()) continue;
		}
		const auto PreviousStage = Job.Stage;
		const int32 PreviousProgress = Job.NextStartValidationIndex + Job.NextCandidateProjectionIndex + Job.Debug.PathQueries;
		++Job.Debug.PlanningWorldFrames;
		if (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
		{
			Job.NavigationGeneration = AuthorityState->NavigationGeneration;
			Job.ProjectionCache.Reset(); Job.InvalidProjectionCache.Reset();
			RebuildHardReservations(Job);
		}

		if (Job.Stage == EMovePlanningStage::ValidateStarts)
		{
			while (AuthorityState->PlanningBudget.CanWork() && RemainingProjectionBudget > 0
				&& Job.NextStartValidationIndex < Job.Members.Num())
			{
				FMoveMemberPlan& Member = Job.Members[Job.NextStartValidationIndex++];
				if (!Member.bEligible)
				{
					continue;
				}
				--RemainingProjectionBudget;
				FGuLiNavigationWorkBudget::FQueryScope StartTimer(AuthorityState->PlanningBudget);
				++AuthorityState->MoveCandidateProjectionQueries;
				++Job.Debug.CandidateProjectionQueries; Job.LastProgressAt = FPlatformTime::Seconds();
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
				if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
					continue;
				}
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				FNavLocation ProjectedStart;
				if (!Soldier.CanAct() || Soldier.TaskGeneration != Member.TaskGeneration || Soldier.Team != Job.Team
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
					Restored.RadiusCentimeters=Soldier.AvoidanceRadiusCentimeters;
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
				Job.PlannerRequest.MemberSpacingCentimeters = FMath::Max(MemberSpacingCentimeters, Job.MinimumSlotSpacingCentimeters);
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
			while (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::ProjectCandidates)
			{
				while (AuthorityState->PlanningBudget.CanWork() && RemainingProjectionBudget > 0
					&& Job.NextCandidateProjectionIndex < Job.CandidateProjectionLimit)
				{
					const FFreeDestinationCandidate& Candidate =
						Job.HexCandidates[Job.NextCandidateProjectionIndex++];
					if (Job.InvalidProjectionCache.Contains(Candidate.CandidateIndex)) continue;
					const FNavLocation* CachedProjection = Job.ProjectionCache.Find(Candidate.CandidateIndex);
					if (!CachedProjection) { --RemainingProjectionBudget; ++AuthorityState->MoveCandidateProjectionQueries; ++Job.Debug.CandidateProjectionQueries; } Job.LastProgressAt = FPlatformTime::Seconds();
					++Job.Debug.ProjectedCandidates;
					FVector Seed = Candidate.WorldCandidate;
					float LandscapeHeight = 0.0f;
					if (!CachedProjection && (!LandscapeQuery
						|| !LandscapeQuery->TryGetLandscapeHeight(FVector2D(Seed.X, Seed.Y), LandscapeHeight)))
					{
						Job.InvalidProjectionCache.Add(Candidate.CandidateIndex);
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::CandidateProjection)];
						continue;
					}
					Seed.Z = CachedProjection ? CachedProjection->Location.Z : LandscapeHeight;
					FNavLocation Projected = CachedProjection ? *CachedProjection : FNavLocation{};
					FGuLiNavigationWorkBudget::FQueryScope ProjectionTimer(AuthorityState->PlanningBudget);
					if ((!CachedProjection && !ProjectPointToCommanderNavigation(
							*NavigationSystem,
							*NavigationData,
							Seed,
							FVector(
								DestinationMaximumProjectionCorrectionCentimeters,
								DestinationMaximumProjectionCorrectionCentimeters,
								5000.0f),
							Projected))
						|| FVector::DistSquared2D(Seed, Projected.Location)
							> FMath::Square(DestinationMaximumProjectionCorrectionCentimeters)
						|| FVector::DistSquared2D(FVector(Job.Request.Target), Projected.Location)
							> FMath::Square(Job.Debug.MaximumSearchRadiusCentimeters))
					{
						Job.InvalidProjectionCache.Add(Candidate.CandidateIndex);
						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::CandidateProjection)];
						continue;
					}

					if (!CachedProjection) { Job.ProjectionCache.Add(Candidate.CandidateIndex, Projected); }
					if (!World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(Projected.Location,Projected.Location,Job.MaximumMemberRadiusCentimeters)
						|| IsMoveCandidateBlockedByHardReservation(Job, Projected.Location))
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
					Job.LegalSlotIndexByCandidate.Add(Candidate.CandidateIndex, Job.LegalSlots.Num()-1);
					Job.ProjectedNavByCandidateIndex.Add(Candidate.CandidateIndex, Projected);
					Job.LegalSlotBuckets.FindOrAdd(
						MakeMoveDestinationBucket(Projected.Location, Job)).Add(Job.LegalSlots.Num() - 1);
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

		if (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::AssignDestinations)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning_AssignDestinations);
			if (!Job.bMatchingStarted)
			{
				// A single <=25 member cohort has one soft anchor. Its center-first free slots
				// retain the former minimum travel objective without a whole-selection matrix.
				Job.MatchingMembers.Reset();
				for (int32 I=0; I<Job.Members.Num(); ++I) if (Job.Members[I].bStartValid) Job.MatchingMembers.Add(I);
				Job.MatchingMembers.Sort([&](int32 A,int32 B){return Job.Members[A].SoldierId<Job.Members[B].SoldierId;});
				TArray<FVector> Rows,Columns;
				for (int32 I=0; I<FMath::Min(Job.LegalSlots.Num(),Job.MatchingMembers.Num()); ++I) Rows.Add(Job.LegalSlots[I].WorldDestination);
				for (int32 I : Job.MatchingMembers) Columns.Add(Job.Members[I].CommandStart.Location);
				Job.Matching.Begin(MoveTemp(Rows),MoveTemp(Columns)); Job.bMatchingStarted=true;
			}
			while (!Job.Matching.bComplete && AuthorityState->PlanningBudget.CanWork())
			{ Job.Matching.Step(); Job.LastProgressAt=FPlatformTime::Seconds(); }
			if (!Job.Matching.bComplete || !AuthorityState->PlanningBudget.CanWork()) continue;
			FMoveRouteTask RouteTask; RouteTask.CohortId=Job.Cohorts[0].CohortId;
			for (int32 R=0; R<Job.Matching.ColumnByRow.Num(); ++R)
			{
				const int32 M=Job.MatchingMembers[Job.Matching.ColumnByRow[R]]; const auto& Slot=Job.LegalSlots[R];
				if (!IsMoveCandidateAvailableForMember(Job,Job.Members[M],Slot.CandidateIndex)) continue;
				ClaimMoveCandidate(Job,M,Slot,Job.ProjectedNavByCandidateIndex.FindChecked(Slot.CandidateIndex));
				RouteTask.MemberPlanIndices.Add(M);
			}
			if (!RouteTask.MemberPlanIndices.IsEmpty()) Job.RouteTasks.Add(MoveTemp(RouteTask));
			for (auto& Member : Job.Members) if (Member.bStartValid && !Member.bHasDestination) Member.FailureStage=EGuLiMovePlanFailureStage::CandidatesExhausted;
			Job.Stage=EMovePlanningStage::Route;
		}

		if (Job.Stage == EMovePlanningStage::Route)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning_Route);
			while (AuthorityState->PlanningBudget.CanWork() && !Job.RouteTasks.IsEmpty())
			{
				FMoveRouteTask Task = MoveTemp(Job.RouteTasks[0]);
				Job.RouteTasks.RemoveAt(0, 1, EAllowShrinking::No);
				const int32 PreviousMemberCount = Task.MemberPlanIndices.Num();
				Task.MemberPlanIndices.RemoveAll([this, &Job, &Task](const int32 MemberPlanIndex)
				{
					if (!Job.Members.IsValidIndex(MemberPlanIndex))
					{
						return true;
					}
					FMoveMemberPlan& Member = Job.Members[MemberPlanIndex];
					const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
					if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
						|| !AuthorityState->Soldiers[*SoldierIndex].CanAct()
						|| AuthorityState->Soldiers[*SoldierIndex].Team != Job.Team
						|| AuthorityState->Soldiers[*SoldierIndex].TaskGeneration != Member.TaskGeneration)
					{
						Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
						ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
						return true;
					}
					if (!Task.bPathQueried) Member.CommandStart = AuthorityState->Soldiers[*SoldierIndex].LastValidNavLocation;
					return !Member.bHasDestination && !Task.bNeedsCandidate;
				});
				if (PreviousMemberCount != Task.MemberPlanIndices.Num())
				{ Task.bPathQueried = false; Task.NextConnector = 0; Task.CachedPath.Reset(); }
				if (Task.MemberPlanIndices.IsEmpty())
				{
					continue;
				}

				if (Task.bNeedsCandidate)
				{
					const int32 M=Task.MemberPlanIndices[0]; auto& Member=Job.Members[M];
					while (Task.NextFallbackSlot<Job.LegalSlots.Num() && AuthorityState->PlanningBudget.CanWork())
					{
						const auto& Slot=Job.LegalSlots[Task.NextFallbackSlot++]; Job.LastProgressAt=FPlatformTime::Seconds();
						if (Slot.CandidateIndex<=Member.LastCandidateIndex || !IsMoveCandidateAvailableForMember(Job,Member,Slot.CandidateIndex)
							|| IsMoveCandidateBlockedByHardReservation(Job,Slot.WorldDestination)) continue;
						ClaimMoveCandidate(Job,M,Slot,Job.ProjectedNavByCandidateIndex.FindChecked(Slot.CandidateIndex));
						Task.bNeedsCandidate=false; break;
					}
					if (Task.bNeedsCandidate)
					{
						if (Task.NextFallbackSlot<Job.LegalSlots.Num()) { Job.RouteTasks.Insert(MoveTemp(Task),0); break; }
						if (RestartMovePlanningWithCompleteCandidatePool(Job)) break;
						Member.FailureStage=EGuLiMovePlanFailureStage::CandidatesExhausted; continue;
					}
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
				TArray<FVector> PathPoints = MoveTemp(Task.CachedPath);
				if (!Task.bPathQueried)
				{
					if (!AuthorityState->PlanningBudget.TakePath())
					{ Job.RouteTasks.Insert(MoveTemp(Task),0); break; }
					++AuthorityState->PathQueries; ++AuthorityState->MovePlanningPathQueries;
					++Job.Debug.PathQueries; Job.LastProgressAt = FPlatformTime::Seconds();
					{
						FGuLiNavigationWorkBudget::FQueryScope QueryScope(AuthorityState->PlanningBudget);
						Task.bPathValid = BuildCompletePathQuiet(*NavigationSystem, *NavigationData,
							StartMedoid.CommandStart, DestinationMedoid.Destination.WorldDestination, PathPoints);
					}
					Task.bPathQueried = true;
				}
				bool bRouteValid = Task.bPathValid;
				EGuLiMovePlanFailureStage RouteFailure = EGuLiMovePlanFailureStage::SharedPath;
				bool bDeferredConnector = false;
				while (bRouteValid && Task.NextConnector < Task.MemberPlanIndices.Num() * 2)
				{
					if (!AuthorityState->PlanningBudget.TakeProjection()) { bDeferredConnector = true; break; }
					const auto& Member = Job.Members[Task.MemberPlanIndices[Task.NextConnector / 2]];
					{
						FGuLiNavigationWorkBudget::FQueryScope QueryScope(AuthorityState->PlanningBudget);
						bRouteValid = (Task.NextConnector & 1) == 0
							? HasDirectSurfaceConnection(*NavigationData, Member.CommandStart, PathPoints[0])
							: HasDirectSurfaceConnection(*NavigationData, DestinationMedoid.DestinationNav, Member.Destination.WorldDestination);
					}
					++Task.NextConnector; Job.LastProgressAt = FPlatformTime::Seconds();
					if (!bRouteValid) RouteFailure = EGuLiMovePlanFailureStage::Connector;
				}
				if (bDeferredConnector)
				{ Task.CachedPath = MoveTemp(PathPoints); Job.RouteTasks.Insert(MoveTemp(Task),0); break; }

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
					Task.bNeedsCandidate=true; Task.bPathQueried=false; Task.NextConnector=0; Task.NextFallbackSlot=0; Task.CachedPath.Reset();
					Job.RouteTasks.Add(MoveTemp(Task));
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
					20.0f,
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
				AssignFormationSlots(Formation,AuthorityState->Soldiers,AuthorityState->SoldierIndexById,MemberSpacingCentimeters,0,Formation.TransitColumnCount);
				Job.PreparedFormations.Add(MoveTemp(Formation));
			}
			if (Job.Stage == EMovePlanningStage::Route && Job.RouteTasks.IsEmpty())
			{
				Job.Stage = EMovePlanningStage::ReconcileReservations;
			}
		}

		if (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::ReconcileReservations)
		{
			Job.HardReservations.Reset(); Job.HardReservationBuckets.Reset();
			TArray<FVector> RestoredReservations;
			for (FMoveMemberPlan& Member : Job.Members)
			{
				// Superseded members still own their committed endpoint (or stopped location).
				// They no longer participate in this plan, but must still reserve that space.
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
							? Soldier.FinalDestination.Location
							: Soldier.LastValidNavLocation.Location;
					}
				}
				if (!Member.bAccepted && bHasLiveReservation)
				{
					RestoredReservations.Add(Member.OldReservation);
					FMoveDestinationReservation Restored; Restored.OwnerSoldierId=Member.SoldierId.Value; Restored.Location=Member.OldReservation;
					Restored.RadiusCentimeters=AuthorityState->Soldiers[*SoldierIndex].AvoidanceRadiusCentimeters; AddHardReservationToMoveJob(Job,Restored);
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
				const bool bConflicts = IsMoveCandidateBlockedByHardReservation(Job, Member.Destination.WorldDestination) || RestoredReservations.ContainsByPredicate(
					[&Member, &Job](const FVector& Restored)
					{
						return FVector::DistSquared2D(Restored, Member.Destination.WorldDestination)
							< FMath::Square(Job.MinimumSlotSpacingCentimeters);
					});
				if (!bConflicts)
				{
					continue;
				}
				++Job.Debug.ReservationConflictCount;
				RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
				const int32 ConflictingCandidateIndex = Member.LastCandidateIndex;
				ReleaseMoveMemberDestination(Job, MemberPlanIndex, false);
				Job.RejectedCandidatePairs.Add(MakeMoveCandidatePairKey(Member.SoldierId,ConflictingCandidateIndex));
				FMoveRouteTask Retry; Retry.CohortId=Member.CohortId; Retry.MemberPlanIndices.Add(MemberPlanIndex); Retry.bNeedsCandidate=true;
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
		if (Job.Stage != PreviousStage || PreviousProgress != Job.NextStartValidationIndex + Job.NextCandidateProjectionIndex + Job.Debug.PathQueries)
			Job.LastProgressAt = FPlatformTime::Seconds();
	}
	++AuthorityState->PlanningCursor;
}

void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->bPopulationSpawned || !GetWorld()) return;
	auto* World = GetWorld();
	auto* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!MassSubsystem) return;
	auto& EntityManager = MassSubsystem->GetMutableEntityManager();
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->CommitBudget);
	for (const auto& Pointer : AuthorityState->MovePlanningJobs)
	{
		if (!Pointer || Pointer->Stage != EMovePlanningStage::ReadyToCommit) continue;
		auto& Job = *Pointer;
		if (!AuthorityState->CommitBudget.CanWork() || AuthorityState->CommitBudget.Members < Job.Members.Num()) break;
		if (FPlatformTime::Seconds() - Job.PlanningStartedAt >= 30 || FPlatformTime::Seconds() - Job.LastProgressAt >= 5)
		{ CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::TimedOut); continue; }
		const auto* Owner = Job.PlayerState.Get();
		if (!Job.bAutomatic && (!Owner || !IsMovePlanningOwnerCurrent(Job, *Owner) || !Owner->IsCommander() || Owner->GetTeam() != Job.Team))
		{ CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::Unauthorized); continue; }
		const auto* GameState = World->GetGameState<AGuLiBattleGameState>();
		if (!GameState || GameState->GetMatchEpoch() != Job.AuthorityEpoch)
		{ CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::Cancelled); continue; }
		if (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
		{
			// TickMovePlanning owns the budgeted rebuild; no navigation work at the commit boundary.
			Job.Stage = EMovePlanningStage::ValidateStarts; continue;
		}
		// Legacy mixed selections still dispatch their Actor command once, at an authority boundary.
		if (!Job.bActorsSubmitted && !Job.bAutomatic && Owner && !Job.FullSelection.ActorIds.IsEmpty())
		{
			FGuLiMiningCommand Command; Command.RequestId=Job.Request.ClientCommandId;
			Command.Type=EGuLiMiningOrderType::Move; Command.Target=Job.Request.Target;
			Command.SelectionRevision=Job.Request.SelectionRevision;
			Job.bActorsAccepted=World->GetSubsystem<UGuLiCommanderResourceAdapter>()->IssueMiningCommand(*Owner,Job.FullSelection.ActorIds,Command);
			Job.bActorsSubmitted=true;
		}
		bool bRetry = false;
		for (int32 I = 0; I < Job.Members.Num(); ++I)
		{
			auto& Member = Job.Members[I]; if (!Member.bAccepted) continue;
			const auto* Index = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
			if (!Index || !EntityManager.IsEntityValid(AuthorityState->Soldiers[*Index].Entity)
				|| !AuthorityState->Soldiers[*Index].CanAct() || AuthorityState->Soldiers[*Index].TaskGeneration != Member.TaskGeneration
				|| AuthorityState->Soldiers[*Index].Team != Job.Team
				|| (Job.bAutomatic && !AuthorityState->Soldiers[*Index].bAutomaticAdvance))
			{
				ReleaseMoveMemberDestination(Job, I, false); RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
				Job.ReleasedReservationIds.Remove(Member.SoldierId.Value); bRetry = true; continue;
			}
			const auto& Soldier = AuthorityState->Soldiers[*Index];
			if (!Soldier.LastValidNavLocation.NodeRef || Soldier.LastValidNavLocation.NodeRef != Member.CommandStart.NodeRef)
			{
				// The old order advanced after connector validation. Reconnect under the planning budget.
				Member.CommandStart = Soldier.LastValidNavLocation; Member.bAccepted = false;
				RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
				FMoveRouteTask Task; Task.CohortId = Member.CohortId; Task.MemberPlanIndices.Add(I);
				Job.RouteTasks.Add(MoveTemp(Task)); bRetry = true;
			}
			else if (IsMoveCandidateBlockedByHardReservation(Job, Member.Destination.WorldDestination))
			{ bRetry = true; }
		}
		if (bRetry)
		{
			Job.Stage = Job.RouteTasks.IsEmpty() ? EMovePlanningStage::ReconcileReservations : EMovePlanningStage::Route;
			continue;
		}
		int32 AcceptedMembers = 0;
		for (const auto& Member : Job.Members) AcceptedMembers += Member.bAccepted ? 1 : 0;
		if (AcceptedMembers && !Job.SharedBatchOrderId) Job.SharedBatchOrderId = AllocateNonZero(AuthorityState->NextBatchOrderId);
		const uint32 BatchOrderId = Job.SharedBatchOrderId;

		for (FOrderFormationRuntime& Formation : Job.PreparedFormations)
		{
			Formation.MemberIds.RemoveAll([this, &Job](const FGuLiSoldierId SoldierId)
			{
				const int32* M=Job.MemberIndexById.Find(SoldierId.Value);
				const FMoveMemberPlan* Member=M ? &Job.Members[*M] : nullptr;
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
				Soldier.CommandStartLocation = CommandStart->Location;
				Soldier.ActiveOrderId = BatchOrderId;
				Soldier.bAutomaticAdvance = Job.bAutomatic;
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
				RefreshSoldierNavigationState(SoldierId);
				if (!Job.FinishedIds.Contains(SoldierId.Value)) Job.Progress.Committed.Add(SoldierId);
			}

			AuthorityState->OrderFormations.Add(MoveTemp(Formation));
		}

		AuthorityState->CommitBudget.Members -= Job.Members.Num();
		Job.TotalAccepted += AcceptedMembers;
		if (AcceptedMembers && Job.FirstCommitAt == 0) Job.FirstCommitAt = FPlatformTime::Seconds();
		for (auto Receipt : Job.Ack.CohortResults)
		{
			Receipt.AcceptedMemberMask = 0;
			auto& Diagnostic=Job.Debug.Cohorts.AddDefaulted_GetRef();
			Diagnostic.CohortId=Receipt.CohortId; Diagnostic.MemberCount=Receipt.MemberCount; Diagnostic.EligibleMemberMask=Receipt.EligibleMemberMask;
			FGuLiControlCohortDescriptor Updated; Updated.CohortId = Receipt.CohortId; Updated.ActiveOrderId = BatchOrderId;
			for (const auto& Member : Job.Members)
			{
				if (Member.bAccepted) { Receipt.AcceptedMemberMask |= 1u << Member.CohortMemberIndex; Updated.MemberIds.Add(Member.SoldierId); }
				else
				{
					if (!Job.FinishedIds.Contains(Member.SoldierId.Value)) Job.Progress.Failed.Add(Member.SoldierId);
					Job.Debug.FailedSoldierIds.Add(Member.SoldierId); Diagnostic.FailedSoldierIds.Add(Member.SoldierId);
				}
				Job.FinishedIds.Add(Member.SoldierId.Value);
				Job.FrozenTaskGenerations.Remove(Member.SoldierId.Value);
			}
			Receipt.Result = !Receipt.AcceptedMemberMask ? EGuLiCommandAckResult::PathFailed
				: Receipt.AcceptedMemberMask == Receipt.EligibleMemberMask ? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::PartiallyAccepted;
			Job.AggregateAck.CohortResults.Add(Receipt);
			Diagnostic.AcceptedMemberMask=Receipt.AcceptedMemberMask;
			Updated.AliveCount = static_cast<uint8>(Updated.MemberIds.Num());
			if (Updated.AliveCount) Job.UpdatedSelection.Cohorts.Add(MoveTemp(Updated));
		}
		Job.Ack.CohortResults.Reset();
		Job.Progress.BatchOrderId = BatchOrderId;
		Job.LastProgressAt = FPlatformTime::Seconds();
		Job.Stage = EMovePlanningStage::PrepareBatch;
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

FGuLiMovePlanHandle UGuLiBattleAuthoritySubsystem::FindMovePlan(const AGuLiBattlePlayerState& Owner, uint32 Id) const
{
	if (AuthorityState) for (const auto& Job : AuthorityState->MovePlanningJobs)
		if (Job && Job->PlayerState.Get() == &Owner && Job->Request.ClientCommandId == Id) return Job->Handle;
	return {};
}

bool UGuLiBattleAuthoritySubsystem::ConsumeMovePlanProgress(FGuLiMovePlanHandle Handle, FGuLiMovePlanProgress& Out)
{
	Out = FGuLiMovePlanProgress{};
	if (!AuthorityState || !Handle.IsValid()) return false;
	for (int32 I = 0; I < AuthorityState->MovePlanningJobs.Num(); ++I)
	{
		auto& Job = AuthorityState->MovePlanningJobs[I];
		if (!Job || Job->Handle.Value != Handle.Value || Job->Handle.Epoch != Handle.Epoch) continue;
		Out = MoveTemp(Job->Progress); Out.Handle = Handle;
		Job->Progress = FGuLiMovePlanProgress{}; Job->Progress.Handle = Handle;
		Job->Progress.BatchOrderId = Job->SharedBatchOrderId;
		Job->Progress.bComplete = Job->Stage == GuLiCommanderMassPrivate::EMovePlanningStage::Completed;
		Out.bComplete = Job->Progress.bComplete;
		if (Out.bComplete && Job->bAutomatic) AuthorityState->MovePlanningJobs.RemoveAt(I);
		return true;
	}
	return false;
}

const FGuLiMoveEndpointSnapshot* UGuLiBattleAuthoritySubsystem::FindActiveMoveEndpoint(FGuLiSoldierId Id) const
{ return AuthorityState ? AuthorityState->ActiveEndpoints.Find(Id.Value) : nullptr; }

void UGuLiBattleAuthoritySubsystem::RefreshSoldierNavigationState(FGuLiSoldierId Id)
{
	if (!AuthorityState) return;
	const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
	const auto* Soldier = Index && AuthorityState->Soldiers.IsValidIndex(*Index) ? &AuthorityState->Soldiers[*Index] : nullptr;
	if (Soldier) AuthorityState->MoveReservations.Update(*Soldier); else AuthorityState->MoveReservations.Remove(Id.Value);
	if (!Soldier || !Soldier->CanAct() || !Soldier->ActiveOrderId || !Soldier->bHasFinalDestination)
	{
		if (AuthorityState->ActiveEndpoints.Remove(Id.Value))
		{ AuthorityState->DirtyEndpointIds.Remove(Id.Value); AuthorityState->RemovedEndpointIds.Add(Id.Value); }
		return;
	}
	const auto* Previous = AuthorityState->ActiveEndpoints.Find(Id.Value);
	if (Previous && Previous->ActiveOrderId == Soldier->ActiveOrderId && Previous->Team == Soldier->Team
		&& Previous->CommandStart.Equals(Soldier->CommandStartLocation,.01)
		&& Previous->FinalDestination.Equals(Soldier->FinalDestination.Location,.01)) return;
	FGuLiMoveEndpointSnapshot E; E.SoldierId = Id; E.Team = Soldier->Team;
	E.ActiveOrderId = Soldier->ActiveOrderId; E.Revision = Soldier->StateRevision;
	E.CommandStart = Soldier->CommandStartLocation; E.FinalDestination = Soldier->FinalDestination.Location;
	AuthorityState->ActiveEndpoints.Add(Id.Value,E);
	AuthorityState->DirtyEndpointIds.Add(Id.Value); AuthorityState->RemovedEndpointIds.Remove(Id.Value);
}

void UGuLiBattleAuthoritySubsystem::PublishMoveEndpointChanges()
{
	if (!AuthorityState || (AuthorityState->DirtyEndpointIds.IsEmpty() && AuthorityState->RemovedEndpointIds.IsEmpty())) return;
	FGuLiMoveEndpointDelta Delta;
	for (uint32 Id : AuthorityState->DirtyEndpointIds)
		if (const auto* E = AuthorityState->ActiveEndpoints.Find(Id)) Delta.Upserts.Add(*E);
	for (uint32 Id : AuthorityState->RemovedEndpointIds) Delta.Removed.Add(FGuLiSoldierId(Id));
	AuthorityState->DirtyEndpointIds.Reset(); AuthorityState->RemovedEndpointIds.Reset();
	OnMoveEndpointsChanged.Broadcast(Delta);
}

void UGuLiBattleAuthoritySubsystem::BuildActiveMoveEndpointSnapshot(EGuLiTeam Team, TArray<FGuLiMoveEndpointSnapshot>& Out) const
{
	Out.Reset(); if (!AuthorityState) return;
	for (const auto& Pair : AuthorityState->ActiveEndpoints) if (Pair.Value.Team == Team) Out.Add(Pair.Value);
	Out.Sort([](const auto& A, const auto& B) { return A.SoldierId < B.SoldierId; });
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
bool UGuLiBattleAuthoritySubsystem::ResolveValidatedSteeringTargets(FGuLiSoldierId Id,uint32 PathRevision,
	const FVector& Lane,const FVector& Slot,FVector& OutLane,FVector& OutSlot)
{
	using namespace GuLiCommanderMassPrivate;
	const auto* Index=AuthorityState->SoldierIndexById.Find(Id.Value); if (!Index) return false;
	const auto& Soldier=AuthorityState->Soldiers[*Index];
	const auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	const uint32 ObstacleVersion=Registry ? Registry->GetRevision() : 0;
	auto& Cache=AuthorityState->Steering.FindOrAdd(Id.Value);
	const float Tolerance=FMath::Max(10.f,Soldier.AvoidanceRadiusCentimeters*.5f);
	if (Cache.Order!=Soldier.ActiveOrderId || Cache.Path!=PathRevision || Cache.Nav!=AuthorityState->NavigationGeneration
		|| Cache.Obstacles!=ObstacleVersion || Cache.Origin.NodeRef!=Soldier.LastValidNavLocation.NodeRef
		|| !Cache.RequestedLane.Equals(Lane,Tolerance) || !Cache.RequestedSlot.Equals(Slot,Tolerance))
	{
		Cache=FSteeringValidation{}; Cache.Order=Soldier.ActiveOrderId; Cache.Path=PathRevision;
		Cache.Nav=AuthorityState->NavigationGeneration; Cache.Obstacles=ObstacleVersion;
		Cache.Origin=Soldier.LastValidNavLocation; Cache.RequestedLane=Lane; Cache.RequestedSlot=Slot;
	}
	if (Cache.Stage<4 && !AuthorityState->SteeringQueued.Contains(Id.Value))
	{ AuthorityState->SteeringQueued.Add(Id.Value); AuthorityState->SteeringQueue.Enqueue(Id.Value); }
	if (Cache.Stage<4 || !Cache.bLaneValid) return false;
	OutLane=Cache.Lane.Location;
	OutSlot=Cache.bSlotValid ? Cache.Slot.Location : Soldier.Location;
	return true;
}
void UGuLiBattleAuthoritySubsystem::TickSteeringValidation()
{
	using namespace GuLiCommanderMassPrivate;
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	auto* Data=Nav ? GetCommanderNavigationData(*Nav) : nullptr;
	auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!Data || !Registry) return;
	const auto Snapshot=Registry->GetSnapshot();
	uint32 Id=0;
	for (int32 Visited=0; Visited<128 && AuthorityState->PlanningBudget.CanWork() && AuthorityState->PlanningBudget.Projections>0
		&& AuthorityState->SteeringQueue.Dequeue(Id); ++Visited)
	{
		AuthorityState->SteeringQueued.Remove(Id);
		auto* Cache=AuthorityState->Steering.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
		if (!Cache || !Index) { AuthorityState->Steering.Remove(Id); continue; }
		const auto& Soldier=AuthorityState->Soldiers[*Index];
		if (!Soldier.CanAct() || Cache->Order!=Soldier.ActiveOrderId || Cache->Nav!=AuthorityState->NavigationGeneration
			|| Cache->Obstacles!=Snapshot->Revision || Cache->Origin.NodeRef!=Soldier.LastValidNavLocation.NodeRef)
		{ AuthorityState->Steering.Remove(Id); continue; }
		if (Cache->Stage>=4) continue;
		if (!AuthorityState->PlanningBudget.TakeProjection()) break;
		FGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget);
		const bool bLane=Cache->Stage<2;
		auto& Projected=bLane ? Cache->Lane : Cache->Slot;
		const auto& Requested=bLane ? Cache->RequestedLane : Cache->RequestedSlot;
		bool& Valid=bLane ? Cache->bLaneValid : Cache->bSlotValid;
		if ((Cache->Stage&1)==0)
		{
			Valid=ProjectPointToCommanderNavigation(*Nav,*Data,Requested,FVector(10,10,500),Projected)
				&& FVector::DistSquared2D(Requested,Projected.Location)<=100.
				&& Snapshot->IsSegmentClear(Projected.Location,Projected.Location,Soldier.AvoidanceRadiusCentimeters);
		}
		else if (Valid)
			Valid=HasDirectSurfaceConnection(*Data,Cache->Origin,Projected.Location)
				&& Snapshot->IsSegmentClear(Cache->Origin.Location,Projected.Location,Soldier.AvoidanceRadiusCentimeters,true);
		++Cache->Stage;
		if (Cache->Stage<4) { AuthorityState->SteeringQueued.Add(Id); AuthorityState->SteeringQueue.Enqueue(Id); }
	}
}

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
	FGuLiNavigationWorkBudget::FScope FlowScope(AuthorityState->PlanningBudget);
	int32 RemainingSampleBudget = FMath::Min(AuthorityState->PlanningBudget.Projections,FMath::Max(1, FlowFieldWalkabilitySamplesPerTick));
	for (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
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
			if (!AuthorityState->PlanningBudget.TakeProjection()) break;
			FGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget);
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
	AuthorityState->RecoveryWork.Reset(); AuthorityState->RecoveryQueue.Empty(); AuthorityState->ReadyRecoveryQueue.Empty(); AuthorityState->RecoveryScanCursor=0;
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

void UGuLiBattleAuthoritySubsystem::TickNavigationRepairs(int32& RemainingProjectionBudget,int32& RemainingPathBudget)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || AuthorityState->Soldiers.IsEmpty()) return;
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
	auto* World=GetWorld(); auto* State=World->GetGameState<AGuLiBattleGameState>();
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	auto* Data=Nav ? GetCommanderNavigationData(*Nav) : nullptr;
	auto* Registry=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!State || !Data || !Registry) return;
	const auto Obstacles=Registry->GetSnapshot();
	// Discovery is cursor based too. No whole-population scan/sort on each personal retry.
	for (int32 Visit=0; Visit<FMath::Min(128,AuthorityState->Soldiers.Num()) && AuthorityState->PlanningBudget.CanWork(); ++Visit)
	{
		auto& Soldier=AuthorityState->Soldiers[AuthorityState->RecoveryScanCursor++ % AuthorityState->Soldiers.Num()];
		if (!Soldier.CanAct() || AuthorityState->RecoveryWork.Contains(Soldier.SoldierId.Value)) continue;
		const bool Stale=Soldier.FinalDestinationNavigationGeneration!=AuthorityState->NavigationGeneration;
		const bool Retry=Soldier.ActiveOrderId && Soldier.NavigationState==EGuLiSoldierNavigationState::PersonalPathRecovery
			&& (Soldier.PersonalPathRetries==0 || (Soldier.PersonalPathRetries==1 && Soldier.NoProgressSeconds>=PersonalRecoveryRetrySeconds));
		if (!Stale && !Retry) continue;
		FNavigationRecoveryWork Work; Work.Id=Soldier.SoldierId; Work.Order=Soldier.ActiveOrderId;
		Work.Nav=AuthorityState->NavigationGeneration; Work.Epoch=State->GetMatchEpoch(); Work.TaskVersion=Soldier.TaskGeneration;
		Work.bHadFinal=Soldier.bHasFinalDestination && Soldier.ActiveOrderId!=0;
		Work.RequestedFinal=Soldier.FinalDestination.Location; Work.Started=Work.Progress=FPlatformTime::Seconds();
		AuthorityState->RecoveryWork.Add(Work.Id.Value,MoveTemp(Work)); AuthorityState->RecoveryQueue.Enqueue(Soldier.SoldierId.Value);
	}
	uint32 Id=0;
	for (int32 Visit=0; Visit<128 && AuthorityState->PlanningBudget.CanWork() && AuthorityState->RecoveryQueue.Dequeue(Id); ++Visit)
	{
		auto* W=AuthorityState->RecoveryWork.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
		if (!W || !Index) { AuthorityState->RecoveryWork.Remove(Id); continue; }
		const auto& Soldier=AuthorityState->Soldiers[*Index];
		if (!Soldier.CanAct() || W->TaskVersion!=Soldier.TaskGeneration || W->Order!=Soldier.ActiveOrderId
			|| W->Epoch!=State->GetMatchEpoch() || W->Nav!=AuthorityState->NavigationGeneration)
		{ AuthorityState->RecoveryWork.Remove(Id); continue; }
		const double Now=FPlatformTime::Seconds();
		if (Now-W->Started>=30. || Now-W->Progress>=5.) { W->bReady=true; W->bValid=false; }
		if (!W->bReady && W->Stage<2 && AuthorityState->PlanningBudget.TakeProjection())
		{
			FGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget); W->Progress=Now;
			if (W->Stage==0)
			{
				W->bValid=ProjectPointToCommanderNavigation(*Nav,*Data,Soldier.Location,FVector(10,10,5000),W->Start)
					&& FVector::DistSquared2D(Soldier.Location,W->Start.Location)<=100.;
				if (W->bValid) { W->Start.Location.X=Soldier.Location.X; W->Start.Location.Y=Soldier.Location.Y; }
				W->bReady=!W->bValid || !W->bHadFinal; W->Stage=1;
			}
			else
			{
				FVector Target=W->RequestedFinal;
				if (W->Candidate) { const int32 Ring=W->Candidate-1; const double Angle=UE_TWO_PI*(Ring%8)/8.;
					Target+=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*50.f*(Ring/8+1); }
				FMovePlanningJob Reservation; Reservation.Reservations=&AuthorityState->MoveReservations; Reservation.Team=Soldier.Team;
				Reservation.MaximumMemberRadiusCentimeters=Soldier.AvoidanceRadiusCentimeters; Reservation.ReleasedReservationIds.Add(Id);
				W->bValid=ProjectPointToCommanderNavigation(*Nav,*Data,Target,FVector(10,10,5000),W->Final)
					&& FVector::DistSquared2D(Target,W->Final.Location)<=100.
					&& !IsMoveCandidateBlockedByHardReservation(Reservation,W->Final.Location)
					&& Obstacles->IsSegmentClear(W->Final.Location,W->Final.Location,Soldier.AvoidanceRadiusCentimeters);
				if (W->bValid) { W->Stage=2; W->bReady=!W->Order; }
				else if (++W->Candidate>=33) W->bReady=true;
			}
		}
		else if (!W->bReady && W->Stage==2 && AuthorityState->PlanningBudget.TakePath())
		{
			FGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget); W->Progress=Now;
			++AuthorityState->PathQueries; ++AuthorityState->PersonalPathQueries; ++AuthorityState->RecoveryQueries;
			W->bValid=BuildCompletePathQuiet(*Nav,*Data,W->Start,W->Final.Location,W->Path);
			if (W->bValid || ++W->Candidate>=33) W->bReady=true; else W->Stage=1;
		}
		if (W->bReady) AuthorityState->ReadyRecoveryQueue.Enqueue(Id); else AuthorityState->RecoveryQueue.Enqueue(Id);
	}
}

void UGuLiBattleAuthoritySubsystem::CommitReadyNavigationRepairs()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->MassEntitySubsystem.IsValid()) return;
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->CommitBudget);
	auto* World=GetWorld(); const auto* State=World->GetGameState<AGuLiBattleGameState>(); if (!State) return;
	auto& Manager=AuthorityState->MassEntitySubsystem->GetMutableEntityManager(); uint32 Id=0;
	while (AuthorityState->CommitBudget.CanWork() && AuthorityState->CommitBudget.Members>0 && AuthorityState->ReadyRecoveryQueue.Dequeue(Id))
	{
		auto* W=AuthorityState->RecoveryWork.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
		if (!W || !Index) { AuthorityState->RecoveryWork.Remove(Id); continue; }
		auto& Soldier=AuthorityState->Soldiers[*Index];
		if (!Soldier.CanAct() || !Manager.IsEntityValid(Soldier.Entity) || W->TaskVersion!=Soldier.TaskGeneration || W->Order!=Soldier.ActiveOrderId
			|| W->Nav!=AuthorityState->NavigationGeneration || W->Epoch!=State->GetMatchEpoch())
		{ AuthorityState->RecoveryWork.Remove(Id); continue; }
		const double Now=FPlatformTime::Seconds();
		if (Now-W->Started>=30. || Now-W->Progress>=5.) { W->bValid=false; }
		// Recast may have replaced polygon references even when the member never moved.
		// Reuse a projected start only at that position, or inside the same current-generation polygon.
		const bool bStartStillValid=(FVector::DistSquared2D(Soldier.Location,W->Start.Location)<=1.
			&& FMath::Abs(Soldier.Location.Z-W->Start.Location.Z)<=MaximumSurfaceStepZCentimeters)
			|| (Soldier.FinalDestinationNavigationGeneration==W->Nav && Soldier.LastValidNavLocation.NodeRef==W->Start.NodeRef);
		if (W->bValid && !bStartStillValid)
		{ W->Stage=0; W->bReady=false; AuthorityState->RecoveryQueue.Enqueue(Id); continue; }
		--AuthorityState->CommitBudget.Members;
		if (W->bValid && W->bHadFinal)
		{
			FMovePlanningJob Reservation; Reservation.Reservations=&AuthorityState->MoveReservations; Reservation.Team=Soldier.Team;
			Reservation.MaximumMemberRadiusCentimeters=Soldier.AvoidanceRadiusCentimeters; Reservation.ReleasedReservationIds.Add(Id);
			if (IsMoveCandidateBlockedByHardReservation(Reservation,W->Final.Location))
			{ W->Stage=1; W->bReady=false; AuthorityState->RecoveryQueue.Enqueue(Id); continue; }
		}
		Soldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;
		if (W->bValid)
		{
			// A changed endpoint is committed together with its new personal path. CommandStart stays static.
			if (W->bHadFinal) Soldier.FinalDestination=W->Final;
			Soldier.LastValidNavLocation=FNavLocation(Soldier.Location,W->Start.NodeRef);
			if (W->Order)
			{
				Soldier.PersonalPathPoints=MoveTemp(W->Path); Soldier.PersonalPathPointIndex=Soldier.PersonalPathPoints.Num()>1 ? 1 : 0;
				Soldier.NavigationState=EGuLiSoldierNavigationState::PersonalPathRecovery; ++Soldier.PersonalPathRetries;
				Soldier.NoProgressSeconds=0; Soldier.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
				Soldier.ConsecutiveSurfaceFailures=0; Soldier.TotalSurfaceFailures=0; Soldier.bForceMovementUpdate=true;
				for (auto& Formation : AuthorityState->OrderFormations) if (Formation.BatchOrderId==W->Order && Formation.FinalDestinationBySoldierId.Contains(Id))
				{ Formation.FinalDestinationBySoldierId.Add(Id,W->Final); break; }
			}
		}
		else
		{
			++AuthorityState->RecoveryFailures;
			Soldier.LastFailedOrderId=Soldier.ActiveOrderId; Soldier.ActiveOrderId=0; Soldier.Velocity=FVector::ZeroVector;
			Soldier.Location=Soldier.LastValidNavLocation.Location; Soldier.NavigationState=EGuLiSoldierNavigationState::Blocked;
			Soldier.NavigationFailure=EGuLiSoldierNavigationFailure::PersonalPathFailed;
			Soldier.FailureSimulationSeconds=AuthorityState->SimulationSeconds;
		}
		++Soldier.StateRevision;
		auto& Order=Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId=Soldier.ActiveOrderId; Order.OrderRevision=Soldier.StateRevision; Order.bHasMoveTarget=Soldier.ActiveOrderId!=0;
		Order.FormationTarget=Soldier.FinalDestination.Location;
		auto& Move=Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.CreateNewAction(Soldier.ActiveOrderId ? EMassMovementAction::Move : EMassMovementAction::Stand,*World);
		Move.Center=Soldier.ActiveOrderId ? Soldier.FinalDestination.Location : Soldier.Location;
		Move.DesiredSpeed=FMassInt16Real(Soldier.ActiveOrderId ? MovementSpeedCentimetersPerSecond : 0.f);
		RefreshSoldierNavigationState(Soldier.SoldierId); AuthorityState->RecoveryWork.Remove(Id);
	}
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
	if (AuthorityState->ServerSimTick % GuLiCommanderNavigationPolicy::MovementUpdateIntervalTicks == 0)
	{
		CommitReadyMovePlans(); CommitReadyNavigationRepairs();
	}

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

	const UGuLiDynamicObstacleRegistrySubsystem* GroundObstacleRegistry =
		World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	check(GroundObstacleRegistry);
	TArray<FVector> GroundMechYieldVelocities;
	TBitArray<> bGroundMechYieldReturning;
	auto SetGroundMechYieldStandTarget = [World, &EntityManager](FSoldierRuntime& Soldier)
	{
		if (!EntityManager.IsEntityValid(Soldier.Entity)) return;
		FMassMoveTargetFragment& MoveTarget = EntityManager
			.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		if (MoveTarget.GetCurrentAction() != EMassMovementAction::Stand)
			MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
		MoveTarget.Center = Soldier.Location;
		MoveTarget.Forward = FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f).Vector();
		MoveTarget.DesiredSpeed = FMassInt16Real(0.0f);
	};
	int32 GroundMechYieldingSoldiers = 0;
	const bool bHasGroundMechObstacle = GroundObstacleRegistry->GetObstacles().ContainsByPredicate(
		[](const FGuLiDynamicObstacle& Obstacle)
		{
			return Obstacle.Kind == EGuLiDynamicObstacleKind::GroundMech
				&& (Obstacle.Team == EGuLiTeam::Red || Obstacle.Team == EGuLiTeam::Blue);
		});
	const bool bNeedsGroundMechYieldUpdate = bHasGroundMechObstacle
		|| AuthorityState->GroundMechYieldingSoldiers > 0;
	if (bNeedsGroundMechYieldUpdate)
	{
		struct FGroundMechYieldPressure
		{
			FVector Location = FVector::ZeroVector;
			float RadiusCentimeters = 0.0f;
			uint32 StableId = 0u;
			double DistanceSquared = TNumericLimits<double>::Max();
			bool bValid = false;
		};
		TArray<FGroundMechYieldPressure> GroundMechYieldPressures;
		GroundMechYieldPressures.SetNum(AuthorityState->Soldiers.Num());
		if (bHasGroundMechObstacle)
		{
			TArray<FGuLiGroundMassYieldCandidate> GroundMechYieldCandidates;
			GroundMechYieldCandidates.SetNum(AuthorityState->Soldiers.Num());
			using FGroundMechYieldCandidateBucket = TArray<int32, TInlineAllocator<8>>;
			TMap<FIntPoint, FGroundMechYieldCandidateBucket> GroundMechYieldCandidateGrid;
			float MaximumYieldCandidateRadiusCentimeters = 0.0f;
			for (int32 SoldierIndex = 0;
				SoldierIndex < AuthorityState->Soldiers.Num();
				++SoldierIndex)
			{
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
				FGuLiGroundMassYieldCandidate& Candidate =
					GroundMechYieldCandidates[SoldierIndex];
				Candidate.StableSoldierId = Soldier.SoldierId.Value;
				Candidate.Team = Soldier.Team;
				Candidate.Location = Soldier.Location;
				Candidate.RadiusCentimeters = Soldier.AvoidanceRadiusCentimeters;
				Candidate.bCanYield = Soldier.IsPresent() && Soldier.CanAct()
					&& Soldier.ActiveOrderId == 0u
					&& (Soldier.NavigationState == EGuLiSoldierNavigationState::Idle
						|| Soldier.NavigationState == EGuLiSoldierNavigationState::Arrived);
				if (Candidate.bCanYield
					&& !Candidate.Location.ContainsNaN()
					&& FMath::IsFinite(Candidate.RadiusCentimeters)
					&& Candidate.RadiusCentimeters > 0.0f)
				{
					GroundMechYieldCandidateGrid.FindOrAdd(
						GuLiCommanderNavigationPolicy::MakeAvoidanceSpatialCell(
							Candidate.Location,
							GroundMechYieldQueryCellSizeCentimeters)).Add(SoldierIndex);
					MaximumYieldCandidateRadiusCentimeters = FMath::Max(
						MaximumYieldCandidateRadiusCentimeters,
						Candidate.RadiusCentimeters);
				}
			}
			TArray<FGuLiGroundMassYieldCandidate> NearbyYieldCandidates;
			TArray<int32> NearbyYieldSoldierIndices;
			TArray<int32> SelectedYieldCandidates;
			for (const FGuLiDynamicObstacle& Obstacle : GroundObstacleRegistry->GetObstacles())
			{
				if (Obstacle.Kind != EGuLiDynamicObstacleKind::GroundMech
					|| (Obstacle.Team != EGuLiTeam::Red
						&& Obstacle.Team != EGuLiTeam::Blue)) continue;
				NearbyYieldCandidates.Reset();
				NearbyYieldSoldierIndices.Reset();
				const float QueryRadiusCentimeters = Obstacle.RadiusCentimeters
					+ MaximumYieldCandidateRadiusCentimeters
					+ GroundMechYieldActivationPaddingCentimeters;
				const int32 QueryCellRadius = FMath::CeilToInt(
					QueryRadiusCentimeters / GroundMechYieldQueryCellSizeCentimeters);
				const FIntPoint ObstacleCell =
					GuLiCommanderNavigationPolicy::MakeAvoidanceSpatialCell(
						Obstacle.Location,
						GroundMechYieldQueryCellSizeCentimeters);
				for (int32 CellX = ObstacleCell.X - QueryCellRadius;
					CellX <= ObstacleCell.X + QueryCellRadius;
					++CellX)
				{
					for (int32 CellY = ObstacleCell.Y - QueryCellRadius;
						CellY <= ObstacleCell.Y + QueryCellRadius;
						++CellY)
					{
						const FGroundMechYieldCandidateBucket* CandidateIndices =
							GroundMechYieldCandidateGrid.Find(FIntPoint(CellX, CellY));
						if (!CandidateIndices) continue;
						for (const int32 SoldierIndex : *CandidateIndices)
						{
							if (!GroundMechYieldCandidates.IsValidIndex(SoldierIndex)) continue;
							NearbyYieldCandidates.Add(GroundMechYieldCandidates[SoldierIndex]);
							NearbyYieldSoldierIndices.Add(SoldierIndex);
						}
					}
				}
				GuLiGroundMassCollision::SelectFriendlyYieldCandidates(
					NearbyYieldCandidates,
					Obstacle.Team,
					Obstacle.Location,
					Obstacle.RadiusCentimeters,
					GroundMechYieldActivationPaddingCentimeters,
					AvoidanceAgentHeightCentimeters,
					MaximumFriendlyYieldUnitsPerMech,
					SelectedYieldCandidates);
				for (const int32 NearbyIndex : SelectedYieldCandidates)
				{
					if (!NearbyYieldSoldierIndices.IsValidIndex(NearbyIndex)) continue;
					const int32 SoldierIndex = NearbyYieldSoldierIndices[NearbyIndex];
					if (!AuthorityState->Soldiers.IsValidIndex(SoldierIndex)) continue;
					const double DistanceSquared = FVector::DistSquared2D(
						Obstacle.Location,
						AuthorityState->Soldiers[SoldierIndex].Location);
					FGroundMechYieldPressure& Pressure =
						GroundMechYieldPressures[SoldierIndex];
					if (!Pressure.bValid || DistanceSquared < Pressure.DistanceSquared
						|| (DistanceSquared == Pressure.DistanceSquared
							&& Obstacle.Handle.Value < Pressure.StableId))
					{
						Pressure.Location = Obstacle.Location;
						Pressure.RadiusCentimeters = Obstacle.RadiusCentimeters;
						Pressure.StableId = Obstacle.Handle.Value;
						Pressure.DistanceSquared = DistanceSquared;
						Pressure.bValid = true;
					}
				}
			}
		}

		GroundMechYieldVelocities.Init(FVector::ZeroVector, AuthorityState->Soldiers.Num());
		bGroundMechYieldReturning.Init(false, AuthorityState->Soldiers.Num());
		for (int32 SoldierIndex = 0;
			SoldierIndex < AuthorityState->Soldiers.Num();
			++SoldierIndex)
		{
			FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
			const bool bEligibleIdle = Soldier.IsPresent() && Soldier.CanAct()
				&& Soldier.ActiveOrderId == 0u
				&& (Soldier.NavigationState == EGuLiSoldierNavigationState::Idle
					|| Soldier.NavigationState == EGuLiSoldierNavigationState::Arrived);
			if (!bEligibleIdle)
			{
				Soldier.bGroundMechYielding = false;
				Soldier.GroundMechLastPressureSimulationSeconds = -1.0;
				continue;
			}
			const FGroundMechYieldPressure& Pressure = GroundMechYieldPressures[SoldierIndex];
			if (Pressure.bValid)
			{
				if (!Soldier.bGroundMechYielding)
				{
					Soldier.bGroundMechYielding = true;
					Soldier.GroundMechYieldAnchor = Soldier.Location;
				}
				Soldier.GroundMechYieldTarget = GuLiGroundMassCollision::ComputeYieldTarget(
					Soldier.GroundMechYieldAnchor,
					Soldier.Location,
					Pressure.Location,
					Pressure.StableId,
					Soldier.SoldierId.Value,
					Pressure.RadiusCentimeters + Soldier.AvoidanceRadiusCentimeters
						+ GroundMechYieldClearancePaddingCentimeters,
					GroundMechYieldMaximumAnchorOffsetCentimeters);
				Soldier.GroundMechLastPressureSimulationSeconds =
					AuthorityState->SimulationSeconds;
			}
			else if (Soldier.bGroundMechYielding)
			{
				bool bReturning = false;
				Soldier.GroundMechYieldTarget =
					GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(
						Soldier.GroundMechYieldAnchor,
						Soldier.GroundMechYieldTarget,
						AuthorityState->SimulationSeconds,
						Soldier.GroundMechLastPressureSimulationSeconds,
						GroundMechYieldReturnDelaySeconds,
						bReturning);
				bGroundMechYieldReturning[SoldierIndex] = bReturning;
			}
			if (!Soldier.bGroundMechYielding) continue;
			++GroundMechYieldingSoldiers;
			FVector ToTarget = Soldier.GroundMechYieldTarget - Soldier.Location;
			ToTarget.Z = 0.0f;
			const float Distance = ToTarget.Size2D();
			if (Distance <= UE_SMALL_NUMBER)
			{
				if (bGroundMechYieldReturning[SoldierIndex])
				{
					Soldier.bGroundMechYielding = false;
					--GroundMechYieldingSoldiers;
				}
				Soldier.Velocity = FVector::ZeroVector;
				SetGroundMechYieldStandTarget(Soldier);
				continue;
			}
			const float MaximumSpeed = MovementSpeedCentimetersPerSecond
				* GroundMechYieldSpeedFraction;
			const float Speed = FMath::Min(MaximumSpeed, Distance / FixedDeltaSeconds);
			GroundMechYieldVelocities[SoldierIndex] = ToTarget / Distance * Speed;
		}
	}
	AuthorityState->GroundMechYieldingSoldiers = GroundMechYieldingSoldiers;

	// Yielding is capped per mech, but checking each yielded step against every soldier
	// would still turn the path into O(yielders * soldiers). Build one current-position
	// grid and visit only cells that can overlap the candidate step. The extra movement
	// allowance covers soldiers that advance earlier in the integration loop below.
	using FGroundMechYieldBucket = TArray<int32, TInlineAllocator<8>>;
	TMap<FIntPoint, FGroundMechYieldBucket> GroundMechYieldSpatialGrid;
	float GroundMechYieldCellSizeCentimeters = 1.0f;
	float MaximumGroundMechYieldBodyRadiusCentimeters = 0.0f;
	if (GroundMechYieldingSoldiers > 0)
	{
		for (const FSoldierRuntime& Soldier : AuthorityState->Soldiers)
		{
			if (Soldier.IsPresent()
				&& !Soldier.Location.ContainsNaN()
				&& FMath::IsFinite(Soldier.AvoidanceRadiusCentimeters)
				&& Soldier.AvoidanceRadiusCentimeters > 0.0f)
			{
				MaximumGroundMechYieldBodyRadiusCentimeters = FMath::Max(
					MaximumGroundMechYieldBodyRadiusCentimeters,
					Soldier.AvoidanceRadiusCentimeters);
			}
		}
		GroundMechYieldCellSizeCentimeters = FMath::Max(
			1.0f,
			MaximumGroundMechYieldBodyRadiusCentimeters * 2.0f);
		for (int32 SoldierIndex = 0;
			SoldierIndex < AuthorityState->Soldiers.Num();
			++SoldierIndex)
		{
			const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
			if (!Soldier.IsPresent()
				|| Soldier.Location.ContainsNaN()
				|| !FMath::IsFinite(Soldier.AvoidanceRadiusCentimeters)
				|| Soldier.AvoidanceRadiusCentimeters <= 0.0f)
			{
				continue;
			}
			GroundMechYieldSpatialGrid.FindOrAdd(
				GuLiCommanderNavigationPolicy::MakeAvoidanceSpatialCell(
					Soldier.Location,
					GroundMechYieldCellSizeCentimeters)).Add(SoldierIndex);
		}
	}

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
			&& Formation.SlotBySoldierId.Num() != ActiveIndices.Num() && AuthorityState->PlanningBudget.CanWork())
		{
			FGuLiNavigationWorkBudget::FScope ReassignScope(AuthorityState->PlanningBudget);
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

		if (!Formation.bFinalApproachStarted && AuthorityState->PlanningBudget.CanWork())
		{
			FGuLiNavigationWorkBudget::FScope WidthScope(AuthorityState->PlanningBudget);
			const int32 DesiredColumnCount = DetermineTransitFormationColumns(
				NavigationSystem,
				CommanderNavigationData,
				Formation.GuideAnchor,
				Formation.TravelFacingYawDegrees,
				Formation.MemberSpacingCentimeters,AuthorityState->PlanningBudget,Formation.TransitColumnCount);
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
					* Formation.MemberSpacingCentimeters + Formation.MaximumMemberRadiusCentimeters);

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
						Formation.MemberSpacingCentimeters,
						Formation.TransitColumnCount).Y
					: 0.0f;
				FVector LaneWaypoint = GuLiCommanderNavigationPolicy::CalculatePathLaneWaypoint(
					Formation.PathPoints,
					FMath::Clamp(*MemberPathPointIndex, 1, Formation.PathPoints.Num() - 1),
					LaneOffset);
				FVector SlotCorrectionTarget = Formation.GuideAnchor;
				FVector LocalSlotOffset = FVector::ZeroVector;
				if (TransitSlotIndex)
				{
					LocalSlotOffset = MakeFormationSlotOffset(
						static_cast<int32>(*TransitSlotIndex),
						Formation.MemberSpacingCentimeters,
						Formation.TransitColumnCount);
					SlotCorrectionTarget += FRotator(
						0.0f,
						Formation.TravelFacingYawDegrees,
						0.0f).RotateVector(LocalSlotOffset);
				}
				FVector ValidLane,ValidSlot;
				const bool bValidated=ResolveValidatedSteeringTargets(Soldier.SoldierId,HashCombine(Formation.PathRevision,GetTypeHash(*MemberPathPointIndex)),LaneWaypoint,SlotCorrectionTarget,ValidLane,ValidSlot);
				LaneWaypoint=bValidated ? ValidLane : Formation.PathPoints[*MemberPathPointIndex];
				SlotCorrectionTarget=bValidated ? ValidSlot : Soldier.Location;
				const FVector TravelDirection=(LaneWaypoint-Soldier.Location).GetSafeNormal();
				const FVector SlotVelocity = (SlotCorrectionTarget - Soldier.Location)
					.GetClampedToMaxSize(MovementSpeedCentimetersPerSecond * SlotCorrectionWeight * Soldier.FormationCorrectionAlpha);
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
				20.0f,
				MovementSpeedCentimetersPerSecond
					* MovementUpdateDeltaSeconds[SoldierIndex]);
			if (bRunsMovementUpdate[SoldierIndex]
				&& bCanApproachFinalSlot
				&& FVector::Dist2D(Soldier.Location, FinalDestination->Location)
					<= ArrivalSnapDistance)
			{
				FNavLocation SurfaceDestination;
				++AuthorityState->SurfaceMoveCalls;
				const bool bFinalSurfaceMoveSucceeded = FindMoveAlongCurrentNavigationSurface(
					*CommanderNavigationData,
					Soldier.LastValidNavLocation,
					FinalDestination->Location,
					SurfaceDestination,
					AuthorityState->PlanningBudget,
					this);
				const bool bFinalSurfaceMoveAccepted =
					GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
						bFinalSurfaceMoveSucceeded,
						Soldier.LastValidNavLocation.Location,
						SurfaceDestination.Location,
						MaximumSurfaceStepZCentimeters);
				const bool bFinalSegmentValid = bFinalSurfaceMoveAccepted
					&& World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(Soldier.Location,SurfaceDestination.Location,Soldier.AvoidanceRadiusCentimeters,true)
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
			AuthorityState->Soldiers.Num());
		bool bAnySoldierReceivesAvoidance = false;
		float MaximumSoldierRadius = MemberAgentRadiusCentimeters;
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
			Agent.RadiusCentimeters = Soldier.AvoidanceRadiusCentimeters;
			MaximumSoldierRadius = FMath::Max(MaximumSoldierRadius, Agent.RadiusCentimeters);
			bAnySoldierReceivesAvoidance |= Agent.bReceivesAvoidance;
		}
		float AvoidanceCellSizeCentimeters = MaximumSoldierRadius * 2.0f;

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


	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (!Soldier.IsAlive() || Soldier.ActiveOrderId == 0u
			|| Soldier.NavigationState != EGuLiSoldierNavigationState::PersonalPathRecovery)
		{
			continue;
		}
		if (!AuthorityState->RecoveryWork.Contains(Soldier.SoldierId.Value) && GuLiCommanderNavigationPolicy::ShouldBlockPersonalPathRecovery(
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
	}

	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}

		if (Soldier.bExternalActionsLocked && Soldier.IsAlive()) { RefreshSoldierNavigationState(Soldier.SoldierId); continue; }
		FGuLiMassHealthFragment& Health = EntityManager
			.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
		if (!Soldier.IsAlive())
		{
			RefreshSoldierNavigationState(Soldier.SoldierId);
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

			FVector TargetVelocity = (DesiredVelocities[SoldierIndex] + AvoidanceVelocity)
				.GetClampedToMaxSize(MovementSpeedCentimetersPerSecond);
			if (auto* Registry=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
				TargetVelocity=ConstrainEnvironmentVelocity(Soldier,*Registry->GetSnapshot(),TargetVelocity,MovementDeltaSeconds,MovementSpeedCentimetersPerSecond);
			Soldier.Velocity = TargetVelocity.IsNearlyZero(1.0f)
				? FVector::ZeroVector
				: FMath::VInterpTo(
					Soldier.Velocity,
					TargetVelocity,
					MovementDeltaSeconds,
					8.0f);

			if (Soldier.ContactObstacle) Soldier.Velocity=TargetVelocity;
			if (!Soldier.Velocity.IsNearlyZero(1.0f))
			{
				const FVector CandidateLocation = Soldier.LastValidNavLocation.Location
					+ Soldier.Velocity * MovementDeltaSeconds;
				FNavLocation SurfaceLocation;
				++AuthorityState->SurfaceMoveCalls;
				const bool bSurfaceMoveSucceeded = CommanderNavigationData
					&& FindMoveAlongCurrentNavigationSurface(
						*CommanderNavigationData,
						Soldier.LastValidNavLocation,
						CandidateLocation,
						SurfaceLocation,
					AuthorityState->PlanningBudget,
						this);
				const bool bSurfaceMoveAccepted = bSurfaceMoveSucceeded && World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(
					Soldier.Location,SurfaceLocation.Location,Soldier.AvoidanceRadiusCentimeters,true)
					&& GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
						bSurfaceMoveSucceeded,
						Soldier.LastValidNavLocation.Location,
						SurfaceLocation.Location,
						MaximumSurfaceStepZCentimeters);
				if (bSurfaceMoveAccepted)
				{
					Soldier.Velocity=(SurfaceLocation.Location-Soldier.Location)/FMath::Max(MovementDeltaSeconds,UE_SMALL_NUMBER);
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
		else if (Soldier.bGroundMechYielding
			&& GroundMechYieldVelocities.IsValidIndex(SoldierIndex)
			&& !GroundMechYieldVelocities[SoldierIndex].IsNearlyZero(1.0f))
		{
			const FVector YieldVelocity = GroundMechYieldVelocities[SoldierIndex];
			const FVector CandidateLocation = Soldier.LastValidNavLocation.Location
				+ YieldVelocity * FixedDeltaSeconds;
			bool bCandidateClear = true;
			const float MaximumEarlierStepCentimeters = MovementSpeedCentimetersPerSecond
				* GuLiCommanderNavigationPolicy::MaximumMovementUpdateDeltaSeconds;
			const float QueryRadiusCentimeters = Soldier.AvoidanceRadiusCentimeters
				+ MaximumGroundMechYieldBodyRadiusCentimeters
				+ MaximumEarlierStepCentimeters;
			const int32 QueryCellRadius = FMath::CeilToInt(
				QueryRadiusCentimeters / GroundMechYieldCellSizeCentimeters);
			const FIntPoint CandidateCell =
				GuLiCommanderNavigationPolicy::MakeAvoidanceSpatialCell(
					CandidateLocation,
					GroundMechYieldCellSizeCentimeters);
			for (int32 CellX = CandidateCell.X - QueryCellRadius;
				CellX <= CandidateCell.X + QueryCellRadius && bCandidateClear;
				++CellX)
			{
				for (int32 CellY = CandidateCell.Y - QueryCellRadius;
					CellY <= CandidateCell.Y + QueryCellRadius && bCandidateClear;
					++CellY)
				{
					const FGroundMechYieldBucket* NeighborIndices =
						GroundMechYieldSpatialGrid.Find(FIntPoint(CellX, CellY));
					if (!NeighborIndices) continue;
					for (const int32 OtherIndex : *NeighborIndices)
					{
						if (OtherIndex == SoldierIndex
							|| !AuthorityState->Soldiers.IsValidIndex(OtherIndex)) continue;
						const FSoldierRuntime& Other = AuthorityState->Soldiers[OtherIndex];
						if (!Other.IsPresent() || Other.Location.ContainsNaN()) continue;
						const float MinimumDistance = Soldier.AvoidanceRadiusCentimeters
							+ Other.AvoidanceRadiusCentimeters;
						const double CandidateDistanceSquared = FVector::DistSquared2D(
							CandidateLocation,
							Other.Location);
						if (CandidateDistanceSquared
							>= FMath::Square(static_cast<double>(MinimumDistance))) continue;
						const double CurrentDistanceSquared = FVector::DistSquared2D(
							Soldier.Location,
							Other.Location);
						if (CandidateDistanceSquared <= CurrentDistanceSquared + 1.0)
						{
							bCandidateClear = false;
							break;
						}
					}
				}
			}

			FNavLocation SurfaceLocation;
			++AuthorityState->SurfaceMoveCalls;
			const bool bSurfaceMoveSucceeded = bCandidateClear
				&& CommanderNavigationData
				&& FindMoveAlongCurrentNavigationSurface(
					*CommanderNavigationData,
					Soldier.LastValidNavLocation,
					CandidateLocation,
					SurfaceLocation,
					AuthorityState->PlanningBudget,
					this);
			const bool bSurfaceMoveAccepted =
				GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
					bSurfaceMoveSucceeded,
					Soldier.LastValidNavLocation.Location,
					SurfaceLocation.Location,
					MaximumSurfaceStepZCentimeters);
			if (bSurfaceMoveAccepted)
			{
				Soldier.Velocity=(SurfaceLocation.Location-Soldier.Location)/FMath::Max(FixedDeltaSeconds,UE_SMALL_NUMBER);
				Soldier.Location = SurfaceLocation.Location;
				Soldier.LastValidNavLocation = SurfaceLocation;
				Soldier.FacingYawDegrees = FMath::FixedTurn(
					Soldier.FacingYawDegrees,
					Soldier.Velocity.GetSafeNormal2D().Rotation().Yaw,
					FacingRateDegreesPerSecond * FixedDeltaSeconds);
				++AuthorityState->GroundMechYieldSteps;
				if (bGroundMechYieldReturning[SoldierIndex]
					&& FVector::DistSquared2D(
						Soldier.Location,
						Soldier.GroundMechYieldAnchor)
						<= FMath::Square(GroundMechYieldArrivalToleranceCentimeters))
				{
					Soldier.bGroundMechYielding = false;
					Soldier.GroundMechLastPressureSimulationSeconds = -1.0;
					Soldier.Velocity = FVector::ZeroVector;
					AuthorityState->GroundMechYieldingSoldiers = FMath::Max(
						0,
						AuthorityState->GroundMechYieldingSoldiers - 1);
				}
				FMassMoveTargetFragment& MoveTarget = EntityManager
					.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				const EMassMovementAction YieldAction = Soldier.bGroundMechYielding
					? EMassMovementAction::Move
					: EMassMovementAction::Stand;
				if (MoveTarget.GetCurrentAction() != YieldAction)
					MoveTarget.CreateNewAction(YieldAction, *World);
				MoveTarget.Center = Soldier.bGroundMechYielding
					? Soldier.GroundMechYieldTarget
					: Soldier.Location;
				MoveTarget.Forward = Soldier.Velocity.GetSafeNormal2D();
				MoveTarget.DesiredSpeed = FMassInt16Real(Soldier.Velocity.Size2D());
			}
			else
			{
				Soldier.Velocity = FVector::ZeroVector;
				SetGroundMechYieldStandTarget(Soldier);
				if (bCandidateClear) ++AuthorityState->SurfaceMoveFailures;
			}
		}
		else if (!bHasMovingOrder || bSuppressMovementForStep[SoldierIndex])
		{
			Soldier.Velocity = FVector::ZeroVector;
		}

		RefreshSoldierNavigationState(Soldier.SoldierId);
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
		if (const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
			if (const auto* Definition = Data->FindSoldierDefinition(Soldier.UnitTypeId))
				Population.Last().WorldBounds = Definition->GetModelBoundsCentimeters().TransformBy(
					FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location));
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
	TArray<FGuLiSoldierId> NewIds;
	GuLiCommanderSelectionQuery::CombineMembership(ExistingIds, HitIds, Request.Modifier, NewIds);
	if (NewIds.Num() > int32(GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE))
	{ OutAck.Result = EGuLiCommandAckResult::InvalidRequest; return false; }
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
			if (Soldier.Team != Team || !Soldier.IsAlive())
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
		OutImmediateAck = Existing->Stage==EMovePlanningStage::Completed ? Existing->Ack : Existing->AggregateAck;
		return true;
	}
	int32 TeamJobs=0;
	for (const auto& Existing : AuthorityState->MovePlanningJobs)
		if (Existing && Existing->Stage!=EMovePlanningStage::Completed && Existing->Team==PlayerState.GetTeam()) ++TeamJobs;
	if (TeamJobs>=8) { OutImmediateAck.Result=EGuLiCommandAckResult::RateLimited; return false; }

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
	Job.Handle = {AuthorityState->NextPlanId++, CurrentAuthorityEpoch};
	Job.Progress.Handle = Job.Handle;
	Job.PlayerState = &PlayerState;
	Job.OwningController = Cast<AController>(PlayerState.GetOwner());
	Job.bRequireOwningController = Job.OwningController.IsValid();
	Job.Team = PlayerState.GetTeam();
	Job.Request = Request;
	Job.FullSelection = Selection;
	Job.UpdatedSelection = Selection;
	Job.UpdatedSelection.Cohorts.Reset();
	Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.AuthorityEpoch = CurrentAuthorityEpoch;
	Job.PlanningStartedAt = Job.LastProgressAt = FPlatformTime::Seconds();
	Job.AggregateAck = OutImmediateAck;
	Job.AggregateAck.CommandKind = EGuLiCommandKind::Move;
	Job.Debug.ClientCommandId = Request.ClientCommandId;
	Job.Debug.RequestedTarget = FVector(Request.Target);
	Job.MaximumMemberRadiusCentimeters = MemberAgentRadiusCentimeters;
	Job.Reservations = &AuthorityState->MoveReservations;
	// A version must be frozen at admission, not when its later batch happens to run.
	for (const auto& Cohort : Selection.Cohorts) for (const auto Id : Cohort.MemberIds)
		if (const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value))
		{
			const auto& Soldier = AuthorityState->Soldiers[*Index];
			Job.FrozenTaskGenerations.Add(Id.Value, Soldier.TaskGeneration);
			Job.MaximumMemberRadiusCentimeters = FMath::Max(Job.MaximumMemberRadiusCentimeters, Soldier.AvoidanceRadiusCentimeters);
		}
	Job.MinimumSlotSpacingCentimeters = FMath::Max(DestinationMinimumSeparationCentimeters, Job.MaximumMemberRadiusCentimeters * 2);
	Job.DestinationBucketSizeCentimeters = FMath::Max(Job.MinimumSlotSpacingCentimeters,
		Job.MaximumMemberRadiusCentimeters + AuthorityState->MoveReservations.MaximumRadius);
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
		InvalidateTaskSoldierPlans(MakeArrayView(&Soldier.SoldierId,1));
		RefreshSoldierNavigationState(Soldier.SoldierId);
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
		if (const auto* Data = Authority->GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
			if (const auto* Definition = Data->FindSoldierDefinition(Debug.UnitTypeId))
			{
				const FBox Bounds = Definition->GetModelBoundsCentimeters();
				if (Bounds.IsValid) OutSnapshot.CollisionRadius = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
			}
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
			Out.Add({Soldier.SoldierId, FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location), Soldier.AvoidanceRadiusCentimeters});
	}
	Out.Sort([](const auto& A, const auto& B) { return A.Id.Value < B.Id.Value; });
}

bool UGuLiBattleAuthoritySubsystem::IsSoldierPhased(const FGuLiSoldierId Id) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	return Index && AuthorityState->Soldiers[*Index].bPhased;
}

void UGuLiBattleAuthoritySubsystem::CollectExternalUnitsForClearance(const FBox& Bounds, TArray<FGuLiMassExternalUnit>& Out) const
{
	Out.Reset();
	if (!AuthorityState || !Bounds.IsValid) return;
	for (const auto& Soldier : AuthorityState->Soldiers)
		if (Soldier.IsAlive() && Bounds.ExpandBy(Soldier.AvoidanceRadiusCentimeters * 2.0f).IsInsideOrOn(Soldier.Location))
			Out.Add({ Soldier.SoldierId, FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location), Soldier.AvoidanceRadiusCentimeters });
	Out.Sort([](const auto& A, const auto& B) { return A.Id.Value < B.Id.Value; });
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
	if (!Data || !Nav->ProjectPointToNavigation(Desired, Projected, FVector(20,20,2000), Data)
		|| FVector::DistSquared2D(Desired, Projected.Location) > FMath::Square(20.0)) return false;
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
	if (bRelocate || bLocked)
	{
		TArray<FGuLiSoldierId> Displaced;
		for (const auto& Entry : Participants)
		{
			Displaced.Add(Entry.Id);
			if (auto* Tasks = GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>())
				Tasks->CancelPendingMove(FGuLiTaskUnitId::Soldier(Entry.Id));
		}
		InvalidateTaskSoldierPlans(Displaced);
	}
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
			Soldier.FinalDestinationNavigationGeneration=0;
			auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			auto* Data = Nav ? GuLiCommanderMassPrivate::GetCommanderNavigationData(*Nav) : nullptr;
			if (Data) Nav->ProjectPointToNavigation(Soldier.Location, Soldier.LastValidNavLocation, FVector(20,20,300), Data);
			Soldier.DisplacementFrameFloor = AuthorityState->NextPoseFrameSequence;
			Soldier.DisplacementLocation = Soldier.Location;
			Soldier.DisplacementSimulationTime = AuthorityState->SimulationSeconds;
		}
		++Soldier.StateRevision;
		RefreshSoldierNavigationState(Soldier.SoldierId);
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
	TArray<TArray<int32>> ChunkSoldierIndices;
	ChunkSoldierIndices.Reserve(FMath::DivideAndRoundUp(SortedIndices.Num(), ChunkSize));
	for (const int32 SoldierIndex : SortedIndices)
	{
		const GuLiCommanderMassPrivate::FSoldierRuntime& CandidateSoldier =
			AuthorityState->Soldiers[SoldierIndex];
		bool bStartNewChunk = ChunkSoldierIndices.IsEmpty()
			|| ChunkSoldierIndices.Last().Num() >= ChunkSize;
		if (!bStartNewChunk)
		{
			const GuLiCommanderMassPrivate::FSoldierRuntime& FirstSoldier =
				AuthorityState->Soldiers[ChunkSoldierIndices.Last()[0]];
			bStartNewChunk = FirstSoldier.SoldierId.Value % CapturePosePhaseCount
				!= CandidateSoldier.SoldierId.Value % CapturePosePhaseCount;
		}

		if (bStartNewChunk)
		{
			ChunkSoldierIndices.AddDefaulted();
			ChunkSoldierIndices.Last().Reserve(ChunkSize);
		}
		ChunkSoldierIndices.Last().Add(SoldierIndex);
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
		Chunk.AuthorityEpoch = AuthorityState->AuthorityEpoch;
		Chunk.FrameSequence = FrameSequence;
		Chunk.ServerSimTick = AuthorityState->ServerSimTick;
		Chunk.ServerTimeSeconds = static_cast<float>(AuthorityState->SimulationSeconds);
		Chunk.ChunkIndex = static_cast<uint16>(ChunkIndex);
		Chunk.ChunkCount = static_cast<uint16>(ChunkCount);
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
				const double SampleSeconds = FMath::Max(0.0, AuthorityState->SimulationSeconds - Soldier.LastCapturedPoseSimulationSeconds);
				const double SpeedBound = FMath::Max(MovementSpeedCentimetersPerSecond, Soldier.LastCapturedMovementSpeed);
				const double AllowedStep = FMath::Max(200.0, SpeedBound * (SampleSeconds + GuLiCommanderSimulationTiming::StepSeconds));
				if (CapturedStepCentimeters > AllowedStep)
				{
					UE_LOG(
						LogGuLiCommanderMass,
						Error,
						TEXT("Authority pose discontinuity: soldier=%u previous_frame=%u frame=%u step_cm=%.1f previous=(%.1f,%.1f,%.1f) current=(%.1f,%.1f,%.1f) velocity=(%.1f,%.1f,%.1f) sample_seconds=%.3f allowed_step_cm=%.1f."),
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
						Soldier.Velocity.Z, SampleSeconds, AllowedStep);
				}
			}
			Soldier.LastCapturedPoseLocation = Soldier.Location;
			Soldier.LastCapturedPoseFrameSequence = FrameSequence;
			Soldier.LastCapturedPoseSimulationSeconds = AuthorityState->SimulationSeconds;
			Soldier.LastCapturedMovementSpeed = MovementSpeedCentimetersPerSecond;
			FGuLiQuantizedSoldierPose& Pose = Chunk.Samples.AddDefaulted_GetRef();
			Pose.SoldierId = Soldier.SoldierId;
			if (!GuLiCommanderPoseCodec::Quantize(Soldier.Location, Soldier.Velocity, Soldier.FacingYawDegrees, Pose))
			{
				UE_LOG(LogGuLiCommanderMass, Error, TEXT("Pose quantization range exceeded: soldier=%u"), Soldier.SoldierId.Value);
				OutChunks.Reset();
				return;
			}
			Pose.ActiveOrderId = Soldier.ActiveOrderId;
			Pose.State = Soldier.IsAlive()
				? (Soldier.ActiveOrderId != 0u
					? EGuLiSoldierPoseState::Moving
					: EGuLiSoldierPoseState::Idle)
				: EGuLiSoldierPoseState::Destroyed;
			Pose.Flags = Soldier.DisplacementFrameFloor != 0 && int32(FrameSequence - Soldier.DisplacementFrameFloor) < 3
				? GULI_SOLDIER_POSE_FLAG_TELEPORT : 0u;
		}
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

void UGuLiBattleAuthoritySubsystem::BuildGroundAvoidanceSnapshot(TArray<FGuLiGroundAvoidanceBody>& OutBodies) const
{
	OutBodies.Reset();
	if (!AuthorityState) return; // World exists before the battle is initialized.
	OutBodies.Reserve(AuthorityState->Soldiers.Num());
	for (const auto& Soldier : AuthorityState->Soldiers)
	{
		if (Soldier.IsPresent())
			OutBodies.Add({Soldier.SoldierId.Value, Soldier.Location, Soldier.Velocity, Soldier.AvoidanceRadiusCentimeters});
	}
}

void UGuLiBattleAuthoritySubsystem::BuildGroundCollisionSnapshot(
	TArray<FGuLiGroundMassBody>& OutBodies, uint32& OutEpoch, uint32& OutSequence,
	double& OutSimulationSeconds) const
{
	OutBodies.Reset(); OutEpoch = 0; OutSequence = 0; OutSimulationSeconds = 0.0;
	if (!AuthorityState || !GetWorld()) return;
	const auto* State = GetWorld()->GetGameState<AGuLiBattleGameState>();
	OutEpoch = State ? State->GetMatchEpoch() : 0u;
	OutSequence = AuthorityState->ServerSimTick;
	OutSimulationSeconds = AuthorityState->SimulationSeconds;
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	OutBodies.Reserve(AuthorityState->Soldiers.Num());
	for (const auto& Soldier : AuthorityState->Soldiers)
	{
		if (!Soldier.IsPresent()) continue;
		FGuLiGroundMassBody Body;
		Body.SoldierId = Soldier.SoldierId; Body.Team = Soldier.Team; Body.UnitTypeId = Soldier.UnitTypeId;
		Body.Location = Soldier.Location; Body.Velocity = Soldier.Velocity;
		Body.Rotation = FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f).Quaternion();
		Body.SampleSimulationSeconds = OutSimulationSeconds;
		Body.DisplacementRevision = Soldier.DisplacementFrameFloor;
		Body.RadiusCentimeters = Soldier.AvoidanceRadiusCentimeters;
		const auto* Definition = Data ? Data->FindSoldierDefinition(Soldier.UnitTypeId) : nullptr;
		GuLiGroundMassCollision::ResolveBodyBounds(Definition ? Definition->GetModelBoundsCentimeters() : FBox(ForceInit), Body);
		if (ensureMsgf(Body.IsValid(), TEXT("Invalid authoritative collision body %u"), Soldier.SoldierId.Value)) OutBodies.Add(Body);
	}
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
		// Passive deployments remain in CombatSamples as targets, but never emit automatic attacks.
		if (Soldier.bAllowAutomaticFire && Soldier.CanAct() && Soldier.WeaponProfiles) for (auto& Weapon : Soldier.Weapons)
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
			if (Event.ExecutorId == TEXT("GroundMachineGun"))
			{
				Request.Motion.Speed = Event.ProjectileSpeedCentimetersPerSecond;
				Request.Motion.MaximumLifetime = Event.ProjectileLifetimeSeconds;
				Request.Motion.SweepRadius = Event.ProjectileSweepRadiusCentimeters;
			}
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
	OutDebug.Velocity = Soldier.Velocity;
	OutDebug.LastValidNavLocation = Soldier.LastValidNavLocation.Location;
	OutDebug.NavigationNodeRef = Soldier.LastValidNavLocation.NodeRef;
	OutDebug.NavigationGeneration = AuthorityState->NavigationGeneration;
	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		if (const ANavigationData* NavigationData = GuLiCommanderMassPrivate::GetCommanderNavigationData(*NavigationSystem))
		{
			OutDebug.bNavigationNodeRefValid = NavigationData->IsNodeRefValid(Soldier.LastValidNavLocation.NodeRef);
		}
	}
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
	Result.GroundMechYieldSteps = AuthorityState->GroundMechYieldSteps;
	Result.GroundMechYieldingSoldiers = AuthorityState->GroundMechYieldingSoldiers;
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
	const float SpawnRadius = Definition->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters);
	FNavLocation Projected;
	if (!ProjectPointToCommanderNavigation(*Navigation, *NavData, Location,
		FVector(MemberAgentRadiusCentimeters, MemberAgentRadiusCentimeters, 5000.0f), Projected)
		|| FVector::DistSquared2D(Location, Projected.Location) > FMath::Square(MemberAgentRadiusCentimeters)) return false;
	if (AuthorityState->Soldiers.ContainsByPredicate([&](const FSoldierRuntime& Existing)
		{ return Existing.IsPresent() && FVector::DistSquared2D(Existing.Location, Projected.Location) < FMath::Square(SpawnRadius + Existing.AvoidanceRadiusCentimeters); })) return false;
	if (World->OverlapBlockingTestByChannel(Projected.Location + FVector(0,0,SpawnRadius + 20),
		FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(SpawnRadius))) return false;
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
	Soldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;
	InitializeSoldierCombat(Soldier, *Definition, EffectiveRuntimeTuning, World->GetSubsystem<UGuLiArmySkillSubsystem>());
	EntityManager.GetFragmentDataChecked<FGuLiCommanderStateTreeFragment>(Soldier.Entity).Tree = Definition->StateTreeAsset;
	Soldier.AvoidanceRadiusCentimeters = SpawnRadius;
	AuthorityState->SoldierIndexById.Add(Soldier.SoldierId.Value, AuthorityState->Soldiers.Num() - 1);
	EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity).SetTransform(FTransform(Soldier.Location));
	EntityManager.GetFragmentDataChecked<FAgentRadiusFragment>(Soldier.Entity).Radius = Soldier.AvoidanceRadiusCentimeters;
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
	if (auto* Tasks = GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()) Tasks->RegisterSoldiers(Team, OutIds);
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
		FGuLiSoldierStateItem FinalState;
		FinalState.SoldierId = Soldier.SoldierId; FinalState.Team = Soldier.Team; FinalState.UnitTypeId = Soldier.UnitTypeId;
		FinalState.LifeState = EGuLiSoldierLifeState::Destroyed; FinalState.Health = 0; FinalState.MaxHealth = Soldier.MaxHealth;
		FinalState.StateRevision = Soldier.StateRevision; FinalState.ActiveOrderId = 0;
		FinalState.bPhased = Soldier.bPhased; FinalState.bExternalActionsLocked = Soldier.bExternalActionsLocked;
		FinalState.DisplacementFrameFloor = Soldier.DisplacementFrameFloor;
		FinalState.DisplacementLocation = Soldier.DisplacementLocation; FinalState.DisplacementYaw = Soldier.FacingYawDegrees;
		FinalState.DisplacementSimulationTime = Soldier.DisplacementSimulationTime;
		AuthorityState->MoveReservations.Remove(Soldier.SoldierId.Value);
		if (AuthorityState->ActiveEndpoints.Remove(Soldier.SoldierId.Value)) AuthorityState->RemovedEndpointIds.Add(Soldier.SoldierId.Value);
		AuthorityState->DirtyEndpointIds.Remove(Soldier.SoldierId.Value);
		OnSoldierRetiring.Broadcast(FinalState);
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
bool UGuLiBattleAuthoritySubsystem::GetTaskSoldierInfo(FGuLiSoldierId Id, EGuLiTeam& Team, uint16& UnitTypeId, FVector& Location) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	if (!Index || !AuthorityState->Soldiers[*Index].IsAlive()) return false;
	const auto& Soldier = AuthorityState->Soldiers[*Index]; Team = Soldier.Team; UnitTypeId = Soldier.UnitTypeId; Location = Soldier.Location;
	return true;
}
void UGuLiBattleAuthoritySubsystem::InvalidateTaskSoldierPlans(TConstArrayView<FGuLiSoldierId> Soldiers)
{
	using namespace GuLiCommanderMassPrivate;
	if (!IsAuthorityWorld() || !AuthorityState) return;
	for (auto Id : Soldiers)
	{
		const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
		if (!Index) continue;
		auto& Soldier = AuthorityState->Soldiers[*Index];
		++Soldier.TaskGeneration;
		for (auto& Job : AuthorityState->MovePlanningJobs)
		{
			if (!Job || Job->Stage == EMovePlanningStage::Completed) continue;
			if (Job->FrozenTaskGenerations.Remove(Id.Value) && !Job->FinishedIds.Contains(Id.Value))
			{ Job->Progress.Failed.Add(Id); Job->FinishedIds.Add(Id.Value); }
			for (int32 M = 0; M < Job->Members.Num(); ++M) if (Job->Members[M].SoldierId == Id)
			{
				auto& Member = Job->Members[M]; Member.bEligible = false; Member.bStartValid = false;
				Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;
				Job->ReleasedReservationIds.Remove(Id.Value);
				Member.OldReservation = Soldier.ActiveOrderId && Soldier.bHasFinalDestination
					? Soldier.FinalDestination.Location : Soldier.Location;
				ReleaseMoveMemberDestination(*Job, M, false); RemoveMoveMemberFromPreparedFormations(*Job, Id);
				if (Job->Stage == EMovePlanningStage::ReadyToCommit) Job->Stage = EMovePlanningStage::ReconcileReservations;
			}
		}
	}

	for (auto& Job : AuthorityState->MovePlanningJobs)
		if (Job && Job->Stage!=EMovePlanningStage::Completed && Job->FrozenTaskGenerations.IsEmpty())
			CompleteMovePlanningJobWithSystemFailure(*Job,EGuLiCommandAckResult::Cancelled);

}
void UGuLiBattleAuthoritySubsystem::StopTaskSoldiers(TConstArrayView<FGuLiSoldierId> Soldiers)
{
	using namespace GuLiCommanderMassPrivate;
	if (!IsAuthorityWorld() || !AuthorityState || !AuthorityState->MassEntitySubsystem.IsValid()) return;
	InvalidateTaskSoldierPlans(Soldiers);
	auto& Manager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	for (auto Id : Soldiers)
	{
		const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
		if (!Index) continue;
		auto& Soldier = AuthorityState->Soldiers[*Index];
		Soldier.ActiveOrderId = 0; Soldier.bAutomaticAdvance = false; Soldier.bAttackMoveHolding = false;
		Soldier.bHasFinalDestination = false; Soldier.NavigationState = EGuLiSoldierNavigationState::Idle;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None; Soldier.PersonalPathPoints.Reset();
		Soldier.PersonalPathPointIndex = 0; Soldier.Velocity = FVector::ZeroVector; Soldier.bForceMovementUpdate = true;
		++Soldier.StateRevision;
		RefreshSoldierNavigationState(Id);
		if (!Manager.IsEntityValid(Soldier.Entity)) continue;
		auto& Order = Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = 0; Order.bHasMoveTarget = false; Order.OrderRevision = Soldier.StateRevision;
		auto& Move = Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.CreateNewAction(EMassMovementAction::Stand, *GetWorld()); Move.Center = Soldier.Location; Move.DesiredSpeed = FMassInt16Real(0.f);
	}
	AuthorityState->bForceManualAvoidanceRefresh = true;
}
bool UGuLiBattleAuthoritySubsystem::SetExplicitSelection(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers,
	TConstArrayView<FGuLiControllableActorId> Actors, FGuLiCommanderSelectionState& Selection)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !IsAuthorityWorld() || Soldiers.Num() > int32(GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE)
		|| Actors.Num() > int32(GULI_MAX_CONTROLLABLE_ACTOR_SELECTION)) return false;
	FGuLiCommanderSelectionState Next; Next.SelectionRevision = Selection.SelectionRevision + 1;
	if (!Next.SelectionRevision) ++Next.SelectionRevision;
	TSet<FGuLiSoldierId> Seen;
	for (auto Id : Soldiers)
	{
		EGuLiTeam UnitTeam; uint16 Type; FVector Location;
		if (Seen.Contains(Id) || !GetTaskSoldierInfo(Id, UnitTeam, Type, Location) || UnitTeam != Team) continue;
		Seen.Add(Id);
		if (Next.Cohorts.IsEmpty() || Next.Cohorts.Last().MemberIds.Num() == int32(GULI_CONTROL_COHORT_TARGET_SIZE))
			Next.Cohorts.AddDefaulted_GetRef().CohortId = FGuLiControlCohortId(AllocateNonZero(AuthorityState->NextControlCohortId));
		Next.Cohorts.Last().MemberIds.Add(Id); ++Next.Cohorts.Last().AliveCount;
	}
	Next.ActorIds.Append(Actors.GetData(), Actors.Num()); Next.AcceptedClientRequestId = Selection.AcceptedClientRequestId;
	Selection = MoveTemp(Next); return true;
}
bool UGuLiBattleAuthoritySubsystem::IsAutomaticallyAdvancing(FGuLiSoldierId Id) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	return Index && AuthorityState->Soldiers[*Index].IsAlive() && AuthorityState->Soldiers[*Index].bAutomaticAdvance;
}
void UGuLiBattleAuthoritySubsystem::StopAutomaticMove(TConstArrayView<FGuLiSoldierId> Soldiers)
{
	check(IsAuthorityWorld());
	InvalidateTaskSoldierPlans(Soldiers);
	auto& Manager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	for (auto Id : Soldiers)
	{
		if (!IsAutomaticallyAdvancing(Id)) continue;
		auto& Soldier = AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)];
		Soldier.ActiveOrderId = 0; Soldier.bHasFinalDestination = false;
		Soldier.NavigationState = EGuLiSoldierNavigationState::Idle;
		Soldier.Velocity = FVector::ZeroVector; ++Soldier.StateRevision;
		RefreshSoldierNavigationState(Id);
		auto& Order = Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = 0; Order.bHasMoveTarget = false; Order.OrderRevision = Soldier.StateRevision;
		auto& Move = Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.CreateNewAction(EMassMovementAction::Stand, *GetWorld()); Move.Center = Soldier.Location;
		Move.DesiredSpeed = FMassInt16Real(0.f);
	}
}
FGuLiMovePlanHandle UGuLiBattleAuthoritySubsystem::BeginAutomaticMovePlanning(EGuLiTeam Team,
	TConstArrayView<FGuLiSoldierId> Soldiers, const FVector& Destination)
{
	using namespace GuLiCommanderMassPrivate;
	if (!IsAuthorityWorld() || !AuthorityState || !AuthorityState->bPopulationSpawned || Destination.ContainsNaN()
		|| Soldiers.IsEmpty() || Soldiers.Num() > SoldierCountPerFormation) return {};
	int32 TeamJobs=0;
	for (const auto& Existing : AuthorityState->MovePlanningJobs)
		if (Existing && Existing->Team==Team && Existing->Stage!=EMovePlanningStage::Completed) ++TeamJobs;
	if (TeamJobs>=4) return {};
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!GameState || !GameState->GetMatchEpoch()) return {};
	auto Pointer = MakeUnique<FMovePlanningJob>(); auto& Job = *Pointer;
	Job.Handle = {AuthorityState->NextPlanId++, GameState->GetMatchEpoch()}; Job.Progress.Handle = Job.Handle;
	Job.bAutomatic = true; Job.Team = Team; Job.Request.Target = Destination;
	Job.MaximumMemberRadiusCentimeters=MemberAgentRadiusCentimeters;
	Job.PlanningStartedAt = Job.LastProgressAt = FPlatformTime::Seconds();
	Job.AuthorityEpoch = Job.Handle.Epoch; Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.Reservations = &AuthorityState->MoveReservations;
	auto& Cohort = Job.FullSelection.Cohorts.AddDefaulted_GetRef();
	Cohort.CohortId = FGuLiControlCohortId(AllocateNonZero(AuthorityState->NextControlCohortId));
	Cohort.MemberIds.Append(Soldiers.GetData(), Soldiers.Num());
	for (auto Id : Soldiers) if (const auto* Index = AuthorityState->SoldierIndexById.Find(Id.Value))
	{
		const auto& Soldier = AuthorityState->Soldiers[*Index];
		Job.FrozenTaskGenerations.Add(Id.Value, Soldier.TaskGeneration);
		Job.MaximumMemberRadiusCentimeters = FMath::Max(Job.MaximumMemberRadiusCentimeters, Soldier.AvoidanceRadiusCentimeters);
	}
	Job.MinimumSlotSpacingCentimeters = FMath::Max(DestinationMinimumSeparationCentimeters, Job.MaximumMemberRadiusCentimeters * 2);
	Job.DestinationBucketSizeCentimeters = FMath::Max(Job.MinimumSlotSpacingCentimeters, Job.MaximumMemberRadiusCentimeters + AuthorityState->MoveReservations.MaximumRadius);
	const auto Handle = Job.Handle; AuthorityState->MovePlanningJobs.Add(MoveTemp(Pointer)); return Handle;
}

bool UGuLiBattleAuthoritySubsystem::IssueAttackMove(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers,
	const FVector& Destination)
{
	return BeginAutomaticMovePlanning(Team, Soldiers, Destination).IsValid();
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
		if (Definition && !Definition->RangeSourceSlot.IsNone())
		{
			const auto* RangeProfile = GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>()->FindResolvedSkill(
				Soldier.Team, Soldier.UnitTypeId, Definition->RangeSourceSlot);
			if (RangeProfile && RangeProfile->bUnlocked && RangeProfile->bEquipped)
				Caster.ResolvedSourceRange = RangeProfile->RangeCentimeters;
		}
		auto& Context = Caster.Context;
		Context.Commander = &PlayerState; Context.SoldierId = Id; Context.Source = MakeSoldierTargetHandle(Id);
		Context.SourceTransform = FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location);
		Context.GroundPoint = GroundPoint; Context.RequestId = RequestId; Context.SkillId = Definition ? Definition->SkillId : NAME_None;
		Context.Level = Soldier.ActiveSkill.Level;
		Context.UnitTypeId = Soldier.UnitTypeId;
		Context.ShotOrdinal = Soldier.ActiveSkill.SuccessfulCasts;
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

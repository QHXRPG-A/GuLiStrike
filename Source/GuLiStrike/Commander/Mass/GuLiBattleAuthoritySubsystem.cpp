// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Units/GuLiEngineeringCollision.h"
#include "Commander/Mass/GuLiSoldierLocalPose.h"
#include "GuLiNetRules.h"
#include "GuLiNetAsyncClient.h"
#include "GuLiNetGameplayPool.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/GameInstance.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"

void RegisterGuLiHybridMassAdapters(UGuLiBattleAuthoritySubsystem& Authority);
class FGuLiHybridMassAdapter;
namespace GuLiHybridMass {struct FPosePipeline;}
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Commander/Mass/GuLiCommanderSpawnLayout.h"
#include "Commander/Behavior/GuLiCommanderMassStateTreeProcessor.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "GuLiNetPopulation.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Network/GuLiCommanderPoseCodec.h"

#include "Algo/MinElement.h"
#include "Algo/BinarySearch.h"
#include "Avoidance/MassAvoidanceFragments.h"
#include "Async/Async.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderDeploymentPoint.h"
#include "Commander/Framework/GuLiCommanderGameState.h"
#include "Commander/Framework/GuLiCommanderResourceAdapter.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Mass/GuLiCommanderSelectionQuery.h"
#include "Commander/Mass/GuLiControlCohortBuilder.h"
#include "Commander/Mass/Navigation/GuLiCommanderDestinationPlanner.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.h"
#include "Commander/Mass/GuLiMassMovementTuning.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Commander/Mass/Navigation/GuLiLocalFlowField.h"
#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"
#include "Commander/Mass/Navigation/GuLiNavigationDependency.h"
#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Containers/Queue.h"
#include "Components/BoxComponent.h"
#include "Commander/Mass/Navigation/GuLiIncrementalAssignment.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"
#include "Gameplay/CommanderSkills/GuLiUnitSkillExecution.h"
#include "Gameplay/CommanderSkills/GuLiWarMachineMissileSkill.h"
#include "Gameplay/Presentation/GuLiVATAnimation.h"
#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
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
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshPath.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/ObjectKey.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCommanderMass, Log, All);
CSV_DEFINE_CATEGORY(GuLiCommanderAuthority, true);

// 本文件是 Commander 的服务端战斗权威入口：维护独立士兵、解析选兵/移动意图，
// 常规战斗以 10 Hz 推进；本机手动移动按 Hybrid 逻辑步消费时间。
// SoldierId 跨网络，Mass Entity 句柄只在本地使用。
// 建议阅读顺序：生命周期与生成 -> ResolveSelection -> BeginMovePlanning -> TickAuthority -> 快照输出。
namespace GuLiCommanderMassPrivate
{
	TAutoConsoleVariable<int32> ImmediateMovement(TEXT("gs.Commander.ImmediateMovement"), 1,
		TEXT("Advance local manual Hybrid movement on each 60 Hz logical step. Combat and network pose publication remain at 10 Hz."));
	TAutoConsoleVariable<int32> ActiveSoftPairs(TEXT("gs.Commander.ActiveSoftPairs"), 1,
		TEXT("Local manual movement soft avoidance: 0 all pairs, 1 active endpoints only, 2 compare both outputs (diagnostic overhead)."));
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> MovementClockSoldier(TEXT("gs.Commander.MovementClockSoldier"), 0,
		TEXT("With move latency diagnostics, log one SoldierId's logical movement clock for 250 ms after admission. 0 disables."));
#endif
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
	constexpr int32 MaximumFriendlyYieldUnitsPerMech = 16;
	constexpr float GroundMechYieldQueryCellSizeCentimeters = 1000.0f;
	constexpr float GroundMechYieldActivationPaddingCentimeters = 100.0f;
	constexpr float GroundMechYieldClearancePaddingCentimeters = 50.0f;
	constexpr float GroundMechYieldMaximumAnchorOffsetCentimeters = 1250.0f;
	constexpr double GroundMechYieldReturnDelaySeconds = 0.5;
	constexpr float GroundMechYieldArrivalToleranceCentimeters = 5.0f;
	constexpr float MaximumSurfaceStepZCentimeters = 50.0f;
	constexpr float NavigationRepairRingStepCentimeters = 300.0f;
	constexpr float MaximumNavigationRepairDistanceCentimeters = 600.0f;
	constexpr double NavigationRepairDistanceToleranceCentimeters = 0.01;
	constexpr int32 NavigationRepairCandidates = 18; // Exact, nearest, then eight directions at 300/600 cm.
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
	static_assert(GuLiCommanderSimulationTiming::RateHz == 10u, "Navigation repair detection runs once per 10 Hz authority step.");

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
        FGuLiHybridObjectKey HybridKey;
        bool bLocalSimulation=true;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint16 UnitTypeId = GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID;
		float AvoidanceRadiusCentimeters = 150.0f;
		bool bMovementEvenArchetype = true;
		FVector Location = FVector::ZeroVector;
		EGuLiUnitMovementLayer MovementLayer = EGuLiUnitMovementLayer::Ground;
		float AltitudeCentimeters = 0.f;
		FVector GroundLocation() const { return GuLiUnitMovement::GroundPosition(Location, AltitudeCentimeters); }
		FVector WorldFromGround(const FVector& P) const { return GuLiUnitMovement::WorldPosition(P, AltitudeCentimeters); }
		FVector Velocity = FVector::ZeroVector;
		double LastMovementUpdateSimulationSeconds = 0.0;
		float FacingYawDegrees = 0.0f;
		float FixedSpawnYawDegrees = 0.0f;
		EGuLiSoldierFacingPolicy FacingPolicy = EGuLiSoldierFacingPolicy::FaceVelocity;
		FGuLiMechanicalAnimationState MechanicalPose;
		FGuLiVATPlayback VATPlayback;
		uint8 NextMechanicalMuzzle = 0;
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
		TSharedPtr<FGuLiSharedMoveIntent> MoveIntent;
        FGuLiGridMotion GridMotion;
        uint32 GridOrderId=0;
        TWeakObjectPtr<ACharacter> EngineeringCarrier;
        bool bEngineering=false;
        uint32 EngineeringDisplacementRevision=0;
        float ArrivalAcceptance=20.f;
		TSharedPtr<FGuLiSharedRouteGoal> RouteGoal;
		TSharedPtr<FGuLiSharedMoveRoute> SharedRoute;
		FGuLiDirectMoveCheck DirectDock;
		int32 RouteCursor = 0;
		bool bHasDockTarget = false;
		GuLiMoveLatency::FContext MoveTrace;
		double CommandAcceptedAt = 0, DirectionAppliedAt = 0, FirstDisplacementAt = 0;
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
		TSharedPtr<FGuLiNavigationDependency> ActiveNavigation;
		bool bNavigationBudgetPending = false;
		float NoProgressSeconds = 0.0f;
		double FailureSimulationSeconds = 0.0;
		float BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		int32 LastProgressPathPointIndex = INDEX_NONE;
		int32 ConsecutiveSurfaceFailures = 0;
		int32 TotalSurfaceFailures = 0;
		TArray<FVector> PersonalPathPoints;
		int32 PersonalPathPointIndex = 0;
		int32 PersonalPathRetries = 0;
		double NextPersonalPathQuerySeconds = 0;
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
        bool CanSimulate() const { return CanAct() && (!HybridKey.IsValid() || bLocalSimulation); }
		bool IsPresent() const { return IsAlive() && !bPhased; }

		bool IsAlive() const
		{
			return FMath::IsFinite(Health) && Health > 0.0f;
		}
	};

	void BeginPersonalPathRecovery(FSoldierRuntime& Soldier)
	{
		Soldier.NavigationState = EGuLiSoldierNavigationState::PersonalPathRecovery;
		Soldier.NoProgressSeconds = 0;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		Soldier.LastProgressPathPointIndex = INDEX_NONE;
		Soldier.PersonalPathPoints.Reset();
		Soldier.PersonalPathPointIndex = 0;
		Soldier.PersonalPathRetries = 0;
		Soldier.NextPersonalPathQuerySeconds = 0;
		Soldier.ActiveNavigation.Reset();
		Soldier.DirectDock = {};
		Soldier.bNavigationBudgetPending = true;
	}

	void GetNavigationRepairEscape(const FHitResult& Hit, const FVector& BodyCenter, const float Radius,
		FVector& Normal, double& Depth)
	{
		Normal = Hit.Normal;
		Depth = Hit.PenetrationDepth;
		// A short building can report the ground-facing MTD even though a walking unit
		// must leave horizontally. For an upright box, use its nearest horizontal face.
		const auto* Box = Cast<UBoxComponent>(Hit.GetComponent());
		if (!Box || Normal.SizeSquared2D() > UE_KINDA_SMALL_NUMBER
			|| Box->GetUpVector().Z < .999) return;
		const FVector Local = Box->GetComponentTransform().InverseTransformPositionNoScale(BodyCenter);
		const FVector Extent = Box->GetScaledBoxExtent();
		if (FMath::Abs(Local.X) > Extent.X || FMath::Abs(Local.Y) > Extent.Y
			|| FMath::Abs(Local.Z) > Extent.Z) return;
		const double DepthX = Extent.X - FMath::Abs(Local.X) + Radius;
		const double DepthY = Extent.Y - FMath::Abs(Local.Y) + Radius;
		const FVector LocalNormal = DepthX <= DepthY
			? FVector(Local.X >= 0. ? 1. : -1., 0, 0)
			: FVector(0, Local.Y >= 0. ? 1. : -1., 0);
		Normal = Box->GetComponentQuat().RotateVector(LocalNormal);
		Depth = FMath::Min(DepthX, DepthY);
	}

	bool CanRebindNavigationAtCurrentLocation(const FGuLiDynamicObstacleSnapshot& Obstacles,
		const FSoldierRuntime& Soldier, const FVector& Projected)
	{
		const float Radius=Soldier.AvoidanceRadiusCentimeters;
		return !Projected.ContainsNaN() && !Soldier.GroundLocation().ContainsNaN() && FMath::IsFinite(Radius) && Radius>0.f
			&& FVector::DistSquared2D(Projected,Soldier.GroundLocation())<=FMath::Square(NavigationRepairDistanceToleranceCentimeters)
			&& FMath::Abs(Projected.Z-Soldier.GroundLocation().Z)<=MaximumSurfaceStepZCentimeters
			&& Obstacles.IsSegmentClear(Soldier.GroundLocation(),Soldier.GroundLocation(),Radius, false, Soldier.MovementLayer);
	}

	// A rebuilt tile can strand a body just outside the new walkable surface. Permit only a
	// short, collision-checked escape, including outward depenetration from a new building.
	bool IsNavigationRepairSegmentClear(UWorld& World, const FGuLiDynamicObstacleSnapshot& Obstacles,
		const FSoldierRuntime& Soldier, const FVector& Target)
	{
		const FVector Delta = Target - Soldier.GroundLocation();
		const float Radius = Soldier.AvoidanceRadiusCentimeters;
		if (Target.ContainsNaN() || Soldier.GroundLocation().ContainsNaN() || !FMath::IsFinite(Radius) || Radius <= 0.f
			|| Delta.SizeSquared2D() > FMath::Square(MaximumNavigationRepairDistanceCentimeters + NavigationRepairDistanceToleranceCentimeters)
			|| FMath::Abs(Delta.Z) > MaximumSurfaceStepZCentimeters
			|| !Obstacles.IsSegmentClear(Soldier.GroundLocation(), Target, Radius, true, Soldier.MovementLayer)
			|| !Obstacles.IsSegmentClear(Target, Target, Radius, false, Soldier.MovementLayer)) return false;

		// Same body convention as gift-building clearance: foot location + radius + floor clearance.
		const FVector Lift(0, 0, Radius + 3.f);
		const FCollisionShape Body = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiMassNavigationRepair), false);
		if (Soldier.bEngineering) Params.IgnoreMask = GuLiEngineeringCollision::VehicleMask;
		if (const auto* Carrier=Soldier.EngineeringCarrier.Get())
		{
			Params.AddIgnoredActor(Carrier);
			TArray<AActor*> Children;Carrier->GetAllChildActors(Children,true);Params.AddIgnoredActors(Children);
		}
		GuLiUnitMovement::IgnoreOtherLayerBodies(World,Params,(Soldier.GroundLocation()+Target)*.5+Lift,
            Radius+float(Delta.Size())*.5f,Soldier.MovementLayer);
		Params.bFindInitialOverlaps = true;
		if (World.OverlapBlockingTestByChannel(Target + Lift, FQuat::Identity, ECC_Pawn, Body, Params)) return false;
		TArray<FHitResult> Hits;
		World.SweepMultiByChannel(Hits, Soldier.GroundLocation() + Lift, Target + Lift, FQuat::Identity, ECC_Pawn, Body, Params);
		FCollisionQueryParams RemainingGeometry = Params;
		bool bHasInitialPenetration = false;
		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bBlockingHit) continue;
			// Never ignore a wall along the segment. An initial overlap is escapable only along its outward normal.
			FVector EscapeNormal; double EscapeDepth;
			GetNavigationRepairEscape(Hit, Soldier.GroundLocation() + Lift, Radius, EscapeNormal, EscapeDepth);
			if (!Hit.bStartPenetrating || FVector::DotProduct(Delta, EscapeNormal) <= UE_KINDA_SMALL_NUMBER) return false;
			RemainingGeometry.AddIgnoredComponent(Hit.GetComponent());
			bHasInitialPenetration = true;
		}
		// Initial hits can hide a second wall. Check the complete segment against every
		// other component before accepting escape from the original overlapping bodies.
		if (bHasInitialPenetration && World.SweepTestByChannel(Soldier.GroundLocation() + Lift, Target + Lift,
			FQuat::Identity, ECC_Pawn, Body, RemainingGeometry)) return false;
		// Initial blocking overlaps can shorten a forward sweep. Sweep back from the clear end
		// as well: the first obstruction must be confined to an original penetration at the start.
		FHitResult ReverseHit;
		if (World.SweepSingleByChannel(ReverseHit, Target + Lift, Soldier.GroundLocation() + Lift,
			FQuat::Identity, ECC_Pawn, Body, Params))
		{
			const bool bOriginalPenetration = Hits.ContainsByPredicate([&](const FHitResult& Hit)
			{
				FVector EscapeNormal; double EscapeDepth;
				GetNavigationRepairEscape(Hit, Soldier.GroundLocation() + Lift, Radius, EscapeNormal, EscapeDepth);
				const double OutwardDistance = FVector::DotProduct(Delta, EscapeNormal);
				return Hit.bBlockingHit && Hit.bStartPenetrating && Hit.GetComponent() == ReverseHit.GetComponent()
					&& OutwardDistance > UE_KINDA_SMALL_NUMBER && FVector::DotProduct(Delta, ReverseHit.Normal) > 0.
					&& ReverseHit.Time >= 1. - (EscapeDepth + 1.) / OutwardDistance;
			});
			if (!bOriginalPenetration) return false;
		}
		return true;
	}

	template<class TSoldier>
	FVector ConstrainEnvironmentVelocity(TSoldier& Soldier,const FGuLiDynamicObstacleSnapshot& Snapshot,FVector Velocity,float Dt,float Speed,
		bool bApplyApproachSlowdown = true, const FVector* WorkTarget = nullptr)
	{
		TArray<int32> Nearby; const float Reach=FMath::Max(80.f,Speed*.5f);
		Snapshot.Query(Soldier.GroundLocation(),Soldier.AvoidanceRadiusCentimeters+Reach+100.f,Nearby);
		const auto NeedsBypass = [&](const FGuLiDynamicObstacle& O)
		{
			if (!WorkTarget || O.Kind!=EGuLiDynamicObstacleKind::StaticWorld) return true;
			// A mining/work approach that ends clear of the body must not keep
			// receiving tangential steering just because it is near that body.
			// The hard nonpenetration constraint below still applies to every step.
			FVector Segment=*WorkTarget-Soldier.GroundLocation();Segment.Z=0;
			const double T=FMath::Clamp(FVector::DotProduct(O.Location-Soldier.GroundLocation(),Segment)
				/FMath::Max(Segment.SizeSquared(),1.0),0.0,1.0);
			return FVector::DistSquared2D(Soldier.GroundLocation()+Segment*T,O.Location)
				< FMath::Square(O.RadiusCentimeters+Soldier.AvoidanceRadiusCentimeters+1.f);
		};
		const FGuLiDynamicObstacle* Chosen=nullptr; double Best=TNumericLimits<double>::Max();
		if (const auto* Existing=Snapshot.FindObstacle(Soldier.ContactObstacle))
		{
			const auto& O=*Existing;
			if (FVector::Dist2D(Soldier.GroundLocation(),O.Location)<O.RadiusCentimeters+Soldier.AvoidanceRadiusCentimeters+Reach+100.f
				&& FMath::Abs(Soldier.GroundLocation().Z-O.Location.Z)<=300.f+O.HalfHeight && !O.IgnoresLayer(Soldier.MovementLayer) && NeedsBypass(O)) Chosen=&O;
		}
		for (int32 I : Nearby)
		{
			const auto& O=Snapshot.GetObstacle(I); if (O.IgnoresLayer(Soldier.MovementLayer)) continue; if (FMath::Abs(Soldier.GroundLocation().Z-O.Location.Z)>300.f+O.HalfHeight) continue;
			const double Gap=FVector::Dist2D(Soldier.GroundLocation(),O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
			if (!Chosen && Gap<Best && Gap<Reach && !O.IgnoresLayer(Soldier.MovementLayer) && NeedsBypass(O)) { Best=Gap; }
		}
		if (!Chosen && !Soldier.ContactObstacle)
			for (int32 I : Nearby)
			{
				const auto& O=Snapshot.GetObstacle(I); if (O.IgnoresLayer(Soldier.MovementLayer)) continue;
				const double Gap=FVector::Dist2D(Soldier.GroundLocation(),O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
				if (FMath::Abs(Soldier.GroundLocation().Z-O.Location.Z)<=300.f+O.HalfHeight && Gap<=Best && Gap<Reach && !O.IgnoresLayer(Soldier.MovementLayer) && NeedsBypass(O))
				{ Chosen=&O; Best=Gap; }
			}
		float Pressure=0;
		if (Chosen)
		{
			FVector Normal=(Soldier.GroundLocation()-Chosen->Location).GetSafeNormal2D();
			if (Normal.IsNearlyZero()) Normal=FVector(1,0,0);
			const FVector Left(-Normal.Y,Normal.X,0);
			if (!Soldier.ContactObstacle)
			{
				Soldier.ContactObstacle=Chosen->Handle.Value;
				const double Side=FVector::DotProduct(Velocity,Left);
				Soldier.BypassSide=FMath::Abs(Side)>1. ? (Side>0 ? 1.f : -1.f) : ((Soldier.SoldierId.Value&1) ? 1.f : -1.f);
			}
			const double Gap=FVector::Dist2D(Soldier.GroundLocation(),Chosen->Location)-Chosen->RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
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
			const auto& O=Snapshot.GetObstacle(I); if (O.IgnoresLayer(Soldier.MovementLayer)) continue; if (FMath::Abs(Soldier.GroundLocation().Z-O.Location.Z)>300.f+O.HalfHeight) continue;
			const FVector N=(Soldier.GroundLocation()-O.Location).GetSafeNormal2D();
			const double Gap=FVector::Dist2D(Soldier.GroundLocation(),O.Location)-O.RadiusCentimeters-Soldier.AvoidanceRadiusCentimeters;
			const double Into=FVector::DotProduct(Velocity,N);
			const double Limit=-FMath::Max(0.,Gap-1.)/FMath::Max(.001f,Dt);
			if (Into<Limit) Velocity+=N*(Limit-Into);
		}
		return Velocity.GetClampedToMaxSize(Speed * (bApplyApproachSlowdown ? Soldier.EnvironmentSpeedScale : 1.0f));
	}

    struct FMovementUnitCalculation
    {
        FMassEntityHandle Entity;FGuLiSoldierId SoldierId;FGuLiHybridObjectKey HybridKey;
        uint64 OwnerEpoch=0,ControlEpoch=0,TaskGeneration=0;
        uint32 StateRevision=0,ActiveOrderId=0,DisplacementFrameFloor=0;
        EGuLiTeam Team=EGuLiTeam::Unassigned;
        EGuLiSoldierNavigationState NavigationState=EGuLiSoldierNavigationState::Idle;
        FVector Location,Velocity,Waypoint,NavLocation,PredictiveAcceleration;
        EGuLiUnitMovementLayer MovementLayer=EGuLiUnitMovementLayer::Ground;
        float AltitudeCentimeters=0.f;
        FVector GroundLocation() const {return GuLiUnitMovement::GroundPosition(Location,AltitudeCentimeters);}
        FVector WorkTarget=FVector::ZeroVector;
        bool bHasWorkTarget=false;
        float AvoidanceRadiusCentimeters=0,Speed=0,MoveDelta=0;
        bool bPresent=false,bCanSimulate=false,bReceives=false,bCanBePushed=false,bMoveIntent=false;
        bool bGroundMechYielding=false,bStandTarget=false;
        FVector GroundMechYieldAnchor,GroundMechYieldTarget;
        double GroundMechLastPressureSimulationSeconds=-1;
        uint32 ContactObstacle=0;
        float BypassSide=0,ContactClearSeconds=0,FormationCorrectionAlpha=1,EnvironmentSpeedScale=1;
        int32 ConsecutiveSurfaceFailures=0;
        FVector DesiredVelocity=FVector::ZeroVector,ConstrainedVelocity=FVector::ZeroVector;
        FVector CandidateLocation=FVector::ZeroVector,PassiveVelocity=FVector::ZeroVector;
        FGuLiObstacleRegionStamp ObstacleRegion;
        bool IsPresent() const {return bPresent;}
        bool CanSimulate() const {return bCanSimulate;}
    };
    struct FMovementCalculation final : GuLiNet::IGameplayCalculation
    {
        bool CanRunParallel() const override {return true;}
        TArray<FMovementUnitCalculation> Units;
        TArray<GuLiCommanderNavigationPolicy::FManualAvoidanceAgent> Agents;
        GuLiCommanderNavigationPolicy::FManualAvoidanceSpatialGrid Grid;
        TArray<FVector> Velocities,GroundMechYieldVelocities;
        TBitArray<> bGroundMechYieldReturning;
        GuLiCommanderNavigationPolicy::FManualAvoidanceMetrics Metrics;
        TSharedPtr<const FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe> Obstacles;
        float FixedDeltaSeconds=0,AvoidanceAgentHeightCentimeters=0;
        double SimulationSeconds=0;
        uint32 NavigationGeneration=0,LogicalStep=0;
        int32 PreviousYielding=0,YieldingCount=0;
        void Compute() override
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_WorkerMovement);
            const bool bHasGroundMechObstacle=Obstacles->Obstacles.ContainsByPredicate([](const FGuLiDynamicObstacle& O)
                {return O.Kind==EGuLiDynamicObstacleKind::GroundMech&&(O.Team==EGuLiTeam::Red||O.Team==EGuLiTeam::Blue);});
            const bool bNeedsGroundMechYieldUpdate=bHasGroundMechObstacle||PreviousYielding>0;
            int32 GroundMechYieldingSoldiers=0;
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
		GroundMechYieldPressures.SetNum(Units.Num());
		if (bHasGroundMechObstacle)
		{
			TArray<FGuLiGroundMassYieldCandidate> GroundMechYieldCandidates;
			GroundMechYieldCandidates.SetNum(Units.Num());
			using FGroundMechYieldCandidateBucket = TArray<int32, TInlineAllocator<8>>;
			TMap<FIntPoint, FGroundMechYieldCandidateBucket> GroundMechYieldCandidateGrid;
			float MaximumYieldCandidateRadiusCentimeters = 0.0f;
			for (int32 SoldierIndex = 0;
				SoldierIndex < Units.Num();
				++SoldierIndex)
			{
				const FMovementUnitCalculation& Soldier = Units[SoldierIndex];
				FGuLiGroundMassYieldCandidate& Candidate =
					GroundMechYieldCandidates[SoldierIndex];
				Candidate.StableSoldierId = Soldier.SoldierId.Value;
				Candidate.Team = Soldier.Team;
				Candidate.Location = Soldier.Location;
				Candidate.RadiusCentimeters = Soldier.AvoidanceRadiusCentimeters;
				Candidate.bCanYield = Soldier.MovementLayer==EGuLiUnitMovementLayer::Ground && Soldier.IsPresent() && Soldier.CanSimulate()
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
			for (const FGuLiDynamicObstacle& Obstacle : Obstacles->Obstacles)
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
					if (!Units.IsValidIndex(SoldierIndex)) continue;
					const double DistanceSquared = FVector::DistSquared2D(
						Obstacle.Location,
						Units[SoldierIndex].Location);
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

		GroundMechYieldVelocities.Init(FVector::ZeroVector, Units.Num());
		bGroundMechYieldReturning.Init(false, Units.Num());
		for (int32 SoldierIndex = 0;
			SoldierIndex < Units.Num();
			++SoldierIndex)
		{
			FMovementUnitCalculation& Soldier = Units[SoldierIndex];
			const bool bEligibleIdle = Soldier.IsPresent() && Soldier.CanSimulate()
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
					SimulationSeconds;
			}
			else if (Soldier.bGroundMechYielding)
			{
				bool bReturning = false;
				Soldier.GroundMechYieldTarget =
					GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(
						Soldier.GroundMechYieldAnchor,
						Soldier.GroundMechYieldTarget,
						SimulationSeconds,
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
				Soldier.bStandTarget=true;
				continue;
			}
			const float MaximumSpeed = Soldier.Speed;
			const float Speed = FMath::Min(MaximumSpeed, Distance / FixedDeltaSeconds);
			GroundMechYieldVelocities[SoldierIndex] = ToTarget / Distance * Speed;
		}
	}
	YieldingCount = GroundMechYieldingSoldiers;

            for (int32 I=0;I<Units.Num();++I)
            {
                auto& U=Units[I];auto& A=Agents[I];
                if (U.bReceives)
                {
                    const float StepSpeed=FMath::Min(U.Speed,FVector::Dist2D(U.Location,U.Waypoint)/U.MoveDelta);
                    U.DesiredVelocity=(U.Waypoint-U.Location).GetSafeNormal2D()*StepSpeed;
                    const FVector Target=(U.DesiredVelocity+U.PredictiveAcceleration.GetClampedToMaxSize(U.Speed*4.f)*U.MoveDelta).GetClampedToMaxSize(U.Speed);
                    A.DesiredVelocity=FMath::VInterpTo(U.Velocity,Target,U.MoveDelta,8.f);
                }
                A.bCanBePushed=U.bCanBePushed&&!U.bGroundMechYielding;
            }
            Metrics=GuLiCommanderNavigationPolicy::SolveSoftAvoidanceVelocities(Agents,AvoidanceAgentHeightCentimeters,Grid,Velocities,nullptr);
            for (int32 I=0;I<Units.Num();++I)
            {
                auto& U=Units[I];
                if (U.bReceives)
                {
                    U.ConstrainedVelocity=ConstrainEnvironmentVelocity(U,*Obstacles,Velocities[I],U.MoveDelta,U.Speed,
                        true,U.bHasWorkTarget?&U.WorkTarget:nullptr);
                    if (U.bMoveIntent)
                    {
                        const FVector To=U.Waypoint-U.Location;const FVector Forward=To.GetSafeNormal2D();
                        const double Excess=FVector::DotProduct(U.ConstrainedVelocity,Forward)-To.Size2D()/U.MoveDelta;
                        if (Excess>0) U.ConstrainedVelocity-=Forward*Excess;
                    }
                    U.CandidateLocation=U.NavLocation+U.ConstrainedVelocity*U.MoveDelta;
                }
                else if (Agents[I].bCanBePushed&&!Velocities[I].IsNearlyZero(1.f))
                {
                    const float Speed=U.Speed;
                    U.PassiveVelocity=ConstrainEnvironmentVelocity(U,*Obstacles,
                        Velocities[I],FixedDeltaSeconds,Speed,false);
                }
                const float Reach=U.AvoidanceRadiusCentimeters+FMath::Max(80.f,U.Speed*.5f)+100.f;
                U.ObstacleRegion=Obstacles->CaptureRegion(U.Location,U.CandidateLocation.IsZero()?U.Location:U.CandidateLocation,Reach);
            }
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
        Soldier.MovementLayer=Definition.MovementLayer;Soldier.AltitudeCentimeters=Definition.AltitudeCentimeters;
		Soldier.FacingPolicy = Definition.FacingPolicy;
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
		TSharedPtr<FGuLiNavigationDependency> Navigation;
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
		FGuLiNavigationDependency LaneConnection, SlotConnection;
		FGuLiObstacleRegionStamp Region;
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
		TWeakObjectPtr<const ANavigationData> DestinationData;
		FVector OldReservation = FVector::ZeroVector;
		GuLiCommanderDestinationPlanner::FFreeDestinationSlot Destination;
		EGuLiMovePlanFailureStage FailureStage = EGuLiMovePlanFailureStage::None;
		int32 LastCandidateIndex = INDEX_NONE;
		int32 CommitConnectionRetryCount = 0;
		FGuLiNavigationDependency StartConnection, EndConnection;
		FGuLiObstacleRegionStamp EndpointRegion, StartRegion, EndRegion;
		FVector StartConnectionTarget=FVector::ZeroVector, EndConnectionOrigin=FVector::ZeroVector;
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
		TSharedPtr<FGuLiNavigationDependency> Navigation;
		int32 NextConnector = 0;
		FNavLocation EndAnchor;
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

	struct FMoveStageProfile
	{
		double Seconds=0;
		int32 Checks=0, Paths=0;
	};
	struct FMoveStageScope
	{
		FMoveStageProfile& Profile;
		FGuLiNavigationWorkBudget& Budget;
		double Started;
		int32 Checks,Paths;
		FMoveStageScope(FMoveStageProfile& In,FGuLiNavigationWorkBudget& B)
			: Profile(In),Budget(B),Started(GuLiMoveLatency::IsEnabled()?FPlatformTime::Seconds():0),Checks(B.Projections),Paths(B.Paths) {}
		~FMoveStageScope()
		{
			if (!Started) return;
			Profile.Seconds+=FPlatformTime::Seconds()-Started;
			Profile.Checks+=Checks-Budget.Projections; Profile.Paths+=Paths-Budget.Paths;
		}
	};
	struct FMovePlanningJob
	{
		TSharedPtr<FGuLiSharedMoveIntent> MoveIntent;
		// Mutually exclusive stage scopes: destinations, matching, routes/starts, final review/commit.
		FMoveStageProfile StageProfile[4];
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
		uint32 SourceCommandId = 0;
		bool bFirstWorkRecorded = false;
		double LastScheduledAt = 0;
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
		TMap<int32,uint32> InvalidProjectionCache;
		TWeakObjectPtr<const ANavigationData> NavigationData;
		bool bCommitReady = false;
		bool bExpandingFallback = false;
		int32 DependencyCursor = 0;
		uint64 InvalidatedItems = 0;
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
		uint64 TaskVersion = 0, LastQueryFrame = MAX_uint64;
		uint32 Order = 0, Epoch = 0;
		TWeakObjectPtr<const ANavigationData> NavigationData;
		FVector Source = FVector::ZeroVector;
		FNavLocation Target;
		int32 Candidate = 0;
		bool bNodeOnly = false;
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

		Job.bExpandingFallback=true;
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
		Member.DestinationData=Job.NavigationData;
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

	void RequeueMoveMember(FMovePlanningJob& Job,int32 Index,bool bNewCandidate,bool bKeepPath,const TCHAR* Reason)
	{
		auto& Member=Job.Members[Index];
		if (Job.FinishedIds.Contains(Member.SoldierId.Value)) return;
		FMoveRouteTask Retry; Retry.CohortId=Member.CohortId; Retry.MemberPlanIndices.Add(Index);
		if (bKeepPath) for (const auto& Formation : Job.PreparedFormations)
			if (Formation.FinalDestinationBySoldierId.Contains(Member.SoldierId.Value))
			{
				Retry.CachedPath=Formation.PathPoints; Retry.Navigation=Formation.Navigation;
				Retry.bPathQueried=Retry.bPathValid=true;
				Retry.EndAnchor=FNavLocation(Formation.TargetAnchor,Member.EndConnection.Nodes.IsEmpty() ? 0 : Member.EndConnection.Nodes[0]); break;
			}
		RemoveMoveMemberFromPreparedFormations(Job,Member.SoldierId);
		Member.bAccepted=false;
		if (bNewCandidate) { ReleaseMoveMemberDestination(Job,Index,true); Retry.bNeedsCandidate=true; }
		Job.RouteTasks.Add(MoveTemp(Retry)); Job.bCommitReady=false; ++Job.InvalidatedItems;
		Job.Stage=EMovePlanningStage::Route;
		if (GuLiMoveLatency::IsEnabled())
		{
			auto Trace=GuLiMoveLatency::Context(Job.PlayerState.Get(),Job.SourceCommandId,Job.SharedBatchOrderId);
			Trace.Epoch=Job.AuthorityEpoch; Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value;
			GuLiMoveLatency::Record(TEXT("revalidate"),Trace,1,Reason);
		}
	}

	bool HasDirectSurfaceConnection(const ANavigationData&,const FNavLocation&,const FVector&,FGuLiNavigationDependency*,uint32);

	bool ValidatePreparedMove(FMovePlanningJob& Job,const ANavigationData& Data,uint32 Generation,
		FGuLiNavigationWorkBudget& Budget,const FGuLiDynamicObstacleSnapshot& Obstacles,
		const TArray<FSoldierRuntime>& Soldiers,const TMap<uint32,int32>& Indices)
	{
		using Check=FGuLiNavigationDependency::ECheck;
		while (Job.DependencyCursor<Job.Members.Num() && Budget.CanWork())
		{
			const int32 I=Job.DependencyCursor; auto& Member=Job.Members[I];
			if (!Member.bAccepted || Job.FinishedIds.Contains(Member.SoldierId.Value)) { ++Job.DependencyCursor; continue; }
			if (Member.DestinationData.Get()!=&Data || !Data.IsNodeRefValid(Member.DestinationNav.NodeRef))
			{ RequeueMoveMember(Job,I,true,false,TEXT("endpoint-nav")); ++Job.DependencyCursor; continue; }
			if (!Obstacles.IsRegionCurrent(Member.EndpointRegion))
			{
				if (!Budget.TakeProjection()) return false;
				if (!Obstacles.IsSegmentClear(Member.DestinationNav.Location,Member.DestinationNav.Location,Job.MaximumMemberRadiusCentimeters))
				{ RequeueMoveMember(Job,I,true,false,TEXT("endpoint-obstacle")); ++Job.DependencyCursor; continue; }
				Member.EndpointRegion=Obstacles.CaptureRegion(Member.DestinationNav.Location,Member.DestinationNav.Location,Job.MaximumMemberRadiusCentimeters);
			}
			const int32* SI=Indices.Find(Member.SoldierId.Value);
			if (SI && (!Soldiers[*SI].Location.Equals(Member.CommandStart.Location,1.) || !Obstacles.IsRegionCurrent(Member.StartRegion)))
			{
				if (!Budget.TakeProjection()) return false;
				FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
				const auto& Actual=Soldiers[*SI].LastValidNavLocation;
				if (!HasDirectSurfaceConnection(Data,Actual,Member.StartConnectionTarget,&Member.StartConnection,Generation)
					|| !Obstacles.IsSegmentClear(Actual.Location,Member.StartConnectionTarget,Job.MaximumMemberRadiusCentimeters,true))
				{ Member.CommandStart=Actual; RequeueMoveMember(Job,I,false,false,TEXT("start-connector")); ++Job.DependencyCursor; continue; }
				Member.CommandStart=Actual;
				Member.StartRegion=Obstacles.CaptureRegion(Actual.Location,Member.StartConnectionTarget,Job.MaximumMemberRadiusCentimeters);
			}
			if (!Obstacles.IsRegionCurrent(Member.EndRegion))
			{
				if (!Budget.TakeProjection()) return false;
				if (!Obstacles.IsSegmentClear(Member.EndConnectionOrigin,Member.DestinationNav.Location,Job.MaximumMemberRadiusCentimeters))
				{ RequeueMoveMember(Job,I,false,false,TEXT("end-connector-obstacle")); ++Job.DependencyCursor; continue; }
				Member.EndRegion=Obstacles.CaptureRegion(Member.EndConnectionOrigin,Member.DestinationNav.Location,Job.MaximumMemberRadiusCentimeters);
			}
			const Check Start=Member.StartConnection.Check(Data,Generation,Budget);
			if (Start==Check::Pending) return false;
			const Check End=Member.EndConnection.Check(Data,Generation,Budget);
			if (End==Check::Pending) return false;
			if (Start==Check::Invalid || End==Check::Invalid) RequeueMoveMember(Job,I,false,false,TEXT("connector"));
			++Job.DependencyCursor;
		}
		if (Job.DependencyCursor<Job.Members.Num()) return false;
		for (auto& Formation : Job.PreparedFormations)
		{
			if (Formation.MemberIds.IsEmpty()) continue;
			const Check Result=Formation.Navigation ? Formation.Navigation->Check(Data,Generation,Budget) : Check::Invalid;
			if (Result==Check::Pending) return false;
			if (Result==Check::Invalid)
			{
				const auto Ids=Formation.MemberIds;
				for (auto Id : Ids) if (const auto* Index=Job.MemberIndexById.Find(Id.Value)) RequeueMoveMember(Job,*Index,false,false,TEXT("path"));
			}
		}
		Job.NavigationGeneration=Generation; Job.DependencyCursor=0;
		return true;
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

			auto Receipt = decltype(Job.Ack.CohortResults)::ElementType{};
			Receipt.CohortId = Cohort.CohortId; Receipt.MemberCount = static_cast<uint8>(Cohort.MemberIds.Num()); Receipt.Result = Result;
			for (auto Id : Cohort.MemberIds) if (!Job.FinishedIds.Contains(Id.Value))
			{ Job.Progress.Failed.Add(Id); Job.FinishedIds.Add(Id.Value); }
			if (!Job.AggregateAck.CohortResults.ContainsByPredicate([&](const auto& R) { return R.CohortId==Receipt.CohortId; })) Job.AggregateAck.CohortResults.Add(Receipt);
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
		auto Trace=GuLiMoveLatency::Context(Job.PlayerState.Get(),Job.SourceCommandId,Job.SharedBatchOrderId);
		Trace.Epoch=Job.AuthorityEpoch; Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value;
		const FString Detail=FString::Printf(TEXT("result=%u accepted=%d failed=%d"),uint8(Result),Job.TotalAccepted,Job.FinishedIds.Num()-Job.TotalAccepted);
		GuLiMoveLatency::Record(TEXT("terminated"),Trace,Job.FinishedIds.Num(),*Detail);
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
		AvoidanceParameters.ObstaclePredictiveAvoidanceStiffness = 700.0f;
		AvoidanceParameters.PredictiveAvoidanceRadiusScale = 1.0f;
		AvoidanceParameters.PredictiveAvoidanceTime = GuLiMassMovementTuning::GetLookaheadSeconds();

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
		bool& bBudgetPending,
		const UObject* Querier = nullptr)
	{
		bBudgetPending=false;
		FNavLocation CurrentStart = Start;
		if (!NavigationData.IsNodeRefValid(CurrentStart.NodeRef))
		{
			// Dynamic tile rebuilds invalidate polygon references even when the same
			// ground remains walkable. Refresh only the reference at this position;
			// never turn a stale reference into a teleport past a new obstacle.
			FGuLiNavigationWorkBudget::FScope Scope(Budget);
			if (!Budget.TakeProjection()) { bBudgetPending=true; return false; }
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
		TArray<FVector>& OutPathPoints, TSharedPtr<FGuLiNavigationDependency>* OutNavigation = nullptr, uint32 Generation = 0)
	{
		OutPathPoints.Reset();
		FPathFindingQuery Query(nullptr, NavigationData, Start.Location, Target);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = NavigationSystem.FindPathSync(MoveTemp(Query));
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
		{
			return false;
		}
		Result.Path->EnableRecalculationOnInvalidation(false);
		if (OutNavigation) { *OutNavigation=MakeShared<FGuLiNavigationDependency>(); (*OutNavigation)->Capture(NavigationData,Generation,Result.Path); }
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

	bool HasDirectSurfaceConnection(const ANavigationData& NavigationData,const FNavLocation& Start,
		const FVector& Target,FGuLiNavigationDependency* Proof=nullptr,uint32 Generation=0)
	{
		if (!NavigationData.IsNodeRefValid(Start.NodeRef) || Start.Location.ContainsNaN() || Target.ContainsNaN()) return false;
		FVector Hit; bool bBlocked;
		if (const auto* Recast=Cast<ARecastNavMesh>(&NavigationData))
		{
			ARecastNavMesh::FRaycastResult Ray;
			bBlocked=ARecastNavMesh::NavMeshRaycast(Recast,Start.Location,Target,Hit,NavigationData.GetDefaultQueryFilter(),nullptr,Ray);
			if (Proof)
			{
				*Proof={}; Proof->Data=&NavigationData; Proof->CheckedGeneration=Generation;
				Proof->Nodes.Append(Ray.CorridorPolys,Ray.CorridorPolysCount);
				Proof->bComplete=Ray.bIsRaycastEndInCorridor && Ray.CorridorPolysCount>0;
			}
		}
		else
		{
			bBlocked=NavigationData.Raycast(Start.Location,Target,Hit,NavigationData.GetDefaultQueryFilter());
			if (Proof) { *Proof={}; Proof->Data=&NavigationData; Proof->CheckedGeneration=Generation; }
		}
		return !bBlocked && FVector::DistSquared2D(Hit,Target)<=400.;
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
			Sum += Soldier.GroundLocation();
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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMovementWorkerFrozenInputTest,
    "GuLiStrike.Commander.Mass.Navigation.WorkerFrozenInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiMovementWorkerFrozenInputTest::RunTest(const FString&)
{
    using namespace GuLiCommanderMassPrivate;
    auto Work=std::make_shared<FMovementCalculation>();Work->FixedDeltaSeconds=.1f;Work->AvoidanceAgentHeightCentimeters=300;
    auto Obstacles=MakeShared<FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe>();
    FGuLiDynamicObstacle Obstacle;Obstacle.Handle.Value=1;Obstacle.Location=FVector(300,100,0);Obstacle.RadiusCentimeters=60;
    Obstacles->Obstacles.Add(Obstacle);Obstacles->ByHandle.Add(1,0);
    for (int32 X=-1;X<=1;++X) for (int32 Y=-1;Y<=1;++Y) Obstacles->Cells.FindOrAdd({X,Y}).Add(0);
    Work->Obstacles=Obstacles;
    for (int32 I=0;I<48;++I)
    {
        auto& U=Work->Units.AddDefaulted_GetRef();auto& A=Work->Agents.AddDefaulted_GetRef();
        U.SoldierId=FGuLiSoldierId(I+1);U.Team=EGuLiTeam::Red;U.bPresent=U.bCanSimulate=true;
        U.Location=FVector((I%8)*90,(I/8)*85,I>=40?500:0);U.NavLocation=U.Location;
        U.Waypoint=U.Location+FVector(700,(I%2?1:-1)*180,0);U.Velocity=FVector(I%3?35:0,0,0);
        U.PredictiveAcceleration=FVector(0,(I%2?1:-1)*17,0);U.Speed=720;U.MoveDelta=.1f;U.AvoidanceRadiusCentimeters=55;
        U.bReceives=I%5!=0;U.bCanBePushed=!U.bReceives;U.bMoveIntent=U.bReceives;U.ActiveOrderId=U.bReceives?1:0;
        A.StableSoldierId=I+1;A.StableEntityKey=I+1;A.Team=uint8(U.Team);A.Location=U.Location;A.RadiusCentimeters=U.AvoidanceRadiusCentimeters;
        A.MaximumSpeed=U.Speed;A.bParticipates=true;A.bReceivesAvoidance=U.bReceives;A.bCanBePushed=U.bCanBePushed;
    }
    auto Original=Work->Units;auto Agents=Work->Agents;
    // The synchronous branch's original sequence, applied to frozen inputs.
    for (int32 I=0;I<Original.Num();++I) if (Original[I].bReceives)
    {
        auto& U=Original[I];const float Speed=FMath::Min(U.Speed,FVector::Dist2D(U.Location,U.Waypoint)/U.MoveDelta);
        U.DesiredVelocity=(U.Waypoint-U.Location).GetSafeNormal2D()*Speed;
        Agents[I].DesiredVelocity=FMath::VInterpTo(U.Velocity,
            (U.DesiredVelocity+U.PredictiveAcceleration.GetClampedToMaxSize(U.Speed*4)*U.MoveDelta).GetClampedToMaxSize(U.Speed),U.MoveDelta,8);
    }
    GuLiCommanderNavigationPolicy::FManualAvoidanceSpatialGrid Grid;TArray<FVector> Velocities;
    GuLiCommanderNavigationPolicy::SolveSoftAvoidanceVelocities(Agents,300,Grid,Velocities,nullptr);
    for (int32 I=0;I<Original.Num();++I)
    {
        auto& U=Original[I];
        if (U.bReceives)
        {
            U.ConstrainedVelocity=ConstrainEnvironmentVelocity(U,*Obstacles,Velocities[I],U.MoveDelta,U.Speed,
                true,U.bHasWorkTarget?&U.WorkTarget:nullptr);
            const FVector To=U.Waypoint-U.Location;const FVector Forward=To.GetSafeNormal2D();
            const double Excess=FVector::DotProduct(U.ConstrainedVelocity,Forward)-To.Size2D()/U.MoveDelta;
            if (Excess>0) U.ConstrainedVelocity-=Forward*Excess;
            U.CandidateLocation=U.NavLocation+U.ConstrainedVelocity*U.MoveDelta;
        }
        else if (Agents[I].bCanBePushed&&!Velocities[I].IsNearlyZero(1))
            U.PassiveVelocity=ConstrainEnvironmentVelocity(U,*Obstacles,Velocities[I],.1f,U.Speed,false);
    }
    const auto Input=*Work;
    const GuLiNet::Id Match{{1,2,3,4}},Player{{5,6,7,8}};
    for (const unsigned Threads:{0u,1u,2u,4u})
    {
        Work=std::make_shared<FMovementCalculation>(Input);bool Applied=false;
        GuLiNet::AsyncClientWorld Worker(Match,Player,1,[](const auto&)->std::unique_ptr<GuLiNet::IObjectAdapter>{return {};},
            [](auto,auto,auto,bool){return true;},{},false,{},0,{true,Threads?GuLiNet::MakeGameplayTaskPool(Threads):nullptr});
        TestTrue(TEXT("Frozen calculation admitted to the owner or bounded pool"),Worker.SubmitGameplay("frozen-movement",Work,65536,65536,[&]{Applied=true;}));
        const double Deadline=FPlatformTime::Seconds()+5;
        while (!Applied&&FPlatformTime::Seconds()<Deadline) {Worker.PumpApplications();FPlatformProcess::SleepNoStats(.001f);}
        TestTrue(TEXT("Frozen result acknowledged without waiting in an adapter"),Applied);
        if (Applied) for (int32 I=0;I<Original.Num();++I)
        {
            const auto& A=Original[I];const auto& B=Work->Units[I];
            TestTrue(TEXT("Owner and pool candidates agree with synchronous movement, crowded pressure and height filtering"),
                A.CandidateLocation.Equals(B.CandidateLocation,1.e-6)&&A.PassiveVelocity.Equals(B.PassiveVelocity,1.e-6)
                &&A.ConstrainedVelocity.Equals(B.ConstrainedVelocity,1.e-6)&&A.ContactObstacle==B.ContactObstacle);
        }
        Worker.Stop();
    }
    return true;
}
#endif

// PImpl：头文件只暴露不完整类型，实体管理器、索引、路径与异步任务留在实现文件中。
// 自定义 Deleter 在此处看到完整定义后再 delete，避免生成代码对不完整类型执行删除。
struct FGuLiBattleAuthorityState
{
	explicit FGuLiBattleAuthorityState(FGuLiNavigationWorkBudget& InBudget):PlanningBudget(InBudget) {}
	TWeakObjectPtr<UMassEntitySubsystem> MassEntitySubsystem;
	FMassArchetypeHandle AuthorityArchetype;
	FMassArchetypeHandle RuntimeTuningEvenBaseArchetype;
	FMassArchetypeHandle RuntimeTuningOddBaseArchetype;
	TArray<GuLiCommanderMassPrivate::FSoldierRuntime> Soldiers;
	TMap<uint32, int32> SoldierIndexById;
	TArray<GuLiCommanderMassPrivate::FOrderFormationRuntime> OrderFormations;
	TArray<TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>> MovePlanningJobs;
	// Local scheduling only. Identity, order and authority are checked again before use.
	TSet<uint32> ImmediateMoveIds;
	GuLiCommanderMassPrivate::FMoveReservationLedger MoveReservations;
	FGuLiNavigationWorkBudget& PlanningBudget;
	uint64 NavigationBudgetFrame=MAX_uint64;
	TMap<uint32,GuLiCommanderMassPrivate::FSteeringValidation> Steering;
	TQueue<uint32> SteeringQueue;
	TSet<uint32> SteeringQueued;
	FGuLiNavigationWorkBudget CommitBudget;
	FGuLiSharedMoveRoutePool SharedRoutes;
    TArray<TFunction<void()>> EngineeringPosePublishes;
	TArray<TWeakPtr<FGuLiSharedMoveIntent>> MoveIntents;
	int32 SharedDiscoveryCursor[2] = {}, SharedDiscoveryRemaining[2] = {}, IntentCursor = 0;
	uint64 SharedDiscoveryFrame[2] = {MAX_uint64,MAX_uint64};
	TSet<uint32> SharedPriorityVisited;
	uint64 SharedPriorityFrame = MAX_uint64;
	uint32 LastPrioritySoldierId = 0;
	uint64 NextPlanId = 1;
	uint32 PlanningCursor[2] = {};
	TWeakObjectPtr<const AGuLiBattlePlayerState> LastManualOwner;
	struct FCommandGroup { uint32 Batch = 0; double LastUsed = 0; };
	TMap<TPair<FObjectKey,uint32>,FCommandGroup> CommandGroups;
	uint64 RecoveryDetectionSteps = 0;
	FGuLiNavigationWorkBudget::FAllowance CategoryUsage[4];
	int32 ReservationBootstrapCursor = 0;
	TMap<uint32, FGuLiMoveEndpointSnapshot> ActiveEndpoints;
	TSet<uint32> DirtyEndpointIds;
	TSet<uint32> RemovedEndpointIds;
	TMap<uint32,GuLiCommanderMassPrivate::FNavigationRecoveryWork> RecoveryWork;
	TQueue<uint32> RecoveryQueue, ReadyRecoveryQueue;
	uint64 RecoveryQueries=0, RecoveryFailures=0, RecoveryRepairs=0, RecoveryRebinds=0;
	double NextPlanningDiagnostic=0;
	TMap<FIntPoint, TArray<int32>> SpatialGrid;
	GuLiCommanderNavigationPolicy::FManualAvoidanceSpatialGrid ManualAvoidanceSpatialGrid;
	TArray<GuLiCommanderNavigationPolicy::FManualAvoidanceAgent> ManualAvoidanceAgents;
    bool bMovementStepPending=false;
    std::shared_ptr<GuLiHybridMass::FPosePipeline> PosePipeline;
    struct FPendingPosePublish
    {
        TWeakPtr<FGuLiHybridMassAdapter> Adapter;
        uint64 Tick=0, OwnerEpoch=0, ControlEpoch=0;
    };
    // Within one logical step only. Never serialized or retained across custody changes.
    TArray<FPendingPosePublish> PendingPosePublishes, DispatchPosePublishes;
    // Local presentation identity only; never included in an ownership snapshot.
    TWeakObjectPtr<AGuLiSoldierStateReplicator> HybridReadModel;
    std::shared_ptr<GuLiCommanderMassPrivate::FMovementCalculation> MovementCalculation;
    std::function<void()> MovementApplication;
	TArray<FVector> CachedManualAvoidanceVelocities;
	TSharedPtr<const GuLiCommanderAvoidancePolicy::FSharedAvoidanceGeometry> AvoidanceGeometry;
	// Recycle storage only after the published version and every reader released it.
	TArray<TSharedPtr<GuLiCommanderAvoidancePolicy::FSharedAvoidanceGeometry>> AvoidanceGeometryPool;
	uint64 NextAvoidanceGeometryVersion = 1;
	TArray<GuLiCommanderAvoidancePolicy::FAgentSnapshot> SoftGeometryScratch;
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

TSharedPtr<const GuLiCommanderAvoidancePolicy::FSharedAvoidanceGeometry>
UGuLiBattleAuthoritySubsystem::AcquireAvoidanceGeometry(
	TConstArrayView<GuLiCommanderAvoidancePolicy::FAgentSnapshot> Agents)
{
	using namespace GuLiCommanderAvoidancePolicy;
	check(IsInGameThread());
	if (!AuthorityState) return nullptr;
	FGuLiPerformanceScope Timing(GetWorld(), TEXT("Avoidance.SharedGeometryMs"));
	auto* Perf = GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();
	if (Perf) Perf->Record(TEXT("Avoidance.SharedIndexAccesses"), 1);
	const auto& Previous = AuthorityState->AvoidanceGeometry;
	bool bSame = Previous && Previous->Agents.Num() == Agents.Num();
	const TCHAR* ChangeReason = Previous ? TEXT("Avoidance.GeometryMembershipChanges") : TEXT("Avoidance.GeometryFirstBuilds");
	// Full identity includes entity generation. Exact comparison covers teleports,
	// radius/health/membership and dynamic obstacles without missing a write path.
	for (const auto& A : Agents)
	{
		if (!bSame) break;
		const int32* Index = Previous->IndexByIdentity.Find(A.StableKey);
		if (!Index) { bSame = false; break; }
		const auto& B = Previous->Agents[*Index];
		if (A.Radius != B.Radius) ChangeReason = TEXT("Avoidance.GeometryRadiusChanges");
		else if (A.bParticipates != B.bParticipates || A.bEnvironment != B.bEnvironment
			|| A.bEngineering != B.bEngineering || A.MovementLayer!=B.MovementLayer || A.bLayerBound!=B.bLayerBound) ChangeReason = TEXT("Avoidance.GeometryParticipationChanges");
		else if (A.Location != B.Location) ChangeReason = A.bEnvironment ? TEXT("Avoidance.GeometryEnvironmentMoves") : TEXT("Avoidance.GeometryUnitMoves");
		bSame = A.Location == B.Location && A.Radius == B.Radius
			&& A.bParticipates == B.bParticipates && A.bEnvironment == B.bEnvironment
			&& A.bEngineering == B.bEngineering && A.MovementLayer == B.MovementLayer
			&& A.bLayerBound == B.bLayerBound;
	}
	if (bSame)
	{
		if (Perf) Perf->Record(TEXT("Avoidance.SharedIndexReuses"), 1);
		return Previous;
	}
	TSharedPtr<FSharedAvoidanceGeometry> Next;
	for (const auto& Storage : AuthorityState->AvoidanceGeometryPool)
		if (Storage.GetSharedReferenceCount() == 1) { Next = Storage; break; }
	if (!Next)
	{
		Next = MakeShared<FSharedAvoidanceGeometry>();
		AuthorityState->AvoidanceGeometryPool.Add(Next);
	}
	Next->Version = AuthorityState->NextAvoidanceGeometryVersion++;
	Next->Agents.Reset(); Next->IndexByIdentity.Reset();
	Next->Agents.Append(Agents.GetData(), Agents.Num());
	float MaximumRadius = 0;
	for (int32 I = 0; I < Next->Agents.Num(); ++I)
	{
		const auto& A = Next->Agents[I];
		Next->IndexByIdentity.Add(A.StableKey, I);
		if (A.bParticipates && !A.bEnvironment) MaximumRadius = FMath::Max(MaximumRadius, A.Radius);
	}
	Next->CellSize = FMath::Max(SpatialCellSizeCentimeters, MaximumRadius * 2);
	Next->MaximumBucketOccupancy = BuildSpatialGrid(Next->Agents, Next->Grid, Next->CellSize);
	AuthorityState->AvoidanceGeometry = Next;
	if (Perf) { Perf->Record(TEXT("Avoidance.SharedIndexBuilds"), 1); Perf->Record(TEXT("Avoidance.SharedIndexVersion"), double(Next->Version)); Perf->Record(ChangeReason, 1); }
	return Next;
}

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
    if (UGuLiNetworkSettings::IsHybrid(GetWorld())) Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
	Collection.InitializeDependency<UMassEntitySubsystem>();
	Collection.InitializeDependency<UGuLiRuntimeTuningSubsystem>();
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	Collection.InitializeDependency<UGuLiArmySkillSubsystem>();
	Collection.InitializeDependency<UGuLiCommanderResourceAdapter>();
	Collection.InitializeDependency<UGuLiDynamicObstacleRegistrySubsystem>();
	Collection.InitializeDependency<UGuLiGroundGridSubsystem>();
	AuthorityState.Reset(new FGuLiBattleAuthorityState(GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>()->FrameBudget()));
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
        if (UGuLiNetworkSettings::IsHybrid(World)) RegisterGuLiHybridMassAdapters(*this);
	}
}

void UGuLiBattleAuthoritySubsystem::Deinitialize()
{
    if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) Hybrid->UnregisterFactories(*this);
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
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()) && !bAdvancingHybrid) return;
	CSV_SCOPED_TIMING_STAT(GuLiCommanderAuthority, WorldTick);
	if (!bSoldierSimulationEnabled || !AuthorityState || !IsAuthorityWorld())
	{
		return;
	}
	if (!AuthorityState->bPopulationSpawned)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	const int32 ClockSoldierId = GuLiMoveLatency::IsEnabled()
		? GuLiCommanderMassPrivate::MovementClockSoldier.GetValueOnGameThread() : 0;
	const int32* ClockIndex = ClockSoldierId > 0 ? AuthorityState->SoldierIndexById.Find(ClockSoldierId) : nullptr;
	const FVector ClockStart = ClockIndex ? AuthorityState->Soldiers[*ClockIndex].Location : FVector::ZeroVector;
	const double ClockLast = ClockIndex ? AuthorityState->Soldiers[*ClockIndex].LastMovementUpdateSimulationSeconds : 0.0;
#endif
	// 可走性采样按世界帧分配预算，放在固定步循环外，避免补帧时成倍增加 NavMesh 查询。
	if (AuthorityState->NavigationBudgetFrame!=GFrameCounter)
	{
		AuthorityState->NavigationBudgetFrame=GFrameCounter;
		GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>()->FrameBudget();
		AuthorityState->CommitBudget.Reset(MoveCommitMilliseconds, 0, 0, FMath::Max(25, MoveCommitMembersPerFrame));
	}
	auto& Budget = AuthorityState->PlanningBudget;
	const bool bImmediateMovement = bAdvancingHybrid
		&& GuLiCommanderMassPrivate::ImmediateMovement.GetValueOnGameThread() != 0
		&& !UGuLiHotspotOptions::Enabled(GetWorld(), EGuLiHotspot::GameplayWorker);
	FGuLiNavigationWorkBudget::FAllowance Shares[4]={{.0010,16,2},{.0004,16,2},{.0003,24,0},{.0003,8,0}};
	static const auto* AdmissionBatchVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("gs.Commander.BatchMoveAdmissions"));
	const bool bBatchAdmissions = AdmissionBatchVariable && AdmissionBatchVariable->GetInt()!=0;
	auto RunCategory=[&](int32 Category,FGuLiNavigationWorkBudget::FAllowance& Allowance)
	{
		if (!Budget.CanWork() || Allowance.Seconds<=0) return;
		FGuLiNavigationWorkBudget::FSlice Slice(Budget,Allowance,Category==0 && bBatchAdmissions ? .0005 : .0002);
		if (Category==0)
		{
			if (auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()) Tasks->PumpMoveAdmissions(Budget);
			TickMovePlanning(Budget.Projections,Budget.Paths,true);
			if (bImmediateMovement) CommitReadyMovePlans();
			TArray<uint32> Priority = bImmediateMovement ? AuthorityState->ImmediateMoveIds.Array() : TArray<uint32>{};
			Priority.Sort();
			TickSharedNavigation(0, Priority);
		}
		else if (Category==1) { TickMovePlanning(Budget.Projections,Budget.Paths,false); TickSharedNavigation(1); }
		else if (Category==2) TickSharedNavigation(2);
		else
        {
            TickNavigationRepairs();
            if (auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady()) Grid->Advance(Budget);
        }
	};
	for (int32 Round=0; Round<8 && Budget.CanWork(); ++Round)
	{
		RunCategory(0,Shares[0]);
		for (int32 Offset=0; Offset<3; ++Offset) { const int32 C=1+(GFrameCounter+Offset)%3; RunCategory(C,Shares[C]); }
	}
	for (int32 C=0; C<4; ++C) AuthorityState->CategoryUsage[C]=Shares[C];
	for (int32 Round=0; Round<8 && Budget.CanWork(); ++Round)
		for (int32 Offset=0; Offset<4 && Budget.CanWork(); ++Offset)
		{
			const int32 C=(GFrameCounter+Offset)%4;
			FGuLiNavigationWorkBudget::FAllowance Borrowed{Budget.LimitSeconds-Budget.Elapsed(),Budget.Projections,Budget.Paths};
			RunCategory(C,Borrowed);
			auto& Used=AuthorityState->CategoryUsage[C]; Used.UsedSeconds+=Borrowed.UsedSeconds;
			Used.UsedProjections+=Borrowed.UsedProjections; Used.UsedPaths+=Borrowed.UsedPaths;
		}
	// Validation and routing keep the shared per-frame budget. Newly committed
	// manual commands are visited directly instead of waiting for the discovery cursor.
	CommitReadyMovePlans();
	if (bImmediateMovement && !AuthorityState->ImmediateMoveIds.IsEmpty())
	{
		for (auto It = AuthorityState->ImmediateMoveIds.CreateIterator(); It; ++It)
		{
			const int32* Index = AuthorityState->SoldierIndexById.Find(*It);
			const auto* S = Index ? &AuthorityState->Soldiers[*Index] : nullptr;
			if (!S || !S->CanSimulate() || !S->ActiveOrderId || !S->MoveIntent
				|| S->bAutomaticAdvance) It.RemoveCurrent();
		}
		if (!AuthorityState->ImmediateMoveIds.IsEmpty())
		{
			TArray<uint32> Priority = AuthorityState->ImmediateMoveIds.Array();
			Priority.Sort();
			TickSharedNavigation(2);
			TickSharedNavigation(0, Priority);
		}
	}
	if (auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()) Tasks->ConsumeMoveProgress();
	PublishMoveEndpointChanges();

	// 负 DeltaTime 按 0 处理；每次消耗 0.1 秒，累计上限把本帧模拟工作限制在最多 4 步。
	const double UnclampedAccumulator = AuthorityState->FixedStepAccumulator
		+ static_cast<double>(FMath::Max(0.0f, DeltaTime));
	const double MaximumAccumulator =
		static_cast<double>(GuLiCommanderMassPrivate::MaxAccumulatedSeconds);
    const bool bWorkerMovement=UGuLiHotspotOptions::Enabled(GetWorld(),EGuLiHotspot::GameplayWorker);
	if (!bWorkerMovement && UnclampedAccumulator > MaximumAccumulator)
	{
		const uint64 DroppedSteps = static_cast<uint64>(FMath::Max<int64>(1,
			FMath::CeilToInt64((UnclampedAccumulator - MaximumAccumulator)
				/ GuLiCommanderMassPrivate::FixedStepSeconds)));
		AuthorityState->DroppedFixedStepCount += DroppedSteps;
		FGuLiWingmanQAInvariantRegistry::Add(TEXT("COMMANDER_FIXED_STEP_DROP"),
			static_cast<int64>(DroppedSteps));
	}
	AuthorityState->FixedStepAccumulator = bWorkerMovement?UnclampedAccumulator:FMath::Min(UnclampedAccumulator, MaximumAccumulator);
    if (AuthorityState->bMovementStepPending)
    {
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (H&&!H->HasPendingGameplayCalculation(TEXT("MassMovement"))&&AuthorityState->MovementCalculation)
        {
            const auto Work=AuthorityState->MovementCalculation;const uint64 Bytes=1024+uint64(Work->Units.Num())*4096;
            H->SubmitGameplayCalculation(TEXT("MassMovement"),Work,Bytes,Bytes,AuthorityState->MovementApplication);
        }
    }
	while (!AuthorityState->bMovementStepPending && AuthorityState->FixedStepAccumulator >= GuLiCommanderMassPrivate::FixedStepSeconds)
	{
		TickAuthority(GuLiCommanderMassPrivate::FixedStepSeconds);
		AuthorityState->FixedStepAccumulator -= GuLiCommanderMassPrivate::FixedStepSeconds;
	}
	if (bImmediateMovement && !AuthorityState->bMovementStepPending
		&& !AuthorityState->ImmediateMoveIds.IsEmpty() && AuthorityState->FixedStepAccumulator > UE_DOUBLE_SMALL_NUMBER)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_ImmediateMovement);
		TickAuthority(static_cast<float>(AuthorityState->FixedStepAccumulator), true);
	}
	PublishMoveEndpointChanges();
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, PlanningMs, AuthorityState->PlanningBudget.Elapsed()*1000., ECsvCustomStatOp::Set);
#if !UE_BUILD_SHIPPING
	if (const int32* Index = ClockSoldierId > 0 ? AuthorityState->SoldierIndexById.Find(ClockSoldierId) : nullptr)
	{
		const auto& S = AuthorityState->Soldiers[*Index];
		const double Now = FPlatformTime::Seconds();
		if (S.CommandAcceptedAt > 0.0 && Now - S.CommandAcceptedAt < .25)
		{
			const FString Detail = FString::Printf(TEXT("soldier=%u delta=%.6f sim=%.6f remainder=%.6f lastBefore=%.6f lastAfter=%.6f stepCm=%.4f totalCm=%.4f speed=%.3f immediate=%d local=%d order=%u nav=%d route=%llu routeState=%d covered=%d ready=%.6f budgetPending=%d"),
				S.SoldierId.Value, DeltaTime, AuthorityState->SimulationSeconds, AuthorityState->FixedStepAccumulator,
				ClockLast, S.LastMovementUpdateSimulationSeconds, FVector::Dist2D(ClockStart, S.Location),
				FVector::Dist2D(S.CommandStartLocation, S.Location), S.Velocity.Size2D(),
				AuthorityState->ImmediateMoveIds.Contains(S.SoldierId.Value), S.bLocalSimulation, S.ActiveOrderId,
				int32(S.NavigationState), S.SharedRoute ? S.SharedRoute->Handle : 0,
				S.SharedRoute ? int32(S.SharedRoute->State) : -1,
				S.SharedRoute && S.SharedRoute->PolygonIndex.Contains(S.LastValidNavLocation.NodeRef),
				S.SharedRoute ? S.SharedRoute->ReadyAt : 0.0, S.bNavigationBudgetPending);
			GuLiMoveLatency::Record(TEXT("movement-clock"), S.MoveTrace, 1, *Detail, Now);
		}
	}
#endif
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, PositionChecks, MovePositionQueriesPerFrame-AuthorityState->PlanningBudget.Projections, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, PlanningPaths, MovePathQueriesPerFrame-AuthorityState->PlanningBudget.Paths, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, CommitMs, AuthorityState->CommitBudget.Elapsed()*1000., ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, CommittedMembers, FMath::Max(25,MoveCommitMembersPerFrame)-AuthorityState->CommitBudget.Members, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, RecoveryPending, AuthorityState->RecoveryWork.Num(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiCommanderAuthority, QueryOverruns, static_cast<int32>(AuthorityState->PlanningBudget.FrameQueryOverruns), ECsvCustomStatOp::Set);
	if (FPlatformTime::Seconds()>=AuthorityState->NextPlanningDiagnostic)
	{
		AuthorityState->NextPlanningDiagnostic=FPlatformTime::Seconds()+1.;
		for (auto It=AuthorityState->CommandGroups.CreateIterator(); It; ++It)
			if (FPlatformTime::Seconds()-It.Value().LastUsed>90.) It.RemoveCurrent();
		if (GuLiMoveLatency::IsEnabled())
		{
			for (int32 Category=0; Category<4; ++Category)
			{
				const auto& Used=AuthorityState->CategoryUsage[Category];
				UE_LOG(LogGuLiCommanderMass,Display,TEXT("MassBudget category=%d ms=%.3f positions=%d paths=%d slicesOverrun=%u"),Category,Used.UsedSeconds*1000.,Used.UsedProjections,Used.UsedPaths,AuthorityState->PlanningBudget.FrameSliceOverruns);
			}
			double OldestBackground=0.;
			for (const auto& Job : AuthorityState->MovePlanningJobs) if (Job && (Job->bAutomatic || Job->SharedBatchOrderId) && Job->Stage!=GuLiCommanderMassPrivate::EMovePlanningStage::Completed)
				OldestBackground=FMath::Max(OldestBackground,FPlatformTime::Seconds()-Job->LastProgressAt);
			UE_LOG(LogGuLiCommanderMass,Display,TEXT("MassBudget backgroundWaitMs=%.3f jobs=%d recovery=%d"),OldestBackground*1000.,AuthorityState->MovePlanningJobs.Num(),AuthorityState->RecoveryWork.Num());
		}
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
	AuthorityState->ImmediateMoveIds.Reset();
	AuthorityState->SoldierIndexById.Reset();
	AuthorityState->OrderFormations.Reset();
	AuthorityState->MovePlanningJobs.Reset();
	AuthorityState->RecoveryWork.Reset(); AuthorityState->RecoveryQueue.Empty(); AuthorityState->ReadyRecoveryQueue.Empty();
	AuthorityState->RecoveryDetectionSteps=0; AuthorityState->RecoveryQueries=0; AuthorityState->RecoveryFailures=0; AuthorityState->RecoveryRepairs=0;
	AuthorityState->RecoveryRebinds=0;
	AuthorityState->SpatialGrid.Reset();
	AuthorityState->ManualAvoidanceSpatialGrid.Reset();
	AuthorityState->AvoidanceGeometry.Reset();
	AuthorityState->AvoidanceGeometryPool.Reset();
	AuthorityState->NextAvoidanceGeometryVersion = 1;
	AuthorityState->SoftGeometryScratch.Reset();
	AuthorityState->ManualAvoidanceAgents.Reset();
	AuthorityState->CachedManualAvoidanceVelocities.Reset();
	AuthorityState->RequestGates.Reset();
	AuthorityState->CommandGroups.Reset();
	AuthorityState->MoveIntents.Reset();
	AuthorityState->SharedRoutes = {};
	AuthorityState->IntentCursor = 0;
	AuthorityState->SharedPriorityVisited.Reset();
	AuthorityState->SharedPriorityFrame = MAX_uint64;
	AuthorityState->LastPrioritySoldierId = 0;
	for (int32 I=0; I<2; ++I) { AuthorityState->SharedDiscoveryCursor[I]=0; AuthorityState->SharedDiscoveryFrame[I]=MAX_uint64; }
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

bool UGuLiBattleAuthoritySubsystem::FindSoldierEntity(FGuLiSoldierId Id,FMassEntityHandle& OutEntity) const
{
	const auto* Index=AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	if (!Index) return false;
	OutEntity=AuthorityState->Soldiers[*Index].Entity;
	return AuthorityState->MassEntitySubsystem.IsValid() && AuthorityState->MassEntitySubsystem->GetEntityManager().IsEntityValid(OutEntity);
}

void UGuLiBattleAuthoritySubsystem::SetMovePlanOrigin(const AGuLiBattlePlayerState& Owner,uint32 ExecutionId,uint32 CommandId)
{
	if (!AuthorityState) return;
	for (auto& Job : AuthorityState->MovePlanningJobs) if (Job && Job->PlayerState.Get()==&Owner && Job->Request.ClientCommandId==ExecutionId)
	{
		Job->SourceCommandId=CommandId;
		auto& Group=AuthorityState->CommandGroups.FindOrAdd({FObjectKey(&Owner),CommandId});
		Group.LastUsed=FPlatformTime::Seconds(); Job->SharedBatchOrderId=Group.Batch;
		auto Trace=GuLiMoveLatency::Context(&Owner,CommandId,Group.Batch); Trace.Execution=ExecutionId; Trace.Plan=Job->Handle.Value;
		GuLiMoveLatency::Record(TEXT("admission"),Trace);
		break;
	}
}

TSharedPtr<FGuLiSharedMoveIntent> UGuLiBattleAuthoritySubsystem::CreateSharedMoveIntent(const FVector& Target,uint32 RandomSeed)
{
	if (!AuthorityState || !IsAuthorityWorld()) return {};
	auto Intent=MakeShared<FGuLiSharedMoveIntent>();
	Intent->Click=Target; Intent->Goal=AuthorityState->SharedRoutes.Goal(Target);
	Intent->Random.Initialize(int32(HashCombine(RandomSeed,AuthorityState->AuthorityEpoch)));
	AuthorityState->MoveIntents.Add(Intent);
	return Intent;
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
		auto Trace=GuLiMoveLatency::Context(Job.PlayerState.Get(),Job.SourceCommandId,Job.SharedBatchOrderId);
		Trace.Epoch=Job.AuthorityEpoch; Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value;
		const FString Detail=FString::Printf(TEXT("result=%u accepted=%d failed=%d invalidated=%llu"),uint8(Job.Ack.Result),Job.TotalAccepted,Job.Debug.FailedMembers,Job.InvalidatedItems);
		GuLiMoveLatency::Record(TEXT("complete"),Trace,Job.FinishedIds.Num(),*Detail);
		for (int32 I=0; I<4; ++I)
		{
			const auto& P=Job.StageProfile[I];
			const FString Cost=FString::Printf(TEXT("phase=%d cpuMs=%.6f checks=%d paths=%d"),I,P.Seconds*1000.,P.Checks,P.Paths);
			GuLiMoveLatency::Record(TEXT("work-cost"),Trace,Job.FinishedIds.Num(),*Cost);
		}
		Job.Stage = EMovePlanningStage::Completed;
		return;
	}
	Job.Members.Reset(); Job.Ack.CohortResults.Reset();
	const auto& Source=Job.FullSelection.Cohorts[Job.NextCohort++];
	auto& Receipt=Job.Ack.CohortResults.AddDefaulted_GetRef();
	Receipt.CohortId=Source.CohortId; Receipt.MemberCount=uint8(Source.MemberIds.Num());
	for (int32 I=0; I<Source.MemberIds.Num(); ++I)
	{
		auto& M=Job.Members.AddDefaulted_GetRef(); M.SoldierId=Source.MemberIds[I]; M.CohortId=Source.CohortId; M.CohortMemberIndex=I;
		const auto* Index=AuthorityState->SoldierIndexById.Find(M.SoldierId.Value);
		const auto* Version=Job.FrozenTaskGenerations.Find(M.SoldierId.Value);
		if (!Index || !Version) continue;
		const auto& Soldier=AuthorityState->Soldiers[*Index]; M.TaskGeneration=*Version;
		M.bEligible=Soldier.CanSimulate() && Soldier.TaskGeneration==*Version && Soldier.Team==Job.Team
			&& (!Job.bAutomatic || Soldier.bAutomaticAdvance);
		if (M.bEligible) Receipt.EligibleMemberMask|=1u<<I;
	}
	Job.TotalEligible+=FPlatformMath::CountBits(Receipt.EligibleMemberMask);
	Job.bCommitReady=true; Job.Stage=EMovePlanningStage::ReadyToCommit;
}

void UGuLiBattleAuthoritySubsystem::TickMovePlanning(int32& RemainingProjectionBudget,int32& RemainingPathBudget,bool bManualFirst)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState) return;
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
	AuthorityState->MovePlanningJobs.RemoveAll([](const auto& J)
	{ return !J || (!J->bAutomatic && !J->PlayerState.IsValid()) || (J->bAutomatic && J->Stage==EMovePlanningStage::Completed && FPlatformTime::Seconds()-J->PlanningStartedAt>35.); });
	int32 Remaining=AuthorityState->MovePlanningJobs.Num(); auto& Cursor=AuthorityState->PlanningCursor[bManualFirst?0:1];
	while (Remaining-- && AuthorityState->PlanningBudget.CanWork() && !AuthorityState->MovePlanningJobs.IsEmpty())
	{
		Cursor%=AuthorityState->MovePlanningJobs.Num(); auto& Job=*AuthorityState->MovePlanningJobs[Cursor++];
		if (Job.bAutomatic==bManualFirst || Job.Stage!=EMovePlanningStage::PrepareBatch) continue;
		if (!Job.bFirstWorkRecorded)
		{
			Job.bFirstWorkRecorded=true; auto Trace=GuLiMoveLatency::Context(Job.PlayerState.Get(),Job.SourceCommandId,Job.SharedBatchOrderId);
			Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value; GuLiMoveLatency::Record(TEXT("first-work"),Trace);
		}
		PrepareNextMoveBatch(Job.Handle.Value);
	}
}

void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->MassEntitySubsystem.IsValid()) return;
	UWorld* World=GetWorld(); auto& A=*AuthorityState;
	auto& Manager=A.MassEntitySubsystem->GetMutableEntityManager();
	FGuLiNavigationWorkBudget::FScope Scope(A.CommitBudget);
	for (auto& Ptr : A.MovePlanningJobs)
	{
		if (!Ptr || !Ptr->bCommitReady || Ptr->Stage==EMovePlanningStage::Completed) continue;
		auto& Job=*Ptr;
		if (!A.CommitBudget.CanWork() || A.CommitBudget.Members<Job.Members.Num()) break;
		const auto* Owner=Job.PlayerState.Get(); const auto* Game=World->GetGameState<AGuLiBattleGameState>();
		if (!Game || Game->GetMatchEpoch()!=Job.AuthorityEpoch)
		{ CompleteMovePlanningJobWithSystemFailure(Job,EGuLiCommandAckResult::Cancelled); continue; }
		if (!Job.bAutomatic && (!Owner || !Owner->IsCommander() || Owner->GetTeam()!=Job.Team || !IsMovePlanningOwnerCurrent(Job,*Owner)))
		{ CompleteMovePlanningJobWithSystemFailure(Job,EGuLiCommandAckResult::Unauthorized); continue; }
		if (!Job.bActorsSubmitted && Owner && !Job.FullSelection.ActorIds.IsEmpty())
		{
			FGuLiMiningCommand C; C.RequestId=Job.Request.ClientCommandId; C.Type=EGuLiMiningOrderType::Move;
			C.Target=Job.Request.Target; C.SelectionRevision=Job.Request.SelectionRevision;
			Job.bActorsAccepted=World->GetSubsystem<UGuLiCommanderResourceAdapter>()->IssueMiningCommand(*Owner,Job.FullSelection.ActorIds,C);
			Job.bActorsSubmitted=true;
		}
		FGuLiBattleAuthorityState::FCommandGroup* Group=nullptr;
		if (Owner && Job.SourceCommandId) Group=&A.CommandGroups.FindOrAdd({FObjectKey(Owner),Job.SourceCommandId});
		if (Group) { Job.SharedBatchOrderId=Group->Batch; Group->LastUsed=FPlatformTime::Seconds(); }
		if (!Job.SharedBatchOrderId) Job.SharedBatchOrderId=AllocateNonZero(A.NextBatchOrderId);
		if (Group) Group->Batch=Job.SharedBatchOrderId;
		auto Intent=Job.MoveIntent;
		check(Intent);
		auto Trace=GuLiMoveLatency::Context(Owner,Job.SourceCommandId,Job.SharedBatchOrderId);
		Trace.Epoch=Job.AuthorityEpoch; Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value;
		int32 Accepted=0;
		for (auto Receipt : Job.Ack.CohortResults)
		{
			auto& Updated=Job.UpdatedSelection.Cohorts.AddDefaulted_GetRef(); Updated.CohortId=Receipt.CohortId; Updated.ActiveOrderId=Job.SharedBatchOrderId;
			for (auto& M : Job.Members)
			{
				const auto* Index=A.SoldierIndexById.Find(M.SoldierId.Value);
				auto* Soldier=Index ? &A.Soldiers[*Index] : nullptr;
				const bool Valid=M.bEligible && Soldier && Soldier->CanSimulate() && Soldier->Team==Job.Team && Soldier->TaskGeneration==M.TaskGeneration
					&& Manager.IsEntityValid(Soldier->Entity) && (!Job.bAutomatic || Soldier->bAutomaticAdvance);
				if (!Valid) { Job.Progress.Failed.Add(M.SoldierId); Job.Debug.FailedSoldierIds.Add(M.SoldierId); Job.FinishedIds.Add(M.SoldierId.Value); continue; }
				auto& S=*Soldier; S.CommandStartLocation=S.Location; S.ActiveOrderId=Job.SharedBatchOrderId;
				S.MoveIntent=Intent; S.RouteGoal=Intent->Goal; S.SharedRoute.Reset(); S.ActiveNavigation.Reset();
				S.bHasDockTarget=false; S.RouteCursor=0; S.DirectDock={};
				S.FinalDestination=FNavLocation(Intent->Click); S.bHasFinalDestination=true;
				S.bAutomaticAdvance=Job.bAutomatic; S.bAttackMoveHolding=false;
				S.NavigationState=EGuLiSoldierNavigationState::Normal; S.NavigationFailure=EGuLiSoldierNavigationFailure::None;
				S.PersonalPathPoints.Reset(); S.PersonalPathPointIndex=0; S.PersonalPathRetries=0; S.NextPersonalPathQuerySeconds=0;
				S.ConsecutiveSurfaceFailures=S.TotalSurfaceFailures=0;
				S.NoProgressSeconds=0; S.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
				S.LastProgressPathPointIndex=INDEX_NONE; S.bNavigationBudgetPending=false;
				S.bForceMovementUpdate=true; S.LastMovementUpdateSimulationSeconds=A.SimulationSeconds;
				if (!Job.bAutomatic && bAdvancingHybrid
					&& ImmediateMovement.GetValueOnGameThread() != 0
					&& !UGuLiHotspotOptions::Enabled(World, EGuLiHotspot::GameplayWorker))
				{
					// A command cannot consume time that elapsed before its admission.
					S.LastMovementUpdateSimulationSeconds += A.FixedStepAccumulator;
					A.ImmediateMoveIds.Add(S.SoldierId.Value);
					A.SharedPriorityVisited.Remove(S.SoldierId.Value);
				}
				S.CurrentNavigationWaypoint=Intent->Click; S.CommandAcceptedAt=FPlatformTime::Seconds();
				S.DirectionAppliedAt=S.FirstDisplacementAt=0; S.MoveTrace=Trace; ++S.StateRevision;
				auto& Order=Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(S.Entity);
				Order.ActiveOrderId=S.ActiveOrderId; Order.OrderRevision=S.StateRevision; Order.FormationTarget=Intent->Click; Order.bHasMoveTarget=true;
				auto& Move=Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity);
				Move.CreateNewAction(EMassMovementAction::Move,*World); Move.IntentAtGoal=EMassMovementAction::Stand;
				Move.Center=Intent->Click; Move.DesiredSpeed=FMassInt16Real(GetUnitMovementSpeed(S.Team,S.UnitTypeId));
				Manager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(S.Entity).Value=FVector::ZeroVector;
				RefreshSoldierNavigationState(S.SoldierId); Job.Progress.Committed.Add(S.SoldierId); Job.FinishedIds.Add(S.SoldierId.Value);
				Receipt.AcceptedMemberMask|=1u<<M.CohortMemberIndex; Updated.MemberIds.Add(S.SoldierId); ++Accepted;
			}
			Updated.AliveCount=uint8(Updated.MemberIds.Num());
			Receipt.Result=Receipt.AcceptedMemberMask==Receipt.EligibleMemberMask && Receipt.AcceptedMemberMask ? EGuLiCommandAckResult::Accepted
				: Receipt.AcceptedMemberMask ? EGuLiCommandAckResult::PartiallyAccepted : EGuLiCommandAckResult::PathFailed;
			Job.AggregateAck.CohortResults.Add(Receipt);
		}
		A.CommitBudget.Members-=Job.Members.Num(); A.bForceManualAvoidanceRefresh|=Accepted>0;
		Job.TotalAccepted+=Accepted; if (Accepted && !Job.FirstCommitAt) Job.FirstCommitAt=FPlatformTime::Seconds();
		Job.LastProgressAt=FPlatformTime::Seconds(); Job.Progress.BatchOrderId=Job.SharedBatchOrderId;
		Job.bCommitReady=false; Job.Stage=EMovePlanningStage::PrepareBatch;
		GuLiMoveLatency::Record(TEXT("commit"),Trace,Accepted);
		if (Job.NextCohort>=Job.FullSelection.Cohorts.Num()) PrepareNextMoveBatch(Job.Handle.Value);
	}
}

void UGuLiBattleAuthoritySubsystem::TickSharedNavigation(int32 Category, TConstArrayView<uint32> PriorityIds)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState) return;
	auto& A=*AuthorityState; auto& Budget=A.PlanningBudget;
	FGuLiNavigationWorkBudget::FScope Scope(Budget);
	auto* System=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	auto* Data=System ? GetCommanderNavigationData(*System) : nullptr;
	if (!Data || !Budget.CanWork()) return;
	if (Category==2)
	{
		{
			TGuardValue<double> MaintenanceSlice(Budget.LimitSeconds,FMath::Min(Budget.LimitSeconds,Budget.Elapsed()+.00005));
			A.SharedRoutes.Maintain(*Data,A.NavigationGeneration,Budget);
		}
		const auto* Definitions=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
		int32 Remaining=A.MoveIntents.Num();
		while (Remaining-- && !A.MoveIntents.IsEmpty() && Budget.CanWork())
		{
			A.IntentCursor%=A.MoveIntents.Num(); auto Intent=A.MoveIntents[A.IntentCursor].Pin();
			if (!Intent) { A.MoveIntents.RemoveAtSwap(A.IntentCursor); continue; }
			++A.IntentCursor;
            if (auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady())
            {
                while (Intent->MetadataCursor<Intent->UnassignedMembers.Num() && Budget.CanWork())
                {
                    const uint32 Id=Intent->UnassignedMembers[Intent->MetadataCursor++];
                    if (Intent->GridSizes.Contains(Id) && Intent->MovementLayers.Contains(Id)) continue;
                    if (const int32* Index=A.SoldierIndexById.Find(Id))
                        { Intent->GridSizes.Add(Id,GuLiGrid::FootprintFromDiameter(A.Soldiers[*Index].AvoidanceRadiusCentimeters*2.));
                          Intent->MovementLayers.Add(Id,A.Soldiers[*Index].MovementLayer); }
                }
                if (Intent->MetadataCursor==Intent->UnassignedMembers.Num()) Intent->GenerateGrid(*Grid,*Data,Budget);
                continue;
            }
			// Freeze the maximum scaled model footprint for the ENTIRE command, before generating any slots.
			while (!Intent->bMetadataReady && Intent->MetadataCursor<Intent->UnassignedMembers.Num() && Budget.CanWork())
			{
				const uint32 Id=Intent->UnassignedMembers[Intent->MetadataCursor++];
				const auto* Index=A.SoldierIndexById.Find(Id); if (!Index) continue;
				const auto& Soldier=A.Soldiers[*Index];
				Intent->MovementLayers.Add(Id,Soldier.MovementLayer);
				const float* Known=Intent->WidthByUnitType.Find(Soldier.UnitTypeId);
				float Width=Known ? *Known : 2.f*Soldier.AvoidanceRadiusCentimeters;
				if (!Known)
				{
					if (const auto* Definition=Definitions ? Definitions->FindSoldierDefinition(Soldier.UnitTypeId) : nullptr)
					{
						const FBox Bounds=Definition->GetModelBoundsCentimeters();
						if (Bounds.IsValid) Width=FMath::Max(Bounds.GetSize().X,Bounds.GetSize().Y);
					}
					Intent->WidthByUnitType.Add(Soldier.UnitTypeId,Width);
				}
				Intent->Pitch=FMath::Max(Intent->Pitch,FMath::Max(1.f,Width));
			}
			if (!Intent->bMetadataReady && Intent->MetadataCursor==Intent->UnassignedMembers.Num())
			{ Intent->WantedPoints=Intent->UnassignedMembers.Num(); Intent->bMetadataReady=true; }
			Intent->Generate(*Data,A.NavigationGeneration,Budget);
		}
		return;
	}
	if (A.SharedDiscoveryFrame[Category]!=GFrameCounter)
	{ A.SharedDiscoveryFrame[Category]=GFrameCounter; A.SharedDiscoveryRemaining[Category]=A.Soldiers.Num(); }
	if (A.SharedPriorityFrame!=GFrameCounter)
	{
		A.SharedPriorityFrame=GFrameCounter;
		A.SharedPriorityVisited.Reset();
	}
	// Continue after the last visited ID across slices and frames. Restarting at
	// index zero makes every 32-member slice revisit the same moving units and
	// can indefinitely starve a later command. Each priority unit is visited at
	// most once per world frame; a newly committed command clears that visit.
	int32 Work=0, PriorityRemaining=PriorityIds.Num();
	int32 PriorityCursor=Algo::UpperBound(PriorityIds,A.LastPrioritySoldierId);
	while ((PriorityRemaining>0 || A.SharedDiscoveryRemaining[Category]>0)
		&& !A.Soldiers.IsEmpty() && Budget.CanWork() && Work<32)
	{
		int32 SoldierIndex;
		if (PriorityRemaining>0)
		{
			PriorityCursor%=PriorityIds.Num();
			const uint32 Id=PriorityIds[PriorityCursor++];
			--PriorityRemaining;
			if (A.SharedPriorityVisited.Contains(Id)) continue;
			A.SharedPriorityVisited.Add(Id);
			A.LastPrioritySoldierId=Id;
			const int32* Found=A.SoldierIndexById.Find(Id);
			if (!Found) continue;
			SoldierIndex=*Found;
		}
		else
		{
			auto& Cursor=A.SharedDiscoveryCursor[Category]; Cursor%=A.Soldiers.Num();
			SoldierIndex=Cursor++; --A.SharedDiscoveryRemaining[Category];
		}
		++Work;
		auto& S=A.Soldiers[SoldierIndex];
		if (!S.MoveIntent || !S.ActiveOrderId || !S.CanSimulate() || S.bAutomaticAdvance!=(Category==1)
			|| !Data->IsNodeRefValid(S.LastValidNavLocation.NodeRef)) continue;
		if (S.bAttackMoveHolding) continue;
        if (S.bHasDockTarget && S.NavigationState!=EGuLiSoldierNavigationState::PersonalPathRecovery
            && !Data->IsNodeRefValid(S.FinalDestination.NodeRef))
        {
            if (!Budget.TakeProjection()) { S.bNavigationBudgetPending=true;continue; }
            FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
            FNavLocation Dock;
            const FVector Position=S.FinalDestination.Location;
            if (!Data->ProjectPoint(Position,Dock,FVector(.1,.1,5000),Data->GetDefaultQueryFilter(),this)
                || !Dock.NodeRef || FVector::DistSquared2D(Position,Dock.Location)>.01)
            { BeginPersonalPathRecovery(S);continue; }
            Dock.Location.X=Position.X;Dock.Location.Y=Position.Y;
            S.FinalDestination=Dock;S.MoveIntent->AssignedPoints.Add(S.SoldierId.Value,Dock);S.DirectDock={};
        }
		if (S.NavigationState==EGuLiSoldierNavigationState::PersonalPathRecovery)
		{
			if (!S.bHasDockTarget) continue;
			if (S.ActiveNavigation && S.ActiveNavigation->Data.Get()==Data
				&& S.ActiveNavigation->Path->IsUpToDate()) continue;
			S.ActiveNavigation.Reset(); S.PersonalPathPoints.Reset();
			if (A.SimulationSeconds<S.NextPersonalPathQuerySeconds) continue;
			if (!Budget.TakePath()) { S.bNavigationBudgetPending=true; continue; }
			bool bCompletePath;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_RecoveryFindPath);
				FGuLiNavigationWorkBudget::FQueryScope QueryTimer(Budget);
				bCompletePath=BuildCompletePathQuiet(*System,*Data,S.LastValidNavLocation,S.FinalDestination.Location,
					S.PersonalPathPoints,&S.ActiveNavigation,A.NavigationGeneration);
			}
			++A.PersonalPathQueries; ++S.PersonalPathRetries;
			S.NextPersonalPathQuerySeconds=A.SimulationSeconds+PersonalRecoveryRetrySeconds;
			S.bNavigationBudgetPending=false;
			if (bCompletePath && S.PersonalPathPoints.Num()>=2)
			{
				S.PersonalPathPointIndex=1; S.LastProgressPathPointIndex=INDEX_NONE;
				S.NoProgressSeconds=0; S.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
				S.ConsecutiveSurfaceFailures=0; S.NavigationFailure=EGuLiSoldierNavigationFailure::None;
				S.bForceMovementUpdate=true;
			}
			else
			{ S.ActiveNavigation.Reset(); S.PersonalPathPoints.Reset(); S.NavigationFailure=EGuLiSoldierNavigationFailure::PersonalPathFailed; }
			continue;
		}
		// Nearby is not the same as reachable: keep the shared corridor until the exact
		// current foot position has a clear CommanderSoldier ray to its fixed slot.
		if (S.bHasDockTarget && S.NavigationState==EGuLiSoldierNavigationState::Normal
			&& S.MoveIntent->InDockingArea(S.Location)
			&& S.DirectDock.Update(*Data,A.NavigationGeneration,S.LastValidNavLocation,S.FinalDestination,Budget,this))
		{
			if (S.DirectDock.bClear) continue;
			if (S.SharedRoute && S.SharedRoute->State==FGuLiSharedMoveRoute::EState::Ready
				&& S.SharedRoute->Navigation.Path->IsUpToDate()
				&& !S.SharedRoute->Navigation.Nodes.IsEmpty()
				&& S.SharedRoute->Navigation.Nodes.Last()==S.LastValidNavLocation.NodeRef)
			{ BeginPersonalPathRecovery(S); continue; }
		}
		if (S.bHasDockTarget && S.SharedRoute && S.SharedRoute->State==FGuLiSharedMoveRoute::EState::Unavailable)
		{ BeginPersonalPathRecovery(S); continue; }
		if (S.SharedRoute && S.SharedRoute->Goal==S.RouteGoal && S.SharedRoute->State==FGuLiSharedMoveRoute::EState::Ready
			&& S.SharedRoute->Navigation.Data.Get()==Data && S.SharedRoute->Navigation.Path->IsUpToDate()
			&& S.SharedRoute->PolygonIndex.Contains(S.LastValidNavLocation.NodeRef)) { S.SharedRoute->LastUsed=FPlatformTime::Seconds(); continue; }
		S.RouteGoal=S.MoveIntent->Goal;
		S.SharedRoute=A.SharedRoutes.Request(S.RouteGoal,S.LastValidNavLocation,Category==1,*Data,A.NavigationGeneration);
	}
	A.SharedRoutes.Tick(Category==1,*System,*Data,A.NavigationGeneration,Budget);
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
	if (Index && (!Soldier->ActiveOrderId || !Soldier->CanAct()))
	{
		auto& S=AuthorityState->Soldiers[*Index]; S.MoveIntent.Reset(); S.RouteGoal.Reset(); S.SharedRoute.Reset();
		S.DirectDock={}; S.ActiveNavigation.Reset(); S.PersonalPathPoints.Reset();
		S.NextPersonalPathQuerySeconds=0; S.bNavigationBudgetPending=false;
	}
	if (!Soldier || Soldier->bEngineering || !Soldier->CanAct() || !Soldier->ActiveOrderId || !Soldier->bHasFinalDestination)
	{
		if (AuthorityState->ActiveEndpoints.Remove(Id.Value))
		{ AuthorityState->DirtyEndpointIds.Remove(Id.Value); AuthorityState->RemovedEndpointIds.Add(Id.Value); }
		return;
	}
	const FVector DisplayTarget=Soldier->MoveIntent ? Soldier->MoveIntent->Click : Soldier->FinalDestination.Location;
	const auto* Previous = AuthorityState->ActiveEndpoints.Find(Id.Value);
	if (Previous && Previous->ActiveOrderId == Soldier->ActiveOrderId && Previous->Team == Soldier->Team
		&& Previous->CommandStart.Equals(Soldier->CommandStartLocation,.01)
		&& Previous->FinalDestination.Equals(DisplayTarget,.01)) return;
	FGuLiMoveEndpointSnapshot E; E.SoldierId = Id; E.Team = Soldier->Team;
	E.ActiveOrderId = Soldier->ActiveOrderId; E.Revision = Soldier->StateRevision;
	E.CommandStart = Soldier->CommandStartLocation; E.FinalDestination = DisplayTarget;
	AuthorityState->ActiveEndpoints.Add(Id.Value,E);
	AuthorityState->DirtyEndpointIds.Add(Id.Value); AuthorityState->RemovedEndpointIds.Remove(Id.Value);
}

void UGuLiBattleAuthoritySubsystem::PublishMoveEndpointChanges()
{
	if (!AuthorityState || (AuthorityState->DirtyEndpointIds.IsEmpty() && AuthorityState->RemovedEndpointIds.IsEmpty())) return;
	FGuLiAuthorityMoveDelta Committed;
 Committed.Epoch = AuthorityState->AuthorityEpoch;
 auto& Delta = Committed.Endpoints;
 for (uint32 Id : AuthorityState->DirtyEndpointIds)
  if (const auto* E = AuthorityState->ActiveEndpoints.Find(Id)) Delta.Upserts.Add(*E);
 for (uint32 Id : AuthorityState->RemovedEndpointIds) Delta.Removed.Add(FGuLiSoldierId(Id));
 for (uint32 Id : AuthorityState->DirtyEndpointIds)
 { FGuLiSoldierStateItem State; if (BuildSoldierState(FGuLiSoldierId(Id),State)) Committed.States.Add(State); }
 for (uint32 Id : AuthorityState->RemovedEndpointIds)
 { FGuLiSoldierStateItem State; if (BuildSoldierState(FGuLiSoldierId(Id),State)) Committed.States.Add(State); }
 AuthorityState->DirtyEndpointIds.Reset(); AuthorityState->RemovedEndpointIds.Reset();
 // Publish the authoritative discrete state BEFORE endpoint consumers prepare paired v19 records.
 OnAuthorityMoveDelta.Broadcast(Committed);
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
	const auto Snapshot=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot();
	auto& Cache=AuthorityState->Steering.FindOrAdd(Id.Value);
	const float Tolerance=FMath::Max(10.f,Soldier.AvoidanceRadiusCentimeters*.5f);
	if (Cache.Order!=Soldier.ActiveOrderId || Cache.Path!=PathRevision || !Snapshot->IsRegionCurrent(Cache.Region)
		|| Cache.Origin.NodeRef!=Soldier.LastValidNavLocation.NodeRef
		|| !Cache.RequestedLane.Equals(Lane,Tolerance) || !Cache.RequestedSlot.Equals(Slot,Tolerance))
	{
		Cache={}; Cache.Order=Soldier.ActiveOrderId; Cache.Path=PathRevision; Cache.Nav=AuthorityState->NavigationGeneration;
		Cache.Origin=Soldier.LastValidNavLocation; Cache.RequestedLane=Lane; Cache.RequestedSlot=Slot;
		Cache.Region=Snapshot->CaptureRegion(Cache.Origin.Location,Lane,Soldier.AvoidanceRadiusCentimeters);
		const auto SlotRegion=Snapshot->CaptureRegion(Cache.Origin.Location,Slot,Soldier.AvoidanceRadiusCentimeters);
		Cache.Region.Cells.Append(SlotRegion.Cells); Cache.Region.bComplete &= SlotRegion.bComplete;
	}
	if ((Cache.Stage<4 || Cache.Nav!=AuthorityState->NavigationGeneration) && !AuthorityState->SteeringQueued.Contains(Id.Value))
	{ AuthorityState->SteeringQueued.Add(Id.Value); AuthorityState->SteeringQueue.Enqueue(Id.Value); }
	if (Cache.Stage<4 || Cache.Nav!=AuthorityState->NavigationGeneration || !Cache.bLaneValid) return false;
	OutLane=Cache.Lane.Location; OutSlot=Cache.bSlotValid ? Cache.Slot.Location : Soldier.Location;
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
	const auto Snapshot=Registry->GetSnapshot(); uint32 Id=0;
	for (int32 Visit=0; Visit<128 && AuthorityState->PlanningBudget.CanWork() && AuthorityState->PlanningBudget.Projections>0
		&& AuthorityState->SteeringQueue.Dequeue(Id); ++Visit)
	{
		AuthorityState->SteeringQueued.Remove(Id);
		auto* Cache=AuthorityState->Steering.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
		if (!Cache || !Index) { AuthorityState->Steering.Remove(Id); continue; }
		const auto& Soldier=AuthorityState->Soldiers[*Index];
		if (!Soldier.CanAct() || Cache->Order!=Soldier.ActiveOrderId || !Snapshot->IsRegionCurrent(Cache->Region)
			|| Cache->Origin.NodeRef!=Soldier.LastValidNavLocation.NodeRef)
		{ AuthorityState->Steering.Remove(Id); continue; }
		if (Cache->Nav!=AuthorityState->NavigationGeneration)
		{
			using Check=FGuLiNavigationDependency::ECheck;
			const auto Lane=Cache->LaneConnection.Check(*Data,AuthorityState->NavigationGeneration,AuthorityState->PlanningBudget);
			const auto Slot=Cache->SlotConnection.Check(*Data,AuthorityState->NavigationGeneration,AuthorityState->PlanningBudget);
			if (Lane==Check::Pending || Slot==Check::Pending)
			{ AuthorityState->SteeringQueued.Add(Id); AuthorityState->SteeringQueue.Enqueue(Id); continue; }
			if (Lane==Check::Invalid || Slot==Check::Invalid) { Cache->Stage=0; Cache->bLaneValid=Cache->bSlotValid=false; }
			Cache->Nav=AuthorityState->NavigationGeneration;
		}
		if (Cache->Stage>=4) continue;
		if (!AuthorityState->PlanningBudget.TakeProjection()) { AuthorityState->SteeringQueued.Add(Id); AuthorityState->SteeringQueue.Enqueue(Id); break; }
		FGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget);
		const bool bLane=Cache->Stage<2;
		auto& Projected=bLane ? Cache->Lane : Cache->Slot;
		const auto& Requested=bLane ? Cache->RequestedLane : Cache->RequestedSlot;
		bool& Valid=bLane ? Cache->bLaneValid : Cache->bSlotValid;
		if ((Cache->Stage&1)==0)
			Valid=ProjectPointToCommanderNavigation(*Nav,*Data,Requested,FVector(10,10,500),Projected)
				&& FVector::DistSquared2D(Requested,Projected.Location)<=100.
				&& Snapshot->IsSegmentClear(Projected.Location,Projected.Location,Soldier.AvoidanceRadiusCentimeters,false,Soldier.MovementLayer);
		else if (Valid)
			Valid=HasDirectSurfaceConnection(*Data,Cache->Origin,Projected.Location,bLane ? &Cache->LaneConnection : &Cache->SlotConnection,AuthorityState->NavigationGeneration)
				&& Snapshot->IsSegmentClear(Cache->Origin.Location,Projected.Location,Soldier.AvoidanceRadiusCentimeters,true,Soldier.MovementLayer);
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
	// are invalidated; the 10 Hz detector queues invalid foot coordinates for bounded surface repair.
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

void UGuLiBattleAuthoritySubsystem::DetectNavigationRepairs()
{
	using namespace GuLiCommanderMassPrivate;
	++AuthorityState->RecoveryDetectionSteps;
	auto* World = GetWorld();
	const auto* State = World->GetGameState<AGuLiBattleGameState>();
	auto* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const auto* Data = System ? GetCommanderNavigationData(*System) : nullptr;
	if (!State || !Data) return;

	// Cheap validity detection for every present member on EVERY fixed step, independent of
	// render rate, selection, movement/attack hold and the amortized navigation-query budget.
	for (const auto& Soldier : AuthorityState->Soldiers)
	{
		if (!Soldier.CanSimulate() || !Soldier.IsPresent() || Soldier.Location.ContainsNaN()
			|| Data->IsNodeRefValid(Soldier.LastValidNavLocation.NodeRef)
			|| AuthorityState->RecoveryWork.Contains(Soldier.SoldierId.Value)) continue;
		FNavigationRecoveryWork Work;
		Work.Id = Soldier.SoldierId;
		Work.TaskVersion = Soldier.TaskGeneration;
		Work.Order = Soldier.ActiveOrderId;
		Work.NavigationData = Data;
		Work.Epoch = State->GetMatchEpoch();
		Work.Source = Soldier.Location;
		AuthorityState->RecoveryWork.Add(Work.Id.Value, Work);
		AuthorityState->RecoveryQueue.Enqueue(Work.Id.Value);
	}
}

void UGuLiBattleAuthoritySubsystem::TickNavigationRepairs()
{
	using namespace GuLiCommanderMassPrivate;
	auto& A = *AuthorityState;
	if (A.RecoveryQueue.IsEmpty()) return;
	auto& Budget = A.PlanningBudget;
	FGuLiNavigationWorkBudget::FScope Scope(Budget);
	auto* World = GetWorld();
	const auto* State = World->GetGameState<AGuLiBattleGameState>();
	auto* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	auto* Data = System ? GetCommanderNavigationData(*System) : nullptr;
	auto* Registry = World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!State || !Data || !Registry) return;
	const auto Obstacles = Registry->GetSnapshot();
	uint32 Id = 0;
	const int32 Visits = FMath::Min(32, A.RecoveryWork.Num());
	for (int32 Visit = 0; Visit < Visits && Budget.CanWork() && A.RecoveryQueue.Dequeue(Id); ++Visit)
	{
		auto* Work = A.RecoveryWork.Find(Id);
		const auto* Index = A.SoldierIndexById.Find(Id);
		if (!Work || !Index) { A.RecoveryWork.Remove(Id); continue; }
		const auto& Soldier = A.Soldiers[*Index];
		if (!Soldier.CanSimulate() || !Soldier.IsPresent() || Work->TaskVersion != Soldier.TaskGeneration
			|| Work->Order != Soldier.ActiveOrderId || Work->Epoch != State->GetMatchEpoch()
			|| Work->NavigationData.Get() != Data || !Work->Source.Equals(Soldier.Location, .01)
			|| Data->IsNodeRefValid(Soldier.LastValidNavLocation.NodeRef))
		{ A.RecoveryWork.Remove(Id); continue; }
		// One candidate per member per world frame; queue rotation shares the budget fairly.
		if (Work->LastQueryFrame == GFrameCounter || !Budget.TakeProjection())
		{ A.RecoveryQueue.Enqueue(Id); continue; }
		Work->LastQueryFrame = GFrameCounter;
		FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
		++A.RecoveryQueries;
		FVector Probe = GuLiUnitMovement::GroundPosition(Work->Source,Soldier.AltitudeCentimeters);
		float Extent = .1f;
		if (Work->Candidate == 1) Extent = MaximumNavigationRepairDistanceCentimeters;
		else if (Work->Candidate >= 2)
		{
			const int32 Direction = (Work->Candidate - 2) % 8;
			const int32 Ring = (Work->Candidate - 2) / 8 + 1;
			const double Angle = UE_DOUBLE_TWO_PI * Direction / 8.;
			Probe += FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (NavigationRepairRingStepCentimeters * Ring);
			Extent = NavigationRepairRingStepCentimeters * .5f;
		}
		const bool bProjected = ProjectPointToCommanderNavigation(*System, *Data, Probe,
			FVector(Extent, Extent, MaximumSurfaceStepZCentimeters), Work->Target);
		// No body moves when a rebuilt tile only changes the node handle. A sphere
		// around a valid foot point can overlap a slope even while the unit stands still.
		Work->bNodeOnly = bProjected && CanRebindNavigationAtCurrentLocation(*Obstacles,Soldier,Work->Target.Location);
		const bool bValid = bProjected && (Work->bNodeOnly
			|| IsNavigationRepairSegmentClear(*World, *Obstacles, Soldier, Work->Target.Location));
		if (bValid) A.ReadyRecoveryQueue.Enqueue(Id);
		else if (++Work->Candidate < NavigationRepairCandidates) A.RecoveryQueue.Enqueue(Id);
		else
		{
			// No timeout or terminal task failure: the next 10 Hz scan may retry after geometry changes.
			++A.RecoveryFailures;
			A.RecoveryWork.Remove(Id);
		}
	}
}

void UGuLiBattleAuthoritySubsystem::CommitReadyNavigationRepairs()
{
	using namespace GuLiCommanderMassPrivate;
	auto& A = *AuthorityState;
	if (A.ReadyRecoveryQueue.IsEmpty()) return;
	FGuLiNavigationWorkBudget::FScope Scope(A.CommitBudget);
	auto* World = GetWorld();
	const auto* State = World->GetGameState<AGuLiBattleGameState>();
	auto* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	auto* Data = System ? GetCommanderNavigationData(*System) : nullptr;
	auto* Registry = World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!State || !Data || !Registry || !A.MassEntitySubsystem.IsValid()) return;
	const auto Obstacles = Registry->GetSnapshot();
	auto& Manager = A.MassEntitySubsystem->GetMutableEntityManager();
	uint32 Id = 0;
	while (A.CommitBudget.CanWork() && A.CommitBudget.Members > 0 && A.ReadyRecoveryQueue.Dequeue(Id))
	{
		const auto* Work = A.RecoveryWork.Find(Id);
		const auto* Index = A.SoldierIndexById.Find(Id);
		if (!Work || !Index) { A.RecoveryWork.Remove(Id); continue; }
		auto& Soldier = A.Soldiers[*Index];
		if (!Soldier.CanSimulate() || !Soldier.IsPresent() || !Manager.IsEntityValid(Soldier.Entity)
			|| Work->TaskVersion != Soldier.TaskGeneration || Work->Order != Soldier.ActiveOrderId
			|| Work->Epoch != State->GetMatchEpoch() || Work->NavigationData.Get() != Data
			|| !Work->Source.Equals(Soldier.Location, .01) || !Data->IsNodeRefValid(Work->Target.NodeRef)
			|| Data->IsNodeRefValid(Soldier.LastValidNavLocation.NodeRef))
		{ A.RecoveryWork.Remove(Id); continue; }
		--A.CommitBudget.Members;
		// Physics/obstacles can change between projection and this fixed-step commit.
		if (!(Work->bNodeOnly ? CanRebindNavigationAtCurrentLocation(*Obstacles,Soldier,Work->Target.Location)
			: IsNavigationRepairSegmentClear(*World, *Obstacles, Soldier, Work->Target.Location)))
		{
			if (++A.RecoveryWork.FindChecked(Id).Candidate < NavigationRepairCandidates) A.RecoveryQueue.Enqueue(Id);
			else { ++A.RecoveryFailures; A.RecoveryWork.Remove(Id); }
			continue;
		}

		const FVector Previous = Soldier.Location;
		if (Work->bNodeOnly) ++A.RecoveryRebinds;
		else Soldier.Location = Soldier.WorldFromGround(Work->Target.Location);
		Soldier.LastValidNavLocation = FNavLocation(Soldier.GroundLocation(),Work->Target.NodeRef);
		Soldier.Velocity = FVector::ZeroVector;
		Soldier.SharedRoute.Reset(); Soldier.RouteCursor = 0;
		Soldier.DirectDock = {}; Soldier.ActiveNavigation.Reset(); Soldier.PersonalPathPoints.Reset();
		Soldier.NextPersonalPathQuerySeconds = 0;
		Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
		Soldier.ConsecutiveSurfaceFailures = 0;
		Soldier.FailureSimulationSeconds = 0;
		Soldier.bNavigationBudgetPending = false;
		Soldier.NoProgressSeconds = 0;
		Soldier.BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
		Soldier.bForceMovementUpdate = Soldier.ActiveOrderId != 0;
		Soldier.CurrentNavigationWaypoint = Soldier.GroundLocation();
		Soldier.LastMovementUpdateSimulationSeconds = A.SimulationSeconds;
		if (!Previous.Equals(Soldier.Location, .01))
		{
			// Reuse the existing authoritative displacement barrier so stale poses cannot undo repair.
			Soldier.DisplacementFrameFloor = A.NextPoseFrameSequence;
			Soldier.DisplacementLocation = Soldier.Location;
			Soldier.DisplacementSimulationTime = A.SimulationSeconds;
		}
		++Soldier.StateRevision;
		Manager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity).SetTransform(
			FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location));
		Manager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		Manager.GetFragmentDataChecked<FMassForceFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		Manager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity).OrderRevision = Soldier.StateRevision;
		auto& Move = Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
		Move.Center = Soldier.Location; Move.DesiredSpeed = FMassInt16Real(0.f);
		// The intent, fixed dock, automatic/manual mode, command start and task version all survive.
		RefreshSoldierNavigationState(Soldier.SoldierId);
		A.bForceManualAvoidanceRefresh = true;
		++A.RecoveryRepairs;
		A.RecoveryWork.Remove(Id);
	}
}
// 常规步：配置、移动、导航修复、战斗。即时步复用相同导航/避让/碰撞约束，
// 只处理正在执行的手动移动，不推进战斗时钟，也不重复消费已积分的移动时间。
// 实际位置由这里积分并写回 Fragment；导航策略函数负责判定，不直接改世界状态。
bool UGuLiBattleAuthoritySubsystem::EnsureAuthorityArchetypes()
{
        using namespace GuLiCommanderMassPrivate;
        auto& State=*AuthorityState; if (State.RuntimeTuningEvenBaseArchetype.IsValid()) return true;
        auto* Mass=State.MassEntitySubsystem.Get(); if (!Mass) return false;
        auto& Manager=Mass->GetMutableEntityManager();
        TArray<const UScriptStruct*> Types={FTransformFragment::StaticStruct(),FAgentRadiusFragment::StaticStruct(),FMassVelocityFragment::StaticStruct(),
            FMassForceFragment::StaticStruct(),FMassMoveTargetFragment::StaticStruct(),FMassNavigationObstacleGridCellLocationFragment::StaticStruct(),
            FGuLiMassIdentityFragment::StaticStruct(),FGuLiCommanderStateTreeFragment::StaticStruct(),FGuLiMassHealthFragment::StaticStruct(),
            FGuLiMassSoldierStatsFragment::StaticStruct(),FGuLiMassOrderFragment::StaticStruct(),FGuLiMassSlotTargetFragment::StaticStruct(),
            FGuLiMassAvoidanceOutputFragment::StaticStruct(),FGuLiMassAvoidanceStateFragment::StaticStruct(),FGuLiMassAvoidanceParticipantTag::StaticStruct(),
            FGuLiServerAuthorityMassTag::StaticStruct(),FGuLiMassRuntimeTuningEvenTag::StaticStruct()};
        FMassArchetypeCreationParams Params; Params.DebugName=TEXT("GuLiHybridGrantedSoldiers");
        State.RuntimeTuningEvenBaseArchetype=Manager.CreateArchetype(Types,Params);
        Types.RemoveSingle(FGuLiMassRuntimeTuningEvenTag::StaticStruct()); Types.Add(FGuLiMassRuntimeTuningOddTag::StaticStruct());
        Params.DebugName=TEXT("GuLiHybridGrantedSoldiersOdd"); State.RuntimeTuningOddBaseArchetype=Manager.CreateArchetype(Types,Params);
        State.AuthorityArchetype=State.RuntimeTuningEvenBaseArchetype;
        State.bPopulationSpawned=State.RuntimeTuningEvenBaseArchetype.IsValid() && State.RuntimeTuningOddBaseArchetype.IsValid();
        if (auto* GI=GetWorld()->GetGameInstance())
            if (const auto* Session=GI->GetSubsystem<UGuLiHybridSessionSubsystem>()) State.NextPlanId=(uint64(GetTypeHash(Session->GetLocalPlayerId())|1u)<<32)|1u;
        bSoldierSimulationEnabled=State.bPopulationSpawned; return State.bPopulationSpawned;
    }

void UGuLiBattleAuthoritySubsystem::TickAuthority(const float FixedDeltaSeconds, const bool bImmediateCommands)
{
	CSV_SCOPED_TIMING_STAT(GuLiCommanderAuthority, FixedStep);
	using namespace GuLiCommanderMassPrivate;
	check(AuthorityState);
    const bool bWorker=!bImmediateCommands && UGuLiHotspotOptions::Enabled(GetWorld(),EGuLiHotspot::GameplayWorker);
    std::shared_ptr<FMovementCalculation> Work;
#if !UE_BUILD_SHIPPING
	const double StepStartedAt = FPlatformTime::Seconds();
#endif
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	UWorld* World = GetWorld();
	if (!MassSubsystem || !World)
	{
		return;
	}

    for (const auto& S:AuthorityState->Soldiers) if (S.bEngineering) SyncEngineeringMass(S.SoldierId);

	if (!bImmediateCommands)
	{
		ResetRogueMovementForMatch();
		ApplyPendingMovementSpeed();
		CommitCombatProfiles();
	}
	float MaximumUnitMovementSpeed=MovementSpeedCentimetersPerSecond;
	for (const auto& Unit : AuthorityState->Soldiers) MaximumUnitMovementSpeed=FMath::Max(MaximumUnitMovementSpeed,GetUnitMovementSpeed(Unit.Team,Unit.UnitTypeId));
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	const ARecastNavMesh* CommanderRecast = Cast<ARecastNavMesh>(CommanderNavigationData);
	// UE 5.7 exposes the authored slope as a reflected setting, without a native getter.
	static const FFloatProperty* MaximumSlopeProperty =
		FindFProperty<FFloatProperty>(ARecastNavMesh::StaticClass(), TEXT("AgentMaxSlope"));
	const float MaximumSurfaceSlopeDegrees = CommanderRecast && MaximumSlopeProperty
		? MaximumSlopeProperty->GetPropertyValue_InContainer(CommanderRecast) : 0.0f;
	if (!bImmediateCommands)
	{
		AuthorityState->SimulationSeconds += FixedDeltaSeconds;
		++AuthorityState->ServerSimTick;
	}
	const double MovementSimulationSeconds = AuthorityState->SimulationSeconds
		+ (bImmediateCommands ? AuthorityState->FixedStepAccumulator : 0.0);

	auto SetTerminalNavigationState = [this, World, &EntityManager, MovementSimulationSeconds](
		FSoldierRuntime& Soldier,
		const EGuLiSoldierNavigationState TerminalState,
		const EGuLiSoldierNavigationFailure Failure)
	{
		const uint32 FinishedOrderId = Soldier.ActiveOrderId;
		if (TerminalState == EGuLiSoldierNavigationState::Arrived)
		{

			Soldier.LastCompletedOrderId = FinishedOrderId;
			Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
			Soldier.FailureSimulationSeconds = 0.0;
		}
		else
		{
			Soldier.Location = Soldier.WorldFromGround(Soldier.LastValidNavLocation.Location);
			Soldier.LastFailedOrderId = FinishedOrderId;
			Soldier.NavigationFailure = Failure;
			Soldier.FailureSimulationSeconds = MovementSimulationSeconds;
		}
		Soldier.NavigationState = TerminalState;
		Soldier.ActiveOrderId = 0u;
		Soldier.Velocity = FVector::ZeroVector;
		Soldier.LastMovementUpdateSimulationSeconds = MovementSimulationSeconds;
		Soldier.bForceMovementUpdate = false;
		Soldier.CurrentNavigationWaypoint = Soldier.GroundLocation();
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
		Soldier.DirectDock = {};
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
	if (!bWorker && !bImmediateCommands && bNeedsGroundMechYieldUpdate)
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
				Candidate.bCanYield = Soldier.MovementLayer==EGuLiUnitMovementLayer::Ground && Soldier.IsPresent() && Soldier.CanSimulate()
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
			const bool bEligibleIdle = Soldier.IsPresent() && Soldier.CanSimulate()
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
			const float MaximumSpeed = GetUnitMovementSpeed(Soldier.Team,Soldier.UnitTypeId);
			const float Speed = FMath::Min(MaximumSpeed, Distance / FixedDeltaSeconds);
			GroundMechYieldVelocities[SoldierIndex] = ToTarget / Distance * Speed;
		}
	}
	if (!bWorker && !bImmediateCommands) AuthorityState->GroundMechYieldingSoldiers = GroundMechYieldingSoldiers;

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
        if (Soldier.HybridKey.IsValid() && !Soldier.bLocalSimulation) continue;
		if (bImmediateCommands && (!AuthorityState->ImmediateMoveIds.Contains(Soldier.SoldierId.Value)
			|| Soldier.bAutomaticAdvance)) continue;
		const bool bHasMovingOrder = Soldier.CanSimulate()
			&& !Soldier.bAttackMoveHolding
			&& Soldier.ActiveOrderId != 0u
			&& (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
				|| Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
				|| Soldier.NavigationState == EGuLiSoldierNavigationState::PersonalPathRecovery);
		if (!bHasMovingOrder)
		{
			Soldier.LastMovementUpdateSimulationSeconds = MovementSimulationSeconds;
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

		const float MoveDelta = bAdvancingHybrid
			? GuLiCommanderNavigationPolicy::ResolveUnconsumedMovementDeltaSeconds(
				MovementSimulationSeconds, Soldier.LastMovementUpdateSimulationSeconds)
			: GuLiCommanderNavigationPolicy::ResolveMovementUpdateDeltaSeconds(
				MovementSimulationSeconds, Soldier.LastMovementUpdateSimulationSeconds, FixedDeltaSeconds);
		if (MoveDelta <= UE_SMALL_NUMBER) continue;
		bRunsMovementUpdate[SoldierIndex] = true;
		bForcedMovementUpdate[SoldierIndex] = bWasForced;
		MovementUpdateDeltaSeconds[SoldierIndex] = MoveDelta;
		Soldier.LastMovementUpdateSimulationSeconds = MovementSimulationSeconds;
		Soldier.bForceMovementUpdate = bImmediateCommands;
	}

	for (int32 I=0; I<AuthorityState->Soldiers.Num(); ++I)
	{
		auto& S=AuthorityState->Soldiers[I];
		if (!S.MoveIntent || !S.ActiveOrderId || !S.CanSimulate() || !bRunsMovementUpdate[I]) continue;
		auto& Intent=*S.MoveIntent;
		if (!S.bHasDockTarget)
			if (const FNavLocation* Dock=Intent.AssignedPoints.Find(S.SoldierId.Value))
			{
				S.bHasDockTarget=true; S.FinalDestination=*Dock;
				S.DirectDock={};
				S.NoProgressSeconds=0; S.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
			}
		if (S.bHasDockTarget && FVector::DistSquared2D(S.Location,S.FinalDestination.Location)<=FMath::Square(FMath::Max(20.f,S.ArrivalAcceptance))
			&& FMath::Abs(S.GroundLocation().Z-S.FinalDestination.Location.Z)<=MaximumSurfaceStepZCentimeters)
		{ SetTerminalNavigationState(S,EGuLiSoldierNavigationState::Arrived,EGuLiSoldierNavigationFailure::None); continue; }
		FVector Waypoint=S.GroundLocation();
		bool bHasWaypoint=false;
		const bool bPersonal=S.NavigationState==EGuLiSoldierNavigationState::PersonalPathRecovery;
		const bool bDirectDock=!bPersonal && S.NavigationState==EGuLiSoldierNavigationState::Normal
			&& S.bHasDockTarget && CommanderNavigationData
			&& S.DirectDock.Matches(*CommanderNavigationData,AuthorityState->NavigationGeneration,S.LastValidNavLocation,S.FinalDestination)
			&& S.DirectDock.bClear;
		if (bPersonal && S.ActiveNavigation && S.ActiveNavigation->Data.Get()==CommanderNavigationData
			&& S.ActiveNavigation->Path->IsUpToDate() && !S.PersonalPathPoints.IsEmpty())
		{
			// Capture each corner before turning; a movement step must not overshoot and cut a cliff.
			while (S.PersonalPathPointIndex<S.PersonalPathPoints.Num()-1
				&& FVector::DistSquared2D(S.Location,S.PersonalPathPoints[S.PersonalPathPointIndex])<=FMath::Square(10.f)
				&& FMath::Abs(S.GroundLocation().Z-S.PersonalPathPoints[S.PersonalPathPointIndex].Z)<=MaximumSurfaceStepZCentimeters)
				++S.PersonalPathPointIndex;
			Waypoint=S.PersonalPathPoints[S.PersonalPathPointIndex]; bHasWaypoint=true;
		}
		else if (bDirectDock) { Waypoint=S.FinalDestination.Location; bHasWaypoint=true; }
		else if (!bPersonal && S.SharedRoute && S.SharedRoute->Goal==S.RouteGoal
			&& S.SharedRoute->Navigation.Data.Get()==CommanderNavigationData)
			bHasWaypoint=S.SharedRoute->Sample(S.LastValidNavLocation,50.f,S.RouteCursor,Waypoint);
		if (!bHasWaypoint)
		{
			// Pending route work is not permission to walk straight through an unverified slope.
			if (bPersonal && !S.bNavigationBudgetPending
				&& S.NavigationFailure==EGuLiSoldierNavigationFailure::PersonalPathFailed)
			{
				S.NoProgressSeconds+=MovementUpdateDeltaSeconds[I];
				if (GuLiCommanderNavigationPolicy::ShouldBlockPersonalPathRecovery(
					S.PersonalPathRetries,S.NoProgressSeconds,2,PersonalRecoveryBlockedSeconds))
				{ SetTerminalNavigationState(S,EGuLiSoldierNavigationState::Blocked,EGuLiSoldierNavigationFailure::PersonalPathFailed); continue; }
			}
			S.Velocity=FVector::ZeroVector; S.CurrentNavigationWaypoint=S.GroundLocation();
			bSuppressMovementForStep[I]=true;
			continue;
		}
		const float Distance=FVector::Dist2D(S.Location,Waypoint);
		const int32 PointIndex=bPersonal ? S.PersonalPathPointIndex : S.RouteCursor;
		if (!S.CurrentNavigationWaypoint.Equals(Waypoint,.01) || PointIndex>S.LastProgressPathPointIndex
			|| Distance<S.BestWaypointDistanceCentimeters-ProgressDistanceCentimeters)
		{ S.BestWaypointDistanceCentimeters=Distance; S.NoProgressSeconds=0; }
		else S.NoProgressSeconds+=MovementUpdateDeltaSeconds[I];
		S.LastProgressPathPointIndex=PointIndex;
		// Test stagnation even when Detour reports a successful move clamped to zero,
		// or friendly/environment avoidance has reduced the requested velocity to zero.
		if (S.NavigationState==EGuLiSoldierNavigationState::Normal
			&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(
				S.ConsecutiveSurfaceFailures,S.NoProgressSeconds,SurfaceFailuresBeforeCenterline,CenterlineRecoverySeconds))
		{
			EnterCenterlineRecovery(S);
			if (bDirectDock) { S.Velocity=FVector::ZeroVector; bSuppressMovementForStep[I]=true; continue; }
		}
		else if (S.NavigationState==EGuLiSoldierNavigationState::CenterlineRecovery
			&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(
				S.ConsecutiveSurfaceFailures,S.NoProgressSeconds,SurfaceFailuresBeforePersonalPath,CenterlineRecoverySeconds))
		{
			BeginPersonalPathRecovery(S); S.Velocity=FVector::ZeroVector; bSuppressMovementForStep[I]=true; continue;
		}
		else if (bPersonal && S.NoProgressSeconds>=PersonalRecoveryRetrySeconds)
		{
			S.ActiveNavigation.Reset(); S.PersonalPathPoints.Reset(); S.bNavigationBudgetPending=true;
			S.Velocity=FVector::ZeroVector; bSuppressMovementForStep[I]=true; continue;
		}
		S.CurrentNavigationWaypoint=Waypoint;
		if (!bWorker)
        {
		const float StepSpeed=FMath::Min(GetUnitMovementSpeed(S.Team,S.UnitTypeId),
			Distance/MovementUpdateDeltaSeconds[I]);
		DesiredVelocities[I]=(Waypoint-S.Location).GetSafeNormal2D()*StepSpeed;
		bReceivesAvoidance[I]=true; ++AuthorityState->MovementUpdateCalls;
		AuthorityState->ForcedMovementUpdateCalls+=bForcedMovementUpdate[I]?1u:0u;
		if (!S.DirectionAppliedAt && !DesiredVelocities[I].IsNearlyZero())
		{
			S.DirectionAppliedAt=FPlatformTime::Seconds();
			const FString Detail=FString::Printf(TEXT("soldier=%u acceptedMs=%.3f route=%llu"),S.SoldierId.Value,(S.DirectionAppliedAt-S.CommandAcceptedAt)*1000.,S.SharedRoute?S.SharedRoute->Handle:0);
			GuLiMoveLatency::Record(TEXT("direction"),S.MoveTrace,1,*Detail);
		}
		auto& Move=EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity);
		Move.Center=Waypoint; Move.Forward=DesiredVelocities[I].GetSafeNormal(); Move.DesiredSpeed=FMassInt16Real(StepSpeed);
		EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(S.Entity).WorldTarget=Waypoint;
        }
        else {bReceivesAvoidance[I]=true;++AuthorityState->MovementUpdateCalls;AuthorityState->ForcedMovementUpdateCalls+=bForcedMovementUpdate[I]?1u:0u;}
	}

	// Freeze every candidate before any authority location is committed. The soft solver
	// returns absolute velocities, including full-speed friendly yields on stopped units.
	AuthorityState->ManualAvoidanceAgents.SetNum(AuthorityState->Soldiers.Num());
	for (int32 I = 0; I < AuthorityState->Soldiers.Num(); ++I)
	{
		const FSoldierRuntime& S = AuthorityState->Soldiers[I];
		if (S.ActiveNavigation && S.ActiveNavigation->Path.IsValid() && !S.ActiveNavigation->Path->IsUpToDate())
			bSuppressMovementForStep[I] = true;
		auto& Agent = AuthorityState->ManualAvoidanceAgents[I];
		Agent = {};
		Agent.StableSoldierId = S.SoldierId.Value;
		Agent.StableEntityKey = S.Entity.AsNumber();
		Agent.Team = static_cast<uint8>(S.Team);
		Agent.bEngineering = S.bEngineering;
		Agent.Location = S.GroundLocation();
        Agent.MovementLayer=S.MovementLayer;
		Agent.RadiusCentimeters = S.AvoidanceRadiusCentimeters;
		Agent.MaximumSpeed = GetUnitMovementSpeed(S.Team, S.UnitTypeId);
		Agent.bParticipates = S.IsPresent() && EntityManager.IsEntityValid(S.Entity);
		Agent.bReceivesAvoidance = Agent.bParticipates && S.CanSimulate()
			&& bReceivesAvoidance[I] && !bSuppressMovementForStep[I] && !S.bAttackMoveHolding;
		Agent.bCanBePushed = !bImmediateCommands && Agent.bParticipates && S.CanSimulate() && !S.bGroundMechYielding
			&& (!S.ActiveOrderId || S.bAttackMoveHolding);
		if (Agent.bReceivesAvoidance && !bWorker)
		{
			const FVector PredictiveAcceleration = EntityManager
				.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(S.Entity).Value;
			const FVector Target = (DesiredVelocities[I]
				+ PredictiveAcceleration.GetClampedToMaxSize(Agent.MaximumSpeed * 4.0f) * MovementUpdateDeltaSeconds[I])
				.GetClampedToMaxSize(Agent.MaximumSpeed);
			Agent.DesiredVelocity = FMath::VInterpTo(S.Velocity, Target, MovementUpdateDeltaSeconds[I], 8.0f);
		}
	}
	if (!bWorker)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_ManualAvoidanceRefresh);
		FGuLiPerformanceScope Timing(GetWorld(), TEXT("Avoidance.SoftMs"));
		TSharedPtr<const GuLiCommanderAvoidancePolicy::FSharedAvoidanceGeometry> Geometry;
		{
			auto& Scratch = AuthorityState->SoftGeometryScratch;
			Scratch.Reset();
			for (const auto& A : AuthorityState->ManualAvoidanceAgents)
			{
				if (!A.StableEntityKey) continue;
				auto& G = Scratch.AddDefaulted_GetRef();
				G.StableKey = A.StableEntityKey; G.Location = A.Location;
				G.Radius = A.RadiusCentimeters; G.bParticipates = A.bParticipates;
				G.bEngineering = A.bEngineering;G.MovementLayer=A.MovementLayer;
			}
			if (auto* Registry = GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
				for (const auto& O : Registry->GetSnapshot()->Obstacles)
				{
					auto& G = Scratch.AddDefaulted_GetRef();
					G.StableKey = (uint64(1) << 63) | O.Handle.Value;
					G.Location = O.Location; G.Radius = O.RadiusCentimeters;
					G.bParticipates = true; G.bEnvironment = true;G.bLayerBound=O.Kind==EGuLiDynamicObstacleKind::GroundMech;
				}
			Geometry = AcquireAvoidanceGeometry(Scratch);
		}
		const int32 PairMode = bImmediateCommands ? ActiveSoftPairs.GetValueOnGameThread() : 0;
		const auto Metrics = GuLiCommanderNavigationPolicy::SolveSoftAvoidanceVelocities(
			AuthorityState->ManualAvoidanceAgents, AvoidanceAgentHeightCentimeters,
			AuthorityState->ManualAvoidanceSpatialGrid, AuthorityState->CachedManualAvoidanceVelocities,
			Geometry.Get(), PairMode != 0);
		if (PairMode >= 2)
		{
			GuLiCommanderNavigationPolicy::FManualAvoidanceSpatialGrid ReferenceGrid;
			TArray<FVector> ReferenceVelocities;
			GuLiCommanderNavigationPolicy::SolveSoftAvoidanceVelocities(
				AuthorityState->ManualAvoidanceAgents, AvoidanceAgentHeightCentimeters,
				ReferenceGrid, ReferenceVelocities, Geometry.Get(), false);
			int32 Mismatches = 0;
			double MaximumError = 0;
			for (int32 I = 0; I < ReferenceVelocities.Num(); ++I)
			{
				const FVector Difference = ReferenceVelocities[I] - AuthorityState->CachedManualAvoidanceVelocities[I];
				const bool bMismatch = Difference.ContainsNaN() || !Difference.IsNearlyZero(1.e-6);
				Mismatches += bMismatch ? 1 : 0;
				MaximumError = FMath::Max(MaximumError, Difference.ContainsNaN()
					? TNumericLimits<double>::Max() : Difference.Size());
			}
			if (auto* Perf = GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>())
			{
				Perf->Record(TEXT("Avoidance.SoftVerifiedUnits"), double(ReferenceVelocities.Num()));
				Perf->Record(TEXT("Avoidance.SoftMismatches"), double(Mismatches));
				Perf->Record(TEXT("Avoidance.SoftMaximumError"), MaximumError);
			}
			// A diagnostic mismatch must not change the running simulation.
			if (Mismatches) AuthorityState->CachedManualAvoidanceVelocities = MoveTemp(ReferenceVelocities);
		}
		if (auto* Perf = GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>())
		{
			Perf->Record(TEXT("Avoidance.SoftCandidatePairs"), double(Metrics.CandidatePairs));
			Perf->Record(TEXT("Avoidance.SoftOverlapPairs"), double(Metrics.OverlapPairs));
			Perf->Record(TEXT("Avoidance.SoftBucketVisits"), double(Metrics.BucketEntriesVisited));
		}
		++AuthorityState->ManualAvoidanceRefreshes;
		AuthorityState->ManualAvoidanceCandidatePairs += Metrics.CandidatePairs;
		AuthorityState->ManualAvoidanceOverlapPairs += Metrics.OverlapPairs;
		AuthorityState->MaximumManualAvoidanceBucketOccupancy = FMath::Max(
			AuthorityState->MaximumManualAvoidanceBucketOccupancy, Metrics.MaximumBucketOccupancy);
		AuthorityState->bForceManualAvoidanceRefresh = false;
	}

    if (bWorker)
    {
        Work=std::make_shared<FMovementCalculation>();Work->FixedDeltaSeconds=FixedDeltaSeconds;
        Work->AvoidanceAgentHeightCentimeters=AvoidanceAgentHeightCentimeters;
        Work->SimulationSeconds=AuthorityState->SimulationSeconds;Work->LogicalStep=AuthorityState->ServerSimTick;
        Work->NavigationGeneration=AuthorityState->NavigationGeneration;Work->PreviousYielding=AuthorityState->GroundMechYieldingSoldiers;
        Work->Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot();
        Work->Agents=AuthorityState->ManualAvoidanceAgents;Work->Units.SetNum(AuthorityState->Soldiers.Num());
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        for (int32 I=0;I<AuthorityState->Soldiers.Num();++I)
        {
            const auto& S=AuthorityState->Soldiers[I];auto& U=Work->Units[I];
            U.Entity=S.Entity;U.SoldierId=S.SoldierId;U.HybridKey=S.HybridKey;U.StateRevision=S.StateRevision;
            U.ActiveOrderId=S.ActiveOrderId;U.TaskGeneration=S.TaskGeneration;U.DisplacementFrameFloor=S.DisplacementFrameFloor;
            U.Team=S.Team;U.NavigationState=S.NavigationState;U.Location=S.Location;U.Velocity=S.Velocity;
            U.MovementLayer=S.MovementLayer;U.AltitudeCentimeters=S.AltitudeCentimeters;
            U.Waypoint=S.CurrentNavigationWaypoint;U.NavLocation=S.LastValidNavLocation.Location;
            U.bHasWorkTarget=S.bEngineering&&S.bHasDockTarget;U.WorkTarget=S.FinalDestination.Location;
            U.AvoidanceRadiusCentimeters=S.AvoidanceRadiusCentimeters;U.Speed=GetUnitMovementSpeed(S.Team,S.UnitTypeId);
            U.MoveDelta=MovementUpdateDeltaSeconds[I];U.bPresent=S.IsPresent();U.bCanSimulate=S.CanSimulate();
            U.bReceives=Work->Agents[I].bReceivesAvoidance;U.bCanBePushed=Work->Agents[I].bParticipates&&S.CanSimulate()&&(!S.ActiveOrderId||S.bAttackMoveHolding);
            U.bMoveIntent=S.MoveIntent.IsValid();
            U.bGroundMechYielding=S.bGroundMechYielding;U.GroundMechYieldAnchor=S.GroundMechYieldAnchor;
            U.GroundMechYieldTarget=S.GroundMechYieldTarget;U.GroundMechLastPressureSimulationSeconds=S.GroundMechLastPressureSimulationSeconds;
            U.ContactObstacle=S.ContactObstacle;U.BypassSide=S.BypassSide;U.ContactClearSeconds=S.ContactClearSeconds;
            U.FormationCorrectionAlpha=S.FormationCorrectionAlpha;U.EnvironmentSpeedScale=S.EnvironmentSpeedScale;
            U.ConsecutiveSurfaceFailures=S.ConsecutiveSurfaceFailures;
            if (U.bReceives) U.PredictiveAcceleration=EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(S.Entity).Value;
            FGuLiHybridObjectState V;if (H&&U.HybridKey.IsValid()&&H->GetObjectView(U.HybridKey,V)) {U.OwnerEpoch=V.OwnerEpoch;U.ControlEpoch=V.ControlEpoch;}
        }
    }
    const TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Weak(this);
#if !UE_BUILD_SHIPPING
    const double PreparationMilliseconds=(FPlatformTime::Seconds()-StepStartedAt)*1000.;
#endif
    auto ApplyMovement=[this,Weak,Work,FixedDeltaSeconds,MaximumUnitMovementSpeed,MaximumSurfaceSlopeDegrees,
		bImmediateCommands,
        bRunsMovementUpdate=MoveTemp(bRunsMovementUpdate),MovementUpdateDeltaSeconds=MoveTemp(MovementUpdateDeltaSeconds),
        bSuppressMovementForStep=MoveTemp(bSuppressMovementForStep),FormationBySoldierId=MoveTemp(FormationBySoldierId),
        GroundMechYieldVelocities=MoveTemp(GroundMechYieldVelocities),bGroundMechYieldReturning=MoveTemp(bGroundMechYieldReturning),
        GroundMechYieldSpatialGrid=MoveTemp(GroundMechYieldSpatialGrid),GroundMechYieldCellSizeCentimeters,
        MaximumGroundMechYieldBodyRadiusCentimeters,EnterCenterlineRecovery,SetGroundMechYieldStandTarget
#if !UE_BUILD_SHIPPING
        ,StepStartedAt,PreparationMilliseconds
#endif
        ]() mutable
    {
        if (!Weak.IsValid()||!AuthorityState) return;
        const double ApplyStartedAt=FPlatformTime::Seconds();
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovementApply);
        auto* World=GetWorld();auto* Mass=World?World->GetSubsystem<UMassEntitySubsystem>():nullptr;if (!Mass) return;
        auto& EntityManager=Mass->GetMutableEntityManager();
        auto* NavigationSystem=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        auto* CommanderNavigationData=NavigationSystem?GetCommanderNavigationData(*NavigationSystem):nullptr;
        auto* H=World->GetSubsystem<UGuLiHybridWorldSubsystem>();
        const auto ObstacleSnapshot=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot();
        TMap<uint32,int32> SnapshotIndices;
        if (Work)
        {
            for (int32 I=0;I<Work->Units.Num();++I) SnapshotIndices.Add(Work->Units[I].SoldierId.Value,I);
            GroundMechYieldVelocities=Work->GroundMechYieldVelocities;bGroundMechYieldReturning=Work->bGroundMechYieldReturning;
            AuthorityState->CachedManualAvoidanceVelocities=Work->Velocities;AuthorityState->ManualAvoidanceAgents=Work->Agents;
            AuthorityState->GroundMechYieldingSoldiers=Work->YieldingCount;
            const auto& Metrics=Work->Metrics;++AuthorityState->ManualAvoidanceRefreshes;
            AuthorityState->ManualAvoidanceCandidatePairs+=Metrics.CandidatePairs;AuthorityState->ManualAvoidanceOverlapPairs+=Metrics.OverlapPairs;
            AuthorityState->MaximumManualAvoidanceBucketOccupancy=FMath::Max(AuthorityState->MaximumManualAvoidanceBucketOccupancy,Metrics.MaximumBucketOccupancy);
            AuthorityState->bForceManualAvoidanceRefresh=false;
            if (auto* Perf=World->GetSubsystem<UGuLiPerformanceSubsystem>())
            {Perf->Record(TEXT("Avoidance.SoftCandidatePairs"),double(Metrics.CandidatePairs));Perf->Record(TEXT("Avoidance.SoftOverlapPairs"),double(Metrics.OverlapPairs));}
            GroundMechYieldSpatialGrid.Reset();MaximumGroundMechYieldBodyRadiusCentimeters=0;
            for (const auto& S:AuthorityState->Soldiers) if (S.IsPresent()) MaximumGroundMechYieldBodyRadiusCentimeters=FMath::Max(MaximumGroundMechYieldBodyRadiusCentimeters,S.AvoidanceRadiusCentimeters);
            GroundMechYieldCellSizeCentimeters=FMath::Max(1.f,MaximumGroundMechYieldBodyRadiusCentimeters*2.f);
            for (int32 I=0;I<AuthorityState->Soldiers.Num();++I)
                if (const auto& S=AuthorityState->Soldiers[I];S.IsPresent())
                    GroundMechYieldSpatialGrid.FindOrAdd(GuLiCommanderNavigationPolicy::MakeAvoidanceSpatialCell(S.Location,GroundMechYieldCellSizeCentimeters)).Add(I);
        }


	for (int32 CurrentSoldierIndex=0;CurrentSoldierIndex<AuthorityState->Soldiers.Num();++CurrentSoldierIndex)
    {
        FSoldierRuntime& Soldier=AuthorityState->Soldiers[CurrentSoldierIndex];
		if (bImmediateCommands && !bRunsMovementUpdate[CurrentSoldierIndex]) continue;
        const int32* CapturedIndex=Work?SnapshotIndices.Find(Soldier.SoldierId.Value):nullptr;
        if (Work&&!CapturedIndex) continue;
        const int32 SoldierIndex=Work?*CapturedIndex:CurrentSoldierIndex;
        const FMovementUnitCalculation* U=Work?&Work->Units[SoldierIndex]:nullptr;
        if (U)
        {
            FGuLiHybridObjectState View;
            const bool Current=!U->HybridKey.IsValid()||(H&&H->GetObjectView(U->HybridKey,View)&&View.OwnerEpoch==U->OwnerEpoch&&View.ControlEpoch==U->ControlEpoch&&H->HasSimulation(U->HybridKey,true));
            if (!Current||Soldier.Entity!=U->Entity||Soldier.HybridKey!=U->HybridKey||Soldier.StateRevision!=U->StateRevision
                ||Soldier.TaskGeneration!=U->TaskGeneration||Soldier.DisplacementFrameFloor!=U->DisplacementFrameFloor
                ||!Soldier.Location.Equals(U->Location,.01)||Work->NavigationGeneration!=AuthorityState->NavigationGeneration
                ||!ObstacleSnapshot->IsRegionCurrent(U->ObstacleRegion)) {Soldier.bForceMovementUpdate=true;continue;}
            Soldier.bGroundMechYielding=U->bGroundMechYielding;Soldier.GroundMechYieldAnchor=U->GroundMechYieldAnchor;
            Soldier.GroundMechYieldTarget=U->GroundMechYieldTarget;Soldier.GroundMechLastPressureSimulationSeconds=U->GroundMechLastPressureSimulationSeconds;
            Soldier.ContactObstacle=U->ContactObstacle;Soldier.BypassSide=U->BypassSide;Soldier.ContactClearSeconds=U->ContactClearSeconds;
            Soldier.FormationCorrectionAlpha=U->FormationCorrectionAlpha;Soldier.EnvironmentSpeedScale=U->EnvironmentSpeedScale;
            if (U->bStandTarget) {Soldier.Velocity=U->Velocity;SetGroundMechYieldStandTarget(Soldier);}
            if (U->bReceives&&EntityManager.IsEntityValid(Soldier.Entity))
            {
                auto& Move=EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
                Move.Center=U->Waypoint;Move.Forward=U->DesiredVelocity.GetSafeNormal();Move.DesiredSpeed=FMassInt16Real(U->DesiredVelocity.Size2D());
                EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(Soldier.Entity).WorldTarget=U->Waypoint;
                if (!Soldier.DirectionAppliedAt&&!U->DesiredVelocity.IsNearlyZero())
                {Soldier.DirectionAppliedAt=FPlatformTime::Seconds();GuLiMoveLatency::Record(TEXT("direction"),Soldier.MoveTrace,1,TEXT("worker application"));}
            }
        }
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}

		if (Soldier.bExternalActionsLocked && Soldier.IsAlive()) { RefreshSoldierNavigationState(Soldier.SoldierId); continue; }
        if (Soldier.HybridKey.IsValid() && !Soldier.bLocalSimulation) continue;
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

		if (Soldier.ActiveNavigation && Soldier.ActiveNavigation->Path.IsValid() && !Soldier.ActiveNavigation->Path->IsUpToDate())
		{ bSuppressMovementForStep[SoldierIndex]=true; Soldier.Velocity=FVector::ZeroVector; }
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
			FVector TargetVelocity = AuthorityState->CachedManualAvoidanceVelocities[SoldierIndex];
			if (!Work) if (auto* Registry = World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
				TargetVelocity = ConstrainEnvironmentVelocity(Soldier, *Registry->GetSnapshot(), TargetVelocity,
					MovementDeltaSeconds, GetUnitMovementSpeed(Soldier.Team, Soldier.UnitTypeId),true,
					Soldier.bEngineering && Soldier.bHasDockTarget ? &Soldier.FinalDestination.Location : nullptr);
			if (!Work && Soldier.MoveIntent)
			{
				// Avoidance keeps its lateral component, but residual speed cannot carry a
				// soldier past the corner that must be captured before the next route leg.
				const FVector ToWaypoint=Soldier.CurrentNavigationWaypoint-Soldier.Location;
				const FVector Forward=ToWaypoint.GetSafeNormal2D();
				const double Excess=FVector::DotProduct(TargetVelocity,Forward)-ToWaypoint.Size2D()/MovementDeltaSeconds;
				if (Excess>0.) TargetVelocity-=Forward*Excess;
			}
			if (U) TargetVelocity=U->ConstrainedVelocity;
			Soldier.Velocity = TargetVelocity.IsNearlyZero(1.0f) ? FVector::ZeroVector : TargetVelocity;

			if (!Soldier.Velocity.IsNearlyZero(1.0f))
			{
				const FVector CandidateLocation = U ? U->CandidateLocation : Soldier.LastValidNavLocation.Location
					+ Soldier.Velocity * MovementDeltaSeconds;
				FNavLocation SurfaceLocation;
				++AuthorityState->SurfaceMoveCalls;
				const bool bSurfaceMoveSucceeded = CommanderNavigationData
					&& FindMoveAlongCurrentNavigationSurface(
						*CommanderNavigationData,
						Soldier.LastValidNavLocation,
						CandidateLocation,
						SurfaceLocation,
					AuthorityState->PlanningBudget, Soldier.bNavigationBudgetPending,
						this);
				const bool bSurfaceMoveAccepted = bSurfaceMoveSucceeded && World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(
					Soldier.GroundLocation(),SurfaceLocation.Location,Soldier.AvoidanceRadiusCentimeters,true,Soldier.MovementLayer)
					&& GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
						bSurfaceMoveSucceeded,
						Soldier.LastValidNavLocation.Location,
						SurfaceLocation.Location,
						MaximumSurfaceStepZCentimeters, MaximumSurfaceSlopeDegrees);
				if (bSurfaceMoveAccepted)
				{
					if (Soldier.MoveIntent && !Soldier.FirstDisplacementAt && FVector::DistSquared2D(SurfaceLocation.Location,Soldier.CommandStartLocation)>1.)
					{
						Soldier.FirstDisplacementAt=FPlatformTime::Seconds();
						const FString Detail=FString::Printf(TEXT("soldier=%u acceptedMs=%.3f"),Soldier.SoldierId.Value,(Soldier.FirstDisplacementAt-Soldier.CommandAcceptedAt)*1000.);
						GuLiMoveLatency::Record(TEXT("displacement"),Soldier.MoveTrace,1,*Detail);
					}
					Soldier.Velocity=(Soldier.WorldFromGround(SurfaceLocation.Location)-Soldier.Location)/FMath::Max(MovementDeltaSeconds,UE_SMALL_NUMBER);
					Soldier.Location = Soldier.WorldFromGround(SurfaceLocation.Location);
					Soldier.LastValidNavLocation = SurfaceLocation;
					Soldier.ConsecutiveSurfaceFailures = 0;
					Soldier.NavigationFailure = EGuLiSoldierNavigationFailure::None;
					if (!Soldier.Velocity.IsNearlyZero(1.0f) && Soldier.FacingPolicy != EGuLiSoldierFacingPolicy::FixedSpawnYaw)
					{
						Soldier.FacingYawDegrees = FMath::FixedTurn(
							Soldier.FacingYawDegrees,
							Soldier.Velocity.GetSafeNormal2D().Rotation().Yaw,
						FacingRateDegreesPerSecond * GuLiMassMovementTuning::GetTurnRateScale() * MovementDeltaSeconds);
					}
				}
				else
				{
					Soldier.Location = Soldier.WorldFromGround(Soldier.LastValidNavLocation.Location);
					Soldier.Velocity = FVector::ZeroVector;
					if (!Soldier.bNavigationBudgetPending) ++AuthorityState->SurfaceMoveFailures;
					if (!Soldier.bNavigationBudgetPending) ++Soldier.ConsecutiveSurfaceFailures;
					if (!Soldier.bNavigationBudgetPending) ++Soldier.TotalSurfaceFailures;
					if (FOrderFormationRuntime* const* Formation =
						FormationBySoldierId.Find(Soldier.SoldierId.Value))
					{
						(*Formation)->bLastMovementStepSucceeded = false;
					}
					Soldier.NavigationFailure = bSurfaceMoveSucceeded
						? EGuLiSoldierNavigationFailure::ExcessiveHeightDelta
						: EGuLiSoldierNavigationFailure::SurfaceMoveFailed;
					if (!Soldier.MoveIntent && Soldier.NavigationState == EGuLiSoldierNavigationState::Normal
						&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(
							Soldier.ConsecutiveSurfaceFailures,
							Soldier.NoProgressSeconds,
							SurfaceFailuresBeforeCenterline,
							CenterlineRecoverySeconds))
					{
						EnterCenterlineRecovery(Soldier);
					}
					else if (!Soldier.MoveIntent && Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery
						&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(
							Soldier.TotalSurfaceFailures,
							Soldier.NoProgressSeconds,
							SurfaceFailuresBeforePersonalPath,
							CenterlineRecoverySeconds))
					{
						BeginPersonalPathRecovery(Soldier);
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
			const float MaximumEarlierStepCentimeters = MaximumUnitMovementSpeed
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
						if (OtherIndex == CurrentSoldierIndex
							|| !AuthorityState->Soldiers.IsValidIndex(OtherIndex)) continue;
						const FSoldierRuntime& Other = AuthorityState->Soldiers[OtherIndex];
						if (!Other.IsPresent() || Other.MovementLayer!=Soldier.MovementLayer || Other.Location.ContainsNaN()
							|| GuLiEngineeringCollision::IgnoresPeer(Soldier.bEngineering, Other.bEngineering)) continue;
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
					AuthorityState->PlanningBudget, Soldier.bNavigationBudgetPending,
					this);
			const bool bSurfaceMoveAccepted =
				GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(
					bSurfaceMoveSucceeded,
					Soldier.LastValidNavLocation.Location,
					SurfaceLocation.Location,
					MaximumSurfaceStepZCentimeters, MaximumSurfaceSlopeDegrees);
			if (bSurfaceMoveAccepted)
			{
				Soldier.Velocity=(Soldier.WorldFromGround(SurfaceLocation.Location)-Soldier.Location)/FMath::Max(FixedDeltaSeconds,UE_SMALL_NUMBER);
				Soldier.Location = Soldier.WorldFromGround(SurfaceLocation.Location);
				Soldier.LastValidNavLocation = SurfaceLocation;
				if (Soldier.FacingPolicy != EGuLiSoldierFacingPolicy::FixedSpawnYaw)
					Soldier.FacingYawDegrees = FMath::FixedTurn(
						Soldier.FacingYawDegrees,
						Soldier.Velocity.GetSafeNormal2D().Rotation().Yaw,
						FacingRateDegreesPerSecond * GuLiMassMovementTuning::GetTurnRateScale() * FixedDeltaSeconds);
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
				if (bCandidateClear) if (!Soldier.bNavigationBudgetPending) ++AuthorityState->SurfaceMoveFailures;
			}
		}
		else if (AuthorityState->ManualAvoidanceAgents[SoldierIndex].bCanBePushed
			&& !AuthorityState->CachedManualAvoidanceVelocities[SoldierIndex].IsNearlyZero(1.0f))
		{
			// Passive pressure never creates a move order, clears S, or restores an old destination.
			const auto Snapshot = World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot();
			const FVector Target = AuthorityState->CachedManualAvoidanceVelocities[SoldierIndex];
			const float PassiveMaximumSpeed = GetUnitMovementSpeed(Soldier.Team, Soldier.UnitTypeId);
			const FVector Velocity = U ? U->PassiveVelocity : ConstrainEnvironmentVelocity(Soldier, *Snapshot,
				Target, FixedDeltaSeconds, PassiveMaximumSpeed, false);
			FNavLocation SurfaceLocation;
			++AuthorityState->SurfaceMoveCalls;
			const bool bMoved = CommanderNavigationData && FindMoveAlongCurrentNavigationSurface(
				*CommanderNavigationData, Soldier.LastValidNavLocation,
				Soldier.GroundLocation() + Velocity * FixedDeltaSeconds, SurfaceLocation,
				AuthorityState->PlanningBudget, Soldier.bNavigationBudgetPending, this);
			if (bMoved && Snapshot->IsSegmentClear(Soldier.GroundLocation(), SurfaceLocation.Location,
				Soldier.AvoidanceRadiusCentimeters, true, Soldier.MovementLayer)
				&& GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(true,
					Soldier.LastValidNavLocation.Location, SurfaceLocation.Location,
					MaximumSurfaceStepZCentimeters, MaximumSurfaceSlopeDegrees))
			{
				Soldier.Velocity = (Soldier.WorldFromGround(SurfaceLocation.Location) - Soldier.Location) / FixedDeltaSeconds;
				Soldier.Location = Soldier.WorldFromGround(SurfaceLocation.Location);
				Soldier.LastValidNavLocation = SurfaceLocation;
				Soldier.LastMovementUpdateSimulationSeconds = AuthorityState->SimulationSeconds;
				if (!Soldier.ActiveOrderId)
				{
					Soldier.CurrentNavigationWaypoint = Soldier.GroundLocation();
					auto& Move = EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
					Move.Center = Soldier.Location;
					Move.DesiredSpeed = FMassInt16Real(0.0f);
				}
			}
			else Soldier.Velocity = FVector::ZeroVector;
		}
		else if (!bHasMovingOrder || bSuppressMovementForStep[SoldierIndex])
		{
			Soldier.Velocity = FVector::ZeroVector;
		}

        if (Soldier.bEngineering && Soldier.CanSimulate())
            if (auto* Carrier=Soldier.EngineeringCarrier.Get();Carrier && UGuLiHybridActorComponent::HasSimulation(Carrier))
            {
                Carrier->SetActorLocationAndRotation(Soldier.Location+FVector(0,0,Carrier->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
                    FRotator(0,Soldier.FacingYawDegrees,0),false,nullptr,ETeleportType::None);
                Carrier->GetCharacterMovement()->Velocity=Soldier.Velocity;
            }
        if (Soldier.CanSimulate()) GuLiGrid::ObserveLocalBody(*World,Soldier.HybridKey,Soldier.Location,
            GuLiGrid::FootprintFromDiameter(Soldier.AvoidanceRadiusCentimeters*2.));
		RefreshSoldierNavigationState(Soldier.SoldierId);
		FTransformFragment& Transform = EntityManager
			.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
		if (Soldier.FacingPolicy == EGuLiSoldierFacingPolicy::FixedSpawnYaw)
			Soldier.FacingYawDegrees = Soldier.FixedSpawnYawDegrees;
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
#if !UE_BUILD_SHIPPING
        if (OnPositionApplied.IsBound() && Soldier.FirstDisplacementAt > 0)
            OnPositionApplied.Broadcast(Soldier.SoldierId,Soldier.MoveTrace.Command,Soldier.Location,FPlatformTime::Seconds());
#endif
	}

    if (!bImmediateCommands && !AuthorityState->EngineeringPosePublishes.IsEmpty())
    {
        auto Pending=MoveTemp(AuthorityState->EngineeringPosePublishes);
        for (auto& Publish:Pending) Publish();
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
	if (!bImmediateCommands)
	{
		CommitReadyNavigationRepairs();
		DetectNavigationRepairs();
		TickSoldierCombat();
		RetireExpiredSoldiers();
	}
#if !UE_BUILD_SHIPPING
	const double StepMilliseconds = Work?PreparationMilliseconds+(FPlatformTime::Seconds()-ApplyStartedAt)*1000.
        :(FPlatformTime::Seconds() - StepStartedAt) * 1000.0;
	if (!bImmediateCommands)
	{
		++PerformanceCounters.Steps;
		PerformanceCounters.SimulationMilliseconds += StepMilliseconds;
		PerformanceCounters.MaxSimulationMilliseconds = FMath::Max(
			PerformanceCounters.MaxSimulationMilliseconds, StepMilliseconds);
	}
	else if (auto* Perf = World->GetSubsystem<UGuLiPerformanceSubsystem>())
	{
		Perf->Record(TEXT("Movement.ImmediateMs"), StepMilliseconds);
	}
#endif

        if (Work) {AuthorityState->bMovementStepPending=false;AuthorityState->MovementCalculation.reset();AuthorityState->MovementApplication={};}
    };
    if (!Work) {ApplyMovement();return;}
    AuthorityState->bMovementStepPending=true;AuthorityState->MovementCalculation=Work;
    AuthorityState->MovementApplication=std::move(ApplyMovement);
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const uint64 Bytes=1024+uint64(Work->Units.Num())*4096;
    if (Hybrid) Hybrid->SubmitGameplayCalculation(TEXT("MassMovement"),Work,Bytes,Bytes,AuthorityState->MovementApplication);
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
		if (Soldier.bEngineering) continue;
		Population.Add({Soldier.SoldierId, Soldier.Location, Soldier.Velocity,
			Soldier.Team, Soldier.UnitTypeId, Soldier.CanAct()});
		if (const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
			if (const auto* Definition = Data->FindSoldierDefinition(Soldier.UnitTypeId))
				Population.Last().WorldBounds = Definition->GetModelBoundsCentimeters().TransformBy(
					FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location));
	}
	TArray<FGuLiSoldierId> HitIds;
	if (!GuLiCommanderSelectionQuery::ResolveCandidates(Request, PlayerState.GetTeam(), Population, HitIds, GetWorld()))
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

#if !UE_BUILD_SHIPPING
bool UGuLiBattleAuthoritySubsystem::ResolveReviewSoldierSelection(const AGuLiBattlePlayerState& PlayerState,
	TConstArrayView<FGuLiSoldierId> Members, uint32 RequestId, FGuLiCommanderSelectionState& InOutSelection)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->bPopulationSpawned || !IsAuthorityWorld() || !RequestId
		|| !PlayerState.IsCommander() || !GuLiCommanderProtocol::IsPlayableTeam(PlayerState.GetTeam())
		|| Members.IsEmpty() || Members.Num()>int32(GULI_MAX_CONTROL_COHORTS*GULI_CONTROL_COHORT_TARGET_SIZE)) return false;
	TSet<FGuLiSoldierId> Unique;TArray<GuLiControlCohortBuilder::FCandidate> Candidates;
	for (const auto Id:Members)
	{
		const auto* Index=AuthorityState->SoldierIndexById.Find(Id.Value);
		if (!Index || !AuthorityState->Soldiers.IsValidIndex(*Index) || Unique.Contains(Id)) return false;
		const auto& Soldier=AuthorityState->Soldiers[*Index];
		if (Soldier.bEngineering || !Soldier.CanAct() || Soldier.Team!=PlayerState.GetTeam()) return false;
		Unique.Add(Id);Candidates.Add({Id,Soldier.Location});
	}
	TArray<TArray<FGuLiSoldierId>> Built;GuLiControlCohortBuilder::Build(Candidates,{},0.0f,Built);
	FGuLiCommanderSelectionState Candidate;Candidate.SelectionRevision=InOutSelection.SelectionRevision+1;
	if (!Candidate.SelectionRevision) ++Candidate.SelectionRevision;
	Candidate.AcceptedClientRequestId=RequestId;
	for (auto& MembersInCohort:Built)
	{
		auto& Cohort=Candidate.Cohorts.AddDefaulted_GetRef();
		Cohort.CohortId=FGuLiControlCohortId(AllocateNonZero(AuthorityState->NextControlCohortId));
		Cohort.MemberIds=MoveTemp(MembersInCohort);Cohort.AliveCount=uint8(Cohort.MemberIds.Num());
		TArray<uint32,TInlineAllocator<SoldierCountPerFormation>> Orders;
		for (const auto Id:Cohort.MemberIds) Orders.Add(AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)].ActiveOrderId);
		Cohort.ActiveOrderId=GuLiCommanderNavigationPolicy::ResolveCommonActiveOrderId(Orders);
	}
	Candidate.Sanitize();InOutSelection=MoveTemp(Candidate);return true;
}
#endif

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
			if (Soldier.bEngineering || Soldier.Team != Team || !Soldier.IsAlive())
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
	FGuLiCommandAck& OutImmediateAck,
	const TSharedPtr<FGuLiSharedMoveIntent>& SharedIntent)
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
	if (TeamJobs>=128) { OutImmediateAck.Result=EGuLiCommandAckResult::RateLimited; return false; }

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
	if (!World || CurrentAuthorityEpoch == 0u)
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
	Job.MoveIntent=SharedIntent ? SharedIntent : CreateSharedMoveIntent(Request.Target,Request.ClientCommandId);
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
			if (!SharedIntent) Job.MoveIntent->UnassignedMembers.Add(Id.Value);
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
float UGuLiBattleAuthoritySubsystem::GetUnitBaseMovementSpeed(uint16 UnitTypeId) const
{
	const auto* Data = GetWorld() ? GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	const auto* Unit = Data ? Data->FindSoldierDefinition(UnitTypeId) : nullptr;
	if (!Unit) return MovementSpeedCentimetersPerSecond;
	const float DefaultSpeed = Data->GetDefaultSoldierDefinition().MovementSpeedCmPerSecond;
	return DefaultSpeed > 0 ? Unit->MovementSpeedCmPerSecond * MovementSpeedCentimetersPerSecond / DefaultSpeed
		: MovementSpeedCentimetersPerSecond;
}

float UGuLiBattleAuthoritySubsystem::GetUnitMovementSpeed(EGuLiTeam Team, uint16 UnitTypeId) const
{
	const float* Multiplier=RogueMovementMultipliers.Find((static_cast<uint32>(Team)<<16)|UnitTypeId);
	return GetUnitBaseMovementSpeed(UnitTypeId)*(Multiplier ? *Multiplier : 1.f);
}

bool UGuLiBattleAuthoritySubsystem::RefreshRogueMovementEntities(uint32 Key, float Multiplier, FString& Error)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->MassEntitySubsystem.IsValid()
		|| !AuthorityState->RuntimeTuningEvenBaseArchetype.IsValid() || !AuthorityState->RuntimeTuningOddBaseArchetype.IsValid())
	{ Error=TEXT("Mass movement is not ready"); return false; }
	auto& Manager=AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	struct FChange { int32 Index; float Speed; FMassArchetypeHandle Archetype; FMassArchetypeSharedFragmentValues Shared; };
	TArray<FChange> Changes;
	// Resolve every target before touching the ledger or any entity.
	for (int32 I=0; I<AuthorityState->Soldiers.Num(); ++I)
	{
		const auto& S=AuthorityState->Soldiers[I];
		if (Key!=MAX_uint32 && Key!=((static_cast<uint32>(S.Team)<<16)|S.UnitTypeId)) continue;
		if (!Manager.IsEntityValid(S.Entity)) continue;
		auto& Change=Changes.AddDefaulted_GetRef(); Change.Index=I;
		Change.Speed=Key==MAX_uint32 ? GetUnitMovementSpeed(S.Team,S.UnitTypeId) : GetUnitBaseMovementSpeed(S.UnitTypeId)*Multiplier;
		if (!FMath::IsFinite(Change.Speed) || Change.Speed <= 0 || Change.Speed > MAX_int16)
		{ Error=TEXT("Per-unit movement speed exceeds the Mass numeric limit"); return false; }
		Change.Shared=MakeAuthoritySharedFragmentValues(Manager,Change.Speed,S.AvoidanceRadiusCentimeters);
		Change.Archetype=Manager.GetOrCreateSuitableArchetype(S.bMovementEvenArchetype ? AuthorityState->RuntimeTuningOddBaseArchetype : AuthorityState->RuntimeTuningEvenBaseArchetype,
			Change.Shared.GetSharedFragmentBitSet(),Change.Shared.GetConstSharedFragmentBitSet());
		if (!Change.Archetype.IsValid()) { Error=TEXT("Cannot resolve Mass movement archetype"); return false; }
	}
	for (const auto& Change : Changes)
	{
		auto& S=AuthorityState->Soldiers[Change.Index];
		Manager.MoveEntityToAnotherArchetype(S.Entity,Change.Archetype,&Change.Shared);
		S.bMovementEvenArchetype=!S.bMovementEvenArchetype;
		S.Velocity=S.Velocity.GetClampedToMaxSize(Change.Speed);
		Manager.GetFragmentDataChecked<FMassVelocityFragment>(S.Entity).Value=S.Velocity;
		if (S.ActiveOrderId) Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity).DesiredSpeed=FMassInt16Real(Change.Speed);
		S.bForceMovementUpdate=true;
	}
	AuthorityState->bForceManualAvoidanceRefresh=true; return true;
}

void UGuLiBattleAuthoritySubsystem::ResetRogueMovementForMatch()
{
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {
        if (PendingHybridMovement.IsEmpty() || !AuthorityState || !AuthorityState->bPopulationSpawned) return;
        auto Candidate=RogueMovementMultipliers;
        for (const auto& [Team,Values]:PendingHybridMovement)
        {
            for (auto I=Candidate.CreateIterator();I;++I) if ((I.Key()>>16)==uint32(Team)) I.RemoveCurrent();
            for (const auto& [Unit,Multiplier]:Values) Candidate.Add((uint32(Team)<<16)|Unit,Multiplier);
        }
        for (const auto& [Key,V]:Candidate)
            if (!FMath::IsFinite(V) || V<=0 || GetUnitBaseMovementSpeed(uint16(Key))*V>MAX_int16)
            {GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Committed movement capability exceeds the Mass numeric range"));return;}
        bool Unchanged=Candidate.Num()==RogueMovementMultipliers.Num();
        for (const auto& [Key,V]:Candidate)
        {const auto* Previous=RogueMovementMultipliers.Find(Key);if (!Previous || *Previous!=V) {Unchanged=false;break;}}
        if (Unchanged) {PendingHybridMovement.Reset();return;}
        const auto Previous=RogueMovementMultipliers;RogueMovementMultipliers=MoveTemp(Candidate);FString Error;
        if (!RefreshRogueMovementEntities(MAX_uint32,1.f,Error))
        {RogueMovementMultipliers=Previous;GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(Error);return;}
        if (auto* Published=GetWorld()->GetGameState<AGuLiCommanderGameState>())
        {
            Published->ResetAuthoritativeUnitMovementMultipliers();
            for (const auto& [Key,V]:RogueMovementMultipliers) Published->SetAuthoritativeUnitMovementMultiplier(EGuLiTeam(Key>>16),uint16(Key),V);
        }
        PendingHybridMovement.Reset();return;
    }
	const auto* State=GetWorld()->GetGameState<AGuLiBattleGameState>();
	const uint32 Epoch=State ? State->GetMatchEpoch() : 0;
	if (!Epoch || Epoch==RogueMovementEpoch) return;
	RogueMovementEpoch=Epoch;
	const bool bHadBonuses=!RogueMovementMultipliers.IsEmpty();
	RogueMovementSources.Reset(); RogueMovementMultipliers.Reset();
	if (auto* Published = GetWorld()->GetGameState<AGuLiCommanderGameState>())
		Published->ResetAuthoritativeUnitMovementMultipliers();
	if (bHadBonuses) { FString Error; RefreshRogueMovementEntities(MAX_uint32,1.f,Error); }
}
void UGuLiBattleAuthoritySubsystem::SetHybridMovementState(EGuLiTeam Team,const TMap<uint16,float>& Values)
{if (UGuLiNetworkSettings::IsHybrid(GetWorld()) && GuLiCommanderProtocol::IsPlayableTeam(Team)) PendingHybridMovement.Add(Team,Values);}

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

	for (const auto& Pair : RogueMovementMultipliers)
		if (!FMath::IsFinite(NewMovementSpeed*Pair.Value) || NewMovementSpeed*Pair.Value>MAX_int16)
		{
			UE_LOG(LogGuLiCommanderMass,Warning,TEXT("Movement tuning rejected: rogue multiplier exceeds Mass speed limit."));
			PendingMovementSpeedCmPerSecond.Reset(); return;
		}
	const float PreviousSpeed=MovementSpeedCentimetersPerSecond;
	MovementSpeedCentimetersPerSecond=NewMovementSpeed;
	FString Error;
	if (!RefreshRogueMovementEntities(MAX_uint32,1.f,Error))
	{ MovementSpeedCentimetersPerSecond=PreviousSpeed; return; }
	const int32 UpdatedEntityCount=AuthorityState->Soldiers.Num();
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
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()) && !bApplyingHybridLedger) return false;
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
	GuLiNet::Health Numerical;
	Numerical.Value = Soldier.Health;
	Numerical.Maximum = Soldier.MaxHealth;
	Numerical.Version = 1;
	Numerical.Dead = Soldier.Health <= 0.0f;
	GuLiNet::DamageDelta NumericalDelta;
	GuLiNet::ApplyDamage(Numerical, Amount, NumericalDelta);
	Soldier.Health = Numerical.Value;
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
	if (const auto* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(SoldierId.Value) : nullptr)
	{
		const auto& Soldier = AuthorityState->Soldiers[*Index];
		if (Soldier.bEngineering)
		{
			// The internal Mass movement ID must never become a second network damage identity.
			const auto* Carrier = Soldier.EngineeringCarrier.Get();
			const auto* Health = Carrier ? Carrier->FindComponentByClass<UGuLiCombatHealthComponent>() : nullptr;
			return Health ? Health->GetTargetHandle() : FGuLiTargetHandle{};
		}
	}
	const AGuLiBattleGameState* BattleState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const uint32 MatchEpoch = BattleState ? BattleState->GetMatchEpoch() : 0u;
    auto Result=SoldierId.IsValid() ? GuLiCombatTargets::MakeCommanderSoldierTargetHandle(MatchEpoch,SoldierId.Value) : FGuLiTargetHandle{};
    if (const auto* Index=AuthorityState?AuthorityState->SoldierIndexById.Find(SoldierId.Value):nullptr)
        if (AuthorityState->Soldiers[*Index].HybridKey.IsValid()) Result.Generation=AuthorityState->Soldiers[*Index].HybridKey.LifeGeneration;
    return Result;
}

void UGuLiBattleAuthoritySubsystem::RegisterCombatLedgerTargets()
{
	UnregisterCombatLedgerTargets();
	if (!IsAuthorityWorld() || !AuthorityState)
	{
		return;
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
    if (const auto* I=AuthorityState?AuthorityState->SoldierIndexById.Find(SoldierId.Value):nullptr)
        if (AuthorityState->Soldiers[*I].bEngineering) return false;

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
		if (!Authority) return false;
		const auto* State=Authority->AuthorityState.Get();
		const int32* Index=State?State->SoldierIndexById.Find(SoldierId.Value):nullptr;
		if (!Index || !State->Soldiers.IsValidIndex(*Index)) return false;
		const auto& Soldier=State->Soldiers[*Index];
		if (Soldier.bPhased) return false;
		// Read the current lifetime directly. Snapshot users do not need weapon
		// profiles, cooldowns or the rest of the combat debug presentation.
		OutSnapshot={};OutSnapshot.Handle=Handle;OutSnapshot.Team=Soldier.Team;
		OutSnapshot.Location=Soldier.Location;OutSnapshot.Health=Soldier.Health;OutSnapshot.MaxHealth=Soldier.MaxHealth;
		OutSnapshot.bAlive=Soldier.Health>0.f;
		OutSnapshot.CollisionRadius=Authority->MemberAgentRadiusCentimeters;
		if (const auto* Data=Authority->GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
			if (const auto* Definition=Data->FindSoldierDefinition(Soldier.UnitTypeId))
			{
				const FBox Bounds=Definition->GetModelBoundsCentimeters();
				if (Bounds.IsValid)
				{
					OutSnapshot.CollisionRadius=FMath::Max(Bounds.GetExtent().X,Bounds.GetExtent().Y);
					OutSnapshot.HitCenter=Soldier.Location+FRotator(0.f,Soldier.FacingYawDegrees,0.f).RotateVector(Bounds.GetCenter());
					OutSnapshot.bHasHitCenter=true;
				}
			}
		return !OutSnapshot.Location.ContainsNaN();
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

bool UGuLiBattleAuthoritySubsystem::ProjectExternalUnitLocation(const FVector Desired, FVector& OutLocation,int32 FootprintCells) const
{
    if (const auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady())
        return G->Snapshot()->Project(Desired,FootprintCells,FVector(300,300,2000),OutLocation);
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
				Tasks->CancelPendingMove(FGuLiTaskUnitId::Soldier(Entry.Id,GetWorld()));
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
			Soldier.FacingYawDegrees = Soldier.FacingPolicy == EGuLiSoldierFacingPolicy::FixedSpawnYaw
				? Soldier.FixedSpawnYawDegrees : Entry.Transform.Rotator().Yaw;
			Soldier.LastValidNavLocation = FNavLocation(Soldier.GroundLocation());
			Soldier.FinalDestinationNavigationGeneration=0;
			auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			auto* Data = Nav ? GuLiCommanderMassPrivate::GetCommanderNavigationData(*Nav) : nullptr;
			if (Data) Nav->ProjectPointToNavigation(Soldier.GroundLocation(), Soldier.LastValidNavLocation, FVector(20,20,300), Data);
			Soldier.DisplacementFrameFloor = AuthorityState->NextPoseFrameSequence;
			Soldier.DisplacementLocation = Soldier.Location;
			Soldier.DisplacementSimulationTime = AuthorityState->SimulationSeconds;
		}
        if (auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady())
        {
            auto& M=Soldier.GridMotion;M.Stop(*G,Soldier.Location,AuthorityState->SimulationSeconds,EGuLiGridMoveEnd::Stopped);
            if (bPhased)
            {if (G->Network()) G->Network()->Withdraw(M.Key);G->Traffic().Remove(M.Key,M.Owner,M.Epoch);}
            else if (M.Key.IsValid()) M.Bind(*G,M.Key,M.Owner,M.Epoch,uint8(Soldier.Team),Soldier.Location,GuLiGrid::FootprintFromDiameter(Soldier.AvoidanceRadiusCentimeters*2.),Soldier.MovementLayer);
        }
		++Soldier.StateRevision;
		RefreshSoldierNavigationState(Soldier.SoldierId);
		Soldier.CurrentNavigationWaypoint = Soldier.GroundLocation();
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
bool UGuLiBattleAuthoritySubsystem::BuildSoldierState(FGuLiSoldierId Id, FGuLiSoldierStateItem& State) const
{
 const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
 if (!Index) return false;
 const auto& Soldier = AuthorityState->Soldiers[*Index];
 if (Soldier.bEngineering) return false;
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
 return true;
}

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
		if (Soldier.bEngineering) continue;
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
		if (!AuthorityState->Soldiers[Index].bEngineering) SortedIndices.Add(Index);
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
				const double SpeedBound = FMath::Max(GetUnitMovementSpeed(Soldier.Team,Soldier.UnitTypeId), Soldier.LastCapturedMovementSpeed);
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
			Soldier.LastCapturedMovementSpeed = GetUnitMovementSpeed(Soldier.Team,Soldier.UnitTypeId);
			FGuLiQuantizedSoldierPose& Pose = Chunk.Samples.AddDefaulted_GetRef();
			Pose.SoldierId = Soldier.SoldierId;
			if (!GuLiCommanderPoseCodec::Quantize(Soldier.Location, Soldier.Velocity, Soldier.FacingYawDegrees, Pose))
			{
				UE_LOG(LogGuLiCommanderMass, Error, TEXT("Pose quantization range exceeded: soldier=%u"), Soldier.SoldierId.Value);
				OutChunks.Reset();
				return;
			}
			Pose.ActiveOrderId = Soldier.ActiveOrderId;
			Pose.UpperYaw = FRotator::CompressAxisToShort(Soldier.MechanicalPose.bInitialized
				? Soldier.MechanicalPose.UpperYawDegrees : Soldier.FacingYawDegrees);
			Pose.LeftGunPitch = static_cast<int16>(FMath::RoundToInt(Soldier.MechanicalPose.GunPitchDegrees[0] * 100.0f));
			Pose.RightGunPitch = static_cast<int16>(FMath::RoundToInt(Soldier.MechanicalPose.GunPitchDegrees[1] * 100.0f));
			Pose.HoverBlendStartMilliseconds = Soldier.MechanicalPose.HoverBlendStartMilliseconds;
			Pose.HoverBlendFromWeight = Soldier.MechanicalPose.HoverBlendFromWeight;
			Pose.bHoverIdleTarget = Soldier.MechanicalPose.bHoverIdleTarget;
			Pose.VATClip = Soldier.VATPlayback.Clip;
			Pose.VATPhase = uint16(FMath::RoundToInt(FMath::Clamp(Soldier.VATPlayback.Phase, 0.0f, 1.0f) * 65535));
			Pose.VATRate = uint16(FMath::RoundToInt(FMath::Clamp(Soldier.VATPlayback.Rate, 0.0f, 255.0f) * 256));
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

bool UGuLiBattleAuthoritySubsystem::TryGetMoveResponseSample(
	FGuLiSoldierId Id, const FGuLiHybridObjectKey& Life, GuLiMoveLatency::FMovementSample& Out) const
{
	const auto* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
	if (!Index) return false;
	const auto& S = AuthorityState->Soldiers[*Index];
	if (S.HybridKey != Life) return false;
	Out.Location = S.Location; Out.Command = S.MoveTrace.Command; Out.Order = S.ActiveOrderId;
	Out.AcceptedAt = S.CommandAcceptedAt; Out.FirstMotionAt = S.FirstDisplacementAt;
	Out.bLocal = S.bLocalSimulation; Out.bAlive = S.IsAlive();
	return true;
}

bool UGuLiBattleAuthoritySubsystem::TryGetLocalSoldierPose(
	const FGuLiSoldierId SoldierId, FGuLiSoldierLocalPose& OutPose) const
{
	if (!AuthorityState || !GetWorld()) return false;
	const int32* Index = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
	if (!Index) return false;
	const auto& S = AuthorityState->Soldiers[*Index];
	const auto* Hybrid = GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	if (!S.HybridKey.IsValid() || !S.bLocalSimulation || S.bAutomaticAdvance
		|| !Hybrid || !Hybrid->HasSimulation(S.HybridKey)) return false;
	// Return only committed state. Rendering never integrates movement or issues a command.
	OutPose.Transform = FTransform(FRotator(0.0f, S.FacingYawDegrees, 0.0f), S.Location);
	OutPose.Velocity = S.Velocity;
	OutPose.Mechanical = S.MechanicalPose;
	OutPose.VAT = S.VATPlayback;
	OutPose.ActiveOrderId = S.ActiveOrderId;
	OutPose.AnimationAheadSeconds = FMath::Clamp(AuthorityState->FixedStepAccumulator,
		0.0, double(GuLiCommanderMassPrivate::FixedStepSeconds));
	// VAT/hover state is advanced by the Mass fixed step, not the object's independent clock.
	OutPose.ServerTimeSeconds = AuthorityState->SimulationSeconds + OutPose.AnimationAheadSeconds;
	OutPose.MovementTimeSeconds = S.LastMovementUpdateSimulationSeconds;
	OutPose.DisplacementRevision = S.DisplacementFrameFloor;
	return true;
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
		Body.Location = Soldier.Location; Body.Velocity = Soldier.Velocity;Body.MovementLayer=Soldier.MovementLayer;
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
		if (!Soldier.bEngineering && Soldier.bAllowAutomaticFire && Soldier.CanSimulate() && Soldier.WeaponProfiles) for (auto& Weapon : Soldier.Weapons)
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
        if (Soldier.HybridKey.IsValid() && !Soldier.bLocalSimulation) continue;
		Soldier.bAttackMoveHolding = Soldier.bAutomaticAdvance && Soldier.Weapons.ContainsByPredicate([](const auto& Weapon)
		{
			return Weapon.Attack.TargetId.IsValid() && (Weapon.Attack.StopReason == EGuLiCombatStopReason::Fired
				|| Weapon.Attack.StopReason == EGuLiCombatStopReason::Cooldown);
		});
	}
	// Do not hold Mass fragment references here: death may change an entity's archetype.
	// Mechanical aim belongs to a shared data-only animation model, never a skeletal component.
	const auto* AnimationData = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	// Evaluate every source/target at the same simulation time before calculating aim.
	for (auto& Soldier : AuthorityState->Soldiers)
	{
        if (Soldier.HybridKey.IsValid() && !Soldier.bLocalSimulation) continue;
		const auto* Definition = AnimationData ? AnimationData->FindSoldierDefinition(Soldier.UnitTypeId) : nullptr;
		if (Definition && Definition->VATDefinition)
			GuLiVATAnimation::Step(*Definition->VATDefinition, Soldier.Velocity, Soldier.FacingYawDegrees,
				Soldier.IsAlive(), GuLiCommanderSimulationTiming::StepSeconds, Soldier.VATPlayback);
		if (Soldier.CanAct() && Definition)
			GuLiMechanicalAnimation::StepHover(Definition->MechanicalAnimation, Soldier.SoldierId.Value,
				Soldier.Velocity.Size2D(), GuLiCommanderSimulationTiming::StepSeconds,
				AuthorityState->SimulationSeconds, Soldier.MechanicalPose);
	}
	for (auto& Soldier : AuthorityState->Soldiers)
	{
		const auto* Definition = AnimationData ? AnimationData->FindSoldierDefinition(Soldier.UnitTypeId) : nullptr;
		if (!Soldier.CanSimulate() || !Definition || (!Definition->VATDefinition && !Definition->MechanicalAnimation.IsEnabled())) continue;
		const auto& Config = Definition->MechanicalAnimation;
		FVector TargetPosition;
		const FVector* AimTarget = nullptr;
		const auto* Weapon = Soldier.Weapons.FindByPredicate([](const auto& W) { return W.SlotId == TEXT("BasicAttack"); });
		const int32* TargetIndex = Weapon ? AuthorityState->SoldierIndexById.Find(Weapon->Attack.TargetId.Value) : nullptr;
		if (TargetIndex && AuthorityState->Soldiers[*TargetIndex].IsPresent())
		{
			const auto& Target = AuthorityState->Soldiers[*TargetIndex];
			const FVector* Offset = AnimationData->FindAimOffset(Target.UnitTypeId);
			const auto* TargetDefinition = AnimationData->FindSoldierDefinition(Target.UnitTypeId);
			const FVector LocalAim = TargetDefinition ? GuLiMechanicalAnimation::HoverBodyTransform(
				TargetDefinition->MechanicalAnimation, Target.MechanicalPose).TransformPosition(Offset ? *Offset : FVector::ZeroVector)
				: (Offset ? *Offset : FVector::ZeroVector);
			TargetPosition = Target.Location + FRotator(0, Target.FacingYawDegrees, 0).RotateVector(LocalAim);
			AimTarget = &TargetPosition;
		}
		const float Dt = GuLiCommanderSimulationTiming::StepSeconds;
		if (Config.Model == EGuLiMechanicalModel::WarMachine && Soldier.FacingPolicy != EGuLiSoldierFacingPolicy::FixedSpawnYaw
			&& AimTarget && Soldier.Velocity.SizeSquared2D() < 1)
		{
			Soldier.FacingYawDegrees = FMath::FixedTurn(Soldier.FacingYawDegrees,
				(*AimTarget - Soldier.Location).Rotation().Yaw, Config.LowerTurnRate * GuLiMassMovementTuning::GetTurnRateScale() * Dt);
			if (auto* Mass = AuthorityState->MassEntitySubsystem.Get())
				if (Mass->GetEntityManager().IsEntityValid(Soldier.Entity))
					Mass->GetMutableEntityManager().GetFragmentDataChecked<FTransformFragment>(Soldier.Entity)
						.SetTransform(FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location));
		}
		if (Definition->VATDefinition)
			GuLiVATAnimation::StepAim(*Definition->VATDefinition, Soldier.VATPlayback,
				FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location, FVector(Definition->PresentationScale)),
				Definition->bConstructionOnly ? nullptr : AimTarget, Dt, Soldier.MechanicalPose, GuLiMassMovementTuning::GetTurnRateScale());
		else GuLiMechanicalAnimation::StepAim(Config, FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location),
			AimTarget, GetUnitMovementSpeed(Soldier.Team, Soldier.UnitTypeId), Dt, Soldier.MechanicalPose, GuLiMassMovementTuning::GetTurnRateScale());
	}

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
			if (const auto* Definition = AnimationData ? AnimationData->FindSoldierDefinition(Source.UnitTypeId) : nullptr;
				Definition && (Definition->VATDefinition || Definition->MechanicalAnimation.IsEnabled()) && Event.SourceSlotId == TEXT("BasicAttack"))
			{
				Request.MechanicalPose = Source.MechanicalPose;
				Request.bHasMechanicalPose = true;
				Request.MechanicalMuzzleIndex = Event.MuzzleIndex >= 0 ? uint8(Event.MuzzleIndex) : Source.NextMechanicalMuzzle;
				Request.VATPlayback = Source.VATPlayback;
				Request.bHasVATPose = Definition->VATDefinition != nullptr;
				Request.MechanicalPoseTimeSeconds = static_cast<float>(AuthorityState->SimulationSeconds);
				const FGuLiSoldierId SourceId = Source.SoldierId;
				Request.OnShotAccepted = [this, SourceId](uint8 Side, float Now)
				{
					if (!AuthorityState) return;
					const int32* Index = AuthorityState->SoldierIndexById.Find(SourceId.Value);
					if (!Index) return;
					auto& Unit = AuthorityState->Soldiers[*Index];
					const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
					const auto* Def = Data ? Data->FindSoldierDefinition(Unit.UnitTypeId) : nullptr;
					if (!Def) return;
					GuLiMechanicalAnimation::AcceptShot(Def->MechanicalAnimation, Unit.MechanicalPose, Side, Now);
					Unit.NextMechanicalMuzzle = Def->MechanicalAnimation.Model == EGuLiMechanicalModel::WarMachine ? 1-Side : 0;
				};
			}
			if (Event.ExecutorId == TEXT("GroundMachineGun"))
			{
				Request.Motion.Speed = Event.ProjectileSpeedCentimetersPerSecond;
				Request.Motion.MaximumLifetime = Event.ProjectileLifetimeSeconds;
				Request.Motion.SweepRadius = Event.ProjectileSweepRadiusCentimeters;
				Request.ProjectileSpreadAngleDegrees = Event.ProjectileSpreadAngleDegrees;
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
	OutDebug.bLocalSimulation = Soldier.bLocalSimulation;
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
		? FVector::Dist(Soldier.GroundLocation(), Soldier.FinalDestination.Location)
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

namespace
{
	bool HasBlockingSoldierSpawnOverlap(UWorld& World, const FVector& FootLocation, float Radius,
		const AActor* IgnoredConstructionSite = nullptr, EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground)
	{
		TArray<FOverlapResult> Overlaps;
		World.OverlapMultiByObjectType(Overlaps, FootLocation + FVector(0, 0, Radius + 20),
			FQuat::Identity, FCollisionObjectQueryParams::AllObjects, FCollisionShape::MakeSphere(Radius),
			FCollisionQueryParams(SCENE_QUERY_STAT(GuLiSoldierSpawnClearance), false, IgnoredConstructionSite));
		// Object overlap also reports initial penetrations. Resolve the required Pawn response explicitly
		// so an already-overlapping mesh cannot be admitted as a non-blocking channel-query touch.
		return Overlaps.ContainsByPredicate([Layer](const FOverlapResult& Overlap)
		{
			const auto* Component = Overlap.GetComponent();
            const auto Other=GuLiUnitMovement::LayerForActor(Overlap.GetActor());
			return (!Other.IsSet() || Other.GetValue()==Layer) && Component && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
		});
	}
}

bool UGuLiBattleAuthoritySubsystem::InstallGrantedSoldier(const EGuLiTeam Team, const uint16 UnitTypeId,
	const FVector& Location, FGuLiSoldierId& OutId, float InitialYaw,
	float InitialHealth)
{
    if (!bInstallingHybridGrant || !InstallingHybridOrdinal) return false;
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
	if (!Definition || !Definition->UsesMass() || !MassSubsystem) return false;
	const float SpawnRadius = Definition->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters);
	// Placement was validated by the source before committing the birth. A
	// receiver's delayed neighbours or rebuilding navigation cannot veto it.
	// The local nav reference is only a cache; preserve the committed transform.
	const FVector Ground=GuLiUnitMovement::GroundPosition(Location,Definition->AltitudeCentimeters);
	FNavLocation Projected(Ground), LocalNav;
	if (Navigation && NavData && ProjectPointToCommanderNavigation(*Navigation, *NavData, Ground,
		FVector(MemberAgentRadiusCentimeters, MemberAgentRadiusCentimeters, 5000.0f), LocalNav)
		&& FVector::DistSquared2D(Location, LocalNav.Location) <= FMath::Square(MemberAgentRadiusCentimeters)) Projected=LocalNav;
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	FMassArchetypeSharedFragmentValues SharedValues = MakeAuthoritySharedFragmentValues(
		EntityManager, GetUnitMovementSpeed(Team,UnitTypeId), SpawnRadius);
	const FMassArchetypeHandle SpawnArchetype=EntityManager.GetOrCreateSuitableArchetype(
		AuthorityState->RuntimeTuningEvenBaseArchetype,SharedValues.GetSharedFragmentBitSet(),SharedValues.GetConstSharedFragmentBitSet());
	if (!SpawnArchetype.IsValid()) return false;
	TArray<FMassEntityHandle> Handles;
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.BatchCreateEntities(
		SpawnArchetype, SharedValues, 1, Handles);
	if (Handles.Num() != 1)
	{
		if (!Handles.IsEmpty()) EntityManager.BatchDestroyEntities(Handles);
		return false;
	}
	FSoldierRuntime& Soldier = AuthorityState->Soldiers.AddDefaulted_GetRef();
	Soldier.Entity = Handles[0];
    Soldier.SoldierId=FGuLiSoldierId(InstallingHybridOrdinal?InstallingHybridOrdinal:AllocateNonZero(AuthorityState->NextSoldierId));
	Soldier.Team = Team;
	Soldier.Location = Location;
	Soldier.FacingYawDegrees = Soldier.FixedSpawnYawDegrees = FRotator::NormalizeAxis(InitialYaw);
	Soldier.LastValidNavLocation = Projected;
	Soldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;
	InitializeSoldierCombat(Soldier, *Definition, EffectiveRuntimeTuning, World->GetSubsystem<UGuLiArmySkillSubsystem>());
	if (InitialHealth > 0) Soldier.Health = FMath::Min(InitialHealth, Soldier.MaxHealth);
	if (Definition->bConstructionOnly) Soldier.bAllowAutomaticFire = false;
	EntityManager.GetFragmentDataChecked<FGuLiCommanderStateTreeFragment>(Soldier.Entity).Tree = Definition->StateTreeAsset;
	Soldier.AvoidanceRadiusCentimeters = SpawnRadius;
	AuthorityState->SoldierIndexById.Add(Soldier.SoldierId.Value, AuthorityState->Soldiers.Num() - 1);
	EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity).SetTransform(
		FTransform(FRotator(0, Soldier.FacingYawDegrees, 0), Soldier.Location));
	EntityManager.GetFragmentDataChecked<FAgentRadiusFragment>(Soldier.Entity).Radius = Soldier.AvoidanceRadiusCentimeters;
	FMassMoveTargetFragment& MoveTarget = EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
	MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
	MoveTarget.Center = Soldier.Location;
	MoveTarget.Forward = FRotator(0, Soldier.FacingYawDegrees, 0).Vector();
	FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(Soldier.Entity);
	Identity.SoldierId = Soldier.SoldierId;
	Identity.Team = Team;
    Identity.MovementLayer=Soldier.MovementLayer;Identity.AltitudeCentimeters=Soldier.AltitudeCentimeters;
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

int32 UGuLiBattleAuthoritySubsystem::GetTeamUnitCap() const
{
    const auto* Actors=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>():nullptr;
    const double* Limit=Actors?Actors->GetManifest().Numbers.Find(TEXT("population/group/army/limit")):nullptr;
    return Limit?int32(*Limit):0;
}

FIntPoint UGuLiBattleAuthoritySubsystem::GetTeamPopulation(EGuLiTeam Team) const
{
	if (const auto* H=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr)
	{
		FIntPoint Population(0,0);TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
		for (const auto& O:Objects) if (O.Team==Team && O.LifeState<2)
		{
			const auto* C=O.Capabilities.Find(TEXT("population"));if (!C || C->Data.IsEmpty()) continue;
			GuLiNet::ObjectRecord Record;GuLiNet::CapabilityState State;State.Version=C->Version;
			State.Data.assign(C->Data.GetData(),C->Data.GetData()+C->Data.Num());Record.Capabilities.emplace(GuLiNet::Population::Capability,std::move(State));
			GuLiNet::Population::Claim Claim;
			if (GuLiNet::Population::Read(Record,Claim) && Claim.Group=="army")
				(O.bCompleted && O.LifeState==1?Population.X:Population.Y)+=int32(Claim.Units);
		}
		return Population;
	}
	return FIntPoint::ZeroValue;
}
bool UGuLiBattleAuthoritySubsystem::PlanSummonPlacement(EGuLiTeam Team, FGuLiSoldierId CasterId,
	uint16 UnitTypeId, int32 Count, float Clearance, int32 OuterRings, int32 CandidatesPerRing,
	TArray<FVector>& PlannedLocations, FString& Error)
{
	using namespace GuLiCommanderMassPrivate;
	PlannedLocations.Reset();
	if (!IsInGameThread() || !IsAuthorityWorld() || !AuthorityState || !AuthorityState->bPopulationSpawned
		|| !GuLiCommanderProtocol::IsPlayableTeam(Team) || Count < 1 || Count > 32
		|| !FMath::IsFinite(Clearance) || Clearance < 0 || OuterRings < 1 || OuterRings > 3
		|| CandidatesPerRing < 1 || CandidatesPerRing > 36)
	{ Error = TEXT("Invalid summon context."); return false; }
	const int32* CasterIndex = AuthorityState->SoldierIndexById.Find(CasterId.Value);
	if (!CasterIndex || !AuthorityState->Soldiers[*CasterIndex].CanAct()
		|| AuthorityState->Soldiers[*CasterIndex].Team != Team)
	{ Error = TEXT("Caster is unavailable."); return false; }
	if (AuthorityState->Soldiers.Num() + Count > 10000)
	{ Error = TEXT("Global entity capacity cannot admit the complete summon batch."); return false; }
	UWorld* World = GetWorld();
	const auto* Data = World->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Unit = Data ? Data->FindSoldierDefinition(UnitTypeId) : nullptr;
	const auto* CasterDefinition = Data ? Data->FindSoldierDefinition(AuthorityState->Soldiers[*CasterIndex].UnitTypeId) : nullptr;
	if (!Unit || !Unit->UsesMass() || !Unit->bSummonOnly || Unit->bConstructionOnly || !CasterDefinition)
	{ Error = TEXT("Summon-only Mass definition is required."); return false; }
	// Everything needed from the caster is copied before any Soldiers.Add may reallocate it.
	const auto CasterLocation = AuthorityState->Soldiers[*CasterIndex].GroundLocation();
	const float CasterYaw = AuthorityState->Soldiers[*CasterIndex].FacingYawDegrees;
	const FTransform CasterTransform(FRotator(0, CasterYaw, 0), CasterLocation);
	const FBox Bounds = CasterDefinition->GetModelBoundsCentimeters();
	if (!Bounds.IsValid) { Error = TEXT("Caster gameplay bounds are unavailable."); return false; }
	const float Radius = Unit->GetMassAvoidanceRadius(MemberAgentRadiusCentimeters);
	const FVector Center(Bounds.GetCenter().X, Bounds.GetCenter().Y, 0);
	const FVector Extent(Bounds.GetExtent().X + Radius + Clearance, Bounds.GetExtent().Y + Radius + Clearance, 0);
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	auto* NavData = Navigation ? GetCommanderNavigationData(*Navigation) : nullptr;
	if (!NavData || !AuthorityState->MassEntitySubsystem.IsValid())
	{ Error = TEXT("Commander navigation is unavailable."); return false; }
	TArray<FVector> Locations;
	Locations.Reserve(Count);
	auto PerimeterPoint = [&](int32 Index, int32 Candidates, int32 Ring)
	{
		const FVector E = Extent + FVector(1,1,0) * (Ring * (2 * Radius + Clearance));
		float Along = float(Index) / Candidates * 4 * (E.X + E.Y);
		FVector Local;
		if (Along < 2 * E.X) Local = FVector(-E.X + Along, -E.Y, 0);
		else if ((Along -= 2 * E.X) < 2 * E.Y) Local = FVector(E.X, -E.Y + Along, 0);
		else if ((Along -= 2 * E.Y) < 2 * E.X) Local = FVector(E.X - Along, E.Y, 0);
		else { Along -= 2 * E.X; Local = FVector(-E.X, E.Y - Along, 0); }
		return CasterTransform.TransformPosition(Center + Local);
	};
	auto Admit = [&](const FVector& Desired)
	{
		FNavLocation Projected;
		if (!ProjectPointToCommanderNavigation(*Navigation, *NavData, Desired,
			FVector(MemberAgentRadiusCentimeters, MemberAgentRadiusCentimeters, 5000), Projected)
			|| FVector::DistSquared2D(Desired, Projected.Location) > FMath::Square(MemberAgentRadiusCentimeters)) return false;
		const auto Local = CasterTransform.InverseTransformPosition(Projected.Location) - Center;
		if (FMath::Abs(Local.X) + .01 < Extent.X && FMath::Abs(Local.Y) + .01 < Extent.Y) return false;
		for (const auto& Existing : AuthorityState->Soldiers)
			if (Existing.IsPresent() && Existing.MovementLayer==Unit->MovementLayer && FVector::DistSquared2D(Existing.Location, Projected.Location)
				< FMath::Square(Radius + Existing.AvoidanceRadiusCentimeters + Clearance)) return false;
		for (const auto& Accepted : Locations)
			if (FVector::DistSquared2D(Accepted, Projected.Location) < FMath::Square(2 * Radius + Clearance)) return false;
		if (HasBlockingSoldierSpawnOverlap(*World, Projected.Location, Radius, nullptr, Unit->MovementLayer)) return false;
		Locations.Add(GuLiUnitMovement::WorldPosition(Projected.Location,Unit->AltitudeCentimeters));
		return true;
	};
	for (int32 Index = 0; Index < Count; ++Index) Admit(PerimeterPoint(Index, Count, 0));
	for (int32 Ring = 1; Ring <= OuterRings && Locations.Num() < Count; ++Ring)
		for (int32 Index = 0; Index < CandidatesPerRing && Locations.Num() < Count; ++Index)
			Admit(PerimeterPoint(Index, CandidatesPerRing, Ring));
	if (Locations.Num() != Count)
	{ Error = TEXT("No space for the complete summon batch; cooldown is unchanged."); return false; }
	PlannedLocations=MoveTemp(Locations);
	return true;
}
void UGuLiBattleAuthoritySubsystem::RetireExpiredSoldiers()
{
	auto& Manager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	auto& Ledger = *GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	bool bRemoved = false;
	for (int32 Index = AuthorityState->Soldiers.Num() - 1; Index >= 0; --Index)
	{
		const auto& Soldier = AuthorityState->Soldiers[Index];
		if (Soldier.bEngineering || !Soldier.bWreckExpired) continue;
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
	if (!Index || AuthorityState->Soldiers[*Index].bEngineering || !AuthorityState->Soldiers[*Index].IsAlive()) return false;
	const auto& Soldier = AuthorityState->Soldiers[*Index]; Team = Soldier.Team; UnitTypeId = Soldier.UnitTypeId; Location = Soldier.Location;
	return true;
}
bool UGuLiBattleAuthoritySubsystem::CanSelectionReachMoveTarget(
	const FGuLiCommanderSelectionState& Selection, const FVector& Target,
	const FGuLiSharedMoveIntent* FrozenSlots, FString* Error) const
{
	using namespace GuLiCommanderMassPrivate;
    if (const auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady())
    {
        const auto S=G->Snapshot();
        for (const auto& Cohort:Selection.Cohorts) for (const auto Id:Cohort.MemberIds)
        {
            const auto* I=AuthorityState->SoldierIndexById.Find(Id.Value);if (!I) continue;
            const auto& U=AuthorityState->Soldiers[*I];if (!U.CanAct()) continue;
            const int32 Size=GuLiGrid::FootprintFromDiameter(U.AvoidanceRadiusCentimeters*2.);
            const auto* Slot=FrozenSlots?FrozenSlots->AssignedPoints.Find(Id.Value):nullptr;
            if (FrozenSlots && !Slot || !S->Fits(S->AnchorAt(Slot?Slot->Location:Target,Size),Size))
            {if (Error) *Error=TEXT("Destination has no complete grid footprint");return false;}
        }
        // Shared NavMesh planning resolves reachability under the world budget.
        // Do not repeat a synchronous full-route query for every frozen member at admission.
        return true;
    }
	auto& NavigationSystem = *FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const auto& NavigationData = *GetCommanderNavigationData(NavigationSystem);
	FNavLocation Destination;
	if (!FrozenSlots && !ProjectPointToCommanderNavigation(NavigationSystem, NavigationData, Target, FVector(10, 10, 50), Destination))
	{ if (Error) *Error = TEXT("Move target is outside navigation"); return false; }

	TSet<NavNodeRef> CheckedStarts;
	for (const auto& Cohort : Selection.Cohorts) for (const auto Id : Cohort.MemberIds)
	{
		const auto& Soldier = AuthorityState->Soldiers[AuthorityState->SoldierIndexById.FindChecked(Id.Value)];
		if (!Soldier.CanAct()) continue;
		// The command carries the formation's actual destinations. The click may
		// be over an obstacle even when every surrounding slot is reachable.
		if (FrozenSlots)
		{
			const auto* Slot = FrozenSlots->AssignedPoints.Find(Id.Value);
			if (!Slot || !ProjectPointToCommanderNavigation(NavigationSystem, NavigationData,
				Slot->Location, FVector(10, 10, 50), Destination))
			{ if (Error) *Error = FString::Printf(TEXT("Frozen move slot is outside navigation: unit=%u"), Id.Value); return false; }
		}
		else if (CheckedStarts.Contains(Soldier.LastValidNavLocation.NodeRef)) continue;
		FPathFindingQuery Query(nullptr, NavigationData, Soldier.LastValidNavLocation.Location,
			Destination.Location, NavigationData.GetDefaultQueryFilter());
		Query.SetAllowPartialPaths(false);
		if (!NavigationSystem.TestPathSync(Query, EPathFindingMode::Regular))
		{
			if (Error) *Error = FString::Printf(TEXT("No complete path: unit=%u start=%s destination=%s"),
				Id.Value, *Soldier.LastValidNavLocation.Location.ToString(), *Destination.Location.ToString());
			return false;
		}
		CheckedStarts.Add(Soldier.LastValidNavLocation.NodeRef);
	}
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
				Job->bCommitReady=false;
				for (auto& Route : Job->RouteTasks) Route.MemberPlanIndices.Remove(M);
				Job->RouteTasks.RemoveAll([](const auto& Route){ return Route.MemberPlanIndices.IsEmpty(); });
				if (Job->Stage == EMovePlanningStage::ReadyToCommit || !Job->PreparedFormations.IsEmpty()) Job->Stage = EMovePlanningStage::ReconcileReservations;
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
        if (auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady())
            Soldier.GridMotion.Stop(*Grid,Soldier.Location,AuthorityState->SimulationSeconds,EGuLiGridMoveEnd::Stopped);
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
	if (TeamJobs>=128) return {};
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!GameState || !GameState->GetMatchEpoch()) return {};
	auto Pointer = MakeUnique<FMovePlanningJob>(); auto& Job = *Pointer;
	Job.Handle = {AuthorityState->NextPlanId++, GameState->GetMatchEpoch()}; Job.Progress.Handle = Job.Handle;
	Job.bAutomatic = true; Job.Team = Team; Job.Request.Target = Destination;
	Job.MoveIntent=CreateSharedMoveIntent(Destination,uint32(Job.Handle.Value));
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
		Job.MoveIntent->UnassignedMembers.Add(Id.Value);
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


bool UGuLiBattleAuthoritySubsystem::QueryUnitSkillRuntime(FGuLiSoldierId SoldierId, const UGuLiCommanderSkillCatalog& Catalog, FGuLiActiveSkillRuntime& OutRuntime) const
{
	const int32* Index = AuthorityState ? AuthorityState->SoldierIndexById.Find(SoldierId.Value) : nullptr;
	if (!Index) return false;
	const auto& Soldier = AuthorityState->Soldiers[*Index];
	const auto* Definition = Catalog.FindAvailableUnitSkill(*GetWorld(),Soldier.Team,Soldier.UnitTypeId);
	if (!Definition) return false;
	OutRuntime = Soldier.ActiveSkill;
	OutRuntime.SkillId = Definition->SkillId;
    const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const double SkillSeconds=Hybrid && Soldier.HybridKey.IsValid()
        ?double(Hybrid->GetObjectLogicalTick(Soldier.HybridKey))/GuLiNet::TicksPerSecond:AuthorityState->SimulationSeconds;
	OutRuntime.ReadyAt = GetWorld()->GetTimeSeconds() + FMath::Max(0., OutRuntime.ReadyAt - SkillSeconds);
	return true;
}

FGuLiActiveSkillUnitResult UGuLiBattleAuthoritySubsystem::ExecuteHybridUnitSkill(const FGuLiHybridObjectKey& Key,
    FGuid Request,bool HasGround,FVector Ground,int32 Allocation,FGuid Batch)
{
    FGuLiActiveSkillUnitResult Result;auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState O;
    if (!H || !H->HasSimulation(Key) || !H->CanPublishFacts() || !H->GetObject(Key,O) || !AuthorityState) return Result;
    const auto* Index=AuthorityState->SoldierIndexById.Find(O.Ordinal);if (!Index) return Result;
    const auto& Soldier=AuthorityState->Soldiers[*Index];Result.SoldierId=Soldier.SoldierId;
    if (Soldier.HybridKey!=Key || !Soldier.bLocalSimulation) return Result;
    const auto* Catalog=GetDefault<UGuLiCommanderSkillSettings>()->Catalog.LoadSynchronous();
    const auto* Definition=Catalog?Catalog->FindAvailableUnitSkill(*GetWorld(),Soldier.Team,Soldier.UnitTypeId):nullptr;
    FGuLiUnitSkillCaster Caster;Caster.bEligible=Soldier.CanAct() && Soldier.IsPresent();Caster.bHasGroundPoint=HasGround;
    if (Definition && !Definition->RangeSourceSlot.IsNone())
    {
        const auto* P=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>()->FindResolvedSkill(Soldier.Team,Soldier.UnitTypeId,Definition->RangeSourceSlot);
        if (P && P->bUnlocked && P->bEquipped) Caster.ResolvedSourceRange=P->RangeCentimeters;
    }
    auto& C=Caster.Context;C.ExecutionWorld=GetWorld();C.ExecutionTeam=Soldier.Team;C.ControllerIdentity=O.Controller;
    C.SoldierId=Soldier.SoldierId;C.Source=MakeSoldierTargetHandle(C.SoldierId);C.UnitTypeId=Soldier.UnitTypeId;
    C.SourceTransform=FTransform(FRotator(0,Soldier.FacingYawDegrees,0),Soldier.Location);C.MechanicalPose=Soldier.MechanicalPose;
    C.bHasMechanicalPose=Soldier.MechanicalPose.bInitialized;C.GroundPoint=Ground;C.RequestId=Request;C.SkillId=Definition?Definition->SkillId:NAME_None;
    C.Level=Soldier.ActiveSkill.Level;C.ShotOrdinal=Soldier.ActiveSkill.SuccessfulCasts;C.RemainingGuidanceCapacity=Allocation;C.GuidanceBatchId=Batch;
    auto Runtime=Soldier.ActiveSkill;
    Result=GuLiUnitSkillExecution::Execute(Definition,Caster,Runtime,H->GetLogicalSeconds(),H->GetLogicalSeconds(),
        [Definition](const auto& Context,const UDataAsset* Configuration){return Definition->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->Execute(Context,Configuration);});
    if (const auto* Current=AuthorityState->SoldierIndexById.Find(C.SoldierId.Value)) AuthorityState->Soldiers[*Current].ActiveSkill=Runtime;
    return Result;
}

FString UGuLiBattleAuthoritySubsystem::GetMoveResponseDiagnostics() const
{
	using namespace GuLiCommanderMassPrivate;
	auto Root=MakeShared<FJsonObject>();
	if (!AuthorityState) return TEXT("{}");
	const auto& A=*AuthorityState;
	Root->SetStringField(TEXT("world"),GetWorld()->GetPathName());
	Root->SetNumberField(TEXT("clock"),FPlatformTime::Seconds());
	Root->SetNumberField(TEXT("frame"),double(GFrameCounter));
	Root->SetNumberField(TEXT("sim_tick"),A.ServerSimTick);
	Root->SetNumberField(TEXT("simulation_seconds"),A.SimulationSeconds);
	Root->SetNumberField(TEXT("epoch"),A.AuthorityEpoch);
	Root->SetNumberField(TEXT("nav_generation"),A.NavigationGeneration);
	Root->SetNumberField(TEXT("planning_ms"),A.PlanningBudget.Elapsed()*1000.);
	Root->SetNumberField(TEXT("positions"),MovePositionQueriesPerFrame-A.PlanningBudget.Projections);
	Root->SetNumberField(TEXT("paths"),MovePathQueriesPerFrame-A.PlanningBudget.Paths);
	Root->SetNumberField(TEXT("commit_ms"),A.CommitBudget.Elapsed()*1000.);
	Root->SetNumberField(TEXT("commit_members"),FMath::Max(25,MoveCommitMembersPerFrame)-A.CommitBudget.Members);
	Root->SetNumberField(TEXT("query_overruns"),double(A.PlanningBudget.QueryOverruns));
	Root->SetNumberField(TEXT("slice_overruns"),A.PlanningBudget.FrameSliceOverruns);
	Root->SetNumberField(TEXT("recovery_pending"),A.RecoveryWork.Num());
	Root->SetNumberField(TEXT("recovery_failures"),double(A.RecoveryFailures));
	Root->SetNumberField(TEXT("recovery_detection_hz"),GuLiCommanderSimulationTiming::RateHz);
	Root->SetNumberField(TEXT("recovery_detection_steps"),double(A.RecoveryDetectionSteps));
	Root->SetNumberField(TEXT("recovery_queries"),double(A.RecoveryQueries));
	Root->SetNumberField(TEXT("recovery_repairs"),double(A.RecoveryRepairs));
	Root->SetNumberField(TEXT("recovery_rebinds"),double(A.RecoveryRebinds));
	const auto* GroundGrid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();
	const bool UsesGrid=GroundGrid && GroundGrid->IsReady();
	Root->SetStringField(TEXT("movement_pipeline"),UsesGrid?TEXT("square-slots-shared-navmesh"):TEXT("immediate-shared-navmesh"));
	if (UsesGrid)
	{
		Root->SetNumberField(TEXT("grid_searches"),0.0);
		Root->SetNumberField(TEXT("grid_cache_hits"),0.0);
		Root->SetNumberField(TEXT("grid_capacity_waits"),0.0);
		Root->SetNumberField(TEXT("grid_route_bytes"),0.0);
	}
	const auto* Skills=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	Root->SetNumberField(TEXT("loadout_revision"),Skills?Skills->GetLoadoutRevision():0);
	Root->SetNumberField(TEXT("combat_revision"),A.CommittedCombatRevision);
	Root->SetNumberField(TEXT("resolved_profiles"),Skills?Skills->GetResolvedSkills().Num():0);
	auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const auto* NavigationData=Navigation?GetCommanderNavigationData(*Navigation):nullptr;
	Root->SetNumberField(TEXT("shared_queries"),double(A.SharedRoutes.Queries));
	Root->SetNumberField(TEXT("shared_failures"),double(A.SharedRoutes.FailedQueries));
	Root->SetNumberField(TEXT("shared_hits"),double(A.SharedRoutes.CacheHits));
	Root->SetNumberField(TEXT("shared_invalidations"),double(A.SharedRoutes.Invalidations));
	Root->SetNumberField(TEXT("shared_cache_bytes"),double(A.SharedRoutes.CachedBytes));
	Root->SetNumberField(TEXT("shared_query_cpu_ms"),A.SharedRoutes.QueryCpuSeconds*1000.);
	Root->SetNumberField(TEXT("reservations"),A.MoveReservations.Entries.Num());
	TArray<TSharedPtr<FJsonValue>> Routes;
	for (const auto& R : A.SharedRoutes.Routes)
	{
		auto J=MakeShared<FJsonObject>(); J->SetNumberField(TEXT("handle"),double(R->Handle));
		J->SetNumberField(TEXT("state"),uint8(R->State)); J->SetBoolField(TEXT("partial"),R->bPartial);
		J->SetBoolField(TEXT("automatic"),R->bAutomatic); J->SetNumberField(TEXT("polygons"),R->PolygonIndex.Num());
		J->SetNumberField(TEXT("queued_at"),R->QueuedAt); J->SetNumberField(TEXT("ready_at"),R->ReadyAt);
		Routes.Add(MakeShared<FJsonValueObject>(J));
	}
	Root->SetArrayField(TEXT("shared_routes"),Routes);
	int32 EndpointMismatches=0,FinishedQueued=0;
	TArray<TSharedPtr<FJsonValue>> Soldiers,Plans;
	auto Point=[](const FVector& V) { TArray<TSharedPtr<FJsonValue>> R; R.Add(MakeShared<FJsonValueNumber>(V.X)); R.Add(MakeShared<FJsonValueNumber>(V.Y)); R.Add(MakeShared<FJsonValueNumber>(V.Z)); return R; };
	for (const auto& Unit : A.Soldiers)
	{
		auto S=MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("id"),Unit.SoldierId.Value); S->SetNumberField(TEXT("team"),uint8(Unit.Team));
		S->SetNumberField(TEXT("unit_type"),Unit.UnitTypeId);
		S->SetBoolField(TEXT("engineering"),Unit.bEngineering);
		S->SetNumberField(TEXT("normal_speed"),GetUnitMovementSpeed(Unit.Team,Unit.UnitTypeId));
		S->SetBoolField(TEXT("ground_mech_yielding"),Unit.bGroundMechYielding);
		S->SetBoolField(TEXT("alive"),Unit.IsAlive()); S->SetBoolField(TEXT("can_act"),Unit.CanAct());
		S->SetBoolField(TEXT("local_simulation"),Unit.bLocalSimulation);
		S->SetBoolField(TEXT("can_simulate"),Unit.CanSimulate());
		S->SetBoolField(TEXT("present"),Unit.IsPresent()); S->SetBoolField(TEXT("phased"),Unit.bPhased);
		S->SetBoolField(TEXT("actions_locked"),Unit.bExternalActionsLocked);
		S->SetBoolField(TEXT("automatic_fire"),Unit.bAllowAutomaticFire);
		S->SetNumberField(TEXT("health"),Unit.Health);
		S->SetBoolField(TEXT("nav_valid"),NavigationData && NavigationData->IsNodeRefValid(Unit.LastValidNavLocation.NodeRef));
		TArray<TSharedPtr<FJsonValue>> Weapons;
		for (const auto& Weapon:Unit.Weapons)
		{
			auto W=MakeShared<FJsonObject>(); W->SetStringField(TEXT("slot"),Weapon.SlotId.ToString());
			W->SetStringField(TEXT("stop"),GuLiSoldierCombat::LexToString(Weapon.Attack.StopReason));
			W->SetNumberField(TEXT("target"),Weapon.Attack.TargetId.Value);
			W->SetNumberField(TEXT("shots"),double(Weapon.Attack.ShotsFired));
			W->SetNumberField(TEXT("next_fire"),Weapon.Attack.NextFireSeconds);
			const auto* Profile=Unit.WeaponProfiles && Unit.WeaponProfiles->IsValidIndex(Weapon.ProfileIndex)
				? &(*Unit.WeaponProfiles)[Weapon.ProfileIndex]:nullptr;
			W->SetBoolField(TEXT("profile_valid"),Profile!=nullptr);
			if (Profile)
			{
				W->SetNumberField(TEXT("range_cm"),Profile->RangeCentimeters);
				W->SetNumberField(TEXT("damage"),Profile->Damage);
				W->SetNumberField(TEXT("rate"),Profile->AttackRatePerSecond);
				W->SetBoolField(TEXT("unlocked"),Profile->bUnlocked); W->SetBoolField(TEXT("equipped"),Profile->bEquipped);
				W->SetNumberField(TEXT("trigger"),uint8(Profile->TriggerMode));
				W->SetStringField(TEXT("executor"),Profile->ExecutorId.ToString());
			}
			Weapons.Add(MakeShared<FJsonValueObject>(W));
		}
		S->SetArrayField(TEXT("weapons"),Weapons);
		S->SetNumberField(TEXT("order"),Unit.ActiveOrderId); S->SetNumberField(TEXT("revision"),Unit.StateRevision);
		S->SetNumberField(TEXT("task_version"),double(Unit.TaskGeneration)); S->SetBoolField(TEXT("automatic"),Unit.bAutomaticAdvance);
		S->SetNumberField(TEXT("nav_state"),uint8(Unit.NavigationState)); S->SetBoolField(TEXT("budget_pending"),Unit.bNavigationBudgetPending);
		S->SetNumberField(TEXT("no_progress"),Unit.NoProgressSeconds); S->SetNumberField(TEXT("radius"),Unit.AvoidanceRadiusCentimeters);
		S->SetBoolField(TEXT("nav_repair_pending"),A.RecoveryWork.Contains(Unit.SoldierId.Value));
		S->SetArrayField(TEXT("position"),Point(Unit.Location)); S->SetArrayField(TEXT("velocity"),Point(Unit.Velocity));
		S->SetArrayField(TEXT("legal_position"),Point(Unit.LastValidNavLocation.Location));
		S->SetNumberField(TEXT("command"),Unit.MoveTrace.Command);
		S->SetNumberField(TEXT("accepted_at"),Unit.CommandAcceptedAt); S->SetNumberField(TEXT("direction_at"),Unit.DirectionAppliedAt);
		S->SetNumberField(TEXT("displacement_at"),Unit.FirstDisplacementAt);
		S->SetArrayField(TEXT("movement_target"),Point(Unit.FinalDestination.Location));
		S->SetArrayField(TEXT("waypoint"),Point(Unit.CurrentNavigationWaypoint));
        S->SetNumberField(TEXT("movement_layer"),uint8(Unit.MovementLayer));
        S->SetNumberField(TEXT("altitude_cm"),Unit.AltitudeCentimeters);
        S->SetArrayField(TEXT("ground_position"),Point(Unit.GroundLocation()));
		if (UsesGrid)
		{
			S->SetNumberField(TEXT("grid_footprint"),GuLiGrid::FootprintFromDiameter(Unit.AvoidanceRadiusCentimeters*2.));
		}
		S->SetBoolField(TEXT("has_dock"),Unit.bHasDockTarget); S->SetNumberField(TEXT("route"),Unit.SharedRoute?double(Unit.SharedRoute->Handle):0);
		S->SetNumberField(TEXT("hex_points"),Unit.MoveIntent?Unit.MoveIntent->Points.Num():0);
		S->SetNumberField(TEXT("slot_pitch"),Unit.MoveIntent?Unit.MoveIntent->Pitch:0);
		S->SetNumberField(TEXT("slot_members"),Unit.MoveIntent?Unit.MoveIntent->WantedPoints:0);
		S->SetNumberField(TEXT("slot_assignments"),Unit.MoveIntent?Unit.MoveIntent->AssignedPoints.Num():0);
		if (const auto* E=A.ActiveEndpoints.Find(Unit.SoldierId.Value))
		{
			S->SetArrayField(TEXT("start"),Point(E->CommandStart)); S->SetArrayField(TEXT("end"),Point(E->FinalDestination));
			EndpointMismatches+=!Unit.CanAct() || !Unit.bHasFinalDestination || Unit.ActiveOrderId!=E->ActiveOrderId
				|| !(Unit.MoveIntent?Unit.MoveIntent->Click:Unit.FinalDestination.Location).Equals(E->FinalDestination,.01) || !Unit.CommandStartLocation.Equals(E->CommandStart,.01);
		}
		if (const auto* Reservation=A.MoveReservations.Entries.Find(Unit.SoldierId.Value)) S->SetArrayField(TEXT("reservation"),Point(Reservation->Location));
		Soldiers.Add(MakeShared<FJsonValueObject>(S));
	}
	for (const auto& Job : A.MovePlanningJobs) if (Job)
	{
		auto P=MakeShared<FJsonObject>(); P->SetNumberField(TEXT("plan"),double(Job->Handle.Value));
		P->SetNumberField(TEXT("command"),Job->SourceCommandId); P->SetNumberField(TEXT("execution"),Job->Request.ClientCommandId);
		P->SetNumberField(TEXT("batch"),Job->SharedBatchOrderId); P->SetBoolField(TEXT("automatic"),Job->bAutomatic);
		P->SetNumberField(TEXT("stage"),uint8(Job->Stage)); P->SetNumberField(TEXT("accepted"),Job->TotalAccepted);
		P->SetNumberField(TEXT("invalidated"),double(Job->InvalidatedItems)); P->SetNumberField(TEXT("routes"),Job->RouteTasks.Num());
		P->SetNumberField(TEXT("age_ms"),(FPlatformTime::Seconds()-Job->PlanningStartedAt)*1000.);
		for (const auto& Route : Job->RouteTasks) for (int32 I : Route.MemberPlanIndices)
			FinishedQueued+=Job->FinishedIds.Contains(Job->Members[I].SoldierId.Value) ? 1 : 0;
		Plans.Add(MakeShared<FJsonValueObject>(P));
	}
	Root->SetNumberField(TEXT("endpoint_mismatches"),EndpointMismatches);
	Root->SetNumberField(TEXT("finished_members_queued"),FinishedQueued);
	Root->SetArrayField(TEXT("soldiers"),Soldiers); Root->SetArrayField(TEXT("plans"),Plans);
	FString Result; const auto Writer=TJsonWriterFactory<>::Create(&Result); FJsonSerializer::Serialize(Root,Writer); return Result;
}

#include "Commander/Mass/GuLiHybridMassAdapter.inl"


FGuLiSoldierId UGuLiBattleAuthoritySubsystem::AttachEngineeringMass(ACharacter& Carrier,const FGuLiHybridObjectState& Record,uint16 UnitType)
{
    using namespace GuLiCommanderMassPrivate;
    if (!AuthorityState || !Record.Key.IsValid() || !Record.Ordinal || Record.Ordinal>=0x40000000u
        || (Record.Key.Kind!=EGuLiHybridObjectKind::Miner && Record.Key.Kind!=EGuLiHybridObjectKind::Engineer)
        || !EnsureAuthorityArchetypes()) return {};
    // Internal execution identity only. The original Hybrid key/ordinal and Actor task namespace remain authoritative.
    const FGuLiSoldierId Id((Record.Key.Kind==EGuLiHybridObjectKind::Miner?0x80000000u:0xc0000000u)|Record.Ordinal);
    if (const auto* Existing=AuthorityState->SoldierIndexById.Find(Id.Value))
        return AuthorityState->Soldiers[*Existing].HybridKey==Record.Key?Id:FGuLiSoldierId{};
    if (AuthorityState->Soldiers.Num()>=10000) return {};
    auto& Manager=AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
    const float Radius=Carrier.GetCapsuleComponent()->GetScaledCapsuleRadius();
    auto Shared=MakeAuthoritySharedFragmentValues(Manager,GetUnitMovementSpeed(Record.Team,UnitType),Radius);
    const auto Archetype=Manager.GetOrCreateSuitableArchetype(AuthorityState->RuntimeTuningEvenBaseArchetype,
        Shared.GetSharedFragmentBitSet(),Shared.GetConstSharedFragmentBitSet());
    TArray<FMassEntityHandle> Handles;
    const auto Creation=Manager.BatchCreateEntities(Archetype,Shared,1,Handles);
    if (Handles.Num()!=1) return {};
    auto& S=AuthorityState->Soldiers.AddDefaulted_GetRef();
    S.Entity=Handles[0];S.SoldierId=Id;S.HybridKey=Record.Key;S.Team=Record.Team;S.UnitTypeId=UnitType;
    S.bEngineering=true;S.EngineeringCarrier=&Carrier;S.bLocalSimulation=UGuLiHybridActorComponent::HasSimulation(&Carrier);
    S.Location=Carrier.GetNavAgentLocation();S.FacingYawDegrees=Carrier.GetActorRotation().Yaw;
    if (const auto* Definition=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>()->FindSoldierDefinition(UnitType))
    {S.MovementLayer=Definition->MovementLayer;S.AltitudeCentimeters=Definition->AltitudeCentimeters;}
    S.LastValidNavLocation=FNavLocation(S.GroundLocation());S.AvoidanceRadiusCentimeters=Radius;
    S.Health=Record.Health.Health;S.MaxHealth=Record.Health.MaxHealth;S.bAllowAutomaticFire=false;
    if (auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady())
    {
        // Native navigation binds the actual position through the existing repair queue.
        S.GridMotion.Bind(*Grid,Record.Key,Record.SimulationOwner,Record.OwnerEpoch,uint8(Record.Team),S.Location,
            GuLiGrid::FootprintFromDiameter(2.*Radius),S.MovementLayer);
    }
    AuthorityState->SoldierIndexById.Add(Id.Value,AuthorityState->Soldiers.Num()-1);
    Manager.GetFragmentDataChecked<FTransformFragment>(S.Entity).SetTransform(FTransform(FRotator(0,S.FacingYawDegrees,0),S.Location));
    Manager.GetFragmentDataChecked<FAgentRadiusFragment>(S.Entity).Radius=Radius;
    auto& Identity=Manager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(S.Entity);Identity.SoldierId=Id;Identity.Team=Record.Team;
    Identity.bEngineering=S.bEngineering;Identity.MovementLayer=S.MovementLayer;Identity.AltitudeCentimeters=S.AltitudeCentimeters;
    auto& Health=Manager.GetFragmentDataChecked<FGuLiMassHealthFragment>(S.Entity);Health.Health=S.Health;Health.bDead=S.Health<=0;
    auto& Stats=Manager.GetFragmentDataChecked<FGuLiMassSoldierStatsFragment>(S.Entity);Stats.MaxHealth=S.MaxHealth;Stats.Defense=0;
    auto& Move=Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity);
    Move.CreateNewAction(EMassMovementAction::Stand,*GetWorld());Move.Center=S.Location;Move.Forward=Carrier.GetActorForwardVector();
    Manager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(S.Entity).WorldTarget=S.Location;
    AuthorityState->bForceManualAvoidanceRefresh=true;
    return Id;
}
void UGuLiBattleAuthoritySubsystem::DetachEngineeringMass(FGuLiSoldierId Id)
{
    if (!AuthorityState) return;
    const auto* Found=AuthorityState->SoldierIndexById.Find(Id.Value);if (!Found) return;
    const int32 Index=*Found;const auto& S=AuthorityState->Soldiers[Index];if (!S.bEngineering) return;
    if (auto* Mass=AuthorityState->MassEntitySubsystem.Get())
    {
        auto& Manager=Mass->GetMutableEntityManager();
        if (Manager.IsEntityValid(S.Entity)) Manager.DestroyEntity(S.Entity);
    }
    AuthorityState->SoldierIndexById.Remove(Id.Value);AuthorityState->Soldiers.RemoveAtSwap(Index,1,EAllowShrinking::No);
    if (AuthorityState->Soldiers.IsValidIndex(Index)) AuthorityState->SoldierIndexById.FindChecked(AuthorityState->Soldiers[Index].SoldierId.Value)=Index;
    AuthorityState->RecoveryWork.Remove(Id.Value);AuthorityState->ImmediateMoveIds.Remove(Id.Value);
    AuthorityState->bForceManualAvoidanceRefresh=true;
}
bool UGuLiBattleAuthoritySubsystem::MoveEngineeringMass(FGuLiSoldierId Id,const FVector& Target,float AcceptanceRadius,float BlockedSeconds)
{
    using namespace GuLiCommanderMassPrivate;
    const auto* Index=AuthorityState?AuthorityState->SoldierIndexById.Find(Id.Value):nullptr;if (!Index || Target.ContainsNaN()) return false;
    auto& S=AuthorityState->Soldiers[*Index];
    if (!S.bEngineering || !S.EngineeringCarrier.IsValid() || !UGuLiHybridActorComponent::HasSimulation(S.EngineeringCarrier.Get())) return false;
    S.bLocalSimulation=true;S.ActiveOrderId=AllocateNonZero(AuthorityState->NextBatchOrderId);S.GridOrderId=0;
    S.ArrivalAcceptance=FMath::Max(20.f,AcceptanceRadius);S.LastCompletedOrderId=S.LastFailedOrderId=0;
    S.CommandAcceptedAt=FPlatformTime::Seconds();S.DirectionAppliedAt=S.FirstDisplacementAt=0;
    S.MoveTrace={};
    S.CommandStartLocation=S.Location;S.FinalDestination=FNavLocation(Target);S.bHasFinalDestination=S.bHasDockTarget=true;
    S.MoveIntent=MakeShared<FGuLiSharedMoveIntent>();S.MoveIntent->Click=Target;
    S.MoveIntent->Goal=AuthorityState->SharedRoutes.Goal(Target);S.MoveIntent->AssignedPoints.Add(Id.Value,S.FinalDestination);
    S.MoveIntent->GridSizes.Add(Id.Value,GuLiGrid::FootprintFromDiameter(S.AvoidanceRadiusCentimeters*2.));
    S.MoveIntent->MovementLayers.Add(Id.Value,S.MovementLayer);
    S.MoveIntent->bGridSlotsReady=S.MoveIntent->bMetadataReady=true;
    // A work leg/replacement command owns a new destination. Retaining the previous
    // goal makes the ready-route fast path keep following the old corridor.
    S.RouteGoal=S.MoveIntent->Goal;S.SharedRoute.Reset();S.ActiveNavigation.Reset();
    S.RouteCursor=0;S.DirectDock={};S.FinalDestinationNavigationGeneration=0;
    S.PersonalPathPoints.Reset();S.PersonalPathPointIndex=0;S.PersonalPathRetries=0;S.NextPersonalPathQuerySeconds=0;
    S.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();S.LastProgressPathPointIndex=INDEX_NONE;
    S.bNavigationBudgetPending=false;S.bAutomaticAdvance=false;S.bAttackMoveHolding=false;
    S.CurrentNavigationWaypoint=Target;S.LastMovementUpdateSimulationSeconds=AuthorityState->SimulationSeconds;
    S.ContactObstacle=0;S.BypassSide=0;S.ContactClearSeconds=0;
    S.NavigationState=EGuLiSoldierNavigationState::Normal;S.NavigationFailure=EGuLiSoldierNavigationFailure::None;
    S.NoProgressSeconds=FMath::Max(0.f,BlockedSeconds);S.ConsecutiveSurfaceFailures=S.TotalSurfaceFailures=0;
    S.FailureSimulationSeconds=0;S.bForceMovementUpdate=true;++S.StateRevision;
    auto& Manager=AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
    auto& Order=Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(S.Entity);Order.ActiveOrderId=S.ActiveOrderId;Order.OrderRevision=S.StateRevision;Order.bHasMoveTarget=true;Order.FormationTarget=Target;
    auto& Move=Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity);Move.CreateNewAction(EMassMovementAction::Move,*GetWorld());Move.Center=Target;
    Move.IntentAtGoal=EMassMovementAction::Stand;Move.DesiredSpeed=FMassInt16Real(GetUnitMovementSpeed(S.Team,S.UnitTypeId));
    Manager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(S.Entity).Value=FVector::ZeroVector;
    Manager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(S.Entity).WorldTarget=Target;
    AuthorityState->bForceManualAvoidanceRefresh=true;
    return true;
}
void UGuLiBattleAuthoritySubsystem::SetEngineeringMoveResponseCommand(FGuLiSoldierId Id, uint32 CommandId)
{
    const auto* Index=AuthorityState ? AuthorityState->SoldierIndexById.Find(Id.Value) : nullptr;
    if (Index && AuthorityState->Soldiers[*Index].bEngineering)
        AuthorityState->Soldiers[*Index].MoveTrace.Command=CommandId;
}
bool UGuLiBattleAuthoritySubsystem::CaptureEngineeringMass(FGuLiSoldierId Id,FGuLiGridMotion& Motion) const
{
    const auto* Index=AuthorityState?AuthorityState->SoldierIndexById.Find(Id.Value):nullptr;if (!Index) return false;
    const auto& S=AuthorityState->Soldiers[*Index];if (!S.bEngineering) return false;
    Motion=S.GridMotion;Motion.State.BlockedSeconds=S.NoProgressSeconds;return true;
}
void UGuLiBattleAuthoritySubsystem::QueueEngineeringPosePublish(TFunction<void()>&& Publish)
{
    if (AuthorityState && Publish) AuthorityState->EngineeringPosePublishes.Add(MoveTemp(Publish));
}

void UGuLiBattleAuthoritySubsystem::SyncEngineeringMass(FGuLiSoldierId Id)
{
    const auto* Index=AuthorityState?AuthorityState->SoldierIndexById.Find(Id.Value):nullptr;if (!Index) return;
    auto& S=AuthorityState->Soldiers[*Index];if (!S.bEngineering) return;
    auto* Carrier=S.EngineeringCarrier.Get();const auto* Binding=Carrier?UGuLiHybridActorComponent::Find(Carrier):nullptr;
    const auto* Control=Carrier?Carrier->FindComponentByClass<UGuLiExternalUnitControlComponent>():nullptr;
    if (!Carrier || !Binding || !Control)
    {
        if (S.bLocalSimulation || !S.bPhased) {S.bLocalSimulation=false;S.bPhased=true;++S.StateRevision;}
        return;
    }
    const auto& Record=Binding->GetState();const bool Local=UGuLiHybridActorComponent::HasSimulation(Carrier);
    const bool Phase=Control->IsPhased(),Locked=Control->AreActionsLocked();
    const float Health=Record.LifeState==1?Record.Health.Health:0;
    if (S.bLocalSimulation!=Local || S.bPhased!=Phase || S.bExternalActionsLocked!=Locked || S.Health!=Health)
    {S.bLocalSimulation=Local;S.bPhased=Phase;S.bExternalActionsLocked=Locked;S.Health=Health;++S.StateRevision;}
    const FVector Foot=Carrier->GetNavAgentLocation();
    if (!Foot.Equals(S.Location,.01))
    {
        S.Location=Foot;S.FacingYawDegrees=Carrier->GetActorRotation().Yaw;S.LastValidNavLocation=FNavLocation(S.GroundLocation());

        ++S.StateRevision;
    }
    if (S.GridMotion.Owner!=Record.SimulationOwner || S.GridMotion.Epoch!=Record.OwnerEpoch
        || Control->GetState().DisplacementRevision!=S.EngineeringDisplacementRevision)
    {
        S.EngineeringDisplacementRevision=Control->GetState().DisplacementRevision;
        if (auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady())
            S.GridMotion.Bind(*Grid,Record.Key,Record.SimulationOwner,Record.OwnerEpoch,uint8(Record.Team),S.Location,
                GuLiGrid::FootprintFromDiameter(2.*S.AvoidanceRadiusCentimeters),S.MovementLayer);
        S.GridOrderId=0;++S.StateRevision;
    }
    if (auto* Mass=AuthorityState->MassEntitySubsystem.Get())
    {
        auto& Manager=Mass->GetMutableEntityManager();
        if (Manager.IsEntityValid(S.Entity))
        {
            Manager.GetFragmentDataChecked<FTransformFragment>(S.Entity).SetTransform(FTransform(FRotator(0,S.FacingYawDegrees,0),S.Location));
            auto& H=Manager.GetFragmentDataChecked<FGuLiMassHealthFragment>(S.Entity);H.Health=S.Health;H.bDead=S.Health<=0;H.bPhased=S.bPhased;
        }
    }
}

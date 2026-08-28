// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"

#include "Avoidance/MassAvoidanceFragments.h"
#include "Async/Async.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Mass/GuLiControlCohortBuilder.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Mass/Navigation/GuLiLocalFlowField.h"
#include "Engine/World.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCommanderMass, Log, All);

namespace GuLiCommanderMassPrivate
{
	constexpr int32 SpawnFormationsPerTeam = 10;
	constexpr int32 TeamCount = 2;
	constexpr int32 SoldierCountPerFormation = 25;
	constexpr int32 TotalSoldierCount = SpawnFormationsPerTeam * TeamCount * SoldierCountPerFormation;
	constexpr int32 FormationColumns = 5;
	constexpr int32 FormationRows = 5;
	constexpr float FixedStepSeconds = 1.0f / 30.0f;
	constexpr float MaxAccumulatedSeconds = FixedStepSeconds * 4.0f;
	constexpr int32 MaxRequestsPerSecond = 10;
	constexpr float SpatialCellSizeCentimeters = 10000.0f;
	constexpr float MaximumAutomaticFillDistanceCentimeters = 30000.0f;
	constexpr float FormationGuideMaximumLeadCentimeters = 9000.0f;
	constexpr float FormationWaypointToleranceCentimeters = 1800.0f;
	constexpr float FormationArrivalToleranceCentimeters = 500.0f;
	constexpr float TravelWeight = 0.70f;
	constexpr float SlotCorrectionWeight = 0.30f;
	constexpr float ManualAvoidanceStrength = 0.85f;

	static_assert(FormationColumns * FormationRows == SoldierCountPerFormation);
	static_assert(SoldierCountPerFormation == static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));
	static_assert(FormationColumns == GuLiCommanderNavigationPolicy::MaximumFormationColumns);
	static_assert(SoldierCountPerFormation == GuLiCommanderNavigationPolicy::FormationMemberCapacity);

	struct FSoldierRuntime
	{
		FMassEntityHandle Entity;
		FGuLiSoldierId SoldierId;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float FacingYawDegrees = 0.0f;
		uint8 Health = 100u;
		uint8 MaxHealth = 100u;
		float AttackPower = 0.0f;
		float Defense = 0.0f;
		float AttackRangeCentimeters = 0.0f;
		uint32 StateRevision = 1u;
		uint32 ActiveOrderId = 0u;
		uint32 LastGroundSampleTick = 0u;
		FVector LastCapturedPoseLocation = FVector::ZeroVector;
		uint32 LastCapturedPoseFrameSequence = 0u;
		double DeathSimulationSeconds = -1.0;
		bool bWreckExpired = false;

		bool IsAlive() const
		{
			return Health > 0u;
		}
	};

	struct FOrderFormationRuntime
	{
		uint32 FormationId = 0u;
		uint32 BatchOrderId = 0u;
		FGuLiControlCohortId SourceCohortId;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		TArray<FGuLiSoldierId> MemberIds;
		TMap<uint32, uint8> SlotBySoldierId;
		FVector GuideAnchor = FVector::ZeroVector;
		FVector TargetAnchor = FVector::ZeroVector;
		TArray<FVector> PathPoints;
		int32 PathPointIndex = 0;
		float TravelFacingYawDegrees = 0.0f;
		GuLiCommanderNavigationPolicy::FFinalPathFrame FinalPathFrame;
		uint32 PathRevision = 1u;
		TSharedPtr<const FGuLiLocalFlowField, ESPMode::ThreadSafe> FlowField;
		FGuLiLocalFlowFieldBuildData PendingFlowBuildData;
		int32 NextFlowWalkabilitySample = INDEX_NONE;
		TFuture<TSharedPtr<FGuLiLocalFlowField, ESPMode::ThreadSafe>> FlowBuildFuture;
		bool bFlowBuildInFlight = false;
		bool bPathValid = true;
		int32 InitialAcceptedBatchMemberCount = 0;
		float ArrivalDomainRadiusCentimeters = 0.0f;
		int32 TransitColumnCount = FormationColumns;
	};

	struct FRequestGate
	{
		double WindowStartSeconds = 0.0;
		int32 RequestCount = 0;
		uint32 LastMoveClientCommandId = 0u;
		FGuLiCommandAck LastMoveAck;
	};

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
			LocalOffset.Y =
				(static_cast<float>(ProbeIndex) - static_cast<float>(ColumnCount - 1) * 0.5f)
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

	void GatherSpatialCandidateIndices(
		const TMap<FIntPoint, TArray<int32>>& SpatialGrid,
		const FVector& Center,
		const float RadiusCentimeters,
		TArray<int32>& OutIndices)
	{
		OutIndices.Reset();
		const FIntPoint MinimumCell = MakeSpatialCell(
			Center - FVector(RadiusCentimeters, RadiusCentimeters, 0.0f));
		const FIntPoint MaximumCell = MakeSpatialCell(
			Center + FVector(RadiusCentimeters, RadiusCentimeters, 0.0f));
		for (int32 CellX = MinimumCell.X; CellX <= MaximumCell.X; ++CellX)
		{
			for (int32 CellY = MinimumCell.Y; CellY <= MaximumCell.Y; ++CellY)
			{
				if (const TArray<int32>* CellMembers =
					SpatialGrid.Find(FIntPoint(CellX, CellY)))
				{
					OutIndices.Append(*CellMembers);
				}
			}
		}
	}

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

	bool BuildSharedPath(
		UNavigationSystemV1& NavigationSystem,
		const ANavigationData& NavigationData,
		const FVector& Start,
		const FVector& SharedTarget,
		TArray<FVector>& OutPathPoints)
	{
		OutPathPoints.Reset();
		FNavLocation ProjectedStart;
		const FVector StartProjectionExtent(10000.0f, 10000.0f, 50000.0f);
		if (!ProjectPointToCommanderNavigation(
			NavigationSystem,
			NavigationData,
			Start,
			StartProjectionExtent,
			ProjectedStart))
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
			if (PathPoints.Num() >= 2)
			{
				OutPathPoints.Reserve(PathPoints.Num());
				for (const FNavPathPoint& PathPoint : PathPoints)
				{
					OutPathPoints.Add(PathPoint.Location);
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
		if (Lhs.Cohorts.Num() != Rhs.Cohorts.Num())
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

struct FGuLiBattleAuthorityState
{
	TWeakObjectPtr<UMassEntitySubsystem> MassEntitySubsystem;
	FMassArchetypeHandle AuthorityArchetype;
	FMassArchetypeHandle RuntimeTuningEvenBaseArchetype;
	FMassArchetypeHandle RuntimeTuningOddBaseArchetype;
	TArray<GuLiCommanderMassPrivate::FSoldierRuntime> Soldiers;
	TMap<uint32, int32> SoldierIndexById;
	TArray<GuLiCommanderMassPrivate::FOrderFormationRuntime> OrderFormations;
	TMap<FIntPoint, TArray<int32>> SpatialGrid;
	TMap<FObjectKey, GuLiCommanderMassPrivate::FRequestGate> RequestGates;
	double FixedStepAccumulator = 0.0;
	double SimulationSeconds = 0.0;
	uint32 ServerSimTick = 0u;
	uint32 NextSoldierId = 1u;
	uint32 NextControlCohortId = 1u;
	uint32 NextBatchOrderId = 1u;
	uint32 NextFormationId = 1u;
	uint32 NextPoseFrameSequence = 1u;
	uint32 AuthorityEpoch = 1u;
	uint32 NavigationGeneration = 1u;
	bool bPopulationSpawned = false;
	bool bLoggedNavigationFallback = false;
	bool bUsingRuntimeTuningEvenArchetype = true;
};

void FGuLiBattleAuthorityStateDeleter::operator()(FGuLiBattleAuthorityState* State) const
{
	delete State;
}

UGuLiBattleAuthoritySubsystem::UGuLiBattleAuthoritySubsystem() = default;
UGuLiBattleAuthoritySubsystem::~UGuLiBattleAuthoritySubsystem() = default;

bool UGuLiBattleAuthoritySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& World != nullptr
		&& World->IsGameWorld()
		&& World->GetNetMode() != NM_Client;
}

void UGuLiBattleAuthoritySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMassEntitySubsystem>();
	Collection.InitializeDependency<UGuLiRuntimeTuningSubsystem>();
	AuthorityState.Reset(new FGuLiBattleAuthorityState());
	if (UWorld* World = GetWorld())
	{
		AuthorityState->MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (const UGuLiRuntimeTuningSubsystem* RuntimeTuning =
			World->GetSubsystem<UGuLiRuntimeTuningSubsystem>())
		{
			BaselineRuntimeTuning = RuntimeTuning->GetBaselineSoldierValues();
			EffectiveRuntimeTuning = RuntimeTuning->GetEffectiveSoldierValues();
			MovementSpeedCentimetersPerSecond =
				EffectiveRuntimeTuning.MovementSpeedCmPerSecond;
		}
	}
}

void UGuLiBattleAuthoritySubsystem::Deinitialize()
{
	DestroyAuthorityPopulation();
	AuthorityState.Reset();
	Super::Deinitialize();
}

void UGuLiBattleAuthoritySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
	{
		NavigationSystem->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(
			this,
			&UGuLiBattleAuthoritySubsystem::HandleNavigationGenerationFinished);
	}
	TrySpawnAuthorityPopulation();
}

void UGuLiBattleAuthoritySubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	if (UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
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
	if (!AuthorityState || !IsAuthorityWorld())
	{
		return;
	}
	if (!AuthorityState->bPopulationSpawned && !TrySpawnAuthorityPopulation())
	{
		return;
	}
	// Walkability sampling is a game-thread/world-frame budget, not a fixed-step
	// budget, so catch-up simulation cannot multiply NavMesh queries in one frame.
	TickLocalFlowFields();

	AuthorityState->FixedStepAccumulator = FMath::Min(
		AuthorityState->FixedStepAccumulator + static_cast<double>(FMath::Max(0.0f, DeltaTime)),
		static_cast<double>(GuLiCommanderMassPrivate::MaxAccumulatedSeconds));
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

bool UGuLiBattleAuthoritySubsystem::TrySpawnAuthorityPopulation()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || AuthorityState->bPopulationSpawned || !IsAuthorityWorld())
	{
		return AuthorityState && AuthorityState->bPopulationSpawned;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!World || !World->HasBegunPlay() || !MassSubsystem)
	{
		return false;
	}
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!NavigationSystem || !CommanderNavigationData)
	{
		return false;
	}
	for (const FVector& TeamCenter : {RedSpawnCenter, BlueSpawnCenter})
	{
		FNavLocation ProjectedCenter;
		if (!ProjectPointToCommanderNavigation(
			*NavigationSystem,
			*CommanderNavigationData,
			TeamCenter,
			FVector(10000.0f, 10000.0f, 50000.0f),
			ProjectedCenter))
		{
			// Population creation is retried by Tick only after the baked/dynamic
			// CommanderSoldier NavData can represent both deployment areas.
			return false;
		}
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	TArray<const UScriptStruct*> FragmentAndTagTypes = {
		FTransformFragment::StaticStruct(),
		FAgentRadiusFragment::StaticStruct(),
		FMassVelocityFragment::StaticStruct(),
		FMassForceFragment::StaticStruct(),
		FMassMoveTargetFragment::StaticStruct(),
		FMassNavigationEdgesFragment::StaticStruct(),
		FMassNavigationObstacleGridCellLocationFragment::StaticStruct(),
		FGuLiMassIdentityFragment::StaticStruct(),
		FGuLiMassHealthFragment::StaticStruct(),
		FGuLiMassSoldierStatsFragment::StaticStruct(),
		FGuLiMassOrderFragment::StaticStruct(),
		FGuLiMassSlotTargetFragment::StaticStruct(),
		FGuLiMassAvoidanceOutputFragment::StaticStruct(),
		FGuLiServerAuthorityMassTag::StaticStruct(),
		FGuLiMassRuntimeTuningEvenTag::StaticStruct()
	};

	FMassArchetypeCreationParams ArchetypeParams;
	ArchetypeParams.DebugName = TEXT("GuLiServerAuthority500DynamicSoldiers");
	const FMassArchetypeHandle BaseAuthorityArchetype =
		EntityManager.CreateArchetype(FragmentAndTagTypes, ArchetypeParams);
	if (!BaseAuthorityArchetype.IsValid())
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Failed to create Soldier authority archetype."));
		return false;
	}
	FragmentAndTagTypes.RemoveSingle(FGuLiMassRuntimeTuningEvenTag::StaticStruct());
	FragmentAndTagTypes.Add(FGuLiMassRuntimeTuningOddTag::StaticStruct());
	FMassArchetypeCreationParams OddArchetypeParams;
	OddArchetypeParams.DebugName = TEXT("GuLiServerAuthority500DynamicSoldiers_RuntimeTuningOdd");
	const FMassArchetypeHandle OddBaseAuthorityArchetype =
		EntityManager.CreateArchetype(FragmentAndTagTypes, OddArchetypeParams);
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
	EntityHandles.Reserve(TotalSoldierCount);
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.BatchCreateEntities(
		AuthorityState->AuthorityArchetype,
		SharedValues,
		TotalSoldierCount,
		EntityHandles);
	if (EntityHandles.Num() != TotalSoldierCount)
	{
		UE_LOG(LogGuLiCommanderMass, Error, TEXT("Expected 500 Soldiers but Mass created %d."), EntityHandles.Num());
		EntityManager.BatchDestroyEntities(EntityHandles);
		return false;
	}

	AuthorityState->Soldiers.Reset(TotalSoldierCount);
	AuthorityState->SoldierIndexById.Reset();
	int32 EntityIndex = 0;
	int32 NavigationProjectionCount = 0;
	for (int32 TeamIndex = 0; TeamIndex < TeamCount; ++TeamIndex)
	{
		const EGuLiTeam Team = TeamIndex == 0 ? EGuLiTeam::Red : EGuLiTeam::Blue;
		const FVector TeamCenter = Team == EGuLiTeam::Red ? RedSpawnCenter : BlueSpawnCenter;
		for (int32 FormationIndex = 0; FormationIndex < SpawnFormationsPerTeam; ++FormationIndex)
		{
			const FVector FormationAnchor = TeamCenter
				+ MakeSpawnFormationOffset(FormationIndex, GroupSpacingCentimeters);
			const float FacingYaw = (FVector::ZeroVector - FormationAnchor).GetSafeNormal2D().Rotation().Yaw;
			for (int32 SlotIndex = 0; SlotIndex < SoldierCountPerFormation; ++SlotIndex)
			{
				FSoldierRuntime& Soldier = AuthorityState->Soldiers.AddDefaulted_GetRef();
				Soldier.Entity = EntityHandles[EntityIndex++];
				Soldier.SoldierId = FGuLiSoldierId(AllocateNonZero(AuthorityState->NextSoldierId));
				Soldier.Team = Team;
				Soldier.FacingYawDegrees = FacingYaw;
				Soldier.MaxHealth = EffectiveRuntimeTuning.MaxHealth;
				Soldier.Health = Soldier.MaxHealth;
				Soldier.AttackPower = EffectiveRuntimeTuning.AttackPower;
				Soldier.Defense = EffectiveRuntimeTuning.Defense;
				Soldier.AttackRangeCentimeters =
					EffectiveRuntimeTuning.AttackRangeCentimeters;
				const FVector RequestedLocation = FormationAnchor
					+ FRotator(0.0f, FacingYaw, 0.0f).RotateVector(
						MakeFormationSlotOffset(SlotIndex, MemberSpacingCentimeters));
				FNavLocation ProjectedLocation;
				if (ProjectPointToCommanderNavigation(
					*NavigationSystem,
					*CommanderNavigationData,
					RequestedLocation,
					FVector(5000.0f, 5000.0f, 50000.0f),
					ProjectedLocation)
					&& FVector::DistSquared2D(RequestedLocation, ProjectedLocation.Location)
						<= FMath::Square(10000.0f))
				{
					Soldier.Location = ProjectedLocation.Location;
					++NavigationProjectionCount;
				}
				else
				{
					Soldier.Location = RequestedLocation;
				}

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
				Stats.AttackPower = Soldier.AttackPower;
				Stats.Defense = Soldier.Defense;
				Stats.AttackRangeCentimeters = Soldier.AttackRangeCentimeters;
				EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(Soldier.Entity).WorldTarget = Soldier.Location;
			}
		}
	}
	if (NavigationProjectionCount != TotalSoldierCount)
	{
		UE_LOG(
			LogGuLiCommanderMass,
			Warning,
			TEXT("Deferred Soldier population: CommanderSoldier NavMesh projected %d/%d deployment points."),
			NavigationProjectionCount,
			TotalSoldierCount);
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
	UE_LOG(
		LogGuLiCommanderMass,
		Display,
		TEXT("Spawned %d independent server-authoritative Mass Soldiers (%d CommanderSoldier NavMesh projections); no permanent 25-Soldier groups."),
		AuthorityState->Soldiers.Num(),
		NavigationProjectionCount);
	return true;
}

void UGuLiBattleAuthoritySubsystem::DestroyAuthorityPopulation()
{
	if (!AuthorityState || !AuthorityState->bPopulationSpawned)
	{
		return;
	}

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
	AuthorityState->SpatialGrid.Reset();
	AuthorityState->RequestGates.Reset();
	AuthorityState->AuthorityArchetype = FMassArchetypeHandle();
	AuthorityState->RuntimeTuningEvenBaseArchetype = FMassArchetypeHandle();
	AuthorityState->RuntimeTuningOddBaseArchetype = FMassArchetypeHandle();
	AuthorityState->bUsingRuntimeTuningEvenArchetype = true;
	AuthorityState->bPopulationSpawned = false;
}

void UGuLiBattleAuthoritySubsystem::TickLocalFlowFields()
{
	using namespace GuLiCommanderMassPrivate;
	if (!bEnableLocalFlowField || !AuthorityState || !GetWorld())
	{
		return;
	}

	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!NavigationSystem || !CommanderNavigationData)
	{
		return;
	}

	int32 RemainingSampleBudget = FMath::Max(1, FlowFieldWalkabilitySamplesPerTick);
	for (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		if (Formation.bFlowBuildInFlight && Formation.FlowBuildFuture.IsReady())
		{
			TSharedPtr<FGuLiLocalFlowField, ESPMode::ThreadSafe> BuiltField =
				Formation.FlowBuildFuture.Get();
			Formation.bFlowBuildInFlight = false;
			const FIntPoint CurrentTile = MakeFlowFieldTileCoordinate(Formation.GuideAnchor);
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
			FGuLiLocalFlowFieldBuildData DetachedBuildData =
				MoveTemp(Formation.PendingFlowBuildData);
			Formation.NextFlowWalkabilitySample = INDEX_NONE;
			Formation.bFlowBuildInFlight = true;
			Formation.FlowBuildFuture = Async(
				EAsyncExecution::ThreadPool,
				[BuildData = MoveTemp(DetachedBuildData)]() mutable
				{
					TSharedPtr<FGuLiLocalFlowField, ESPMode::ThreadSafe> Field =
						MakeShared<FGuLiLocalFlowField, ESPMode::ThreadSafe>();
					if (!Field->Build(BuildData))
					{
						Field.Reset();
					}
					return Field;
				});
		}
	}
}

void UGuLiBattleAuthoritySubsystem::HandleNavigationGenerationFinished(
	ANavigationData* NavigationData)
{
	UWorld* World = GetWorld();
	if (!AuthorityState || !World)
	{
		return;
	}
	if (!NavigationData
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
	for (GuLiCommanderMassPrivate::FOrderFormationRuntime& Formation :
		AuthorityState->OrderFormations)
	{
		++Formation.PathRevision;
		if (Formation.PathRevision == 0u)
		{
			++Formation.PathRevision;
		}

		UNavigationSystemV1* NavigationSystem =
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FVector ReprojectedTarget = Formation.TargetAnchor;
		TArray<FVector> RebuiltPath;
		Formation.bPathValid = NavigationSystem
			&& GuLiCommanderMassPrivate::ResolveSharedMoveTarget(
				*NavigationSystem,
				*NavigationData,
				Formation.TargetAnchor,
				MemberAgentRadiusCentimeters,
				ReprojectedTarget)
			&& GuLiCommanderMassPrivate::BuildSharedPath(
				*NavigationSystem,
				*NavigationData,
				Formation.GuideAnchor,
				ReprojectedTarget,
				RebuiltPath);
		const GuLiCommanderNavigationPolicy::FFinalPathFrame RebuiltFinalPathFrame =
			GuLiCommanderNavigationPolicy::ResolveFinalPathFrame(
				RebuiltPath,
				Formation.GuideAnchor,
				ReprojectedTarget);
		if (Formation.bPathValid)
		{
			Formation.TargetAnchor = ReprojectedTarget;
			Formation.PathPoints = MoveTemp(RebuiltPath);
			Formation.PathPointIndex = Formation.PathPoints.Num() > 1 ? 1 : 0;
			Formation.FinalPathFrame = RebuiltFinalPathFrame;
		}
		else
		{
			Formation.PathPoints.Reset();
			Formation.PathPointIndex = 0;
		}
		Formation.FlowField.Reset();
		Formation.NextFlowWalkabilitySample = INDEX_NONE;
		Formation.PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
	}
}

void UGuLiBattleAuthoritySubsystem::TickAuthority(const float FixedDeltaSeconds)
{
	using namespace GuLiCommanderMassPrivate;
	check(AuthorityState);
	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	UWorld* World = GetWorld();
	if (!MassSubsystem || !World)
	{
		return;
	}
	ApplyPendingMovementSpeed();

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	AuthorityState->SimulationSeconds += FixedDeltaSeconds;
	++AuthorityState->ServerSimTick;

	AuthorityState->SpatialGrid.Reset();
	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (Soldier.IsAlive())
		{
			AuthorityState->SpatialGrid.FindOrAdd(MakeSpatialCell(Soldier.Location)).Add(SoldierIndex);
		}
	}
	TArray<FVector> DesiredVelocities;
	DesiredVelocities.Init(FVector::ZeroVector, AuthorityState->Soldiers.Num());

	for (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		int32 ActiveMemberCount = 0;
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
			{
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				ActiveMemberCount += Soldier.IsAlive()
					&& Soldier.ActiveOrderId == Formation.BatchOrderId ? 1 : 0;
			}
		}
		if (Formation.SlotBySoldierId.Num() != ActiveMemberCount)
		{
			AssignFormationSlots(
				Formation,
				AuthorityState->Soldiers,
				AuthorityState->SoldierIndexById,
				MemberSpacingCentimeters,
				Formation.BatchOrderId,
				Formation.TransitColumnCount);
		}
		const FVector ActiveCentroid = ComputeCentroid(
			Formation.MemberIds,
			AuthorityState->Soldiers,
			AuthorityState->SoldierIndexById,
			Formation.BatchOrderId);
		if (ActiveMemberCount == 0 || !Formation.bPathValid || Formation.PathPoints.IsEmpty())
		{
			continue;
		}
		Formation.PathPointIndex = FMath::Clamp(
			Formation.PathPointIndex,
			0,
			Formation.PathPoints.Num() - 1);
		const int32 PreviousPathPointIndex = Formation.PathPointIndex;
		while (Formation.PathPointIndex < Formation.PathPoints.Num() - 1
			&& FVector::DistSquared2D(
				Formation.GuideAnchor,
				Formation.PathPoints[Formation.PathPointIndex])
				<= FMath::Square(FormationWaypointToleranceCentimeters))
		{
			++Formation.PathPointIndex;
		}
		if (Formation.PathPointIndex != PreviousPathPointIndex)
		{
			Formation.FlowField.Reset();
			Formation.NextFlowWalkabilitySample = INDEX_NONE;
			Formation.PendingFlowBuildData = FGuLiLocalFlowFieldBuildData{};
		}

		FVector GuideDirection = Formation.PathPoints[Formation.PathPointIndex] - Formation.GuideAnchor;
		GuideDirection.Z = 0.0f;
		const float GuideDistance = GuideDirection.Size2D();
		const float MaximumStep = MovementSpeedCentimetersPerSecond * FixedDeltaSeconds;
		const bool bGuideMayAdvance = FVector::DistSquared2D(ActiveCentroid, Formation.GuideAnchor)
			<= FMath::Square(FormationGuideMaximumLeadCentimeters);
		if (bGuideMayAdvance && GuideDistance > UE_KINDA_SMALL_NUMBER)
		{
			const FVector NormalizedDirection = GuideDirection / GuideDistance;
			Formation.GuideAnchor += NormalizedDirection * FMath::Min(MaximumStep, GuideDistance);
			Formation.TravelFacingYawDegrees = FMath::FixedTurn(
				Formation.TravelFacingYawDegrees,
				NormalizedDirection.Rotation().Yaw,
				FacingRateDegreesPerSecond * FixedDeltaSeconds);
		}

		FVector TravelDirection = GuideDirection.GetSafeNormal2D();
		if (TravelDirection.IsNearlyZero())
		{
			TravelDirection = (Formation.TargetAnchor - Formation.GuideAnchor).GetSafeNormal2D();
		}
		const bool bGuideAtFinal = Formation.PathPointIndex == Formation.PathPoints.Num() - 1
			&& FVector::DistSquared2D(Formation.GuideAnchor, Formation.TargetAnchor)
				<= FMath::Square(FormationWaypointToleranceCentimeters);
		const bool bInTerminalSegment =
			GuLiCommanderNavigationPolicy::HasEnteredLooseArrivalTerminalPhase(
				Formation.PathPointIndex,
				Formation.PathPoints.Num(),
				Formation.GuideAnchor,
				Formation.TargetAnchor,
				Formation.ArrivalDomainRadiusCentimeters,
				FormationWaypointToleranceCentimeters);
		if (!bInTerminalSegment)
		{
			const int32 DesiredColumnCount = DetermineTransitFormationColumns(
				NavigationSystem,
				CommanderNavigationData,
				Formation.GuideAnchor,
				Formation.TravelFacingYawDegrees,
				MemberSpacingCentimeters);
			if (DesiredColumnCount != Formation.TransitColumnCount)
			{
				Formation.TransitColumnCount = DesiredColumnCount;
				AssignFormationSlots(
					Formation,
					AuthorityState->Soldiers,
					AuthorityState->SoldierIndexById,
					MemberSpacingCentimeters,
					Formation.BatchOrderId,
					Formation.TransitColumnCount);
			}
		}

		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* SoldierIndexPtr = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (!SoldierIndexPtr || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndexPtr))
			{
				continue;
			}

			FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndexPtr];
			if (!Soldier.IsAlive() || Soldier.ActiveOrderId != Formation.BatchOrderId)
			{
				continue;
			}

			FVector LocalSlotOffset = FVector::ZeroVector;
			FVector WorldSlotTarget = Formation.TargetAnchor;
			if (!bInTerminalSegment)
			{
				const uint8* SlotIndexPtr = Formation.SlotBySoldierId.Find(SoldierId.Value);
				if (!SlotIndexPtr)
				{
					continue;
				}
				LocalSlotOffset = MakeFormationSlotOffset(
					static_cast<int32>(*SlotIndexPtr),
					MemberSpacingCentimeters,
					Formation.TransitColumnCount);
				WorldSlotTarget = Formation.GuideAnchor
					+ FRotator(0.0f, Formation.TravelFacingYawDegrees, 0.0f)
						.RotateVector(LocalSlotOffset);
			}
			FVector SlotDelta = WorldSlotTarget - Soldier.Location;
			SlotDelta.Z = 0.0f;
			FVector SoldierTravelDirection = TravelDirection;
			if (bInTerminalSegment)
			{
				SoldierTravelDirection =
					GuLiCommanderNavigationPolicy::CalculateSharedPathFollowDirection(
						Formation.PathPoints,
						Soldier.Location,
						MemberAgentRadiusCentimeters);
				if (SoldierTravelDirection.IsNearlyZero())
				{
					SoldierTravelDirection =
						(Formation.TargetAnchor - Soldier.Location).GetSafeNormal2D();
				}
			}
			else if (bEnableLocalFlowField && Formation.FlowField.IsValid())
			{
				FVector FlowDirection;
				if (Formation.FlowField->SampleDirection(Soldier.Location, FlowDirection)
					&& !FlowDirection.IsNearlyZero())
				{
					SoldierTravelDirection = FlowDirection;
				}
			}

			FVector DesiredVelocity;
			if (bInTerminalSegment)
			{
				const bool bInsideArrivalDomain = FVector::DistSquared2D(
					Soldier.Location,
					Formation.TargetAnchor)
					<= FMath::Square(Formation.ArrivalDomainRadiusCentimeters);
				DesiredVelocity = bGuideAtFinal && bInsideArrivalDomain
					? FVector::ZeroVector
					: SoldierTravelDirection * MovementSpeedCentimetersPerSecond;
			}
			else
			{
				const FVector TravelVelocity = SoldierTravelDirection
					* MovementSpeedCentimetersPerSecond
					* TravelWeight;
				const FVector SlotVelocity = SlotDelta.GetClampedToMaxSize(
					MovementSpeedCentimetersPerSecond * SlotCorrectionWeight);
				DesiredVelocity = (TravelVelocity + SlotVelocity).GetClampedToMaxSize(
					MovementSpeedCentimetersPerSecond);
			}
			DesiredVelocities[*SoldierIndexPtr] = DesiredVelocity;

			if (EntityManager.IsEntityValid(Soldier.Entity))
			{
				FGuLiMassSlotTargetFragment& SlotTarget =
					EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(Soldier.Entity);
				SlotTarget.LocalOffset = LocalSlotOffset;
				SlotTarget.WorldTarget = WorldSlotTarget;

				FMassMoveTargetFragment& MoveTarget =
					EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
				MoveTarget.Center = WorldSlotTarget;
				MoveTarget.Forward = DesiredVelocity.GetSafeNormal2D();
				MoveTarget.DistanceToGoal = FVector::Dist2D(Soldier.Location, Formation.TargetAnchor);
				MoveTarget.DesiredSpeed = FMassInt16Real(
					DesiredVelocity.IsNearlyZero(1.0f)
						? 0.0f
						: MovementSpeedCentimetersPerSecond);
			}
		}
	}

	for (int32 SoldierIndex = 0; SoldierIndex < AuthorityState->Soldiers.Num(); ++SoldierIndex)
	{
		FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}

		FGuLiMassHealthFragment& Health =
			EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
		if (!Soldier.IsAlive())
		{
			Health.WreckSecondsRemaining = FMath::Max(
				0.0f,
				static_cast<float>(Soldier.DeathSimulationSeconds + WreckLifetimeSeconds
					- AuthorityState->SimulationSeconds));
			if (!Soldier.bWreckExpired && Health.WreckSecondsRemaining <= 0.0f)
			{
				FTransformFragment& Transform =
					EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
				FTransform HiddenTransform = Transform.GetTransform();
				HiddenTransform.SetScale3D(FVector::ZeroVector);
				Transform.SetTransform(HiddenTransform);
				Soldier.bWreckExpired = true;
			}
			continue;
		}

		FVector AvoidanceVelocity = FVector::ZeroVector;
		const FIntPoint Cell = MakeSpatialCell(Soldier.Location);
		for (int32 CellX = Cell.X - 1; CellX <= Cell.X + 1; ++CellX)
		{
			for (int32 CellY = Cell.Y - 1; CellY <= Cell.Y + 1; ++CellY)
			{
				const TArray<int32>* NeighborIndices = AuthorityState->SpatialGrid.Find(FIntPoint(CellX, CellY));
				if (!NeighborIndices)
				{
					continue;
				}
				for (const int32 NeighborIndex : *NeighborIndices)
				{
					if (NeighborIndex == SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(NeighborIndex))
					{
						continue;
					}
					const FSoldierRuntime& Neighbor = AuthorityState->Soldiers[NeighborIndex];
					FVector Separation = Soldier.Location - Neighbor.Location;
					Separation.Z = 0.0f;
					const float MinimumDistance = MemberAgentRadiusCentimeters * 2.0f;
					const float DistanceSquared = Separation.SizeSquared2D();
					if (DistanceSquared >= FMath::Square(MinimumDistance))
					{
						continue;
					}
					const float Distance = FMath::Sqrt(FMath::Max(1.0f, DistanceSquared));
					const FVector Normal = DistanceSquared > 1.0f
						? Separation / Distance
						: FVector(Soldier.SoldierId.Value < Neighbor.SoldierId.Value ? -1.0f : 1.0f, 0.0f, 0.0f);
					AvoidanceVelocity += Normal
						* ((MinimumDistance - Distance) / MinimumDistance)
						* MovementSpeedCentimetersPerSecond
						* ManualAvoidanceStrength;
				}
			}
		}

		const FGuLiMassAvoidanceOutputFragment& AvoidanceOutput =
			EntityManager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(Soldier.Entity);
		const FVector EngineAvoidanceDelta = AvoidanceOutput.Value.GetClampedToMaxSize(
			MovementSpeedCentimetersPerSecond * 4.0f) * FixedDeltaSeconds;

		const FVector TargetVelocity = (DesiredVelocities[SoldierIndex]
			+ AvoidanceVelocity
			+ EngineAvoidanceDelta).GetClampedToMaxSize(MovementSpeedCentimetersPerSecond);
		Soldier.Velocity = FMath::VInterpTo(
			Soldier.Velocity,
			TargetVelocity,
			FixedDeltaSeconds,
			8.0f);
		if (TargetVelocity.IsNearlyZero(1.0f))
		{
			Soldier.Velocity = FMath::VInterpTo(
				Soldier.Velocity,
				FVector::ZeroVector,
				FixedDeltaSeconds,
				10.0f);
		}

		if (!Soldier.Velocity.IsNearlyZero(1.0f))
		{
			FVector NewLocation = Soldier.Location + Soldier.Velocity * FixedDeltaSeconds;
			const bool bGroundSampleDue = Soldier.LastGroundSampleTick == 0u
				|| AuthorityState->ServerSimTick - Soldier.LastGroundSampleTick >= 6u;
			if (bGroundSampleDue)
			{
				FNavLocation ProjectedLocation;
				const FVector ProjectionExtent(
					MemberAgentRadiusCentimeters,
					MemberAgentRadiusCentimeters,
					5000.0f);
				if (NavigationSystem && CommanderNavigationData
					&& ProjectPointToCommanderNavigation(
						*NavigationSystem,
						*CommanderNavigationData,
					NewLocation,
						ProjectionExtent,
						ProjectedLocation)
					&& FVector::DistSquared2D(NewLocation, ProjectedLocation.Location)
						<= FMath::Square(MemberAgentRadiusCentimeters * 1.25f))
				{
					NewLocation = ProjectedLocation.Location;
				}
				else
				{
					// Never replace failed navigation with a Landscape straight-line step.
					NewLocation = Soldier.Location;
					Soldier.Velocity = FVector::ZeroVector;
				}
				Soldier.LastGroundSampleTick = AuthorityState->ServerSimTick;
			}
			else
			{
				NewLocation.Z = Soldier.Location.Z;
			}
			Soldier.Location = NewLocation;
			Soldier.FacingYawDegrees = FMath::FixedTurn(
				Soldier.FacingYawDegrees,
				Soldier.Velocity.GetSafeNormal2D().Rotation().Yaw,
				FacingRateDegreesPerSecond * FixedDeltaSeconds);
		}

		FTransformFragment& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);
		Transform.SetTransform(FTransform(
			FRotator(0.0f, Soldier.FacingYawDegrees, 0.0f),
			Soldier.Location));
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = Soldier.Velocity;

		FGuLiMassOrderFragment& Order = EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
		Order.ActiveOrderId = Soldier.ActiveOrderId;
		Order.OrderRevision = Soldier.StateRevision;
		Order.bHasMoveTarget = Soldier.ActiveOrderId != 0u;
	}

	TMap<uint32, TArray<GuLiCommanderNavigationPolicy::FBatchOrderFormationCompletionSample>>
		CompletionSamplesByBatch;
	for (const FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)
	{
		bool bAnyActive = false;
		TArray<GuLiCommanderNavigationPolicy::FFormationMemberProgressSample,
			TInlineAllocator<SoldierCountPerFormation>> MemberProgressSamples;
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
			{
				continue;
			}
			const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
			GuLiCommanderNavigationPolicy::FFormationMemberProgressSample& Sample =
				MemberProgressSamples.AddDefaulted_GetRef();
			Sample.Location = Soldier.Location;
			Sample.bAlive = Soldier.IsAlive();
			Sample.bFollowsOrder = Soldier.ActiveOrderId == Formation.BatchOrderId;
			bAnyActive |= Sample.bAlive && Sample.bFollowsOrder;
		}

		const bool bGuideAtFinal = Formation.bPathValid
			&& !Formation.PathPoints.IsEmpty()
			&& Formation.PathPointIndex == Formation.PathPoints.Num() - 1
			&& FVector::DistSquared2D(Formation.GuideAnchor, Formation.TargetAnchor)
				<= FMath::Square(FormationWaypointToleranceCentimeters);
		const bool bOrderComplete = GuLiCommanderNavigationPolicy::ShouldCompleteOrder(
			bGuideAtFinal,
			Formation.FinalPathFrame,
			Formation.PathPoints,
			Formation.TargetAnchor,
			MemberProgressSamples,
			Formation.ArrivalDomainRadiusCentimeters,
			FormationArrivalToleranceCentimeters);
		GuLiCommanderNavigationPolicy::FBatchOrderFormationCompletionSample& CompletionSample =
			CompletionSamplesByBatch.FindOrAdd(Formation.BatchOrderId).AddDefaulted_GetRef();
		CompletionSample.bHasActiveMembers = bAnyActive;
		CompletionSample.bPathValid = Formation.bPathValid;
		CompletionSample.bGuideAndMembersReady = bOrderComplete;
	}

	TSet<uint32> BatchIdsToRemove;
	for (const TPair<uint32,
		TArray<GuLiCommanderNavigationPolicy::FBatchOrderFormationCompletionSample>>& Pair
		: CompletionSamplesByBatch)
	{
		bool bAnyActive = false;
		bool bAnyActivePathInvalid = false;
		for (const GuLiCommanderNavigationPolicy::FBatchOrderFormationCompletionSample& Sample
			: Pair.Value)
		{
			bAnyActive |= Sample.bHasActiveMembers;
			bAnyActivePathInvalid |= Sample.bHasActiveMembers && !Sample.bPathValid;
		}
		if (!bAnyActive || bAnyActivePathInvalid
			|| GuLiCommanderNavigationPolicy::ShouldCompleteBatchOrder(Pair.Value))
		{
			BatchIdsToRemove.Add(Pair.Key);
		}
	}

	for (int32 FormationIndex = AuthorityState->OrderFormations.Num() - 1;
		FormationIndex >= 0;
		--FormationIndex)
	{
		FOrderFormationRuntime& Formation = AuthorityState->OrderFormations[FormationIndex];
		if (BatchIdsToRemove.Contains(Formation.BatchOrderId))
		{
			for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
			{
				const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
				if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
				{
					continue;
				}
				FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				if (Soldier.ActiveOrderId == Formation.BatchOrderId)
				{
					Soldier.ActiveOrderId = 0u;
					++Soldier.StateRevision;
					FMassMoveTargetFragment& MoveTarget = EntityManager
						.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
					MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
					FGuLiMassOrderFragment& Order = EntityManager
						.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
					Order.ActiveOrderId = 0u;
					Order.OrderRevision = Soldier.StateRevision;
					Order.bHasMoveTarget = false;
				}
			}
			AuthorityState->OrderFormations.RemoveAtSwap(FormationIndex, 1, EAllowShrinking::No);
		}
	}
}

bool UGuLiBattleAuthoritySubsystem::ResolveSelection(
	const AGuLiCommanderPlayerState& PlayerState,
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

	FGuLiCommanderSelectionState WorkingSelection = InOutSelection;
	RefreshSelection(PlayerState.GetTeam(), WorkingSelection);
	const TArray<FGuLiControlCohortDescriptor> PreviousCohorts = WorkingSelection.Cohorts;

	if (Request.Modifier == EGuLiSelectionModifier::Clear)
	{
		WorkingSelection.Cohorts.Reset();
	}
	else
	{
		const FVector SelectionCenter = Request.Center;
		const float SelectionRadius = GuLiCommanderProtocol::GetSelectionRadiusCentimeters(
			Request.RadiusPreset);
		const double SelectionRadiusSquared = FMath::Square(static_cast<double>(SelectionRadius));

		TArray<GuLiControlCohortBuilder::FCandidate> InCircleSeeds;
		TArray<int32> SpatialCandidateIndices;
		GatherSpatialCandidateIndices(
			AuthorityState->SpatialGrid,
			SelectionCenter,
			SelectionRadius,
			SpatialCandidateIndices);
		for (const int32 SoldierIndex : SpatialCandidateIndices)
		{
			if (!AuthorityState->Soldiers.IsValidIndex(SoldierIndex))
			{
				continue;
			}
			const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
			if (Soldier.IsAlive() && Soldier.Team == PlayerState.GetTeam()
				&& FVector::DistSquared2D(Soldier.Location, SelectionCenter) <= SelectionRadiusSquared)
			{
				InCircleSeeds.Add({ Soldier.SoldierId, Soldier.Location });
			}
		}
		InCircleSeeds.Sort([SelectionCenter](
			const GuLiControlCohortBuilder::FCandidate& Lhs,
			const GuLiControlCohortBuilder::FCandidate& Rhs)
		{
			const double LhsDistance = FVector::DistSquared2D(Lhs.Location, SelectionCenter);
			const double RhsDistance = FVector::DistSquared2D(Rhs.Location, SelectionCenter);
			return !FMath::IsNearlyEqual(LhsDistance, RhsDistance)
				? LhsDistance < RhsDistance
				: Lhs.SoldierId.Value < Rhs.SoldierId.Value;
		});

		TSet<uint32> ExcludedSoldiers;
		if (Request.Modifier == EGuLiSelectionModifier::Replace)
		{
			WorkingSelection.Cohorts.Reset();
		}
		else if (Request.Modifier == EGuLiSelectionModifier::Toggle)
		{
			TSet<uint32> HitSoldiers;
			for (const GuLiControlCohortBuilder::FCandidate& Candidate : InCircleSeeds)
			{
				HitSoldiers.Add(Candidate.SoldierId.Value);
			}

			TArray<FGuLiControlCohortDescriptor> RetainedCohorts;
			for (const FGuLiControlCohortDescriptor& Cohort : WorkingSelection.Cohorts)
			{
				bool bCohortWasHit = false;
				for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
				{
					ExcludedSoldiers.Add(SoldierId.Value);
					bCohortWasHit |= HitSoldiers.Contains(SoldierId.Value);
				}
				if (!bCohortWasHit)
				{
					RetainedCohorts.Add(Cohort);
				}
			}
			WorkingSelection.Cohorts = MoveTemp(RetainedCohorts);
			InCircleSeeds.RemoveAll([&ExcludedSoldiers](
				const GuLiControlCohortBuilder::FCandidate& Candidate)
			{
				return ExcludedSoldiers.Contains(Candidate.SoldierId.Value);
			});
		}

		if (!InCircleSeeds.IsEmpty())
		{
			TSet<uint32> SeedIds;
			for (const GuLiControlCohortBuilder::FCandidate& Candidate : InCircleSeeds)
			{
				SeedIds.Add(Candidate.SoldierId.Value);
			}

			TArray<GuLiControlCohortBuilder::FCandidate> FillCandidates;
			GatherSpatialCandidateIndices(
				AuthorityState->SpatialGrid,
				SelectionCenter,
				SelectionRadius + MaximumAutomaticFillDistanceCentimeters,
				SpatialCandidateIndices);
			for (const int32 SoldierIndex : SpatialCandidateIndices)
			{
				if (!AuthorityState->Soldiers.IsValidIndex(SoldierIndex))
				{
					continue;
				}
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[SoldierIndex];
				if (Soldier.IsAlive() && Soldier.Team == PlayerState.GetTeam()
					&& !SeedIds.Contains(Soldier.SoldierId.Value)
					&& !ExcludedSoldiers.Contains(Soldier.SoldierId.Value))
				{
					FillCandidates.Add({ Soldier.SoldierId, Soldier.Location });
				}
			}

			TArray<TArray<FGuLiSoldierId>> BuiltCohorts;
			GuLiControlCohortBuilder::Build(
				InCircleSeeds,
				FillCandidates,
				MaximumAutomaticFillDistanceCentimeters,
				BuiltCohorts);
			for (TArray<FGuLiSoldierId>& MemberIds : BuiltCohorts)
			{
				if (MemberIds.IsEmpty())
				{
					continue;
				}
				FGuLiControlCohortDescriptor& Descriptor =
					WorkingSelection.Cohorts.AddDefaulted_GetRef();
				Descriptor.CohortId = FGuLiControlCohortId(
					AllocateNonZero(AuthorityState->NextControlCohortId));
				Descriptor.MemberIds = MoveTemp(MemberIds);
				Descriptor.AliveCount = static_cast<uint8>(Descriptor.MemberIds.Num());
				Descriptor.ActiveOrderId = 0u;
			}
		}
	}

	WorkingSelection.Sanitize();
	bool bMembershipChanged = PreviousCohorts.Num() != WorkingSelection.Cohorts.Num();
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
		uint32 SharedActiveOrderId = 0u;
		bool bHasAliveMember = false;
		bool bOrdersMatch = true;
		for (const FGuLiSoldierId SoldierId : Existing.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex)
				|| SeenSoldiers.Contains(SoldierId.Value))
			{
				bMembershipChanged = true;
				continue;
			}
			const GuLiCommanderMassPrivate::FSoldierRuntime& Soldier =
				AuthorityState->Soldiers[*SoldierIndex];
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
				if (!bHasAliveMember)
				{
					SharedActiveOrderId = Soldier.ActiveOrderId;
					bHasAliveMember = true;
				}
				else
				{
					bOrdersMatch &= SharedActiveOrderId == Soldier.ActiveOrderId;
				}
			}
		}

		if (Descriptor.AliveCount == 0u || Descriptor.MemberIds.IsEmpty())
		{
			bMembershipChanged = true;
			continue;
		}
		Descriptor.ActiveOrderId = bOrdersMatch ? SharedActiveOrderId : 0u;
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

bool UGuLiBattleAuthoritySubsystem::IssueMove(
	const AGuLiCommanderPlayerState& PlayerState,
	const FGuLiMoveRequest& Request,
	const FGuLiCommanderSelectionState& Selection,
	FGuLiCommandAck& OutAck)
{
	using namespace GuLiCommanderMassPrivate;

	OutAck.ClientCommandId = Request.ClientCommandId;
	OutAck.ServerSelectionRevision = Selection.SelectionRevision;
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
	if (Request.SelectionRevision != Selection.SelectionRevision)
	{
		OutAck.Result = EGuLiCommandAckResult::StaleSelectionRevision;
		return false;
	}
	if (Selection.Cohorts.IsEmpty())
	{
		OutAck.Result = EGuLiCommandAckResult::NoSelection;
		return false;
	}

	TArray<const FGuLiControlCohortDescriptor*> SortedCohorts;
	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		SortedCohorts.Add(&Cohort);
	}
	SortedCohorts.Sort([](
		const FGuLiControlCohortDescriptor& Lhs,
		const FGuLiControlCohortDescriptor& Rhs)
	{
		return Lhs.CohortId.Value < Rhs.CohortId.Value;
	});
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* CommanderNavigationData = NavigationSystem
		? GetCommanderNavigationData(*NavigationSystem)
		: nullptr;
	if (!NavigationSystem || !CommanderNavigationData)
	{
		for (const FGuLiControlCohortDescriptor* Cohort : SortedCohorts)
		{
			FGuLiCohortCommandAck& CohortAck = OutAck.CohortResults.AddDefaulted_GetRef();
			CohortAck.CohortId = Cohort->CohortId;
			CohortAck.Result = EGuLiCommandAckResult::PathFailed;
		}
		OutAck.Result = EGuLiCommandAckResult::PathFailed;
		OutAck.Sanitize();
		return false;
	}

	FVector SharedTargetAnchor;
	if (!ResolveSharedMoveTarget(
		*NavigationSystem,
		*CommanderNavigationData,
		FVector(Request.Target),
		MemberAgentRadiusCentimeters,
		SharedTargetAnchor))
	{
		UE_LOG(
			LogGuLiCommanderMass,
			Warning,
			TEXT("Move rejected: target %s has no CommanderSoldier NavMesh projection within %.0fcm."),
			*FVector(Request.Target).ToCompactString(),
			MemberAgentRadiusCentimeters);
		for (const FGuLiControlCohortDescriptor* Cohort : SortedCohorts)
		{
			FGuLiCohortCommandAck& CohortAck = OutAck.CohortResults.AddDefaulted_GetRef();
			CohortAck.CohortId = Cohort->CohortId;
			CohortAck.Result = EGuLiCommandAckResult::InvalidTarget;
		}
		OutAck.Result = EGuLiCommandAckResult::InvalidTarget;
		OutAck.Sanitize();
		return false;
	}

	const uint32 BatchOrderId = AllocateNonZero(AuthorityState->NextBatchOrderId);
	TArray<FOrderFormationRuntime> AcceptedFormations;
	AcceptedFormations.Reserve(SortedCohorts.Num());
	int32 AcceptedCohorts = 0;
	for (const FGuLiControlCohortDescriptor* CohortPtr : SortedCohorts)
	{
		const FGuLiControlCohortDescriptor& Cohort = *CohortPtr;
		FGuLiCohortCommandAck& CohortAck = OutAck.CohortResults.AddDefaulted_GetRef();
		CohortAck.CohortId = Cohort.CohortId;
		CohortAck.Result = EGuLiCommandAckResult::NoSelection;

		FOrderFormationRuntime Formation;
		Formation.FormationId = AllocateNonZero(AuthorityState->NextFormationId);
		Formation.BatchOrderId = BatchOrderId;
		Formation.SourceCohortId = Cohort.CohortId;
		Formation.Team = PlayerState.GetTeam();
		for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (SoldierIndex && AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
			{
				const FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
				if (Soldier.IsAlive() && Soldier.Team == PlayerState.GetTeam())
				{
					Formation.MemberIds.Add(SoldierId);
				}
			}
		}
		if (Formation.MemberIds.IsEmpty())
		{
			continue;
		}

		Formation.GuideAnchor = ComputeCentroid(
			Formation.MemberIds,
			AuthorityState->Soldiers,
			AuthorityState->SoldierIndexById);
		Formation.TargetAnchor = SharedTargetAnchor;
		if (!BuildSharedPath(
			*NavigationSystem,
			*CommanderNavigationData,
			Formation.GuideAnchor,
			Formation.TargetAnchor,
			Formation.PathPoints))
		{
			UE_LOG(
				LogGuLiCommanderMass,
				Warning,
				TEXT("Move cohort %u rejected: CommanderSoldier shared NavMesh path failed from %s to %s."),
				Cohort.CohortId.Value,
				*Formation.GuideAnchor.ToCompactString(),
				*Formation.TargetAnchor.ToCompactString());
			CohortAck.Result = EGuLiCommandAckResult::PathFailed;
			continue;
		}
		Formation.PathPointIndex = Formation.PathPoints.Num() > 1 ? 1 : 0;
		Formation.FinalPathFrame = GuLiCommanderNavigationPolicy::ResolveFinalPathFrame(
			Formation.PathPoints,
			Formation.GuideAnchor,
			Formation.TargetAnchor);
		FVector InitialTravelDirection = Formation.PathPoints[Formation.PathPointIndex]
			- Formation.GuideAnchor;
		InitialTravelDirection.Z = 0.0f;
		Formation.TravelFacingYawDegrees = InitialTravelDirection.IsNearlyZero()
			? Formation.FinalPathFrame.YawDegrees
			: InitialTravelDirection.Rotation().Yaw;
		AcceptedFormations.Add(MoveTemp(Formation));
		CohortAck.Result = EGuLiCommandAckResult::Accepted;
		++AcceptedCohorts;
	}

	TSet<uint32> AcceptedSoldierIds;
	for (const FOrderFormationRuntime& Formation : AcceptedFormations)
	{
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			AcceptedSoldierIds.Add(SoldierId.Value);
		}
	}
	const int32 InitialAcceptedBatchMemberCount = AcceptedSoldierIds.Num();
	const float ArrivalDomainRadiusCentimeters =
		GuLiCommanderNavigationPolicy::CalculateArrivalDomainRadiusCentimeters(
			InitialAcceptedBatchMemberCount,
			MemberAgentRadiusCentimeters);
	FMassEntityManager& EntityManager =
		AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	for (FOrderFormationRuntime& Formation : AcceptedFormations)
	{
		Formation.InitialAcceptedBatchMemberCount = InitialAcceptedBatchMemberCount;
		Formation.ArrivalDomainRadiusCentimeters = ArrivalDomainRadiusCentimeters;
		for (const FGuLiSoldierId SoldierId : Formation.MemberIds)
		{
			const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
			if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
			{
				continue;
			}
			FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
			Soldier.ActiveOrderId = BatchOrderId;
			++Soldier.StateRevision;
			FGuLiMassOrderFragment& Order =
				EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
			Order.ActiveOrderId = BatchOrderId;
			Order.OrderRevision = Soldier.StateRevision;
			Order.FormationTarget = Formation.TargetAnchor;
			Order.bHasMoveTarget = true;
			FMassMoveTargetFragment& MoveTarget =
				EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
			MoveTarget.CreateNewAction(EMassMovementAction::Move, *GetWorld());
			MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
			MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
		}
		AssignFormationSlots(
			Formation,
			AuthorityState->Soldiers,
			AuthorityState->SoldierIndexById,
			MemberSpacingCentimeters,
			Formation.BatchOrderId);
		AuthorityState->OrderFormations.Add(MoveTemp(Formation));
	}

	OutAck.BatchOrderId = AcceptedCohorts > 0 ? BatchOrderId : 0u;
	OutAck.Result = AcceptedCohorts == SortedCohorts.Num()
		? EGuLiCommandAckResult::Accepted
		: AcceptedCohorts > 0
			? EGuLiCommandAckResult::PartiallyAccepted
			: EGuLiCommandAckResult::PathFailed;
	OutAck.Sanitize();
	return AcceptedCohorts > 0;
}

int32 UGuLiBattleAuthoritySubsystem::ApplyRuntimeTuning(
	const FGuLiSoldierRuntimeTuningValues& Values)
{
	const bool bValuesValid = FMath::IsFinite(Values.MovementSpeedCmPerSecond)
		&& Values.MovementSpeedCmPerSecond > 0.0f
		&& Values.MovementSpeedCmPerSecond <= static_cast<float>(MAX_int16)
		&& Values.MaxHealth > 0u
		&& FMath::IsFinite(Values.AttackPower) && Values.AttackPower >= 0.0f
		&& FMath::IsFinite(Values.Defense) && Values.Defense >= 0.0f
		&& FMath::IsFinite(Values.AttackRangeCentimeters)
		&& Values.AttackRangeCentimeters >= 0.0f;
	if (!bValuesValid || !IsAuthorityWorld() || !AuthorityState)
	{
		UE_LOG(
			LogGuLiCommanderMass,
			Warning,
			TEXT("Rejected invalid or non-authority Soldier runtime tuning request."));
		return 0;
	}

	const FGuLiSoldierRuntimeTuningValues PreviousValues = EffectiveRuntimeTuning;
	EffectiveRuntimeTuning = Values;
	if (!FMath::IsNearlyEqual(
		MovementSpeedCentimetersPerSecond,
		Values.MovementSpeedCmPerSecond))
	{
		PendingMovementSpeedCmPerSecond = Values.MovementSpeedCmPerSecond;
	}
	else
	{
		// The latest request always wins. In particular, Set(new) followed by
		// Reset(baseline) before the next fixed step must cancel the stale Set.
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
	const bool bMaximumHealthChanged = PreviousValues.MaxHealth != Values.MaxHealth;
	for (GuLiCommanderMassPrivate::FSoldierRuntime& Soldier : AuthorityState->Soldiers)
	{
		if (!EntityManager.IsEntityValid(Soldier.Entity))
		{
			continue;
		}

		const uint8 PreviousHealth = Soldier.Health;
		if (bMaximumHealthChanged)
		{
			Soldier.Health = GuLiRuntimeTuning::ScaleHealthPreservingRatio(
				Soldier.Health,
				Soldier.MaxHealth,
				Values.MaxHealth);
		}
		Soldier.MaxHealth = Values.MaxHealth;
		Soldier.AttackPower = Values.AttackPower;
		Soldier.Defense = Values.Defense;
		Soldier.AttackRangeCentimeters = Values.AttackRangeCentimeters;

		FGuLiMassSoldierStatsFragment& Stats = EntityManager
			.GetFragmentDataChecked<FGuLiMassSoldierStatsFragment>(Soldier.Entity);
		Stats.MaxHealth = Soldier.MaxHealth;
		Stats.AttackPower = Soldier.AttackPower;
		Stats.Defense = Soldier.Defense;
		Stats.AttackRangeCentimeters = Soldier.AttackRangeCentimeters;

		if (Soldier.Health != PreviousHealth)
		{
			++Soldier.StateRevision;
			FGuLiMassHealthFragment& Health = EntityManager
				.GetFragmentDataChecked<FGuLiMassHealthFragment>(Soldier.Entity);
			Health.Health = Soldier.Health;
			Health.bDead = Soldier.Health == 0u;
		}
	}
	return AuthorityState->Soldiers.Num();
}

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
		// No Mass shared parameters exist yet. TrySpawnAuthorityPopulation will
		// consume this pending value only after the full population succeeds.
		return;
	}

	UMassEntitySubsystem* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!MassSubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	const FMassArchetypeHandle TargetBaseArchetype =
		AuthorityState->bUsingRuntimeTuningEvenArchetype
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
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value =
			Soldier.Velocity;
		if (Soldier.ActiveOrderId != 0u)
		{
			EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity)
				.DesiredSpeed = FMassInt16Real(NewMovementSpeed);
		}
		++UpdatedEntityCount;
	}

	AuthorityState->AuthorityArchetype = TargetArchetype;
	AuthorityState->bUsingRuntimeTuningEvenArchetype =
		!AuthorityState->bUsingRuntimeTuningEvenArchetype;
	MovementSpeedCentimetersPerSecond = NewMovementSpeed;
	PendingMovementSpeedCmPerSecond.Reset();
	NotifyMovementSpeedCommitted(UpdatedEntityCount);
	UE_LOG(
		LogGuLiCommanderMass,
		Display,
		TEXT("Applied Soldier movement speed %.3f cm/s to %d Mass entities at the 30 Hz authority boundary."),
		MovementSpeedCentimetersPerSecond,
		UpdatedEntityCount);
}

void UGuLiBattleAuthoritySubsystem::NotifyMovementSpeedCommitted(
	const int32 AppliedEntityCount) const
{
	if (UWorld* World = GetWorld())
	{
		if (UGuLiRuntimeTuningSubsystem* RuntimeTuning =
			World->GetSubsystem<UGuLiRuntimeTuningSubsystem>())
		{
			RuntimeTuning->NotifySoldierMovementSpeedCommitted(
				MovementSpeedCentimetersPerSecond,
				AppliedEntityCount);
		}
	}
}

bool UGuLiBattleAuthoritySubsystem::ApplyDamage(const FGuLiSoldierId SoldierId, const uint8 Amount)
{
	if (!AuthorityState || Amount == 0u || !SoldierId.IsValid())
	{
		return false;
	}
	const int32* SoldierIndex = AuthorityState->SoldierIndexById.Find(SoldierId.Value);
	if (!SoldierIndex || !AuthorityState->Soldiers.IsValidIndex(*SoldierIndex))
	{
		return false;
	}

	GuLiCommanderMassPrivate::FSoldierRuntime& Soldier = AuthorityState->Soldiers[*SoldierIndex];
	if (!Soldier.IsAlive())
	{
		return false;
	}
	Soldier.Health = Amount >= Soldier.Health ? 0u : static_cast<uint8>(Soldier.Health - Amount);
	++Soldier.StateRevision;
	FMassEntityManager& EntityManager = AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
	FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(
		Soldier.Entity);
	Health.Health = Soldier.Health;
	Health.bDead = Soldier.Health == 0u;
	if (Soldier.Health == 0u)
	{
		Soldier.ActiveOrderId = 0u;
		Soldier.Velocity = FVector::ZeroVector;
		Soldier.DeathSimulationSeconds = AuthorityState->SimulationSeconds;
		Health.WreckSecondsRemaining = WreckLifetimeSeconds;
		FGuLiMassOrderFragment& Order = EntityManager.GetFragmentDataChecked<FGuLiMassOrderFragment>(
			Soldier.Entity);
		Order.ActiveOrderId = 0u;
		Order.OrderRevision = Soldier.StateRevision;
		Order.bHasMoveTarget = false;
		EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Soldier.Entity).Value = FVector::ZeroVector;
		EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity).CreateNewAction(
			EMassMovementAction::Stand,
			*GetWorld());
		EntityManager.RemoveFragmentFromEntity(
			Soldier.Entity,
			FMassNavigationObstacleGridCellLocationFragment::StaticStruct());
	}
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
		FGuLiSoldierStateItem& State = OutStates.AddDefaulted_GetRef();
		State.SoldierId = Soldier.SoldierId;
		State.Team = Soldier.Team;
		State.LifeState = Soldier.IsAlive()
			? EGuLiSoldierLifeState::Alive
			: EGuLiSoldierLifeState::Destroyed;
		State.Health = Soldier.Health;
		State.StateRevision = Soldier.StateRevision;
		State.ActiveOrderId = Soldier.ActiveOrderId;
	}
}

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
	SortedIndices.Sort([this](const int32 LhsIndex, const int32 RhsIndex)
	{
		const GuLiCommanderMassPrivate::FSoldierRuntime& Lhs = AuthorityState->Soldiers[LhsIndex];
		const GuLiCommanderMassPrivate::FSoldierRuntime& Rhs = AuthorityState->Soldiers[RhsIndex];
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
	constexpr double MaximumEncodableRelativeCentimeters =
		static_cast<double>(MAX_int16 - 1) * GULI_POSE_QUANTIZATION_CENTIMETERS;
	TArray<TArray<int32>> ChunkSoldierIndices;
	TArray<FVector> ChunkLocationSums;
	ChunkSoldierIndices.Reserve(FMath::DivideAndRoundUp(SortedIndices.Num(), ChunkSize));
	ChunkLocationSums.Reserve(ChunkSoldierIndices.Max());
	for (const int32 SoldierIndex : SortedIndices)
	{
		const FVector CandidateLocation = AuthorityState->Soldiers[SoldierIndex].Location;
		bool bStartNewChunk = ChunkSoldierIndices.IsEmpty()
			|| ChunkSoldierIndices.Last().Num() >= ChunkSize;
		if (!bStartNewChunk)
		{
			const TArray<int32>& CurrentChunk = ChunkSoldierIndices.Last();
			const FVector CandidateAnchor =
				(ChunkLocationSums.Last() + CandidateLocation)
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
			GuLiCommanderMassPrivate::FSoldierRuntime& Soldier =
				AuthorityState->Soldiers[ChunkIndices[Offset]];
			if (Soldier.LastCapturedPoseFrameSequence != 0u)
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
			const FVector QuantizedAnchor(
				FMath::RoundToDouble(Chunk.Anchor.X),
				FMath::RoundToDouble(Chunk.Anchor.Y),
				FMath::RoundToDouble(Chunk.Anchor.Z));
			const FVector LocallyReconstructedLocation =
				QuantizedAnchor + Pose.GetRelativeLocationCentimeters();
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
			Pose.Flags = 0u;
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

int32 UGuLiBattleAuthoritySubsystem::GetActiveOrderFormationCount() const
{
	return AuthorityState ? AuthorityState->OrderFormations.Num() : 0;
}

int32 UGuLiBattleAuthoritySubsystem::GetAuthoritativeMemberCount() const
{
	return AuthorityState ? AuthorityState->Soldiers.Num() : 0;
}

uint32 UGuLiBattleAuthoritySubsystem::GetServerSimTick() const
{
	return AuthorityState ? AuthorityState->ServerSimTick : 0u;
}

bool UGuLiBattleAuthoritySubsystem::HasSpawnedAuthorityPopulation() const
{
	return AuthorityState && AuthorityState->bPopulationSpawned;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"

#include "NavigationData.h"
#include "NavigationSystem.h"

namespace GuLiCommanderNavigationPolicy
{
	FName GetRequiredAgentName()
	{
		static const FName RequiredAgentName(TEXT("CommanderSoldier"));
		return RequiredAgentName;
	}

	const FNavDataConfig* FindRequiredAgentConfig(
		const TConstArrayView<FNavDataConfig> SupportedAgents)
	{
		const FName RequiredAgentName = GetRequiredAgentName();
		for (const FNavDataConfig& Config : SupportedAgents)
		{
			if (Config.Name == RequiredAgentName
				&& FMath::IsNearlyEqual(
					Config.AgentRadius,
					RequiredAgentRadiusCentimeters,
					0.5f))
			{
				return &Config;
			}
		}
		return nullptr;
	}

	ANavigationData* ResolveRequiredNavigationData(UNavigationSystemV1& NavigationSystem)
	{
		const FNavDataConfig* RequiredConfig = FindRequiredAgentConfig(
			NavigationSystem.GetSupportedAgents());
		if (!RequiredConfig)
		{
			return nullptr;
		}

		ANavigationData* NavigationData = NavigationSystem.GetNavDataForAgentName(
			RequiredConfig->Name);
		if (!NavigationData)
		{
			return nullptr;
		}

		const FNavDataConfig& ResolvedConfig = NavigationData->GetConfig();
		return ResolvedConfig.Name == RequiredConfig->Name
			&& FMath::IsNearlyEqual(
				ResolvedConfig.AgentRadius,
				RequiredAgentRadiusCentimeters,
				0.5f)
			? NavigationData
			: nullptr;
	}

	FVector MakeFormationSlotOffset(
		const int32 SlotIndex,
		const float SpacingCentimeters,
		const int32 RequestedColumnCount)
	{
		if (SlotIndex < 0 || SlotIndex >= FormationMemberCapacity
			|| !FMath::IsFinite(SpacingCentimeters) || SpacingCentimeters <= 0.0f)
		{
			return FVector::ZeroVector;
		}

		const int32 ColumnCount = FMath::Clamp(
			RequestedColumnCount,
			1,
			MaximumFormationColumns);
		const int32 RowCount = FMath::DivideAndRoundUp(
			FormationMemberCapacity,
			ColumnCount);
		const int32 Row = SlotIndex / ColumnCount;
		const int32 Column = SlotIndex % ColumnCount;
		const int32 SlotsInRow = FMath::Min(
			ColumnCount,
			FormationMemberCapacity - Row * ColumnCount);
		return FVector(
			(static_cast<float>(RowCount - 1) * 0.5f - static_cast<float>(Row))
				* SpacingCentimeters,
			(static_cast<float>(Column) - static_cast<float>(SlotsInRow - 1) * 0.5f)
				* SpacingCentimeters,
			0.0f);
	}

	bool IsProjectedTargetAcceptable(
		const FVector& RequestedTarget,
		const FVector& ProjectedTarget,
		const float AgentRadiusCentimeters)
	{
		if (RequestedTarget.ContainsNaN() || ProjectedTarget.ContainsNaN()
			|| !FMath::IsFinite(AgentRadiusCentimeters)
			|| AgentRadiusCentimeters < 0.0f)
		{
			return false;
		}
		return FVector::DistSquared2D(RequestedTarget, ProjectedTarget)
			<= FMath::Square(static_cast<double>(AgentRadiusCentimeters));
	}

	float CalculateArrivalDomainRadiusCentimeters(
		const int32 InitialAcceptedMemberCount,
		const float AgentRadiusCentimeters,
		const float PaddingCentimeters)
	{
		if (InitialAcceptedMemberCount <= 0
			|| !FMath::IsFinite(AgentRadiusCentimeters)
			|| !FMath::IsFinite(PaddingCentimeters)
			|| AgentRadiusCentimeters < 0.0f
			|| PaddingCentimeters < 0.0f)
		{
			return 0.0f;
		}

		int32 RingCount = 0;
		while (1ll + 3ll * RingCount * (RingCount + 1ll) < InitialAcceptedMemberCount)
		{
			++RingCount;
		}
		return 2.0f * AgentRadiusCentimeters * static_cast<float>(RingCount)
			+ PaddingCentimeters;
	}

	float CalculateLooseArrivalHoldRadiusCentimeters(
		const float ArrivalDomainRadiusCentimeters,
		const float HysteresisCentimeters)
	{
		if (!FMath::IsFinite(ArrivalDomainRadiusCentimeters)
			|| !FMath::IsFinite(HysteresisCentimeters)
			|| ArrivalDomainRadiusCentimeters < 0.0f
			|| HysteresisCentimeters < 0.0f)
		{
			return 0.0f;
		}
		return FMath::Max(500.0f, ArrivalDomainRadiusCentimeters - HysteresisCentimeters);
	}

	float CalculateLooseArrivalMaximumLaneOffsetCentimeters(
		const float ArrivalDomainRadiusCentimeters,
		const float HysteresisCentimeters,
		const float AgentRadiusCentimeters,
		const float MovementSpeedCentimetersPerSecond,
		const float FixedDeltaSeconds)
	{
		if (!FMath::IsFinite(AgentRadiusCentimeters)
			|| !FMath::IsFinite(MovementSpeedCentimetersPerSecond)
			|| !FMath::IsFinite(FixedDeltaSeconds)
			|| AgentRadiusCentimeters < 0.0f
			|| MovementSpeedCentimetersPerSecond < 0.0f
			|| FixedDeltaSeconds < 0.0f)
		{
			return 0.0f;
		}
		const float HoldRadius = CalculateLooseArrivalHoldRadiusCentimeters(
			ArrivalDomainRadiusCentimeters,
			HysteresisCentimeters);
		const float CaptureMargin = FMath::Max(
			AgentRadiusCentimeters,
			2.0f * MovementSpeedCentimetersPerSecond * FixedDeltaSeconds);
		return FMath::Max(0.0f, HoldRadius - CaptureMargin);
	}

	bool HasEnteredLooseArrivalTerminalPhase(
		const int32 PathPointIndex,
		const int32 PathPointCount,
		const FVector& GuideAnchor,
		const FVector& TargetAnchor,
		const float ArrivalDomainRadiusCentimeters,
		const float ApproachPaddingCentimeters)
	{
		if (PathPointCount < 2 || PathPointIndex != PathPointCount - 1
			|| GuideAnchor.ContainsNaN() || TargetAnchor.ContainsNaN()
			|| !FMath::IsFinite(ArrivalDomainRadiusCentimeters)
			|| !FMath::IsFinite(ApproachPaddingCentimeters)
			|| ArrivalDomainRadiusCentimeters < 0.0f
			|| ApproachPaddingCentimeters < 0.0f)
		{
			return false;
		}

		const double TerminalApproachRadius =
			static_cast<double>(ArrivalDomainRadiusCentimeters)
			+ static_cast<double>(ApproachPaddingCentimeters);
		return FVector::DistSquared2D(GuideAnchor, TargetAnchor)
			<= FMath::Square(TerminalApproachRadius);
	}

	int32 AdvanceMemberPathPointIndex(
		const TConstArrayView<FVector> PathPoints,
		const int32 CurrentPathPointIndex,
		const FVector& MemberLocation,
		const float WaypointToleranceCentimeters,
		const float MaximumCrossTrackCentimeters)
	{
		if (PathPoints.Num() < 2 || MemberLocation.ContainsNaN()
			|| !FMath::IsFinite(WaypointToleranceCentimeters)
			|| !FMath::IsFinite(MaximumCrossTrackCentimeters)
			|| WaypointToleranceCentimeters < 0.0f
			|| MaximumCrossTrackCentimeters < 0.0f)
		{
			return FMath::Clamp(CurrentPathPointIndex, 0, FMath::Max(0, PathPoints.Num() - 1));
		}

		int32 Result = FMath::Clamp(CurrentPathPointIndex, 1, PathPoints.Num() - 1);
		while (Result < PathPoints.Num() - 1)
		{
			FVector Segment = PathPoints[Result] - PathPoints[Result - 1];
			Segment.Z = 0.0f;
			if (Segment.ContainsNaN() || Segment.SizeSquared2D() <= 1.0)
			{
				++Result;
				continue;
			}
			FVector FromWaypoint = MemberLocation - PathPoints[Result];
			FromWaypoint.Z = 0.0f;
			const bool bReachedWaypoint = FromWaypoint.SizeSquared2D()
				<= FMath::Square(static_cast<double>(WaypointToleranceCentimeters));
			const FVector SegmentDirection = Segment.GetSafeNormal2D();
			const FVector SegmentRight(-SegmentDirection.Y, SegmentDirection.X, 0.0f);
			const bool bPassedWaypointPlane = FVector::DotProduct(
				FromWaypoint,
				SegmentDirection) >= 0.0f
				&& FMath::Abs(FVector::DotProduct(FromWaypoint, SegmentRight))
					<= MaximumCrossTrackCentimeters;
			if (!bReachedWaypoint && !bPassedWaypointPlane)
			{
				break;
			}
			++Result;
			// A member may fold degenerate points, but never crosses two physical turns in one step.
			break;
		}
		return Result;
	}

	bool HasClearedFinalTurn(
		const FFinalPathFrame& FinalPathFrame,
		const int32 MemberPathPointIndex)
	{
		if (!FinalPathFrame.bHasUsableDirection || MemberPathPointIndex < 0)
		{
			return false;
		}
		return !FinalPathFrame.bRequiresTailClear
			|| (FinalPathFrame.TailClearPathPointIndex != INDEX_NONE
				&& MemberPathPointIndex > FinalPathFrame.TailClearPathPointIndex);
	}

	FVector CalculatePathLaneWaypoint(
		const TConstArrayView<FVector> PathPoints,
		const int32 PathPointIndex,
		const float LateralOffsetCentimeters)
	{
		if (PathPoints.Num() < 2
			|| PathPointIndex <= 0
			|| PathPointIndex >= PathPoints.Num()
			|| !FMath::IsFinite(LateralOffsetCentimeters))
		{
			return FVector::ZeroVector;
		}
		FVector Segment = PathPoints[PathPointIndex] - PathPoints[PathPointIndex - 1];
		Segment.Z = 0.0f;
		if (Segment.ContainsNaN() || Segment.SizeSquared2D() <= 1.0)
		{
			return PathPoints[PathPointIndex];
		}
		const FVector SegmentDirection = Segment.GetSafeNormal2D();
		const FVector SegmentRight(-SegmentDirection.Y, SegmentDirection.X, 0.0f);
		return PathPoints[PathPointIndex] + SegmentRight * LateralOffsetCentimeters;
	}

	FLooseArrivalMemberState UpdateLooseArrivalMemberState(
		const FLooseArrivalMemberState& PreviousState,
		const bool bTailClearObserved,
		const bool bFinalCorridorActive,
		const float DistanceToTargetCentimeters,
		const float ArrivalDomainRadiusCentimeters,
		const float HysteresisCentimeters)
	{
		FLooseArrivalMemberState Result = PreviousState;
		Result.bTailCleared |= bTailClearObserved;
		if (!FMath::IsFinite(DistanceToTargetCentimeters)
			|| !FMath::IsFinite(ArrivalDomainRadiusCentimeters)
			|| !FMath::IsFinite(HysteresisCentimeters)
			|| DistanceToTargetCentimeters < 0.0f
			|| ArrivalDomainRadiusCentimeters < 0.0f
			|| HysteresisCentimeters < 0.0f)
		{
			return Result;
		}

		const float HoldRadius = CalculateLooseArrivalHoldRadiusCentimeters(
			ArrivalDomainRadiusCentimeters,
			HysteresisCentimeters);
		Result.bHasReachedArrival |= bFinalCorridorActive
			&& Result.bTailCleared
			&& DistanceToTargetCentimeters <= HoldRadius;
		if (!Result.bHasReachedArrival)
		{
			Result.bRecovering = false;
		}
		else if (Result.bRecovering)
		{
			Result.bRecovering = DistanceToTargetCentimeters > HoldRadius;
		}
		else
		{
			Result.bRecovering = DistanceToTargetCentimeters > ArrivalDomainRadiusCentimeters;
		}
		return Result;
	}

	FVector CalculateFinalCorridorLaneTarget(
		const FFinalPathFrame& FinalPathFrame,
		const FVector& TargetAnchor,
		const FVector& MemberLocation,
		const float FrozenLateralOffsetCentimeters,
		const float LookAheadCentimeters)
	{
		if (!FinalPathFrame.bHasUsableDirection || TargetAnchor.ContainsNaN()
			|| MemberLocation.ContainsNaN()
			|| !FMath::IsFinite(FrozenLateralOffsetCentimeters)
			|| !FMath::IsFinite(LookAheadCentimeters)
			|| LookAheadCentimeters < 0.0f)
		{
			return MemberLocation;
		}

		const FVector Right(-FinalPathFrame.Forward.Y, FinalPathFrame.Forward.X, 0.0f);
		const float CurrentLateralOffset = FVector::DotProduct(
			MemberLocation - TargetAnchor,
			Right);
		const float RemainingLongitudinalDistance = FVector::DotProduct(
			TargetAnchor - MemberLocation,
			FinalPathFrame.Forward);
		return MemberLocation
			+ FinalPathFrame.Forward * FMath::Clamp(
				RemainingLongitudinalDistance,
				-LookAheadCentimeters,
				LookAheadCentimeters)
			+ Right * (FrozenLateralOffsetCentimeters - CurrentLateralOffset);
	}

	int32 SelectTransitColumnCount(const TConstArrayView<uint8> FitsByColumnCount)
	{
		const int32 HighestRepresentedColumn = FMath::Min(
			FitsByColumnCount.Num(),
			MaximumFormationColumns);
		for (int32 ColumnCount = HighestRepresentedColumn; ColumnCount >= 1; --ColumnCount)
		{
			if (FitsByColumnCount[ColumnCount - 1] != 0u)
			{
				return ColumnCount;
			}
		}
		return 1;
	}

	FFinalPathFrame ResolveFinalPathFrame(
		const TConstArrayView<FVector> PathPoints,
		const FVector& FallbackStart,
		const FVector& FallbackEnd)
	{
		constexpr double MinimumSegmentLengthSquared = 1.0;
		constexpr double SameDirectionDotThreshold = 0.996194698; // Five degrees.
		FFinalPathFrame Result;
		int32 FinalSegmentStartIndex = INDEX_NONE;
		for (int32 PointIndex = PathPoints.Num() - 1; PointIndex > 0; --PointIndex)
		{
			FVector Segment = PathPoints[PointIndex] - PathPoints[PointIndex - 1];
			Segment.Z = 0.0f;
			if (!Segment.ContainsNaN() && Segment.SizeSquared2D() > MinimumSegmentLengthSquared)
			{
				Result.Forward = Segment.GetSafeNormal2D();
				Result.TailClearOrigin = PathPoints[PointIndex - 1];
				Result.YawDegrees = FRotator::NormalizeAxis(Result.Forward.Rotation().Yaw);
				Result.bHasUsableDirection = true;
				FinalSegmentStartIndex = PointIndex - 1;
				break;
			}
		}

		if (!Result.bHasUsableDirection)
		{
			FVector FallbackDirection = FallbackEnd - FallbackStart;
			FallbackDirection.Z = 0.0f;
			if (!FallbackDirection.ContainsNaN()
				&& FallbackDirection.SizeSquared2D() > MinimumSegmentLengthSquared)
			{
				Result.Forward = FallbackDirection.GetSafeNormal2D();
				Result.TailClearOrigin = FallbackStart;
				Result.YawDegrees = FRotator::NormalizeAxis(Result.Forward.Rotation().Yaw);
				Result.bHasUsableDirection = true;
			}
			return Result;
		}

		int32 TerminalRunStartIndex = FinalSegmentStartIndex;
		for (int32 SegmentEndIndex = FinalSegmentStartIndex;
			SegmentEndIndex > 0;
			--SegmentEndIndex)
		{
			FVector PreviousSegment =
				PathPoints[SegmentEndIndex] - PathPoints[SegmentEndIndex - 1];
			PreviousSegment.Z = 0.0f;
			if (PreviousSegment.ContainsNaN()
				|| PreviousSegment.SizeSquared2D() <= MinimumSegmentLengthSquared)
			{
				continue;
			}

			const FVector PreviousDirection = PreviousSegment.GetSafeNormal2D();
			if (FVector::DotProduct(PreviousDirection, Result.Forward)
				>= SameDirectionDotThreshold)
			{
				Result.TailClearOrigin = PathPoints[SegmentEndIndex - 1];
				TerminalRunStartIndex = SegmentEndIndex - 1;
				continue;
			}

			Result.bRequiresTailClear = true;
			break;
		}
		for (int32 PointIndex = 1; PointIndex <= TerminalRunStartIndex; ++PointIndex)
		{
			Result.TailClearPathDistanceCentimeters +=
				FVector::Dist2D(PathPoints[PointIndex - 1], PathPoints[PointIndex]);
		}
		Result.TailClearPathPointIndex = Result.bRequiresTailClear
			? TerminalRunStartIndex
			: INDEX_NONE;
		return Result;
	}

	bool AreActiveMembersInsideArrivalDomainAndPastTail(
		const FFinalPathFrame& FinalPathFrame,
		const TConstArrayView<FVector> PathPoints,
		const FVector& TargetAnchor,
		const TConstArrayView<FFormationMemberProgressSample> Members,
		const float ArrivalDomainRadiusCentimeters,
		const float TailClearToleranceCentimeters)
	{
		static_cast<void>(PathPoints);
		static_cast<void>(TargetAnchor);
		static_cast<void>(ArrivalDomainRadiusCentimeters);
		static_cast<void>(TailClearToleranceCentimeters);
		if (!FinalPathFrame.bHasUsableDirection || TargetAnchor.ContainsNaN()
			|| !FMath::IsFinite(ArrivalDomainRadiusCentimeters)
			|| !FMath::IsFinite(TailClearToleranceCentimeters))
		{
			return false;
		}

		int32 ActiveMemberCount = 0;
		for (const FFormationMemberProgressSample& Member : Members)
		{
			if (!Member.bAlive || !Member.bFollowsOrder)
			{
				continue;
			}
			++ActiveMemberCount;
			if (Member.Location.ContainsNaN()
				|| !Member.bTailCleared
				|| !Member.bHasReachedArrival)
			{
				return false;
			}
		}
		return ActiveMemberCount > 0;
	}

	bool ShouldCompleteOrder(
		const bool bGuideAtFinal,
		const FFinalPathFrame& FinalPathFrame,
		const TConstArrayView<FVector> PathPoints,
		const FVector& TargetAnchor,
		const TConstArrayView<FFormationMemberProgressSample> Members,
		const float ArrivalDomainRadiusCentimeters,
		const float TailClearToleranceCentimeters)
	{
		return bGuideAtFinal
			&& AreActiveMembersInsideArrivalDomainAndPastTail(
				FinalPathFrame,
				PathPoints,
				TargetAnchor,
				Members,
				ArrivalDomainRadiusCentimeters,
				TailClearToleranceCentimeters);
	}

	bool ShouldCompleteBatchOrder(
		const TConstArrayView<FBatchOrderFormationCompletionSample> Formations)
	{
		int32 ActiveFormationCount = 0;
		for (const FBatchOrderFormationCompletionSample& Formation : Formations)
		{
			if (!Formation.bHasActiveMembers)
			{
				continue;
			}
			++ActiveFormationCount;
			if (!Formation.bPathValid || !Formation.bGuideAndMembersReady)
			{
				return false;
			}
		}
		return ActiveFormationCount > 0;
	}

}

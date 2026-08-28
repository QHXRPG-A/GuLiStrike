// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"

#include "NavigationData.h"
#include "NavigationSystem.h"

namespace GuLiCommanderNavigationPolicy
{
	namespace Private
	{
		double CalculateClosestPathProgress2D(
			const TConstArrayView<FVector> PathPoints,
			const FVector& Location)
		{
			double BestDistanceSquared = TNumericLimits<double>::Max();
			double BestProgress = -1.0;
			double AccumulatedDistance = 0.0;
			for (int32 PointIndex = 1; PointIndex < PathPoints.Num(); ++PointIndex)
			{
				FVector Segment = PathPoints[PointIndex] - PathPoints[PointIndex - 1];
				Segment.Z = 0.0f;
				const double SegmentLength = Segment.Size2D();
				if (Segment.ContainsNaN() || SegmentLength <= 1.0)
				{
					continue;
				}
				FVector FromStart = Location - PathPoints[PointIndex - 1];
				FromStart.Z = 0.0f;
				const double Alpha = FMath::Clamp(
					static_cast<double>(FVector::DotProduct(FromStart, Segment))
						/ FMath::Square(SegmentLength),
					0.0,
					1.0);
				const FVector Projected = PathPoints[PointIndex - 1] + Segment * Alpha;
				const double DistanceSquared = FVector::DistSquared2D(Location, Projected);
				const double Progress = AccumulatedDistance + SegmentLength * Alpha;
				if (DistanceSquared < BestDistanceSquared
					|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
						&& Progress > BestProgress))
				{
					BestDistanceSquared = DistanceSquared;
					BestProgress = Progress;
				}
				AccumulatedDistance += SegmentLength;
			}
			return BestProgress;
		}
	}

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
		if (!FinalPathFrame.bHasUsableDirection || TargetAnchor.ContainsNaN()
			|| !FMath::IsFinite(ArrivalDomainRadiusCentimeters)
			|| !FMath::IsFinite(TailClearToleranceCentimeters))
		{
			return false;
		}

		const double ArrivalRadiusSquared = FMath::Square(
			static_cast<double>(FMath::Max(0.0f, ArrivalDomainRadiusCentimeters)));
		const double TailClearTolerance = FMath::Max(0.0f, TailClearToleranceCentimeters);
		int32 ActiveMemberCount = 0;
		for (const FFormationMemberProgressSample& Member : Members)
		{
			if (!Member.bAlive || !Member.bFollowsOrder)
			{
				continue;
			}
			++ActiveMemberCount;
			if (Member.Location.ContainsNaN())
			{
				return false;
			}
			if (FVector::DistSquared2D(Member.Location, TargetAnchor) > ArrivalRadiusSquared)
			{
				return false;
			}
			if (!FinalPathFrame.bRequiresTailClear)
			{
				continue;
			}

			const double MemberPathProgress = Private::CalculateClosestPathProgress2D(
				PathPoints,
				Member.Location);
			if (MemberPathProgress < 0.0
				|| MemberPathProgress + TailClearTolerance
					< FinalPathFrame.TailClearPathDistanceCentimeters)
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

	FVector CalculateSharedPathFollowDirection(
		const TConstArrayView<FVector> PathPoints,
		const FVector& MemberLocation,
		const float LookAheadCentimeters)
	{
		if (PathPoints.Num() < 2 || MemberLocation.ContainsNaN()
			|| !FMath::IsFinite(LookAheadCentimeters))
		{
			return FVector::ZeroVector;
		}

		constexpr double MinimumSegmentLengthSquared = 1.0;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		int32 BestSegmentEndIndex = INDEX_NONE;
		double BestAlpha = 0.0;
		for (int32 SegmentEndIndex = 1; SegmentEndIndex < PathPoints.Num(); ++SegmentEndIndex)
		{
			FVector Segment = PathPoints[SegmentEndIndex] - PathPoints[SegmentEndIndex - 1];
			Segment.Z = 0.0f;
			const double SegmentLengthSquared = Segment.SizeSquared2D();
			if (Segment.ContainsNaN() || SegmentLengthSquared <= MinimumSegmentLengthSquared)
			{
				continue;
			}
			FVector FromStart = MemberLocation - PathPoints[SegmentEndIndex - 1];
			FromStart.Z = 0.0f;
			const double Alpha = FMath::Clamp(
				static_cast<double>(FVector::DotProduct(FromStart, Segment)) / SegmentLengthSquared,
				0.0,
				1.0);
			const FVector Projected = PathPoints[SegmentEndIndex - 1] + Segment * Alpha;
			const double DistanceSquared = FVector::DistSquared2D(MemberLocation, Projected);
			if (DistanceSquared < BestDistanceSquared
				|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
					&& SegmentEndIndex > BestSegmentEndIndex))
			{
				BestDistanceSquared = DistanceSquared;
				BestSegmentEndIndex = SegmentEndIndex;
				BestAlpha = Alpha;
			}
		}
		if (BestSegmentEndIndex == INDEX_NONE)
		{
			return FVector::ZeroVector;
		}

		FVector Cursor = FMath::Lerp(
			PathPoints[BestSegmentEndIndex - 1],
			PathPoints[BestSegmentEndIndex],
			BestAlpha);
		double RemainingLookAhead = FMath::Max(0.0f, LookAheadCentimeters);
		for (int32 SegmentEndIndex = BestSegmentEndIndex;
			SegmentEndIndex < PathPoints.Num();
			++SegmentEndIndex)
		{
			FVector Segment = PathPoints[SegmentEndIndex] - Cursor;
			Segment.Z = 0.0f;
			const double SegmentLength = Segment.Size2D();
			if (SegmentLength > UE_DOUBLE_SMALL_NUMBER)
			{
				if (RemainingLookAhead <= SegmentLength)
				{
					Cursor += Segment / SegmentLength * RemainingLookAhead;
					break;
				}
				RemainingLookAhead -= SegmentLength;
			}
			Cursor = PathPoints[SegmentEndIndex];
		}
		return (Cursor - MemberLocation).GetSafeNormal2D();
	}
}

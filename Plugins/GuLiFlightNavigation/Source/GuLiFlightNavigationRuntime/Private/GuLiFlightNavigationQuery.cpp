#include "GuLiFlightNavigationQuery.h"

#include "Algo/Reverse.h"
#include "Async/Async.h"

namespace
{
	struct FGuLiFlightNavOpenRecord
	{
		int32 CellIndex = INDEX_NONE;
		float Score = BIG_NUMBER;
		uint64 StableId = 0;
	};

	struct FGuLiFlightNavSegmentFrontierRecord
	{
		int32 CellIndex = INDEX_NONE;
		double EntryAlpha = UE_DOUBLE_BIG_NUMBER;
		uint64 StableId = 0;
	};

	bool ComesBefore(const FGuLiFlightNavOpenRecord& A, const FGuLiFlightNavOpenRecord& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score, UE_KINDA_SMALL_NUMBER))
		{
			return A.Score < B.Score;
		}
		if (A.StableId != B.StableId)
		{
			return A.StableId < B.StableId;
		}
		return A.CellIndex < B.CellIndex;
	}

	void HeapPushMin(TArray<FGuLiFlightNavOpenRecord>& Heap, const FGuLiFlightNavOpenRecord& Record)
	{
		int32 ChildIndex = Heap.Add(Record);
		while (ChildIndex > 0)
		{
			const int32 ParentIndex = (ChildIndex - 1) / 2;
			if (!ComesBefore(Heap[ChildIndex], Heap[ParentIndex]))
			{
				break;
			}
			Heap.Swap(ChildIndex, ParentIndex);
			ChildIndex = ParentIndex;
		}
	}

	FGuLiFlightNavOpenRecord HeapPopMin(TArray<FGuLiFlightNavOpenRecord>& Heap)
	{
		check(!Heap.IsEmpty());
		FGuLiFlightNavOpenRecord Result = Heap[0];
		if (Heap.Num() == 1)
		{
			Heap.Pop(EAllowShrinking::No);
			return Result;
		}

		Heap[0] = Heap.Pop(EAllowShrinking::No);
		int32 ParentIndex = 0;
		for (;;)
		{
			const int32 LeftChild = ParentIndex * 2 + 1;
			if (LeftChild >= Heap.Num())
			{
				break;
			}

			const int32 RightChild = LeftChild + 1;
			int32 BestChild = LeftChild;
			if (RightChild < Heap.Num() && ComesBefore(Heap[RightChild], Heap[LeftChild]))
			{
				BestChild = RightChild;
			}

			if (!ComesBefore(Heap[BestChild], Heap[ParentIndex]))
			{
				break;
			}
			Heap.Swap(ParentIndex, BestChild);
			ParentIndex = BestChild;
		}

		return Result;
	}

	bool ComesBefore(
		const FGuLiFlightNavSegmentFrontierRecord& A,
		const FGuLiFlightNavSegmentFrontierRecord& B)
	{
		if (A.EntryAlpha != B.EntryAlpha)
		{
			return A.EntryAlpha < B.EntryAlpha;
		}
		if (A.StableId != B.StableId)
		{
			return A.StableId < B.StableId;
		}
		return A.CellIndex < B.CellIndex;
	}

	void HeapPushMin(
		TArray<FGuLiFlightNavSegmentFrontierRecord>& Heap,
		const FGuLiFlightNavSegmentFrontierRecord& Record)
	{
		int32 ChildIndex = Heap.Add(Record);
		while (ChildIndex > 0)
		{
			const int32 ParentIndex = (ChildIndex - 1) / 2;
			if (!ComesBefore(Heap[ChildIndex], Heap[ParentIndex]))
			{
				break;
			}
			Heap.Swap(ChildIndex, ParentIndex);
			ChildIndex = ParentIndex;
		}
	}

	FGuLiFlightNavSegmentFrontierRecord HeapPopMin(
		TArray<FGuLiFlightNavSegmentFrontierRecord>& Heap)
	{
		check(!Heap.IsEmpty());
		FGuLiFlightNavSegmentFrontierRecord Result = Heap[0];
		if (Heap.Num() == 1)
		{
			Heap.Pop(EAllowShrinking::No);
			return Result;
		}

		Heap[0] = Heap.Pop(EAllowShrinking::No);
		int32 ParentIndex = 0;
		for (;;)
		{
			const int32 LeftChild = ParentIndex * 2 + 1;
			if (LeftChild >= Heap.Num())
			{
				break;
			}

			const int32 RightChild = LeftChild + 1;
			int32 BestChild = LeftChild;
			if (RightChild < Heap.Num() && ComesBefore(Heap[RightChild], Heap[LeftChild]))
			{
				BestChild = RightChild;
			}
			if (!ComesBefore(Heap[BestChild], Heap[ParentIndex]))
			{
				break;
			}
			Heap.Swap(ParentIndex, BestChild);
			ParentIndex = BestChild;
		}
		return Result;
	}

	bool FindSegmentBoxInterval(
		const FVector& Start,
		const FVector& End,
		const FBox& Box,
		const double AlphaTolerance,
		double& OutEnterAlpha,
		double& OutExitAlpha)
	{
		OutEnterAlpha = 0.0;
		OutExitAlpha = 1.0;
		const FVector Segment = End - Start;
		constexpr double PositionToleranceCentimeters = 0.5;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double AxisStart = Start[Axis];
			const double AxisDelta = Segment[Axis];
			const double AxisMin = Box.Min[Axis];
			const double AxisMax = Box.Max[Axis];
			if (FMath::Abs(AxisDelta) <= UE_DOUBLE_SMALL_NUMBER)
			{
				if (AxisStart < AxisMin - PositionToleranceCentimeters
					|| AxisStart > AxisMax + PositionToleranceCentimeters)
				{
					return false;
				}
				continue;
			}

			double AxisEnter = (AxisMin - AxisStart) / AxisDelta;
			double AxisExit = (AxisMax - AxisStart) / AxisDelta;
			if (AxisEnter > AxisExit)
			{
				Swap(AxisEnter, AxisExit);
			}
			OutEnterAlpha = FMath::Max(OutEnterAlpha, AxisEnter);
			OutExitAlpha = FMath::Min(OutExitAlpha, AxisExit);
			if (OutEnterAlpha > OutExitAlpha + AlphaTolerance)
			{
				return false;
			}
		}
		return OutExitAlpha >= -AlphaTolerance && OutEnterAlpha <= 1.0 + AlphaTolerance;
	}

	bool FindSegmentPortalCrossingAlpha(
		const FVector& Start,
		const FVector& End,
		const FGuLiFlightNavPortal& Portal,
		double& OutAlpha)
	{
		OutAlpha = 0.0;
		const FVector Normal = Portal.Normal.GetSafeNormal();
		if (Normal.IsNearlyZero() || Portal.Center.ContainsNaN()
			|| Portal.Extent.ContainsNaN()
			|| Portal.Extent.X < 0.0 || Portal.Extent.Y < 0.0 || Portal.Extent.Z < 0.0)
		{
			return false;
		}

		const FVector Segment = End - Start;
		const double Denominator = FVector::DotProduct(Segment, Normal);
		if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const double Alpha = FVector::DotProduct(Portal.Center - Start, Normal) / Denominator;
		if (Alpha < -UE_DOUBLE_SMALL_NUMBER || Alpha > 1.0 + UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector Delta = Start + Segment * FMath::Clamp(Alpha, 0.0, 1.0) - Portal.Center;
		constexpr double PortalToleranceCentimeters = 0.5;
		const bool bCrossesPortal =
			FMath::Abs(Delta.X) <= static_cast<double>(Portal.Extent.X) + PortalToleranceCentimeters
			&& FMath::Abs(Delta.Y) <= static_cast<double>(Portal.Extent.Y) + PortalToleranceCentimeters
			&& FMath::Abs(Delta.Z) <= static_cast<double>(Portal.Extent.Z) + PortalToleranceCentimeters;
		if (bCrossesPortal)
		{
			OutAlpha = FMath::Clamp(Alpha, 0.0, 1.0);
		}
		return bCrossesPortal;
	}

	bool ContainsAgentCenter(
		const FBox& Bounds,
		const FVector& Point,
		const float AgentRadius)
	{
		if (Bounds.IsValid == 0 || Point.ContainsNaN() || !FMath::IsFinite(AgentRadius)
			|| AgentRadius < 0.0f)
		{
			return false;
		}
		const FVector Inset(AgentRadius);
		const FVector SafeMin = Bounds.Min + Inset;
		const FVector SafeMax = Bounds.Max - Inset;
		return SafeMin.X <= SafeMax.X && SafeMin.Y <= SafeMax.Y && SafeMin.Z <= SafeMax.Z
			&& Point.X >= SafeMin.X && Point.X <= SafeMax.X
			&& Point.Y >= SafeMin.Y && Point.Y <= SafeMax.Y
			&& Point.Z >= SafeMin.Z && Point.Z <= SafeMax.Z;
	}
}

int32 FGuLiFlightNavRuntimeGraph::FindContainingCell(const FVector& Point) const
{
	if (Nodes.IsEmpty() || Metadata.Bounds.IsValid == 0 || !Metadata.Bounds.IsInsideOrOn(Point))
	{
		return INDEX_NONE;
	}

	int32 NodeIndex = 0;
	for (int32 RemainingDepthGuard = 64; RemainingDepthGuard > 0 && Nodes.IsValidIndex(NodeIndex); --RemainingDepthGuard)
	{
		const FGuLiFlightNavOctreeNode& Node = Nodes[NodeIndex];
		if (!FBox::BuildAABB(Node.Center, Node.Extent).IsInsideOrOn(Point))
		{
			return INDEX_NONE;
		}

		if (Node.IsLeaf())
		{
			if (Cells.IsValidIndex(Node.LeafCellIndex) && Cells[Node.LeafCellIndex].GetBounds().IsInsideOrOn(Point))
			{
				return Node.LeafCellIndex;
			}
			return INDEX_NONE;
		}

		uint8 Octant = 0;
		Octant |= Point.X >= Node.Center.X ? 1 : 0;
		Octant |= Point.Y >= Node.Center.Y ? 2 : 0;
		Octant |= Point.Z >= Node.Center.Z ? 4 : 0;
		const uint8 OctantBit = static_cast<uint8>(1u << Octant);
		if ((Node.ChildMask & OctantBit) == 0)
		{
			return INDEX_NONE;
		}

		const uint8 EarlierChildrenMask = static_cast<uint8>(Node.ChildMask & (OctantBit - 1));
		const int32 DenseOffset = FPlatformMath::CountBits(static_cast<uint64>(EarlierChildrenMask));
		NodeIndex = Node.FirstChild + DenseOffset;
	}

	return INDEX_NONE;
}

bool FGuLiFlightNavRuntimeGraph::IsStructurallyValid() const
{
	return Metadata.FormatVersion == GuLiFlightNavigation::CurrentDataFormatVersion
		&& Metadata.Bounds.IsValid != 0
		&& !Nodes.IsEmpty()
		&& !Cells.IsEmpty();
}

void FGuLiFlightNavCancellationToken::Cancel()
{
	bCancelled.Store(true);
}

bool FGuLiFlightNavCancellationToken::IsCancelled() const
{
	return bCancelled.Load();
}

FGuLiFlightNavigationQuery::FGuLiFlightNavigationQuery(
	TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> InGraph)
	: Graph(MoveTemp(InGraph))
{
}

bool FGuLiFlightNavigationQuery::IsValid() const
{
	return Graph.IsValid() && Graph->IsStructurallyValid();
}

int32 FGuLiFlightNavigationQuery::FindContainingCell(const FVector& Point) const
{
	return IsValid() ? Graph->FindContainingCell(Point) : INDEX_NONE;
}

float FGuLiFlightNavigationQuery::ResolveAgentRadius(const float RequestedRadius) const
{
	if (!IsValid())
	{
		return -1.0f;
	}
	return RequestedRadius > 0.0f ? RequestedRadius : Graph->Metadata.BakedAgentRadius;
}

bool FGuLiFlightNavigationQuery::IsSegmentNavigable(
	const FVector& Start,
	const FVector& End,
	const float AgentRadius) const
{
	return ValidateAuthoritativeSegment(Start, End, AgentRadius)
		== EGuLiFlightNavSegmentStatus::Valid;
}

EGuLiFlightNavSegmentStatus FGuLiFlightNavigationQuery::ValidateEndpointsInSameComponent(
	const FVector& Start,
	const FVector& End,
	const float AgentRadius,
	int32* OutStartCell,
	int32* OutEndCell) const
{
	if (OutStartCell)
	{
		*OutStartCell = INDEX_NONE;
	}
	if (OutEndCell)
	{
		*OutEndCell = INDEX_NONE;
	}
	if (Start.ContainsNaN() || End.ContainsNaN() || !IsValid())
	{
		return EGuLiFlightNavSegmentStatus::InvalidData;
	}

	const float EffectiveRadius = ResolveAgentRadius(AgentRadius);
	if (EffectiveRadius < 0.0f || EffectiveRadius > Graph->Metadata.BakedAgentRadius + UE_KINDA_SMALL_NUMBER)
	{
		return EGuLiFlightNavSegmentStatus::InsufficientClearance;
	}
	if (!ContainsAgentCenter(Graph->Metadata.Bounds, Start, EffectiveRadius)
		|| !ContainsAgentCenter(Graph->Metadata.Bounds, End, EffectiveRadius))
	{
		return EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation;
	}

	const int32 StartCell = Graph->FindContainingCell(Start);
	const int32 EndCell = Graph->FindContainingCell(End);
	if (OutStartCell)
	{
		*OutStartCell = StartCell;
	}
	if (OutEndCell)
	{
		*OutEndCell = EndCell;
	}
	if (!Graph->Cells.IsValidIndex(StartCell) || !Graph->Cells.IsValidIndex(EndCell))
	{
		return EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation;
	}
	if (Graph->Cells[StartCell].Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius
		|| Graph->Cells[EndCell].Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius)
	{
		return EGuLiFlightNavSegmentStatus::InsufficientClearance;
	}
	const int32 ComponentId = Graph->Cells[StartCell].ComponentId;
	if (ComponentId == INDEX_NONE || Graph->Cells[EndCell].ComponentId != ComponentId)
	{
		return EGuLiFlightNavSegmentStatus::Disconnected;
	}
	return EGuLiFlightNavSegmentStatus::Valid;
}

EGuLiFlightNavSegmentStatus FGuLiFlightNavigationQuery::ValidateAuthoritativeSegment(
	const FVector& Start,
	const FVector& End,
	const float AgentRadius,
	int32* OutStartCell,
	int32* OutEndCell) const
{
	const EGuLiFlightNavSegmentStatus EndpointStatus = ValidateEndpointsInSameComponent(
		Start, End, AgentRadius, OutStartCell, OutEndCell);
	if (EndpointStatus != EGuLiFlightNavSegmentStatus::Valid)
	{
		return EndpointStatus;
	}

	const int32 StartCell = OutStartCell ? *OutStartCell : Graph->FindContainingCell(Start);
	const int32 EndCell = OutEndCell ? *OutEndCell : Graph->FindContainingCell(End);
	if (StartCell == EndCell)
	{
		return EGuLiFlightNavSegmentStatus::Valid;
	}
	const float EffectiveRadius = ResolveAgentRadius(AgentRadius);
	const int32 ComponentId = Graph->Cells[StartCell].ComponentId;

	// Follow only graph portals geometrically crossed by the submitted segment, in
	// non-decreasing line order. This remains a bounded topology check (each cell
	// expands at most once), not a path search, while correctly handling adaptive
	// octree leaves that are thinner than the former fixed sampling interval.
	const double SegmentLength = FVector::Distance(Start, End);
	constexpr double PositionToleranceCentimeters = 0.5;
	const double AlphaTolerance = FMath::Min(
		1.0, PositionToleranceCentimeters / FMath::Max(SegmentLength, PositionToleranceCentimeters));
	// A submitted movement segment normally crosses only a handful of octree leaves.
	// Keep traversal state proportional to those leaves instead of clearing and reserving
	// arrays sized to the entire baked graph for every avoidance probe.
	TMap<int32, double> EarliestEntryAlpha;
	TSet<int32> Expanded;
	TArray<FGuLiFlightNavSegmentFrontierRecord> Frontier;
	const int32 ExpectedCrossedCellCount = FMath::Min(Graph->Cells.Num(), 16);
	EarliestEntryAlpha.Reserve(ExpectedCrossedCellCount);
	Expanded.Reserve(ExpectedCrossedCellCount);
	Frontier.Reserve(ExpectedCrossedCellCount);
	EarliestEntryAlpha.Add(StartCell, 0.0);
	HeapPushMin(Frontier, { StartCell, 0.0, Graph->Cells[StartCell].StableId });

	bool bSawRelevantInvalidPortal = false;
	bool bSawRelevantInsufficientClearance = false;
	bool bSawRelevantDisconnectedCell = false;
	while (!Frontier.IsEmpty())
	{
		const FGuLiFlightNavSegmentFrontierRecord Current = HeapPopMin(Frontier);
		const double* KnownEntryAlpha = EarliestEntryAlpha.Find(Current.CellIndex);
		if (!Graph->Cells.IsValidIndex(Current.CellIndex)
			|| Expanded.Contains(Current.CellIndex)
			|| KnownEntryAlpha == nullptr
			|| Current.EntryAlpha > *KnownEntryAlpha + AlphaTolerance)
		{
			continue;
		}
		Expanded.Add(Current.CellIndex);
		if (Current.CellIndex == EndCell)
		{
			return EGuLiFlightNavSegmentStatus::Valid;
		}

		const FGuLiFlightNavCell& FromCell = Graph->Cells[Current.CellIndex];
		double FromEnterAlpha = 0.0;
		double FromExitAlpha = 0.0;
		if (!FindSegmentBoxInterval(
			Start, End, FromCell.GetBounds(), AlphaTolerance, FromEnterAlpha, FromExitAlpha))
		{
			return EGuLiFlightNavSegmentStatus::InvalidData;
		}
		if (FromCell.FirstLink < 0 || FromCell.LinkCount < 0
			|| FromCell.FirstLink + FromCell.LinkCount > Graph->Links.Num())
		{
			return EGuLiFlightNavSegmentStatus::InvalidData;
		}

		for (int32 LinkOffset = 0; LinkOffset < FromCell.LinkCount; ++LinkOffset)
		{
			const int32 LinkIndex = FromCell.FirstLink + LinkOffset;
			if (!Graph->Links.IsValidIndex(LinkIndex))
			{
				return EGuLiFlightNavSegmentStatus::InvalidData;
			}
			const FGuLiFlightNavLink& Link = Graph->Links[LinkIndex];
			if (!Graph->Cells.IsValidIndex(Link.ToCell))
			{
				return EGuLiFlightNavSegmentStatus::InvalidData;
			}

			double ToEnterAlpha = 0.0;
			double ToExitAlpha = 0.0;
			const FGuLiFlightNavCell& ToCell = Graph->Cells[Link.ToCell];
			if (!FindSegmentBoxInterval(
				Start, End, ToCell.GetBounds(), AlphaTolerance, ToEnterAlpha, ToExitAlpha)
				|| ToExitAlpha + AlphaTolerance < Current.EntryAlpha)
			{
				continue;
			}
			if (!Graph->Portals.IsValidIndex(Link.PortalIndex))
			{
				bSawRelevantInvalidPortal = true;
				continue;
			}

			const FGuLiFlightNavPortal& Portal = Graph->Portals[Link.PortalIndex];
			const bool bPortalJoinsTransition =
				(Portal.CellA == Current.CellIndex && Portal.CellB == Link.ToCell)
				|| (Portal.CellB == Current.CellIndex && Portal.CellA == Link.ToCell);
			double PortalAlpha = 0.0;
			const FVector PortalNormal = Portal.Normal.GetSafeNormal();
			if (PortalNormal.IsNearlyZero())
			{
				bSawRelevantInvalidPortal = true;
				continue;
			}
			const double DirectionProjection =
				FVector::DotProduct(End - Start, PortalNormal)
				* FVector::DotProduct(ToCell.Center - FromCell.Center, PortalNormal);
			if (bPortalJoinsTransition && DirectionProjection <= UE_DOUBLE_SMALL_NUMBER)
			{
				// Every baked portal has a reverse Link. It is not an invalid
				// transition; it simply points behind this directed segment.
				continue;
			}
			if (!bPortalJoinsTransition
				|| !FindSegmentPortalCrossingAlpha(Start, End, Portal, PortalAlpha)
				|| PortalAlpha + AlphaTolerance < Current.EntryAlpha
				|| FMath::Abs(PortalAlpha - FromExitAlpha) > AlphaTolerance
				|| FMath::Abs(PortalAlpha - ToEnterAlpha) > AlphaTolerance)
			{
				bSawRelevantInvalidPortal = true;
				continue;
			}
			if (Portal.Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius)
			{
				bSawRelevantInvalidPortal = true;
				continue;
			}
			if (ToCell.ComponentId != ComponentId)
			{
				bSawRelevantDisconnectedCell = true;
				continue;
			}
			if (ToCell.Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius)
			{
				bSawRelevantInsufficientClearance = true;
				continue;
			}

			const double* KnownDestinationAlpha = EarliestEntryAlpha.Find(Link.ToCell);
			if (!Expanded.Contains(Link.ToCell)
				&& (KnownDestinationAlpha == nullptr
					|| PortalAlpha + AlphaTolerance < *KnownDestinationAlpha))
			{
				EarliestEntryAlpha.Add(Link.ToCell, PortalAlpha);
				HeapPushMin(Frontier, { Link.ToCell, PortalAlpha, ToCell.StableId });
			}
		}
	}
	if (bSawRelevantInsufficientClearance)
	{
		return EGuLiFlightNavSegmentStatus::InsufficientClearance;
	}
	if (bSawRelevantDisconnectedCell)
	{
		return EGuLiFlightNavSegmentStatus::Disconnected;
	}
	return bSawRelevantInvalidPortal
		? EGuLiFlightNavSegmentStatus::InvalidPortal
		: EGuLiFlightNavSegmentStatus::MissingLink;
}

TArray<FVector> FGuLiFlightNavigationQuery::SmoothPath(
	const TArray<FVector>& RawPoints,
	const float AgentRadius) const
{
	if (RawPoints.Num() <= 2)
	{
		return RawPoints;
	}

	TArray<FVector> Result;
	Result.Reserve(RawPoints.Num());
	Result.Add(RawPoints[0]);
	int32 AnchorIndex = 0;
	while (AnchorIndex < RawPoints.Num() - 1)
	{
		int32 FurthestVisible = AnchorIndex + 1;
		for (int32 CandidateIndex = RawPoints.Num() - 1; CandidateIndex > AnchorIndex + 1; --CandidateIndex)
		{
			if (IsSegmentNavigable(RawPoints[AnchorIndex], RawPoints[CandidateIndex], AgentRadius))
			{
				FurthestVisible = CandidateIndex;
				break;
			}
		}
		Result.Add(RawPoints[FurthestVisible]);
		AnchorIndex = FurthestVisible;
	}
	return Result;
}

FGuLiFlightNavPathResult FGuLiFlightNavigationQuery::FindPath(
	const FVector& Start,
	const FVector& Goal,
	const FGuLiFlightNavPathQueryOptions& Options,
	const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>& CancellationToken) const
{
	FGuLiFlightNavPathResult Result;
	if (!IsValid())
	{
		Result.Status = EGuLiFlightNavPathStatus::InvalidData;
		return Result;
	}
	if (CancellationToken.IsValid() && CancellationToken->IsCancelled())
	{
		Result.Status = EGuLiFlightNavPathStatus::Cancelled;
		return Result;
	}

	const float EffectiveRadius = ResolveAgentRadius(Options.AgentRadius);
	if (EffectiveRadius < 0.0f || EffectiveRadius > Graph->Metadata.BakedAgentRadius + UE_KINDA_SMALL_NUMBER)
	{
		Result.Status = EGuLiFlightNavPathStatus::InsufficientClearance;
		return Result;
	}
	if (!ContainsAgentCenter(Graph->Metadata.Bounds, Start, EffectiveRadius))
	{
		Result.Status = EGuLiFlightNavPathStatus::StartOutsideNavigation;
		return Result;
	}
	if (!ContainsAgentCenter(Graph->Metadata.Bounds, Goal, EffectiveRadius))
	{
		Result.Status = EGuLiFlightNavPathStatus::GoalOutsideNavigation;
		return Result;
	}

	const int32 StartCell = Graph->FindContainingCell(Start);
	if (!Graph->Cells.IsValidIndex(StartCell))
	{
		Result.Status = EGuLiFlightNavPathStatus::StartOutsideNavigation;
		return Result;
	}
	const int32 GoalCell = Graph->FindContainingCell(Goal);
	if (!Graph->Cells.IsValidIndex(GoalCell))
	{
		Result.Status = EGuLiFlightNavPathStatus::GoalOutsideNavigation;
		return Result;
	}
	if (Graph->Cells[StartCell].Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius
		|| Graph->Cells[GoalCell].Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius)
	{
		Result.Status = EGuLiFlightNavPathStatus::InsufficientClearance;
		return Result;
	}
	if (Graph->Cells[StartCell].ComponentId != Graph->Cells[GoalCell].ComponentId)
	{
		Result.Status = EGuLiFlightNavPathStatus::Disconnected;
		return Result;
	}
	if (StartCell == GoalCell)
	{
		Result.Status = EGuLiFlightNavPathStatus::Success;
		Result.Points = { Start, Goal };
		Result.CellPath = { StartCell };
		Result.TotalCost = FVector::Distance(Start, Goal);
		return Result;
	}

	const int32 CellCount = Graph->Cells.Num();
	TArray<float> GScore;
	GScore.Init(BIG_NUMBER, CellCount);
	TArray<int32> ParentCell;
	ParentCell.Init(INDEX_NONE, CellCount);
	TArray<int32> ParentPortal;
	ParentPortal.Init(INDEX_NONE, CellCount);
	TBitArray<> Closed(false, CellCount);
	TArray<FGuLiFlightNavOpenRecord> OpenHeap;
	OpenHeap.Reserve(FMath::Min(CellCount, Options.MaximumExpandedNodes));

	GScore[StartCell] = 0.0f;
	HeapPushMin(OpenHeap, {
		StartCell,
		static_cast<float>(FVector::Distance(Graph->Cells[StartCell].Center, Goal)),
		Graph->Cells[StartCell].StableId
	});

	bool bReachedGoal = false;
	while (!OpenHeap.IsEmpty())
	{
		if (CancellationToken.IsValid() && CancellationToken->IsCancelled())
		{
			Result.Status = EGuLiFlightNavPathStatus::Cancelled;
			return Result;
		}

		const FGuLiFlightNavOpenRecord CurrentRecord = HeapPopMin(OpenHeap);
		const int32 CurrentCell = CurrentRecord.CellIndex;
		if (!Graph->Cells.IsValidIndex(CurrentCell) || Closed[CurrentCell])
		{
			continue;
		}
		if (Result.ExpandedNodes >= FMath::Max(1, Options.MaximumExpandedNodes))
		{
			Result.Status = EGuLiFlightNavPathStatus::NodeLimitExceeded;
			return Result;
		}

		Closed[CurrentCell] = true;
		++Result.ExpandedNodes;
		if (CurrentCell == GoalCell)
		{
			bReachedGoal = true;
			break;
		}

		const FGuLiFlightNavCell& Cell = Graph->Cells[CurrentCell];
		for (int32 LinkOffset = 0; LinkOffset < Cell.LinkCount; ++LinkOffset)
		{
			const int32 LinkIndex = Cell.FirstLink + LinkOffset;
			if (!Graph->Links.IsValidIndex(LinkIndex))
			{
				continue;
			}
			const FGuLiFlightNavLink& Link = Graph->Links[LinkIndex];
			if (!Graph->Cells.IsValidIndex(Link.ToCell)
				|| !Graph->Portals.IsValidIndex(Link.PortalIndex)
				|| Closed[Link.ToCell]
				|| Graph->Portals[Link.PortalIndex].Clearance + UE_KINDA_SMALL_NUMBER < EffectiveRadius)
			{
				continue;
			}

			const float CandidateScore = GScore[CurrentCell] + Link.Cost;
			if (CandidateScore + UE_KINDA_SMALL_NUMBER >= GScore[Link.ToCell])
			{
				continue;
			}

			GScore[Link.ToCell] = CandidateScore;
			ParentCell[Link.ToCell] = CurrentCell;
			ParentPortal[Link.ToCell] = Link.PortalIndex;
			const float Heuristic = static_cast<float>(FVector::Distance(Graph->Cells[Link.ToCell].Center, Goal));
			HeapPushMin(OpenHeap, { Link.ToCell, CandidateScore + Heuristic, Graph->Cells[Link.ToCell].StableId });
		}
	}

	if (!bReachedGoal)
	{
		Result.Status = EGuLiFlightNavPathStatus::NoPath;
		return Result;
	}

	TArray<int32> ReverseCells;
	TArray<int32> ReversePortals;
	for (int32 CellIndex = GoalCell; CellIndex != INDEX_NONE; CellIndex = ParentCell[CellIndex])
	{
		ReverseCells.Add(CellIndex);
		if (CellIndex != StartCell)
		{
			ReversePortals.Add(ParentPortal[CellIndex]);
		}
		else
		{
			break;
		}
	}
	Algo::Reverse(ReverseCells);
	Algo::Reverse(ReversePortals);
	Result.CellPath = MoveTemp(ReverseCells);

	TArray<FVector> RawPoints;
	RawPoints.Reserve(ReversePortals.Num() + 2);
	RawPoints.Add(Start);
	for (const int32 PortalIndex : ReversePortals)
	{
		if (Graph->Portals.IsValidIndex(PortalIndex))
		{
			RawPoints.Add(Graph->Portals[PortalIndex].Center);
		}
	}
	RawPoints.Add(Goal);

	Result.Points = Options.bSmoothPath ? SmoothPath(RawPoints, EffectiveRadius) : MoveTemp(RawPoints);
	Result.TotalCost = 0.0f;
	for (int32 PointIndex = 1; PointIndex < Result.Points.Num(); ++PointIndex)
	{
		Result.TotalCost += static_cast<float>(FVector::Distance(Result.Points[PointIndex - 1], Result.Points[PointIndex]));
	}
	Result.Status = EGuLiFlightNavPathStatus::Success;
	return Result;
}

TFuture<FGuLiFlightNavPathResult> FGuLiFlightNavigationQuery::FindPathAsync(
	const FVector& Start,
	const FVector& Goal,
	const FGuLiFlightNavPathQueryOptions& Options,
	const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>& CancellationToken) const
{
	const FGuLiFlightNavigationQuery QueryCopy = *this;
	return Async(EAsyncExecution::ThreadPool, [QueryCopy, Start, Goal, Options, CancellationToken]()
	{
		return QueryCopy.FindPath(Start, Goal, Options, CancellationToken);
	});
}

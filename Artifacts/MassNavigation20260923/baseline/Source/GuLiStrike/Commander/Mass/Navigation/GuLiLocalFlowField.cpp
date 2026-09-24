// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiLocalFlowField.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace GuLiLocalFlowFieldPrivate
{
	constexpr float DiagonalDistanceScale = 1.41421356237f;
	constexpr float CostEpsilon = 0.001f;

	const FIntPoint NeighbourOffsets[] = {
		FIntPoint(-1, -1),
		FIntPoint(0, -1),
		FIntPoint(1, -1),
		FIntPoint(-1, 0),
		FIntPoint(1, 0),
		FIntPoint(-1, 1),
		FIntPoint(0, 1),
		FIntPoint(1, 1)
	};

	struct FOpenCell
	{
		int32 CellIndex = INDEX_NONE;
		float Cost = MAX_flt;
	};

	struct FOpenCellLess
	{
		bool operator()(const FOpenCell& Lhs, const FOpenCell& Rhs) const
		{
			if (Lhs.Cost != Rhs.Cost)
			{
				return Lhs.Cost < Rhs.Cost;
			}
			return Lhs.CellIndex < Rhs.CellIndex;
		}
	};

	void SetError(FString* OutError, const TCHAR* Message)
	{
		if (OutError != nullptr)
		{
			*OutError = Message;
		}
	}
}

bool FGuLiLocalFlowFieldBuildData::IsWellFormed(FString* OutError) const
{
	using namespace GuLiLocalFlowFieldPrivate;

	if (!FMath::IsFinite(WorldMin.X) || !FMath::IsFinite(WorldMin.Y))
	{
		SetError(OutError, TEXT("WorldMin must be finite."));
		return false;
	}

	if (!FMath::IsFinite(CellSizeCentimeters) || CellSizeCentimeters <= UE_KINDA_SMALL_NUMBER)
	{
		SetError(OutError, TEXT("CellSizeCentimeters must be finite and positive."));
		return false;
	}

	if (Walkable.Num() != FGuLiLocalFlowField::CellCount)
	{
		SetError(OutError, TEXT("Walkable must contain exactly 4096 cells."));
		return false;
	}

	if (!TraversalCosts.IsEmpty() && TraversalCosts.Num() != FGuLiLocalFlowField::CellCount)
	{
		SetError(OutError, TEXT("TraversalCosts must be empty or contain exactly 4096 values."));
		return false;
	}

	for (const float Cost : TraversalCosts)
	{
		if (!FMath::IsFinite(Cost) || Cost <= 0.0f)
		{
			SetError(OutError, TEXT("Every traversal cost must be finite and positive."));
			return false;
		}
	}

	if (OutError != nullptr)
	{
		OutError->Reset();
	}
	return true;
}

bool FGuLiLocalFlowField::Build(const FGuLiLocalFlowFieldBuildData& BuildData, FString* OutError)
{
	using namespace GuLiLocalFlowFieldPrivate;

	Reset();
	if (!BuildData.IsWellFormed(OutError))
	{
		return false;
	}

	BuildKey = BuildData.Key;
	WorldMin = BuildData.WorldMin;
	CellSizeCentimeters = BuildData.CellSizeCentimeters;
	Walkable = BuildData.Walkable;
	TraversalCosts = BuildData.TraversalCosts;

	if (!ResolveGoalCell(BuildData.RequestedGoalCell, ResolvedGoalCell))
	{
		SetError(OutError, TEXT("The flow-field tile contains no walkable goal cell."));
		Reset();
		return false;
	}

	IntegrationCosts.Init(MAX_flt, CellCount);
	Directions.Init(FVector2D::ZeroVector, CellCount);

	const int32 GoalIndex = CellToIndex(ResolvedGoalCell);
	IntegrationCosts[GoalIndex] = 0.0f;

	TArray<FOpenCell> OpenCells;
	OpenCells.Reserve(CellCount);
	OpenCells.HeapPush(FOpenCell{GoalIndex, 0.0f}, FOpenCellLess{});

	while (!OpenCells.IsEmpty())
	{
		FOpenCell Current;
		OpenCells.HeapPop(Current, FOpenCellLess{}, EAllowShrinking::No);
		if (Current.Cost > IntegrationCosts[Current.CellIndex] + CostEpsilon)
		{
			continue;
		}

		const FIntPoint CurrentCell = IndexToCell(Current.CellIndex);
		for (const FIntPoint Offset : NeighbourOffsets)
		{
			const FIntPoint NeighbourCell = CurrentCell + Offset;
			if (!IsCellInBounds(NeighbourCell))
			{
				continue;
			}

			const int32 NeighbourIndex = CellToIndex(NeighbourCell);
			if (!Walkable[NeighbourIndex])
			{
				continue;
			}

			const bool bDiagonal = Offset.X != 0 && Offset.Y != 0;
			if (bDiagonal && !CanTraverseDiagonal(CurrentCell, NeighbourCell))
			{
				continue;
			}

			const float DistanceScale = bDiagonal ? DiagonalDistanceScale : 1.0f;
			const float AverageTerrainCost = 0.5f
				* (GetTraversalCost(Current.CellIndex) + GetTraversalCost(NeighbourIndex));
			const float CandidateCost = Current.Cost
				+ CellSizeCentimeters * DistanceScale * AverageTerrainCost;

			if (CandidateCost + CostEpsilon < IntegrationCosts[NeighbourIndex])
			{
				IntegrationCosts[NeighbourIndex] = CandidateCost;
				OpenCells.HeapPush(FOpenCell{NeighbourIndex, CandidateCost}, FOpenCellLess{});
			}
		}
	}

	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		const float CurrentCost = IntegrationCosts[CellIndex];
		if (!Walkable[CellIndex] || CurrentCost == MAX_flt || CellIndex == GoalIndex)
		{
			continue;
		}

		const FIntPoint CurrentCell = IndexToCell(CellIndex);
		float BestCost = CurrentCost;
		int32 BestIndex = INDEX_NONE;
		for (const FIntPoint Offset : NeighbourOffsets)
		{
			const FIntPoint NeighbourCell = CurrentCell + Offset;
			if (!IsCellInBounds(NeighbourCell))
			{
				continue;
			}

			const int32 NeighbourIndex = CellToIndex(NeighbourCell);
			if (!Walkable[NeighbourIndex]
				|| (Offset.X != 0 && Offset.Y != 0 && !CanTraverseDiagonal(CurrentCell, NeighbourCell)))
			{
				continue;
			}

			const float NeighbourCost = IntegrationCosts[NeighbourIndex];
			if (NeighbourCost + CostEpsilon < BestCost
				|| (FMath::IsNearlyEqual(NeighbourCost, BestCost, CostEpsilon)
					&& NeighbourIndex < BestIndex))
			{
				BestCost = NeighbourCost;
				BestIndex = NeighbourIndex;
			}
		}

		if (BestIndex != INDEX_NONE)
		{
			const FIntPoint BestCell = IndexToCell(BestIndex);
			Directions[CellIndex] = FVector2D(
				static_cast<double>(BestCell.X - CurrentCell.X),
				static_cast<double>(BestCell.Y - CurrentCell.Y)).GetSafeNormal();
		}
	}

	bReady = true;
	if (OutError != nullptr)
	{
		OutError->Reset();
	}
	return true;
}

void FGuLiLocalFlowField::Reset()
{
	BuildKey = FGuLiFlowFieldBuildKey{};
	WorldMin = FVector2D::ZeroVector;
	CellSizeCentimeters = DefaultCellSizeCentimeters;
	ResolvedGoalCell = FIntPoint::ZeroValue;
	Walkable.Reset();
	TraversalCosts.Reset();
	IntegrationCosts.Reset();
	Directions.Reset();
	bReady = false;
}

bool FGuLiLocalFlowField::TryWorldToCell(const FVector& WorldLocation, FIntPoint& OutCell) const
{
	if (!bReady || !FMath::IsFinite(WorldLocation.X) || !FMath::IsFinite(WorldLocation.Y))
	{
		return false;
	}

	const FVector2D Local = (FVector2D(WorldLocation.X, WorldLocation.Y) - WorldMin)
		/ static_cast<double>(CellSizeCentimeters);
	const FIntPoint Candidate(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y));
	if (!IsCellInBounds(Candidate))
	{
		return false;
	}

	OutCell = Candidate;
	return true;
}

FVector FGuLiLocalFlowField::GetCellCenter(const FIntPoint& Cell, const float WorldZ) const
{
	if (!IsCellInBounds(Cell))
	{
		return FVector::ZeroVector;
	}

	return FVector(
		WorldMin.X + (static_cast<double>(Cell.X) + 0.5) * CellSizeCentimeters,
		WorldMin.Y + (static_cast<double>(Cell.Y) + 0.5) * CellSizeCentimeters,
		WorldZ);
}

bool FGuLiLocalFlowField::IsReachable(const FIntPoint& Cell) const
{
	if (!bReady || !IsCellInBounds(Cell))
	{
		return false;
	}

	const int32 CellIndex = CellToIndex(Cell);
	return Walkable[CellIndex] && IntegrationCosts[CellIndex] != MAX_flt;
}

float FGuLiLocalFlowField::GetIntegrationCost(const FIntPoint& Cell) const
{
	return IsReachable(Cell) ? IntegrationCosts[CellToIndex(Cell)] : MAX_flt;
}

bool FGuLiLocalFlowField::SampleDirection(const FVector& WorldLocation, FVector& OutDirection) const
{
	OutDirection = FVector::ZeroVector;

	FIntPoint ContainingCell;
	if (!TryWorldToCell(WorldLocation, ContainingCell) || !IsReachable(ContainingCell))
	{
		return false;
	}

	const FVector2D GridPosition = (FVector2D(WorldLocation.X, WorldLocation.Y) - WorldMin)
		/ static_cast<double>(CellSizeCentimeters) - FVector2D(0.5, 0.5);
	const int32 MinX = FMath::FloorToInt(GridPosition.X);
	const int32 MinY = FMath::FloorToInt(GridPosition.Y);
	const double AlphaX = GridPosition.X - static_cast<double>(MinX);
	const double AlphaY = GridPosition.Y - static_cast<double>(MinY);

	FVector2D BlendedDirection = FVector2D::ZeroVector;
	double TotalWeight = 0.0;
	for (int32 YOffset = 0; YOffset <= 1; ++YOffset)
	{
		for (int32 XOffset = 0; XOffset <= 1; ++XOffset)
		{
			const FIntPoint SampleCell(MinX + XOffset, MinY + YOffset);
			if (!IsReachable(SampleCell))
			{
				continue;
			}

			const double XWeight = XOffset == 0 ? 1.0 - AlphaX : AlphaX;
			const double YWeight = YOffset == 0 ? 1.0 - AlphaY : AlphaY;
			const double Weight = XWeight * YWeight;
			BlendedDirection += Directions[CellToIndex(SampleCell)] * Weight;
			TotalWeight += Weight;
		}
	}

	if (TotalWeight > UE_DOUBLE_SMALL_NUMBER)
	{
		BlendedDirection /= TotalWeight;
	}
	if (BlendedDirection.IsNearlyZero())
	{
		BlendedDirection = Directions[CellToIndex(ContainingCell)];
	}

	BlendedDirection.Normalize();
	OutDirection = FVector(BlendedDirection.X, BlendedDirection.Y, 0.0);
	return true;
}

bool FGuLiLocalFlowField::IsCellInBounds(const FIntPoint& Cell)
{
	return Cell.X >= 0 && Cell.X < GridSize && Cell.Y >= 0 && Cell.Y < GridSize;
}

int32 FGuLiLocalFlowField::CellToIndex(const FIntPoint& Cell)
{
	check(IsCellInBounds(Cell));
	return Cell.Y * GridSize + Cell.X;
}

FIntPoint FGuLiLocalFlowField::IndexToCell(const int32 CellIndex)
{
	check(CellIndex >= 0 && CellIndex < CellCount);
	return FIntPoint(CellIndex % GridSize, CellIndex / GridSize);
}

bool FGuLiLocalFlowField::ResolveGoalCell(const FIntPoint& RequestedGoal, FIntPoint& OutGoal) const
{
	if (IsCellInBounds(RequestedGoal) && Walkable[CellToIndex(RequestedGoal)])
	{
		OutGoal = RequestedGoal;
		return true;
	}

	double BestDistanceSquared = MAX_dbl;
	int32 BestIndex = INDEX_NONE;
	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		if (!Walkable[CellIndex])
		{
			continue;
		}

		const FIntPoint Cell = IndexToCell(CellIndex);
		const double DeltaX = static_cast<double>(Cell.X) - RequestedGoal.X;
		const double DeltaY = static_cast<double>(Cell.Y) - RequestedGoal.Y;
		const double DistanceSquared = DeltaX * DeltaX + DeltaY * DeltaY;
		if (DistanceSquared < BestDistanceSquared
			|| (DistanceSquared == BestDistanceSquared && CellIndex < BestIndex))
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = CellIndex;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	OutGoal = IndexToCell(BestIndex);
	return true;
}

bool FGuLiLocalFlowField::CanTraverseDiagonal(const FIntPoint& From, const FIntPoint& To) const
{
	const int32 DeltaX = To.X - From.X;
	const int32 DeltaY = To.Y - From.Y;
	check(FMath::Abs(DeltaX) == 1 && FMath::Abs(DeltaY) == 1);

	const FIntPoint Horizontal(From.X + DeltaX, From.Y);
	const FIntPoint Vertical(From.X, From.Y + DeltaY);
	return IsCellInBounds(Horizontal)
		&& IsCellInBounds(Vertical)
		&& Walkable[CellToIndex(Horizontal)]
		&& Walkable[CellToIndex(Vertical)];
}

float FGuLiLocalFlowField::GetTraversalCost(const int32 CellIndex) const
{
	return TraversalCosts.IsEmpty() ? 1.0f : TraversalCosts[CellIndex];
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiLocalFlowFieldAutomationTest,
	"GuLiStrike.Commander.Mass.Navigation.LocalFlowField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiLocalFlowFieldAutomationTest::RunTest(const FString& Parameters)
{
	FGuLiFlowFieldBuildKey FirstKey;
	FirstKey.OrderId = 7u;
	FirstKey.NavigationGeneration = 2u;
	FirstKey.PathRevision = 3u;
	FirstKey.PathPointIndex = 4;
	FirstKey.TileCoordinate = FIntPoint(1, 2);
	FGuLiFlowFieldBuildKey NextPathPointKey = FirstKey;
	NextPathPointKey.PathPointIndex = 5;
	TestTrue(TEXT("Flow-field key changes when the shared path advances"), FirstKey != NextPathPointKey);
	TestNotEqual(
		TEXT("Flow-field hash includes the shared path point"),
		GetTypeHash(FirstKey),
		GetTypeHash(NextPathPointKey));

	FGuLiLocalFlowFieldBuildData BuildData;
	BuildData.WorldMin = FVector2D::ZeroVector;
	BuildData.CellSizeCentimeters = FGuLiLocalFlowField::DefaultCellSizeCentimeters;
	BuildData.RequestedGoalCell = FIntPoint(60, 32);
	BuildData.Walkable.Init(true, FGuLiLocalFlowField::CellCount);

	// A vertical wall with one opening forces the left side of the tile toward the gap.
	for (int32 Y = 0; Y < FGuLiLocalFlowField::GridSize; ++Y)
	{
		if (Y != 48)
		{
			BuildData.Walkable[FGuLiLocalFlowField::CellToIndex(FIntPoint(32, Y))] = false;
		}
	}

	FGuLiLocalFlowField Field;
	FString Error;
	TestTrue(TEXT("Build succeeds"), Field.Build(BuildData, &Error));
	TestTrue(TEXT("Build error is empty"), Error.IsEmpty());
	TestTrue(
		TEXT("Goal is unchanged when walkable"),
		Field.GetResolvedGoalCell() == BuildData.RequestedGoalCell);

	FVector Direction;
	const FVector LeftSample = Field.GetCellCenter(FIntPoint(20, 32));
	TestTrue(TEXT("Reachable cell produces a direction"), Field.SampleDirection(LeftSample, Direction));
	TestTrue(TEXT("Direction climbs toward the wall opening"), Direction.Y > 0.1f);

	BuildData.Walkable[Field.CellToIndex(BuildData.RequestedGoalCell)] = false;
	TestTrue(TEXT("Blocked requested goal resolves to a nearby cell"), Field.Build(BuildData, &Error));
	TestTrue(
		TEXT("Resolved goal moves off the blocked cell"),
		Field.GetResolvedGoalCell() != BuildData.RequestedGoalCell);
	return true;
}

#endif

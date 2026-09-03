#include "GuLiFlightNavigationBaker.h"

#include "GuLiFlightNavigationData.h"

namespace
{
	struct FGuLiFaceBuckets
	{
		TMap<int64, TArray<int32>> Negative[3];
		TMap<int64, TArray<int32>> Positive[3];
	};

	struct FGuLiTemporaryLink
	{
		int32 ToCell = INDEX_NONE;
		int32 PortalIndex = INDEX_NONE;
		float Cost = 0.0f;
	};

	class FGuLiDeterministicHash
	{
	public:
		void Add(const uint64 Value)
		{
			for (uint32 Shift = 0; Shift < 64; Shift += 8)
			{
				State ^= static_cast<uint8>((Value >> Shift) & 0xffull);
				State *= 1099511628211ull;
			}
		}

		void Add(const FVector& Value)
		{
			Add(static_cast<uint64>(FMath::RoundToInt64(Value.X * 100.0)));
			Add(static_cast<uint64>(FMath::RoundToInt64(Value.Y * 100.0)));
			Add(static_cast<uint64>(FMath::RoundToInt64(Value.Z * 100.0)));
		}

		uint64 Get() const
		{
			return State == 0 ? 1 : State;
		}

	private:
		uint64 State = 14695981039346656037ull;
	};

	double GetAxis(const FVector& Value, const int32 Axis)
	{
		switch (Axis)
		{
		case 0: return Value.X;
		case 1: return Value.Y;
		default: return Value.Z;
		}
	}

	void SetAxis(FVector& Value, const int32 Axis, const double AxisValue)
	{
		switch (Axis)
		{
		case 0: Value.X = AxisValue; break;
		case 1: Value.Y = AxisValue; break;
		default: Value.Z = AxisValue; break;
		}
	}

	uint64 MakeCellStableId(const FVector& Center, const FVector& Extent)
	{
		FGuLiDeterministicHash Hash;
		Hash.Add(Center);
		Hash.Add(Extent);
		return Hash.Get();
	}

	uint64 MakePortalStableId(const FGuLiFlightNavCell& A, const FGuLiFlightNavCell& B, const FVector& Center)
	{
		FGuLiDeterministicHash Hash;
		Hash.Add(FMath::Min(A.StableId, B.StableId));
		Hash.Add(FMath::Max(A.StableId, B.StableId));
		Hash.Add(Center);
		return Hash.Get();
	}

	int64 QuantizeFaceCoordinate(const double Value, const double Tolerance)
	{
		return FMath::RoundToInt64(Value / Tolerance);
	}

	bool BuildPortalsAndComponents(
		const FGuLiFlightNavBakeSettings& Settings,
		TArray<FGuLiFlightNavCell>& Cells,
		TArray<FGuLiFlightNavPortal>& OutPortals,
		TArray<FGuLiFlightNavLink>& OutLinks,
		FString& OutError)
	{
		FGuLiFaceBuckets Buckets;
		const double FaceTolerance = FMath::Max(0.01, static_cast<double>(Settings.FaceCoordinateTolerance));
		for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
		{
			const FGuLiFlightNavCell& Cell = Cells[CellIndex];
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const double Center = GetAxis(Cell.Center, Axis);
				const double Extent = GetAxis(Cell.Extent, Axis);
				Buckets.Negative[Axis].FindOrAdd(QuantizeFaceCoordinate(Center - Extent, FaceTolerance)).Add(CellIndex);
				Buckets.Positive[Axis].FindOrAdd(QuantizeFaceCoordinate(Center + Extent, FaceTolerance)).Add(CellIndex);
			}
		}

		TSet<uint64> SeenPairs;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (const TPair<int64, TArray<int32>>& PositiveBucket : Buckets.Positive[Axis])
			{
				const TArray<int32>* NegativeCells = Buckets.Negative[Axis].Find(PositiveBucket.Key);
				if (NegativeCells == nullptr)
				{
					continue;
				}

				const int32 AxisU = (Axis + 1) % 3;
				const int32 AxisV = (Axis + 2) % 3;
				for (const int32 CellAIndex : PositiveBucket.Value)
				{
					for (const int32 CellBIndex : *NegativeCells)
					{
						if (CellAIndex == CellBIndex)
						{
							continue;
						}

						const uint32 LowIndex = static_cast<uint32>(FMath::Min(CellAIndex, CellBIndex));
						const uint32 HighIndex = static_cast<uint32>(FMath::Max(CellAIndex, CellBIndex));
						const uint64 PairKey = (static_cast<uint64>(LowIndex) << 32) | HighIndex;
						if (SeenPairs.Contains(PairKey))
						{
							continue;
						}

						const FGuLiFlightNavCell& CellA = Cells[CellAIndex];
						const FGuLiFlightNavCell& CellB = Cells[CellBIndex];
						const double MinU = FMath::Max(
							GetAxis(CellA.Center, AxisU) - GetAxis(CellA.Extent, AxisU),
							GetAxis(CellB.Center, AxisU) - GetAxis(CellB.Extent, AxisU));
						const double MaxU = FMath::Min(
							GetAxis(CellA.Center, AxisU) + GetAxis(CellA.Extent, AxisU),
							GetAxis(CellB.Center, AxisU) + GetAxis(CellB.Extent, AxisU));
						const double MinV = FMath::Max(
							GetAxis(CellA.Center, AxisV) - GetAxis(CellA.Extent, AxisV),
							GetAxis(CellB.Center, AxisV) - GetAxis(CellB.Extent, AxisV));
						const double MaxV = FMath::Min(
							GetAxis(CellA.Center, AxisV) + GetAxis(CellA.Extent, AxisV),
							GetAxis(CellB.Center, AxisV) + GetAxis(CellB.Extent, AxisV));
						const double SpanU = MaxU - MinU;
						const double SpanV = MaxV - MinV;
						if (SpanU + FaceTolerance < Settings.MinimumPortalSpan
							|| SpanV + FaceTolerance < Settings.MinimumPortalSpan)
						{
							continue;
						}

						FGuLiFlightNavPortal& Portal = OutPortals.AddDefaulted_GetRef();
						Portal.CellA = CellAIndex;
						Portal.CellB = CellBIndex;
						Portal.Center = FVector::ZeroVector;
						SetAxis(Portal.Center, Axis, GetAxis(CellA.Center, Axis) + GetAxis(CellA.Extent, Axis));
						SetAxis(Portal.Center, AxisU, (MinU + MaxU) * 0.5);
						SetAxis(Portal.Center, AxisV, (MinV + MaxV) * 0.5);
						Portal.Extent = FVector::ZeroVector;
						SetAxis(Portal.Extent, AxisU, SpanU * 0.5);
						SetAxis(Portal.Extent, AxisV, SpanV * 0.5);
						Portal.Normal = FVector::ZeroVector;
						SetAxis(Portal.Normal, Axis, 1.0);
						Portal.Clearance = Settings.AgentRadius + static_cast<float>(FMath::Min(SpanU, SpanV) * 0.5);
						Portal.StableId = MakePortalStableId(CellA, CellB, Portal.Center);
						SeenPairs.Add(PairKey);
					}
				}
			}
		}

		OutPortals.Sort([&Cells](const FGuLiFlightNavPortal& A, const FGuLiFlightNavPortal& B)
		{
			if (A.StableId != B.StableId)
			{
				return A.StableId < B.StableId;
			}
			const uint64 AMin = FMath::Min(Cells[A.CellA].StableId, Cells[A.CellB].StableId);
			const uint64 BMin = FMath::Min(Cells[B.CellA].StableId, Cells[B.CellB].StableId);
			return AMin < BMin;
		});

		TArray<TArray<FGuLiTemporaryLink>> LinksByCell;
		LinksByCell.SetNum(Cells.Num());
		for (int32 PortalIndex = 0; PortalIndex < OutPortals.Num(); ++PortalIndex)
		{
			const FGuLiFlightNavPortal& Portal = OutPortals[PortalIndex];
			const float Cost = static_cast<float>(FVector::Distance(Cells[Portal.CellA].Center, Cells[Portal.CellB].Center));
			LinksByCell[Portal.CellA].Add({ Portal.CellB, PortalIndex, FMath::Max(Cost, 1.0f) });
			LinksByCell[Portal.CellB].Add({ Portal.CellA, PortalIndex, FMath::Max(Cost, 1.0f) });
		}

		int32 NextComponentId = 0;
		TArray<int32> Queue;
		for (int32 SeedCell = 0; SeedCell < Cells.Num(); ++SeedCell)
		{
			if (Cells[SeedCell].ComponentId != INDEX_NONE)
			{
				continue;
			}

			Cells[SeedCell].ComponentId = NextComponentId++;
			Queue.Reset();
			Queue.Add(SeedCell);
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 CurrentCell = Queue[QueueIndex];
				for (const FGuLiTemporaryLink& Link : LinksByCell[CurrentCell])
				{
					if (Cells[Link.ToCell].ComponentId == INDEX_NONE)
					{
						Cells[Link.ToCell].ComponentId = Cells[SeedCell].ComponentId;
						Queue.Add(Link.ToCell);
					}
				}
			}
		}

		OutLinks.Reset();
		for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
		{
			TArray<FGuLiTemporaryLink>& CellLinks = LinksByCell[CellIndex];
			CellLinks.Sort([&Cells, &OutPortals](const FGuLiTemporaryLink& A, const FGuLiTemporaryLink& B)
			{
				if (Cells[A.ToCell].StableId != Cells[B.ToCell].StableId)
				{
					return Cells[A.ToCell].StableId < Cells[B.ToCell].StableId;
				}
				return OutPortals[A.PortalIndex].StableId < OutPortals[B.PortalIndex].StableId;
			});

			Cells[CellIndex].FirstLink = OutLinks.Num();
			Cells[CellIndex].LinkCount = CellLinks.Num();
			for (const FGuLiTemporaryLink& TemporaryLink : CellLinks)
			{
				FGuLiFlightNavLink& Link = OutLinks.AddDefaulted_GetRef();
				Link.ToCell = TemporaryLink.ToCell;
				Link.PortalIndex = TemporaryLink.PortalIndex;
				Link.Cost = TemporaryLink.Cost;
			}
		}

		if (NextComponentId <= 0)
		{
			OutError = TEXT("No connected component was generated.");
			return false;
		}
		return true;
	}
}

uint64 FGuLiFlightNavigationBaker::ComputeSettingsHash(
	const FGuLiFlightNavBakeSettings& Settings)
{
	FGuLiDeterministicHash Hash;
	Hash.Add(static_cast<uint64>(FMath::RoundToInt64(Settings.MinimumCellSize * 100.0f)));
	Hash.Add(static_cast<uint64>(FMath::RoundToInt64(Settings.AgentRadius * 100.0f)));
	Hash.Add(static_cast<uint64>(Settings.MaximumDepth));
	Hash.Add(static_cast<uint64>(Settings.MaximumNodes));
	Hash.Add(static_cast<uint64>(Settings.MaximumCells));
	Hash.Add(static_cast<uint64>(FMath::RoundToInt64(Settings.FaceCoordinateTolerance * 10000.0f)));
	Hash.Add(static_cast<uint64>(FMath::RoundToInt64(Settings.MinimumPortalSpan * 100.0f)));
	Hash.Add(static_cast<uint64>(Settings.CollisionChannel));
	Hash.Add(Settings.bTraceComplex ? 1ull : 0ull);
	return Hash.Get();
}

bool FGuLiFlightNavigationBaker::Build(
	const FBox& WorldBounds,
	const FGuLiFlightNavBakeSettings& Settings,
	const FName SourceWorldPackage,
	const FString& SourceVolumePath,
	const uint32 DefinitionRevision,
	TFunctionRef<bool(const FBox&)> IsBlocked,
	UGuLiFlightNavigationData& OutData,
	FString& OutError)
{
	OutError.Reset();
	if (WorldBounds.IsValid == 0 || WorldBounds.GetExtent().GetMin() <= UE_SMALL_NUMBER)
	{
		OutError = TEXT("The navigation volume has invalid or empty world bounds.");
		return false;
	}
	if (Settings.MinimumCellSize <= UE_SMALL_NUMBER
		|| Settings.AgentRadius < 0.0f
		|| Settings.MaximumDepth < 0
		|| Settings.MaximumDepth > 16
		|| Settings.MaximumNodes < 9
		|| Settings.MaximumCells < 1
		|| Settings.FaceCoordinateTolerance <= UE_SMALL_NUMBER
		|| Settings.MinimumPortalSpan < 0.0f)
	{
		OutError = TEXT("One or more bake settings are outside their supported range.");
		return false;
	}

	TArray<FGuLiFlightNavOctreeNode> Nodes;
	TArray<FGuLiFlightNavCell> Cells;
	TArray<FGuLiFlightNavPortal> Portals;
	TArray<FGuLiFlightNavLink> Links;
	Nodes.Reserve(FMath::Min(Settings.MaximumNodes, 4096));
	Cells.Reserve(FMath::Min(Settings.MaximumCells, 4096));
	Nodes.AddDefaulted();

	bool bExceededLimit = false;
	TFunction<void(int32, const FBox&, int32)> BuildNode;
	BuildNode = [&](const int32 NodeIndex, const FBox& NodeBounds, const int32 Depth)
	{
		if (bExceededLimit)
		{
			return;
		}

		FGuLiFlightNavOctreeNode& Node = Nodes[NodeIndex];
		Node.Center = NodeBounds.GetCenter();
		Node.Extent = NodeBounds.GetExtent();
		Node.FirstChild = INDEX_NONE;
		Node.LeafCellIndex = INDEX_NONE;
		Node.ChildMask = 0;

		if (!IsBlocked(NodeBounds))
		{
			if (Cells.Num() >= Settings.MaximumCells)
			{
				bExceededLimit = true;
				return;
			}
			FGuLiFlightNavCell& Cell = Cells.AddDefaulted_GetRef();
			Cell.Center = Node.Center;
			Cell.Extent = Node.Extent;
			Cell.Clearance = Settings.AgentRadius + static_cast<float>(Node.Extent.GetMin());
			Cell.StableId = MakeCellStableId(Cell.Center, Cell.Extent);
			Node.LeafCellIndex = Cells.Num() - 1;
			return;
		}

		const bool bCanSubdivide = Depth < Settings.MaximumDepth
			&& Node.Extent.GetMax() * 2.0 > Settings.MinimumCellSize + UE_KINDA_SMALL_NUMBER;
		if (!bCanSubdivide)
		{
			return;
		}
		if (Nodes.Num() + 8 > Settings.MaximumNodes)
		{
			bExceededLimit = true;
			return;
		}

		const FVector ParentCenter = Node.Center;
		const FVector ChildExtent = Node.Extent * 0.5;
		const int32 FirstChildIndex = Nodes.AddDefaulted(8);
		Nodes[NodeIndex].FirstChild = FirstChildIndex;
		Nodes[NodeIndex].ChildMask = 0xff;
		for (uint8 Octant = 0; Octant < 8; ++Octant)
		{
			const FVector Direction(
				(Octant & 1) != 0 ? 1.0 : -1.0,
				(Octant & 2) != 0 ? 1.0 : -1.0,
				(Octant & 4) != 0 ? 1.0 : -1.0);
			const FVector ChildCenter = ParentCenter + Direction * ChildExtent;
			BuildNode(
				FirstChildIndex + Octant,
				FBox::BuildAABB(ChildCenter, ChildExtent),
				Depth + 1);
		}
	};

	BuildNode(0, WorldBounds, 0);
	if (bExceededLimit)
	{
		OutError = FString::Printf(
			TEXT("Bake exceeded MaximumNodes (%d) or MaximumCells (%d). Increase the limit or cell size."),
			Settings.MaximumNodes,
			Settings.MaximumCells);
		return false;
	}
	if (Cells.IsEmpty())
	{
		OutError = TEXT("The volume contains no navigable free-space cells.");
		return false;
	}
	if (!BuildPortalsAndComponents(Settings, Cells, Portals, Links, OutError))
	{
		return false;
	}

	FGuLiFlightNavBakeMetadata Metadata;
	Metadata.FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;
	Metadata.DefinitionRevision = FMath::Max(1u, DefinitionRevision);
	Metadata.BakeId = FGuid::NewGuid();
	Metadata.Bounds = WorldBounds;
	Metadata.MinimumCellSize = Settings.MinimumCellSize;
	Metadata.BakedAgentRadius = Settings.AgentRadius;
	Metadata.SourceWorldPackage = SourceWorldPackage;
	Metadata.SourceVolumePath = SourceVolumePath;
	Metadata.SettingsHash = ComputeSettingsHash(Settings);

	FGuLiDeterministicHash GeometryHash;
	GeometryHash.Add(static_cast<uint64>(Nodes.Num()));
	GeometryHash.Add(static_cast<uint64>(Cells.Num()));
	GeometryHash.Add(static_cast<uint64>(Portals.Num()));
	for (const FGuLiFlightNavOctreeNode& Node : Nodes)
	{
		GeometryHash.Add(Node.Center);
		GeometryHash.Add(Node.Extent);
		GeometryHash.Add(static_cast<uint64>(Node.ChildMask));
		GeometryHash.Add(static_cast<uint64>(static_cast<int64>(Node.LeafCellIndex)));
	}
	for (const FGuLiFlightNavCell& Cell : Cells)
	{
		GeometryHash.Add(Cell.StableId);
	}
	for (const FGuLiFlightNavPortal& Portal : Portals)
	{
		GeometryHash.Add(Portal.StableId);
	}
	Metadata.GeometrySignature = GeometryHash.Get();

	OutData.Metadata = MoveTemp(Metadata);
	OutData.Nodes = MoveTemp(Nodes);
	OutData.Cells = MoveTemp(Cells);
	OutData.Portals = MoveTemp(Portals);
	OutData.Links = MoveTemp(Links);
	OutData.Metadata.ContentChecksum = OutData.ComputeContentChecksum();

	if (!OutData.ValidateData(OutError))
	{
		return false;
	}
	return true;
}

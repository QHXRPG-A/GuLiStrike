#include "GuLiFlightNavigationData.h"

#include "GuLiFlightNavigationLog.h"
#include "GuLiFlightNavigationQuery.h"
#include "UObject/ObjectSaveContext.h"

namespace
{
	class FGuLiStableHash64
	{
	public:
		void AddByte(const uint8 Value)
		{
			State ^= Value;
			State *= 1099511628211ull;
		}

		void AddUInt64(const uint64 Value)
		{
			for (uint32 Shift = 0; Shift < 64; Shift += 8)
			{
				AddByte(static_cast<uint8>((Value >> Shift) & 0xffull));
			}
		}

		void AddInt64(const int64 Value)
		{
			AddUInt64(static_cast<uint64>(Value));
		}

		void AddFloat(const float Value)
		{
			AddInt64(FMath::RoundToInt64(static_cast<double>(Value) * 1000.0));
		}

		void AddVector(const FVector& Value)
		{
			// Quantize to 0.1 mm in Unreal centimetres so platform floating noise is not serialized into the hash.
			AddInt64(FMath::RoundToInt64(Value.X * 100.0));
			AddInt64(FMath::RoundToInt64(Value.Y * 100.0));
			AddInt64(FMath::RoundToInt64(Value.Z * 100.0));
		}

		void AddString(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			AddUInt64(static_cast<uint64>(Utf8.Length()));
			for (int32 Index = 0; Index < Utf8.Length(); ++Index)
			{
				AddByte(static_cast<uint8>(Utf8.Get()[Index]));
			}
		}

		uint64 Get() const
		{
			return State;
		}

	private:
		uint64 State = 14695981039346656037ull;
	};

	bool IsPositiveFiniteExtent(const FVector& Extent)
	{
		return !Extent.ContainsNaN()
			&& Extent.X > UE_SMALL_NUMBER
			&& Extent.Y > UE_SMALL_NUMBER
			&& Extent.Z > UE_SMALL_NUMBER;
	}
}

bool UGuLiFlightNavigationData::HasBakedData() const
{
	return Metadata.FormatVersion == GuLiFlightNavigation::CurrentDataFormatVersion
		&& Metadata.Bounds.IsValid != 0
		&& Nodes.Num() > 0
		&& Cells.Num() > 0
		&& Metadata.ContentChecksum != 0;
}

bool UGuLiFlightNavigationData::ValidateData(FString& OutError) const
{
	OutError.Reset();

	if (Metadata.FormatVersion != GuLiFlightNavigation::CurrentDataFormatVersion)
	{
		OutError = FString::Printf(
			TEXT("Unsupported flight-navigation data format %u (expected %u)."),
			Metadata.FormatVersion,
			GuLiFlightNavigation::CurrentDataFormatVersion);
		return false;
	}

	if (Metadata.Bounds.IsValid == 0 || !IsPositiveFiniteExtent(Metadata.Bounds.GetExtent()))
	{
		OutError = TEXT("Baked bounds are invalid.");
		return false;
	}

	if (Metadata.MinimumCellSize <= UE_SMALL_NUMBER || Metadata.BakedAgentRadius < 0.0f)
	{
		OutError = TEXT("Bake settings contain an invalid minimum cell size or agent radius.");
		return false;
	}
	if (Metadata.GeometrySignature == 0 || Metadata.SettingsHash == 0)
	{
		OutError = TEXT("The baked geometry signature or settings hash is missing.");
		return false;
	}

	if (Nodes.IsEmpty() || Cells.IsEmpty())
	{
		OutError = TEXT("The baked octree or navigable cell set is empty.");
		return false;
	}

	TBitArray<> ReferencedCells(false, Cells.Num());
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FGuLiFlightNavOctreeNode& Node = Nodes[NodeIndex];
		if (Node.Center.ContainsNaN() || !IsPositiveFiniteExtent(Node.Extent))
		{
			OutError = FString::Printf(TEXT("Octree node %d has invalid bounds."), NodeIndex);
			return false;
		}

		if (Node.IsLeaf())
		{
			if (Node.FirstChild != INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Leaf octree node %d unexpectedly references children."), NodeIndex);
				return false;
			}

			if (Node.LeafCellIndex != INDEX_NONE)
			{
				if (!Cells.IsValidIndex(Node.LeafCellIndex) || ReferencedCells[Node.LeafCellIndex])
				{
					OutError = FString::Printf(TEXT("Octree node %d has an invalid or duplicate leaf-cell reference."), NodeIndex);
					return false;
				}
				ReferencedCells[Node.LeafCellIndex] = true;
			}
		}
		else
		{
			const int32 ChildCount = FPlatformMath::CountBits(static_cast<uint64>(Node.ChildMask));
			if (Node.LeafCellIndex != INDEX_NONE
				|| Node.FirstChild < 0
				|| Node.FirstChild + ChildCount > Nodes.Num())
			{
				OutError = FString::Printf(TEXT("Octree node %d has an invalid dense child run."), NodeIndex);
				return false;
			}
		}
	}

	if (ReferencedCells.CountSetBits() != Cells.Num())
	{
		OutError = TEXT("One or more navigable cells are not referenced by an octree leaf.");
		return false;
	}

	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const FGuLiFlightNavCell& Cell = Cells[CellIndex];
		if (Cell.Center.ContainsNaN()
			|| !IsPositiveFiniteExtent(Cell.Extent)
			|| Cell.Clearance < 0.0f
			|| Cell.ComponentId < 0
			|| Cell.FirstLink < 0
			|| Cell.LinkCount < 0
			|| Cell.FirstLink + Cell.LinkCount > Links.Num()
			|| Cell.StableId == 0)
		{
			OutError = FString::Printf(TEXT("Cell %d has invalid topology or geometry."), CellIndex);
			return false;
		}

		for (int32 LinkOffset = 0; LinkOffset < Cell.LinkCount; ++LinkOffset)
		{
			const FGuLiFlightNavLink& Link = Links[Cell.FirstLink + LinkOffset];
			if (!Cells.IsValidIndex(Link.ToCell)
				|| !Portals.IsValidIndex(Link.PortalIndex)
				|| Link.ToCell == CellIndex
				|| Link.Cost <= 0.0f
				|| Cells[Link.ToCell].ComponentId != Cell.ComponentId)
			{
				OutError = FString::Printf(TEXT("Cell %d contains an invalid link."), CellIndex);
				return false;
			}
		}
	}

	for (int32 PortalIndex = 0; PortalIndex < Portals.Num(); ++PortalIndex)
	{
		const FGuLiFlightNavPortal& Portal = Portals[PortalIndex];
		if (!Cells.IsValidIndex(Portal.CellA)
			|| !Cells.IsValidIndex(Portal.CellB)
			|| Portal.CellA == Portal.CellB
			|| Portal.Center.ContainsNaN()
			|| Portal.Normal.ContainsNaN()
			|| Portal.Extent.ContainsNaN()
			|| Portal.Clearance < 0.0f
			|| Portal.StableId == 0)
		{
			OutError = FString::Printf(TEXT("Portal %d is invalid."), PortalIndex);
			return false;
		}
	}

	if (Metadata.ContentChecksum == 0 || Metadata.ContentChecksum != ComputeContentChecksum())
	{
		OutError = TEXT("The baked content checksum does not match the serialized graph.");
		return false;
	}

	return true;
}

uint64 UGuLiFlightNavigationData::ComputeContentChecksum() const
{
	FGuLiStableHash64 Hash;
	Hash.AddUInt64(Metadata.FormatVersion);
	Hash.AddVector(Metadata.Bounds.Min);
	Hash.AddVector(Metadata.Bounds.Max);
	Hash.AddFloat(Metadata.MinimumCellSize);
	Hash.AddFloat(Metadata.BakedAgentRadius);
	Hash.AddString(Metadata.SourceWorldPackage.ToString());
	Hash.AddString(Metadata.SourceVolumePath);
	Hash.AddUInt64(Metadata.GeometrySignature);
	Hash.AddUInt64(Metadata.SettingsHash);

	Hash.AddUInt64(static_cast<uint64>(Nodes.Num()));
	for (const FGuLiFlightNavOctreeNode& Node : Nodes)
	{
		Hash.AddVector(Node.Center);
		Hash.AddVector(Node.Extent);
		Hash.AddInt64(Node.FirstChild);
		Hash.AddInt64(Node.LeafCellIndex);
		Hash.AddByte(Node.ChildMask);
	}

	Hash.AddUInt64(static_cast<uint64>(Cells.Num()));
	for (const FGuLiFlightNavCell& Cell : Cells)
	{
		Hash.AddVector(Cell.Center);
		Hash.AddVector(Cell.Extent);
		Hash.AddFloat(Cell.Clearance);
		Hash.AddInt64(Cell.ComponentId);
		Hash.AddInt64(Cell.FirstLink);
		Hash.AddInt64(Cell.LinkCount);
		Hash.AddUInt64(Cell.StableId);
	}

	Hash.AddUInt64(static_cast<uint64>(Portals.Num()));
	for (const FGuLiFlightNavPortal& Portal : Portals)
	{
		Hash.AddInt64(Portal.CellA);
		Hash.AddInt64(Portal.CellB);
		Hash.AddVector(Portal.Center);
		Hash.AddVector(Portal.Normal);
		Hash.AddVector(Portal.Extent);
		Hash.AddFloat(Portal.Clearance);
		Hash.AddUInt64(Portal.StableId);
	}

	Hash.AddUInt64(static_cast<uint64>(Links.Num()));
	for (const FGuLiFlightNavLink& Link : Links)
	{
		Hash.AddInt64(Link.ToCell);
		Hash.AddInt64(Link.PortalIndex);
		Hash.AddFloat(Link.Cost);
	}

	return Hash.Get();
}

TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> UGuLiFlightNavigationData::CreateRuntimeGraph(FString* OutError) const
{
	FString ValidationError;
	if (!ValidateData(ValidationError))
	{
		if (OutError != nullptr)
		{
			*OutError = MoveTemp(ValidationError);
		}
		return nullptr;
	}

	TSharedPtr<FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> RuntimeGraph =
		MakeShared<FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe>();
	RuntimeGraph->Metadata = Metadata;
	RuntimeGraph->Nodes = Nodes;
	RuntimeGraph->Cells = Cells;
	RuntimeGraph->Portals = Portals;
	RuntimeGraph->Links = Links;
	return RuntimeGraph;
}

void UGuLiFlightNavigationData::ResetBakedData()
{
	const uint32 NextRevision = Metadata.DefinitionRevision + 1;
	Metadata = FGuLiFlightNavBakeMetadata();
	Metadata.DefinitionRevision = NextRevision;
	Nodes.Reset();
	Cells.Reset();
	Portals.Reset();
	Links.Reset();
}

void UGuLiFlightNavigationData::PreSave(FObjectPreSaveContext SaveContext)
{
	if (SaveContext.IsCooking())
	{
		FString ValidationError;
		if (!ValidateData(ValidationError))
		{
			UE_LOG(
				LogGuLiFlightNavigation,
				Error,
				TEXT("[FLIGHTNAV_COOK_GATE][InvalidData] Cook rejected flight-navigation asset '%s': %s"),
				*GetPathName(),
				*ValidationError);
		}
	}

	Super::PreSave(SaveContext);
}

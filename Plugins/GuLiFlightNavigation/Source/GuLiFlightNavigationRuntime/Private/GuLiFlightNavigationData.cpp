#include "GuLiFlightNavigationData.h"

#include "GuLiFlightNavigationCustomVersion.h"
#include "GuLiFlightNavigationLog.h"
#include "GuLiFlightNavigationQuery.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ObjectSaveContext.h"

const FGuid FGuLiFlightNavigationCustomVersion::GUID(
	0xBFC30701,
	0x86B04C84,
	0xB37BE18A,
	0x2C83D219);

static FCustomVersionRegistration GRegisterGuLiFlightNavigationCustomVersion(
	FGuLiFlightNavigationCustomVersion::GUID,
	FGuLiFlightNavigationCustomVersion::LatestVersion,
	TEXT("GuLiFlightNavigationVersion"));

namespace
{
	constexpr uint32 FlightNavPayloadMagic = 0x4E464C47; // "GLFN" in a little-endian archive.
	constexpr uint32 FlightNavPayloadSchemaVersion = 1;
	constexpr int32 MaximumSerializedNodes = 200000;
	constexpr int32 MaximumSerializedCells = 50000;
	constexpr int32 MaximumSerializedPortals = 600000;
	constexpr int32 MaximumSerializedLinks = 1200000;
	constexpr int64 MaximumSerializedPayloadBytes = 1024ll * 1024ll * 1024ll;

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

	void WriteVector(FArchive& Ar, const FVector& Value)
	{
		double X = Value.X;
		double Y = Value.Y;
		double Z = Value.Z;
		Ar << X;
		Ar << Y;
		Ar << Z;
	}

	void ReadVector(FArchive& Ar, FVector& OutValue)
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		Ar << X;
		Ar << Y;
		Ar << Z;
		OutValue = FVector(X, Y, Z);
	}

	bool AreCountsValid(
		const int32 NodeCount,
		const int32 CellCount,
		const int32 PortalCount,
		const int32 LinkCount)
	{
		return NodeCount >= 0 && NodeCount <= MaximumSerializedNodes
			&& CellCount >= 0 && CellCount <= MaximumSerializedCells
			&& PortalCount >= 0 && PortalCount <= MaximumSerializedPortals
			&& LinkCount >= 0 && LinkCount <= MaximumSerializedLinks;
	}

	uint64 ComputeGraphChecksum(
		const FGuLiFlightNavBakeMetadata& Metadata,
		const TArray<FGuLiFlightNavOctreeNode>& Nodes,
		const TArray<FGuLiFlightNavCell>& Cells,
		const TArray<FGuLiFlightNavPortal>& Portals,
		const TArray<FGuLiFlightNavLink>& Links)
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

	bool BuildPayloadBytes(
		const FGuLiFlightNavBakeMetadata& Metadata,
		const TArray<FGuLiFlightNavOctreeNode>& Nodes,
		const TArray<FGuLiFlightNavCell>& Cells,
		const TArray<FGuLiFlightNavPortal>& Portals,
		const TArray<FGuLiFlightNavLink>& Links,
		TArray<uint8>& OutBytes,
		FString& OutError)
	{
		OutBytes.Reset();
		OutError.Reset();
		if (!AreCountsValid(Nodes.Num(), Cells.Num(), Portals.Num(), Links.Num()))
		{
			OutError = TEXT("Flight-navigation payload exceeds the supported deterministic count limits.");
			return false;
		}

		FMemoryWriter Writer(OutBytes, true);
		uint32 Magic = FlightNavPayloadMagic;
		uint32 PayloadSchema = FlightNavPayloadSchemaVersion;
		uint32 DataFormat = Metadata.FormatVersion;
		uint64 ContentChecksum = Metadata.ContentChecksum;
		int32 NodeCount = Nodes.Num();
		int32 CellCount = Cells.Num();
		int32 PortalCount = Portals.Num();
		int32 LinkCount = Links.Num();
		Writer << Magic;
		Writer << PayloadSchema;
		Writer << DataFormat;
		Writer << ContentChecksum;
		Writer << NodeCount;
		Writer << CellCount;
		Writer << PortalCount;
		Writer << LinkCount;

		for (const FGuLiFlightNavOctreeNode& Source : Nodes)
		{
			WriteVector(Writer, Source.Center);
			WriteVector(Writer, Source.Extent);
			int32 FirstChild = Source.FirstChild;
			int32 LeafCellIndex = Source.LeafCellIndex;
			uint8 ChildMask = Source.ChildMask;
			Writer << FirstChild;
			Writer << LeafCellIndex;
			Writer << ChildMask;
		}

		for (const FGuLiFlightNavCell& Source : Cells)
		{
			WriteVector(Writer, Source.Center);
			WriteVector(Writer, Source.Extent);
			float Clearance = Source.Clearance;
			int32 ComponentId = Source.ComponentId;
			int32 FirstLink = Source.FirstLink;
			int32 LinkCountValue = Source.LinkCount;
			uint64 StableId = Source.StableId;
			Writer << Clearance;
			Writer << ComponentId;
			Writer << FirstLink;
			Writer << LinkCountValue;
			Writer << StableId;
		}

		for (const FGuLiFlightNavPortal& Source : Portals)
		{
			int32 CellA = Source.CellA;
			int32 CellB = Source.CellB;
			Writer << CellA;
			Writer << CellB;
			WriteVector(Writer, Source.Center);
			WriteVector(Writer, Source.Normal);
			WriteVector(Writer, Source.Extent);
			float Clearance = Source.Clearance;
			uint64 StableId = Source.StableId;
			Writer << Clearance;
			Writer << StableId;
		}

		for (const FGuLiFlightNavLink& Source : Links)
		{
			int32 ToCell = Source.ToCell;
			int32 PortalIndex = Source.PortalIndex;
			float Cost = Source.Cost;
			Writer << ToCell;
			Writer << PortalIndex;
			Writer << Cost;
		}

		if (Writer.IsError() || OutBytes.Num() <= 0 || OutBytes.Num() > MaximumSerializedPayloadBytes)
		{
			OutBytes.Reset();
			OutError = TEXT("Failed to construct the bounded flight-navigation BulkData payload.");
			return false;
		}
		return true;
	}

	bool ReadPayloadBytes(
		const FMemoryView Payload,
		const FGuLiFlightNavBakeMetadata& Metadata,
		TArray<FGuLiFlightNavOctreeNode>& OutNodes,
		TArray<FGuLiFlightNavCell>& OutCells,
		TArray<FGuLiFlightNavPortal>& OutPortals,
		TArray<FGuLiFlightNavLink>& OutLinks,
		FString& OutError)
	{
		OutNodes.Reset();
		OutCells.Reset();
		OutPortals.Reset();
		OutLinks.Reset();
		OutError.Reset();
		if (Payload.IsEmpty() || Payload.GetSize() > MaximumSerializedPayloadBytes)
		{
			OutError = TEXT("Flight-navigation BulkData payload is empty or exceeds the size limit.");
			return false;
		}

		FMemoryReaderView Reader(Payload, true);
		uint32 Magic = 0;
		uint32 PayloadSchema = 0;
		uint32 DataFormat = 0;
		uint64 ContentChecksum = 0;
		int32 NodeCount = 0;
		int32 CellCount = 0;
		int32 PortalCount = 0;
		int32 LinkCount = 0;
		Reader << Magic;
		Reader << PayloadSchema;
		Reader << DataFormat;
		Reader << ContentChecksum;
		Reader << NodeCount;
		Reader << CellCount;
		Reader << PortalCount;
		Reader << LinkCount;

		if (Reader.IsError()
			|| Magic != FlightNavPayloadMagic
			|| PayloadSchema != FlightNavPayloadSchemaVersion
			|| DataFormat != GuLiFlightNavigation::CurrentDataFormatVersion
			|| DataFormat != Metadata.FormatVersion
			|| ContentChecksum == 0
			|| ContentChecksum != Metadata.ContentChecksum)
		{
			OutError = TEXT("Flight-navigation BulkData header, schema, format, or checksum does not match its metadata.");
			return false;
		}

		if (!AreCountsValid(NodeCount, CellCount, PortalCount, LinkCount))
		{
			OutError = TEXT("Flight-navigation BulkData contains invalid element counts.");
			return false;
		}

		OutNodes.SetNum(NodeCount);
		OutCells.SetNum(CellCount);
		OutPortals.SetNum(PortalCount);
		OutLinks.SetNum(LinkCount);
		for (FGuLiFlightNavOctreeNode& Target : OutNodes)
		{
			ReadVector(Reader, Target.Center);
			ReadVector(Reader, Target.Extent);
			Reader << Target.FirstChild;
			Reader << Target.LeafCellIndex;
			Reader << Target.ChildMask;
		}

		for (FGuLiFlightNavCell& Target : OutCells)
		{
			ReadVector(Reader, Target.Center);
			ReadVector(Reader, Target.Extent);
			Reader << Target.Clearance;
			Reader << Target.ComponentId;
			Reader << Target.FirstLink;
			Reader << Target.LinkCount;
			Reader << Target.StableId;
		}

		for (FGuLiFlightNavPortal& Target : OutPortals)
		{
			Reader << Target.CellA;
			Reader << Target.CellB;
			ReadVector(Reader, Target.Center);
			ReadVector(Reader, Target.Normal);
			ReadVector(Reader, Target.Extent);
			Reader << Target.Clearance;
			Reader << Target.StableId;
		}

		for (FGuLiFlightNavLink& Target : OutLinks)
		{
			Reader << Target.ToCell;
			Reader << Target.PortalIndex;
			Reader << Target.Cost;
		}

		if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
		{
			OutNodes.Reset();
			OutCells.Reset();
			OutPortals.Reset();
			OutLinks.Reset();
			OutError = TEXT("Flight-navigation BulkData is truncated or contains trailing bytes.");
			return false;
		}

		if (ComputeGraphChecksum(Metadata, OutNodes, OutCells, OutPortals, OutLinks)
			!= Metadata.ContentChecksum)
		{
			OutNodes.Reset();
			OutCells.Reset();
			OutPortals.Reset();
			OutLinks.Reset();
			OutError = TEXT("Decoded flight-navigation BulkData failed its deterministic content checksum.");
			return false;
		}
		return true;
	}
}

bool UGuLiFlightNavigationData::HasBakedData() const
{
	return PayloadLoadError.IsEmpty()
		&& Metadata.FormatVersion == GuLiFlightNavigation::CurrentDataFormatVersion
		&& Metadata.Bounds.IsValid != 0
		&& Nodes.Num() > 0
		&& Cells.Num() > 0
		&& Metadata.ContentChecksum != 0;
}

bool UGuLiFlightNavigationData::ValidateData(FString& OutError) const
{
	OutError.Reset();
	if (!PayloadLoadError.IsEmpty())
	{
		OutError = PayloadLoadError;
		return false;
	}

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
	return ComputeGraphChecksum(Metadata, Nodes, Cells, Portals, Links);
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
	SerializedPayload.RemoveBulkData();
	PayloadLoadError.Reset();
}

int64 UGuLiFlightNavigationData::GetSerializedPayloadSize() const
{
	return SerializedPayload.GetBulkDataSize();
}

bool UGuLiFlightNavigationData::RebuildSerializedPayload(FString& OutError)
{
	OutError.Reset();
	if (Nodes.IsEmpty() && Cells.IsEmpty() && Portals.IsEmpty() && Links.IsEmpty()
		&& Metadata.ContentChecksum == 0)
	{
		SerializedPayload.RemoveBulkData();
		PayloadLoadError.Reset();
		return true;
	}

	const FString PreviousPayloadLoadError = PayloadLoadError;
	PayloadLoadError.Reset();
	if (!ValidateData(OutError))
	{
		PayloadLoadError = PreviousPayloadLoadError;
		return false;
	}

	TArray<uint8> PayloadBytes;
	if (!BuildPayloadBytes(Metadata, Nodes, Cells, Portals, Links, PayloadBytes, OutError))
	{
		PayloadLoadError = PreviousPayloadLoadError;
		return false;
	}

	SerializedPayload.ClearBulkDataFlags(BULKDATA_SingleUse);
	SerializedPayload.SetBulkDataFlags(BULKDATA_SerializeCompressed);
	SerializedPayload.Lock(LOCK_READ_WRITE);
	void* Destination = SerializedPayload.Realloc(PayloadBytes.Num());
	FMemory::Memcpy(Destination, PayloadBytes.GetData(), PayloadBytes.Num());
	SerializedPayload.Unlock();
	PayloadLoadError.Reset();
	return true;
}

bool UGuLiFlightNavigationData::ValidateSerializedPayload(FString& OutError) const
{
	OutError.Reset();
	if (!ValidateData(OutError))
	{
		return false;
	}

	const int64 PayloadSize = SerializedPayload.GetBulkDataSize();
	if (PayloadSize <= 0 || PayloadSize > MaximumSerializedPayloadBytes)
	{
		OutError = TEXT("Flight-navigation BulkData payload is missing or outside the size limit.");
		return false;
	}

	const void* PayloadMemory = SerializedPayload.LockReadOnly();
	TArray<FGuLiFlightNavOctreeNode> DecodedNodes;
	TArray<FGuLiFlightNavCell> DecodedCells;
	TArray<FGuLiFlightNavPortal> DecodedPortals;
	TArray<FGuLiFlightNavLink> DecodedLinks;
	const bool bDecoded = PayloadMemory != nullptr
		&& ReadPayloadBytes(
			FMemoryView(PayloadMemory, PayloadSize),
			Metadata,
			DecodedNodes,
			DecodedCells,
			DecodedPortals,
			DecodedLinks,
			OutError);
	SerializedPayload.Unlock();
	if (!bDecoded && OutError.IsEmpty())
	{
		OutError = TEXT("Unable to lock the flight-navigation BulkData payload for validation.");
	}
	return bDecoded;
}

bool UGuLiFlightNavigationData::LoadSerializedPayload(FString& OutError)
{
	OutError.Reset();
	const int64 PayloadSize = SerializedPayload.GetBulkDataSize();
	if (PayloadSize == 0 && Metadata.ContentChecksum == 0)
	{
		Nodes.Reset();
		Cells.Reset();
		Portals.Reset();
		Links.Reset();
		return true;
	}
	if (PayloadSize <= 0 || PayloadSize > MaximumSerializedPayloadBytes)
	{
		OutError = TEXT("Flight-navigation BulkData payload is missing or outside the size limit.");
		return false;
	}

	const void* PayloadMemory = SerializedPayload.LockReadOnly();
	TArray<FGuLiFlightNavOctreeNode> DecodedNodes;
	TArray<FGuLiFlightNavCell> DecodedCells;
	TArray<FGuLiFlightNavPortal> DecodedPortals;
	TArray<FGuLiFlightNavLink> DecodedLinks;
	const bool bDecoded = PayloadMemory != nullptr
		&& ReadPayloadBytes(
			FMemoryView(PayloadMemory, PayloadSize),
			Metadata,
			DecodedNodes,
			DecodedCells,
			DecodedPortals,
			DecodedLinks,
			OutError);
	SerializedPayload.Unlock();
	if (!bDecoded)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Unable to lock the flight-navigation BulkData payload for loading.");
		}
		return false;
	}

	Nodes = MoveTemp(DecodedNodes);
	Cells = MoveTemp(DecodedCells);
	Portals = MoveTemp(DecodedPortals);
	Links = MoveTemp(DecodedLinks);
	PayloadLoadError.Reset();
	if (!ValidateData(OutError))
	{
		Nodes.Reset();
		Cells.Reset();
		Portals.Reset();
		Links.Reset();
		return false;
	}
	return true;
}

void UGuLiFlightNavigationData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FGuLiFlightNavigationCustomVersion::GUID);
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		LoadedCustomVersion = Ar.CustomVer(FGuLiFlightNavigationCustomVersion::GUID);
		if (LoadedCustomVersion
			< FGuLiFlightNavigationCustomVersion::ExplicitBulkDataPayload)
		{
			return;
		}
	}
	else if (Ar.IsSaving())
	{
		LoadedCustomVersion = FGuLiFlightNavigationCustomVersion::LatestVersion;
		FString SerializationError;
		if (!RebuildSerializedPayload(SerializationError))
		{
			UE_LOG(
				LogGuLiFlightNav,
				Error,
				TEXT("Unable to serialize flight-navigation asset '%s': %s"),
				*GetPathName(),
				*SerializationError);
			Ar.SetError();
		}
	}

	SerializedPayload.Serialize(Ar, this, INDEX_NONE, false);
}

void UGuLiFlightNavigationData::PostLoad()
{
	Super::PostLoad();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	PayloadLoadError.Reset();
	if (LoadedCustomVersion < FGuLiFlightNavigationCustomVersion::ExplicitBulkDataPayload)
	{
		Nodes.Reset();
		Cells.Reset();
		Portals.Reset();
		Links.Reset();
		PayloadLoadError = FString::Printf(
			TEXT("Legacy flight-navigation package version %d has no explicit BulkData payload; re-bake the asset with format v%u."),
			LoadedCustomVersion,
			GuLiFlightNavigation::CurrentDataFormatVersion);
		return;
	}

	FString LoadError;
	if (!LoadSerializedPayload(LoadError))
	{
		PayloadLoadError = MoveTemp(LoadError);
		UE_LOG(
			LogGuLiFlightNav,
			Error,
			TEXT("Flight-navigation asset '%s' failed closed while loading BulkData: %s"),
			*GetPathName(),
			*PayloadLoadError);
	}
}

void UGuLiFlightNavigationData::PreSave(FObjectPreSaveContext SaveContext)
{
	FString ValidationError;
	const bool bIsClearedAsset = Nodes.IsEmpty()
		&& Cells.IsEmpty()
		&& Portals.IsEmpty()
		&& Links.IsEmpty()
		&& Metadata.ContentChecksum == 0;
	if (bIsClearedAsset)
	{
		RebuildSerializedPayload(ValidationError);
	}
	else if (!ValidateData(ValidationError))
	{
		UE_LOG(
			LogGuLiFlightNav,
			Error,
			TEXT("[FLIGHTNAV_%s_GATE][InvalidData] Save rejected flight-navigation asset '%s': %s"),
			SaveContext.IsCooking() ? TEXT("COOK") : TEXT("SAVE"),
			*GetPathName(),
			*ValidationError);
	}
	else if (!RebuildSerializedPayload(ValidationError))
	{
		UE_LOG(
			LogGuLiFlightNav,
			Error,
			TEXT("[FLIGHTNAV_%s_GATE][BulkData] Save rejected flight-navigation asset '%s': %s"),
			SaveContext.IsCooking() ? TEXT("COOK") : TEXT("SAVE"),
			*GetPathName(),
			*ValidationError);
	}

	Super::PreSave(SaveContext);
}

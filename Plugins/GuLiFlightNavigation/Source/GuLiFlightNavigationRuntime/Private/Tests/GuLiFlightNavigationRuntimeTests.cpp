#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationVolume.h"
#include "Components/BrushComponent.h"

namespace
{
	UGuLiFlightNavigationData* MakeTwoCellNavigationData()
	{
		UGuLiFlightNavigationData* Data = NewObject<UGuLiFlightNavigationData>();
		Data->Metadata.FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;
		Data->Metadata.DefinitionRevision = 1;
		Data->Metadata.BakeId = FGuid::NewGuid();
		Data->Metadata.Bounds = FBox(FVector(-200.0, -100.0, -100.0), FVector(200.0, 100.0, 100.0));
		Data->Metadata.MinimumCellSize = 100.0f;
		Data->Metadata.BakedAgentRadius = 10.0f;
		Data->Metadata.GeometrySignature = 0x1234ull;
		Data->Metadata.SettingsHash = 0x5678ull;

		Data->Nodes.SetNum(9);
		Data->Nodes[0].Center = FVector::ZeroVector;
		Data->Nodes[0].Extent = FVector(200.0, 100.0, 100.0);
		Data->Nodes[0].FirstChild = 1;
		Data->Nodes[0].ChildMask = 0xff;
		for (uint8 Octant = 0; Octant < 8; ++Octant)
		{
			FGuLiFlightNavOctreeNode& Child = Data->Nodes[1 + Octant];
			Child.Center = FVector(
				(Octant & 1) != 0 ? 100.0 : -100.0,
				(Octant & 2) != 0 ? 50.0 : -50.0,
				(Octant & 4) != 0 ? 50.0 : -50.0);
			Child.Extent = FVector(100.0, 50.0, 50.0);
			Child.LeafCellIndex = Octant < 2 ? Octant : INDEX_NONE;
		}

		Data->Cells.SetNum(2);
		Data->Cells[0].Center = FVector(-100.0, -50.0, -50.0);
		Data->Cells[0].Extent = FVector(100.0, 50.0, 50.0);
		Data->Cells[0].Clearance = 60.0f;
		Data->Cells[0].ComponentId = 0;
		Data->Cells[0].FirstLink = 0;
		Data->Cells[0].LinkCount = 1;
		Data->Cells[0].StableId = 1;
		Data->Cells[1].Center = FVector(100.0, -50.0, -50.0);
		Data->Cells[1].Extent = FVector(100.0, 50.0, 50.0);
		Data->Cells[1].Clearance = 60.0f;
		Data->Cells[1].ComponentId = 0;
		Data->Cells[1].FirstLink = 1;
		Data->Cells[1].LinkCount = 1;
		Data->Cells[1].StableId = 2;

		FGuLiFlightNavPortal& Portal = Data->Portals.AddDefaulted_GetRef();
		Portal.CellA = 0;
		Portal.CellB = 1;
		Portal.Center = FVector(0.0, -50.0, -50.0);
		Portal.Normal = FVector::ForwardVector;
		Portal.Extent = FVector(0.0, 50.0, 50.0);
		Portal.Clearance = 60.0f;
		Portal.StableId = 3;

		FGuLiFlightNavLink& LinkA = Data->Links.AddDefaulted_GetRef();
		LinkA.ToCell = 1;
		LinkA.PortalIndex = 0;
		LinkA.Cost = 200.0f;
		FGuLiFlightNavLink& LinkB = Data->Links.AddDefaulted_GetRef();
		LinkB.ToCell = 0;
		LinkB.PortalIndex = 0;
		LinkB.Cost = 200.0f;

		Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
		return Data;
	}

	UGuLiFlightNavigationData* MakeEqualAlphaCornerChainData()
	{
		UGuLiFlightNavigationData* Data = NewObject<UGuLiFlightNavigationData>();
		Data->Metadata.FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;
		Data->Metadata.DefinitionRevision = 1;
		Data->Metadata.BakeId = FGuid::NewGuid();
		Data->Metadata.Bounds = FBox(
			FVector(-100.0, -100.0, -100.0), FVector(100.0, 100.0, 100.0));
		Data->Metadata.MinimumCellSize = 100.0f;
		Data->Metadata.BakedAgentRadius = 50.0f;
		Data->Metadata.GeometrySignature = 0x9abcull;
		Data->Metadata.SettingsHash = 0xdef0ull;

		Data->Nodes.SetNum(9);
		Data->Nodes[0].Center = FVector::ZeroVector;
		Data->Nodes[0].Extent = FVector(100.0, 100.0, 100.0);
		Data->Nodes[0].FirstChild = 1;
		Data->Nodes[0].ChildMask = 0xff;
		for (uint8 Octant = 0; Octant < 8; ++Octant)
		{
			FGuLiFlightNavOctreeNode& Child = Data->Nodes[1 + Octant];
			Child.Center = FVector(
				(Octant & 1) != 0 ? 50.0 : -50.0,
				(Octant & 2) != 0 ? 50.0 : -50.0,
				(Octant & 4) != 0 ? 50.0 : -50.0);
			Child.Extent = FVector(50.0, 50.0, 50.0);
		}
		Data->Nodes[1 + 0].LeafCellIndex = 0;
		Data->Nodes[1 + 1].LeafCellIndex = 1;
		Data->Nodes[1 + 3].LeafCellIndex = 2;
		Data->Nodes[1 + 7].LeafCellIndex = 3;

		Data->Cells.SetNum(4);
		Data->Cells[0].Center = Data->Nodes[1 + 0].Center;
		Data->Cells[0].Extent = Data->Nodes[1 + 0].Extent;
		Data->Cells[0].FirstLink = 0;
		Data->Cells[0].LinkCount = 1;
		Data->Cells[0].StableId = 10;
		Data->Cells[1].Center = Data->Nodes[1 + 1].Center;
		Data->Cells[1].Extent = Data->Nodes[1 + 1].Extent;
		Data->Cells[1].FirstLink = 1;
		Data->Cells[1].LinkCount = 2;
		Data->Cells[1].StableId = 11;
		Data->Cells[2].Center = Data->Nodes[1 + 3].Center;
		Data->Cells[2].Extent = Data->Nodes[1 + 3].Extent;
		Data->Cells[2].FirstLink = 3;
		Data->Cells[2].LinkCount = 2;
		Data->Cells[2].StableId = 12;
		Data->Cells[3].Center = Data->Nodes[1 + 7].Center;
		Data->Cells[3].Extent = Data->Nodes[1 + 7].Extent;
		Data->Cells[3].FirstLink = 5;
		Data->Cells[3].LinkCount = 1;
		Data->Cells[3].StableId = 13;
		for (FGuLiFlightNavCell& Cell : Data->Cells)
		{
			Cell.Clearance = 100.0f;
			Cell.ComponentId = 0;
		}

		FGuLiFlightNavPortal& Portal01 = Data->Portals.AddDefaulted_GetRef();
		Portal01.CellA = 0;
		Portal01.CellB = 1;
		Portal01.Center = FVector(0.0, -50.0, -50.0);
		Portal01.Normal = FVector::ForwardVector;
		Portal01.Extent = FVector(0.0, 50.0, 50.0);
		Portal01.Clearance = 100.0f;
		Portal01.StableId = 20;

		FGuLiFlightNavPortal& Portal12 = Data->Portals.AddDefaulted_GetRef();
		Portal12.CellA = 1;
		Portal12.CellB = 2;
		Portal12.Center = FVector(50.0, 0.0, -50.0);
		Portal12.Normal = FVector::RightVector;
		Portal12.Extent = FVector(50.0, 0.0, 50.0);
		Portal12.Clearance = 100.0f;
		Portal12.StableId = 21;

		FGuLiFlightNavPortal& Portal23 = Data->Portals.AddDefaulted_GetRef();
		Portal23.CellA = 2;
		Portal23.CellB = 3;
		Portal23.Center = FVector(50.0, 50.0, 0.0);
		Portal23.Normal = FVector::UpVector;
		Portal23.Extent = FVector(50.0, 50.0, 0.0);
		Portal23.Clearance = 100.0f;
		Portal23.StableId = 22;

		FGuLiFlightNavLink& Link01 = Data->Links.AddDefaulted_GetRef();
		Link01.ToCell = 1;
		Link01.PortalIndex = 0;
		Link01.Cost = 200.0f;
		FGuLiFlightNavLink& Link10 = Data->Links.AddDefaulted_GetRef();
		Link10.ToCell = 0;
		Link10.PortalIndex = 0;
		Link10.Cost = 200.0f;
		FGuLiFlightNavLink& Link12 = Data->Links.AddDefaulted_GetRef();
		Link12.ToCell = 2;
		Link12.PortalIndex = 1;
		Link12.Cost = 100.0f;
		FGuLiFlightNavLink& Link21 = Data->Links.AddDefaulted_GetRef();
		Link21.ToCell = 1;
		Link21.PortalIndex = 1;
		Link21.Cost = 100.0f;
		FGuLiFlightNavLink& Link23 = Data->Links.AddDefaulted_GetRef();
		Link23.ToCell = 3;
		Link23.PortalIndex = 2;
		Link23.Cost = 100.0f;
		FGuLiFlightNavLink& Link32 = Data->Links.AddDefaulted_GetRef();
		Link32.ToCell = 2;
		Link32.PortalIndex = 2;
		Link32.Cost = 100.0f;

		Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
		return Data;
	}

	TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> CopyRuntimeGraph(
		const UGuLiFlightNavigationData& Data)
	{
		TSharedPtr<FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> Graph =
			MakeShared<FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe>();
		Graph->Metadata = Data.Metadata;
		Graph->Nodes = Data.Nodes;
		Graph->Cells = Data.Cells;
		Graph->Portals = Data.Portals;
		Graph->Links = Data.Links;
		return Graph;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationVolumeNoCollisionTest,
	"GuLi.FlightNavigation.Runtime.VolumeIsNeverPhysicalWorldGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationVolumeNoCollisionTest::RunTest(const FString& Parameters)
{
	const AGuLiFlightNavigationVolume* Volume = GetDefault<AGuLiFlightNavigationVolume>();
	if (!TestNotNull(TEXT("FlightNav Volume CDO exists"), Volume))
	{
		return false;
	}
	TestFalse(TEXT("FlightNav query envelopes disable Actor collision"),
		Volume->GetActorEnableCollision());
	const UBrushComponent* Brush = Volume->GetBrushComponent();
	return TestNotNull(TEXT("FlightNav Volume owns a BrushComponent"), Brush)
		&& TestEqual(TEXT("The query brush cannot block WorldStatic validation sweeps"),
			Brush->GetCollisionEnabled(), ECollisionEnabled::NoCollision)
		&& TestEqual(TEXT("The query brush ignores the WorldStatic channel"),
			Brush->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Ignore);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationRuntimePathTest,
	"GuLi.FlightNavigation.Runtime.PathAndChecksum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationRuntimePathTest::RunTest(const FString& Parameters)
{
	UGuLiFlightNavigationData* Data = MakeTwoCellNavigationData();
	FString ValidationError;
	TestTrue(TEXT("Golden graph validates"), Data->ValidateData(ValidationError));
	TestTrue(TEXT("Validation error is empty"), ValidationError.IsEmpty());

	const FGuLiFlightNavigationQuery Query(Data->CreateRuntimeGraph(&ValidationError));
	TestTrue(TEXT("Runtime query is valid"), Query.IsValid());
	const FVector Start(-150.0, -50.0, -50.0);
	const FVector Goal(150.0, -50.0, -50.0);
	const FGuLiFlightNavPathResult Result = Query.FindPath(Start, Goal);
	TestEqual(TEXT("A* succeeds"), Result.Status, EGuLiFlightNavPathStatus::Success);
	TestEqual(TEXT("Path crosses both cells"), Result.CellPath.Num(), 2);
	TestTrue(TEXT("Path has endpoints"), Result.Points.Num() >= 2);

	TFuture<FGuLiFlightNavPathResult> Future = Query.FindPathAsync(Start, Goal);
	TestEqual(TEXT("Async A* succeeds"), Future.Get().Status, EGuLiFlightNavPathStatus::Success);

	FGuLiFlightNavPathQueryOptions OversizedAgent;
	OversizedAgent.AgentRadius = 11.0f;
	TestEqual(
		TEXT("Larger-than-baked agent is rejected"),
		Query.FindPath(Start, Goal, OversizedAgent).Status,
		EGuLiFlightNavPathStatus::InsufficientClearance);

	const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe> CancelToken =
		MakeShared<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>();
	CancelToken->Cancel();
	TestEqual(
		TEXT("Cancelled request is deterministic"),
		Query.FindPath(Start, Goal, FGuLiFlightNavPathQueryOptions(), CancelToken).Status,
		EGuLiFlightNavPathStatus::Cancelled);

	Data->Cells[0].Clearance += 1.0f;
	TestFalse(TEXT("Checksum detects graph mutation"), Data->ValidateData(ValidationError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationAuthoritySegmentTopologyTest,
	"GuLi.FlightNavigation.Runtime.AuthoritySegmentTopologyWithoutAStar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationAuthoritySegmentTopologyTest::RunTest(const FString& Parameters)
{
	const FVector Start(-150.0, -50.0, -50.0);
	const FVector End(150.0, -50.0, -50.0);
	UGuLiFlightNavigationData* Data = MakeTwoCellNavigationData();
	Data->Metadata.BakedAgentRadius = 100.0f;
	Data->Cells[0].Clearance = 80.0f;
	Data->Cells[1].Clearance = 80.0f;
	Data->Portals[0].Clearance = 80.0f;

	FGuLiFlightNavigationQuery Query(CopyRuntimeGraph(*Data));
	int32 StartCell = INDEX_NONE;
	int32 EndCell = INDEX_NONE;
	TestEqual(TEXT("A straight segment crosses the declared direct Link/Portal"),
		Query.ValidateAuthoritativeSegment(Start, End, 50.0f, &StartCell, &EndCell),
		EGuLiFlightNavSegmentStatus::Valid);
	TestEqual(TEXT("The start endpoint resolves to its exact cell"), StartCell, 0);
	TestEqual(TEXT("The end endpoint resolves to its exact cell"), EndCell, 1);

	Data->Cells[0].LinkCount = 0;
	Query = FGuLiFlightNavigationQuery(CopyRuntimeGraph(*Data));
	TestEqual(TEXT("Formation endpoints may share a component without a straight direct Link"),
		Query.ValidateEndpointsInSameComponent(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::Valid);
	TestEqual(TEXT("Sampling adjacent cells cannot bypass a missing direct Link"),
		Query.ValidateAuthoritativeSegment(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::MissingLink);

	Data->Cells[0].LinkCount = 1;
	Data->Portals[0].Clearance = 40.0f;
	Query = FGuLiFlightNavigationQuery(CopyRuntimeGraph(*Data));
	TestEqual(TEXT("Portal clearance is checked independently from endpoint-cell clearance"),
		Query.ValidateAuthoritativeSegment(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::InvalidPortal);

	Data->Portals[0].Clearance = 80.0f;
	Data->Portals[0].Center.Y = 500.0f;
	Query = FGuLiFlightNavigationQuery(CopyRuntimeGraph(*Data));
	TestEqual(TEXT("A Link cannot claim a portal the segment does not geometrically cross"),
		Query.ValidateAuthoritativeSegment(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::InvalidPortal);

	Data->Portals[0].Center.Y = -50.0f;
	Data->Cells[1].ComponentId = 1;
	Query = FGuLiFlightNavigationQuery(CopyRuntimeGraph(*Data));
	TestEqual(TEXT("Formation endpoints cannot span baked components"),
		Query.ValidateEndpointsInSameComponent(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::Disconnected);
	TestEqual(TEXT("Endpoints in different baked components are rejected without starting A*"),
		Query.ValidateAuthoritativeSegment(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::Disconnected);

	Data->Cells[1].ComponentId = 0;
	Data->Cells[1].Clearance = 40.0f;
	Query = FGuLiFlightNavigationQuery(CopyRuntimeGraph(*Data));
	TestEqual(TEXT("Endpoint cell clearance is enforced"),
		Query.ValidateAuthoritativeSegment(Start, End, 50.0f),
		EGuLiFlightNavSegmentStatus::InsufficientClearance);

	Data->Cells[1].Clearance = 80.0f;
	Query = FGuLiFlightNavigationQuery(CopyRuntimeGraph(*Data));
	TestEqual(TEXT("The agent radius is inset from the outer navigation bounds"),
		Query.ValidateAuthoritativeSegment(FVector(-151.0, -50.0, -50.0), End, 50.0f),
		EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationAdaptiveLeafSegmentTest,
	"GuLi.FlightNavigation.Runtime.AuthoritySegmentTraversesAdaptiveLeafChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationAdaptiveLeafSegmentTest::RunTest(const FString& Parameters)
{
	UGuLiFlightNavigationData* Data = MakeEqualAlphaCornerChainData();
	FString ValidationError;
	TestTrue(TEXT("The adaptive corner-chain fixture is valid baked data"),
		Data->ValidateData(ValidationError));
	TestTrue(TEXT("The adaptive fixture validation error is empty"), ValidationError.IsEmpty());

	const FGuLiFlightNavigationQuery Query(Data->CreateRuntimeGraph(&ValidationError));
	TestTrue(TEXT("The adaptive runtime graph is valid"), Query.IsValid());
	int32 StartCell = INDEX_NONE;
	int32 EndCell = INDEX_NONE;
	TestEqual(
		TEXT("A segment crosses several thin adaptive leaves that fixed spacing would skip"),
		Query.ValidateAuthoritativeSegment(
			FVector(-75.0, -75.0, -75.0),
			FVector(75.0, 73.0, 71.0),
			25.0f,
			&StartCell,
			&EndCell),
		EGuLiFlightNavSegmentStatus::Valid);
	TestEqual(TEXT("The adaptive corridor starts in cell 0"), StartCell, 0);
	TestEqual(TEXT("The adaptive corridor ends in cell 3"), EndCell, 3);

	TestEqual(
		TEXT("An exact octree corner may traverse several portals at the same segment alpha"),
		Query.ValidateAuthoritativeSegment(
			FVector(-75.0, -75.0, -75.0),
			FVector(75.0, 75.0, 75.0),
			25.0f),
		EGuLiFlightNavSegmentStatus::Valid);

	Data->Cells[2].LinkCount = 1;
	const FGuLiFlightNavigationQuery MissingFinalLinkQuery(CopyRuntimeGraph(*Data));
	TestEqual(
		TEXT("A same-alpha corner chain still fails closed when its final directed Link is absent"),
		MissingFinalLinkQuery.ValidateAuthoritativeSegment(
			FVector(-75.0, -75.0, -75.0),
			FVector(75.0, 75.0, 75.0),
			25.0f),
		EGuLiFlightNavSegmentStatus::MissingLink);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationMetadataChecksumTest,
	"GuLi.FlightNavigation.Runtime.MetadataFieldsAffectChecksum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationMetadataChecksumTest::RunTest(const FString& Parameters)
{
	UGuLiFlightNavigationData* Data = MakeTwoCellNavigationData();
	TestEqual(
		TEXT("Synthetic data uses the current format-v3 contract"),
		Data->Metadata.FormatVersion,
		static_cast<uint32>(3u));

	const uint64 OriginalChecksum = Data->ComputeContentChecksum();
	const uint64 OriginalGeometrySignature = Data->Metadata.GeometrySignature;
	++Data->Metadata.GeometrySignature;
	TestNotEqual(
		TEXT("The source-geometry signature participates in the content checksum"),
		Data->ComputeContentChecksum(),
		OriginalChecksum);

	Data->Metadata.GeometrySignature = OriginalGeometrySignature;
	++Data->Metadata.SettingsHash;
	TestNotEqual(
		TEXT("The bake-settings hash participates in the content checksum"),
		Data->ComputeContentChecksum(),
		OriginalChecksum);

	Data->Metadata.SettingsHash = 0;
	Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
	FString ValidationError;
	TestFalse(TEXT("Format v3 rejects data without a settings hash"), Data->ValidateData(ValidationError));
	TestTrue(
		TEXT("The missing-metadata failure identifies the signature/hash contract"),
		ValidationError.Contains(TEXT("signature"), ESearchCase::IgnoreCase)
			|| ValidationError.Contains(TEXT("settings hash"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationBulkDataPayloadTest,
	"GuLi.FlightNavigation.Runtime.VersionedBulkDataPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationBulkDataPayloadTest::RunTest(const FString& Parameters)
{
	UGuLiFlightNavigationData* Data = MakeTwoCellNavigationData();
	FString Error;
	TestEqual(TEXT("Synthetic graph begins without serialized BulkData"),
		Data->GetSerializedPayloadSize(), static_cast<int64>(0));
	TestTrue(TEXT("A valid graph builds its explicit BulkData payload"),
		Data->RebuildSerializedPayload(Error));
	TestTrue(TEXT("The explicit BulkData payload contains bytes"),
		Data->GetSerializedPayloadSize() > 0);
	TestTrue(TEXT("The explicit BulkData payload decodes and matches the graph checksum"),
		Data->ValidateSerializedPayload(Error));

	Data->Cells[0].Clearance += 1.0f;
	Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
	TestFalse(TEXT("A graph mutation cannot reuse a stale BulkData payload"),
		Data->ValidateSerializedPayload(Error));
	TestTrue(TEXT("Rebuilding synchronizes BulkData after a valid graph mutation"),
		Data->RebuildSerializedPayload(Error));
	TestTrue(TEXT("Rebuilt BulkData validates"), Data->ValidateSerializedPayload(Error));

	Data->Metadata.FormatVersion = 2;
	Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
	TestFalse(TEXT("Legacy format-v2 data fails closed and requires a re-bake"),
		Data->RebuildSerializedPayload(Error));
	TestTrue(TEXT("Legacy rejection identifies the unsupported data format"),
		Error.Contains(TEXT("Unsupported"), ESearchCase::IgnoreCase));

	Data->ResetBakedData();
	TestEqual(TEXT("Clearing baked data also removes its BulkData payload"),
		Data->GetSerializedPayloadSize(), static_cast<int64>(0));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/BrushComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "GuLiFlightNavigationVolume.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	UGuLiFlightNavigationData* MakeSingleCellNavigationData(
		UObject* Outer,
		const FVector& Center,
		const float HalfExtent,
		const uint64 StableId)
	{
		UGuLiFlightNavigationData* Data = NewObject<UGuLiFlightNavigationData>(Outer);
		Data->Metadata.FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;
		Data->Metadata.DefinitionRevision = 1;
		Data->Metadata.BakeId = FGuid::NewGuid();
		Data->Metadata.Bounds = FBox::BuildAABB(Center, FVector(HalfExtent));
		Data->Metadata.MinimumCellSize = HalfExtent * 2.0f;
		Data->Metadata.BakedAgentRadius = 20.0f;
		Data->Metadata.GeometrySignature = StableId + 100;
		Data->Metadata.SettingsHash = StableId + 200;

		FGuLiFlightNavOctreeNode& Node = Data->Nodes.AddDefaulted_GetRef();
		Node.Center = Center;
		Node.Extent = FVector(HalfExtent);
		Node.LeafCellIndex = 0;

		FGuLiFlightNavCell& Cell = Data->Cells.AddDefaulted_GetRef();
		Cell.Center = Center;
		Cell.Extent = FVector(HalfExtent);
		Cell.Clearance = 20.0f;
		Cell.ComponentId = 0;
		Cell.StableId = StableId;

		Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
		return Data;
	}

	struct FFlightNavigationSubsystemFixture
	{
		UWorld* World = nullptr;
		UGuLiFlightNavigationSubsystem* Subsystem = nullptr;
		bool bWorldContextRegistered = false;

		~FFlightNavigationSubsystemFixture()
		{
			if (World == nullptr)
			{
				return;
			}

			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine != nullptr)
			{
				GEngine->DestroyWorldContext(World);
			}
			World = nullptr;
			Subsystem = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for the transient FlightNav runtime World"), GEngine))
			{
				return false;
			}

			World = UWorld::CreateWorld(
				EWorldType::GamePreview,
				false,
				FName(TEXT("GuLiFlightNavigationSubsystemSegmentTestWorld")));
			if (!Test.TestNotNull(TEXT("Transient GamePreview World exists"), World))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::GamePreview).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			Subsystem = NewObject<UGuLiFlightNavigationSubsystem>(World);
			return Test.TestNotNull(TEXT("Transient FlightNav subsystem exists"), Subsystem);
		}

		AGuLiFlightNavigationVolume* AddVolume(
			FAutomationTestBase& Test,
			const FName Name,
			const FVector& Center,
			const float VolumeHalfExtent,
			const float DataHalfExtent,
			const int32 Priority,
			const uint64 StableId) const
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = Name;
			SpawnParameters.ObjectFlags |= RF_Transient;
			AGuLiFlightNavigationVolume* Volume = World->SpawnActor<AGuLiFlightNavigationVolume>(
				Center,
				FRotator::ZeroRotator,
				SpawnParameters);
			if (!Test.TestNotNull(*FString::Printf(TEXT("%s was spawned"), *Name.ToString()), Volume))
			{
				return nullptr;
			}

			UBrushComponent* NavigationBrush = Volume->GetBrushComponent();
			if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s has a brush component"), *Name.ToString()),
				NavigationBrush))
			{
				return nullptr;
			}
			NavigationBrush->BrushBodySetup = NewObject<UBodySetup>(NavigationBrush);
			FKBoxElem& BoundsElement = NavigationBrush->BrushBodySetup->AggGeom.BoxElems.AddDefaulted_GetRef();
			BoundsElement.X = VolumeHalfExtent * 2.0f;
			BoundsElement.Y = VolumeHalfExtent * 2.0f;
			BoundsElement.Z = VolumeHalfExtent * 2.0f;
			NavigationBrush->UpdateBounds();
			const FBoxSphereBounds NavigationBounds = Volume->GetBounds();
			Test.TestTrue(
				*FString::Printf(TEXT("%s brush bounds use the requested center"), *Name.ToString()),
				NavigationBounds.Origin.Equals(Center));
			Test.TestTrue(
				*FString::Printf(TEXT("%s brush bounds use the requested extent"), *Name.ToString()),
				NavigationBounds.BoxExtent.Equals(FVector(VolumeHalfExtent)));

			Volume->NavigationData = MakeSingleCellNavigationData(
				Volume, Center, DataHalfExtent, StableId);
			Volume->QueryPriority = Priority;
			Subsystem->RegisterVolume(Volume);
			Test.TestTrue(
				*FString::Printf(TEXT("%s contains its navigation center"), *Name.ToString()),
				Volume->ContainsNavigationPoint(Center));
			return Volume;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationSubsystemAuthoritySegmentSelectionTest,
	"GuLi.FlightNavigation.Runtime.SubsystemAuthoritySegmentResolvesFromStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationSubsystemAuthoritySegmentSelectionTest::RunTest(
	const FString& Parameters)
{
	FFlightNavigationSubsystemFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AGuLiFlightNavigationVolume* StartVolume = Fixture.AddVolume(
		*this,
		TEXT("StartNavigationVolume"),
		FVector::ZeroVector,
		80.0f,
		100.0f,
		0,
		1);
	if (!StartVolume)
	{
		return false;
	}

	const uint64 StartChecksum = StartVolume->NavigationData->Metadata.ContentChecksum;
	const int32 StartNodeCount = StartVolume->NavigationData->Nodes.Num();
	const int32 StartCellCount = StartVolume->NavigationData->Cells.Num();

	const EGuLiFlightNavSegmentStatus ContainedSegmentStatus =
		Fixture.Subsystem->ValidateAuthoritativeSegment(
			FVector::ZeroVector, FVector(50.0, 0.0, 0.0), 10.0f);
	TestTrue(
		*FString::Printf(
			TEXT("A segment contained by the start-selected graph is valid (status=%d, current bounds=%s)"),
			static_cast<int32>(ContainedSegmentStatus),
			*StartVolume->GetBounds().GetBox().ToString()),
		ContainedSegmentStatus == EGuLiFlightNavSegmentStatus::Valid);
	TestEqual(
		TEXT("Current Volume bounds apply the same agent-radius inset as baked bounds"),
		Fixture.Subsystem->ValidateAuthoritativeSegment(
			FVector::ZeroVector, FVector(75.0, 0.0, 0.0), 10.0f),
		EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation);
	TestEqual(
		TEXT("End outside the start graph is classified as endpoint-outside navigation"),
		Fixture.Subsystem->ValidateAuthoritativeSegment(
			FVector::ZeroVector, FVector(250.0, 0.0, 0.0), 10.0f),
		EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation);

	AGuLiFlightNavigationVolume* EndOnlyVolume = Fixture.AddVolume(
		*this,
		TEXT("EndOnlyNavigationVolume"),
		FVector(250.0, 0.0, 0.0),
		100.0f,
		100.0f,
		100,
		2);
	if (!EndOnlyVolume)
	{
		return false;
	}
	TestEqual(
		TEXT("A higher-priority end-only graph cannot replace the graph containing Start"),
		Fixture.Subsystem->ValidateAuthoritativeSegment(
			FVector::ZeroVector, FVector(250.0, 0.0, 0.0), 10.0f),
		EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation);
	TestEqual(
		TEXT("The baked-radius authority gate remains strict"),
		Fixture.Subsystem->ValidateAuthoritativeSegment(
			FVector::ZeroVector, FVector(50.0, 0.0, 0.0), 21.0f),
		EGuLiFlightNavSegmentStatus::InsufficientClearance);

	TestEqual(
		TEXT("Authority validation does not mutate the baked checksum"),
		StartVolume->NavigationData->Metadata.ContentChecksum,
		StartChecksum);
	TestEqual(
		TEXT("Authority validation does not mutate octree nodes"),
		StartVolume->NavigationData->Nodes.Num(),
		StartNodeCount);
	TestEqual(
		TEXT("Authority validation does not mutate cells"),
		StartVolume->NavigationData->Cells.Num(),
		StartCellCount);

	Fixture.Subsystem->UnregisterVolume(StartVolume);
	TestEqual(
		TEXT("An end-only graph is never selected when no registered graph contains Start"),
		Fixture.Subsystem->ValidateAuthoritativeSegment(
			FVector::ZeroVector, FVector(250.0, 0.0, 0.0), 10.0f),
		EGuLiFlightNavSegmentStatus::InvalidData);
	return true;
}

#endif

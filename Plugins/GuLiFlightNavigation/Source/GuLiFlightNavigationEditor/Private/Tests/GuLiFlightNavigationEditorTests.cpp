#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GuLiFlightNavigationBaker.h"
#include "GuLiFlightNavigationCookGate.h"
#include "GuLiFlightNavigationCookSettings.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationEditorLibrary.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationVolume.h"

namespace GuLiFlightNavigationEditorTests
{
	struct FTransientEditorWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FTransientEditorWorldFixture()
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
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for the transient FlightNav editor World"), GEngine))
			{
				return false;
			}

			World = UWorld::CreateWorld(
				EWorldType::EditorPreview,
				false,
				FName(TEXT("GuLiFlightNavigationValidationTestWorld")));
			if (!Test.TestNotNull(TEXT("Transient EditorPreview World exists"), World))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			return true;
		}

		AGuLiFlightNavigationVolume* SpawnNavigationVolume(
			FAutomationTestBase& Test,
			const FVector& FullSize = FVector(4000.0, 4000.0, 4000.0)) const
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = TEXT("FlightNavigationTestVolume");
			SpawnParameters.ObjectFlags |= RF_Transient;
			AGuLiFlightNavigationVolume* Volume =
				World->SpawnActor<AGuLiFlightNavigationVolume>(SpawnParameters);
			if (!Test.TestNotNull(TEXT("Flight-navigation test volume was spawned"), Volume))
			{
				return nullptr;
			}

			UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(GetTransientPackage());
			CubeBuilder->X = FullSize.X;
			CubeBuilder->Y = FullSize.Y;
			CubeBuilder->Z = FullSize.Z;
			CubeBuilder->Hollow = false;
			CubeBuilder->Tessellated = false;
			UActorFactory::CreateBrushForVolumeActor(Volume, CubeBuilder);
			Volume->NavigationData = NewObject<UGuLiFlightNavigationData>(Volume, TEXT("TransientNavigationData"));
			World->UpdateWorldComponents(true, false);

			Test.TestTrue(
				TEXT("Generated volume brush has finite non-empty bounds"),
				Volume->GetComponentsBoundingBox(true).IsValid != 0
					&& Volume->GetComponentsBoundingBox(true).GetExtent().GetMin() > UE_SMALL_NUMBER);
			return Volume;
		}

		UBoxComponent* SpawnCollisionBox(
			FAutomationTestBase& Test,
			const FName ActorName,
			const FVector& Location,
			const FVector& Extent,
			const EComponentMobility::Type Mobility) const
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = ActorName;
			SpawnParameters.ObjectFlags |= RF_Transient;
			AActor* Actor = World->SpawnActor<AActor>(SpawnParameters);
			if (!Test.TestNotNull(FString::Printf(TEXT("%s Actor was spawned"), *ActorName.ToString()), Actor))
			{
				return nullptr;
			}

			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("CollisionBox"));
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->SetMobility(Mobility);
			Box->SetBoxExtent(Extent);
			Box->SetRelativeLocation(Location);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
			Box->RegisterComponent();
			return Box;
		}
	};

	FGuLiFlightNavBakeSettings MakeCompactBakeSettings()
	{
		FGuLiFlightNavBakeSettings Settings;
		Settings.MinimumCellSize = 500.0f;
		Settings.AgentRadius = 50.0f;
		Settings.MaximumDepth = 4;
		Settings.MaximumNodes = 10000;
		Settings.MaximumCells = 5000;
		Settings.FaceCoordinateTolerance = 0.1f;
		Settings.MinimumPortalSpan = 10.0f;
		Settings.CollisionChannel = ECC_WorldStatic;
		Settings.bTraceComplex = false;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationNoCollisionContainmentTest,
	"GuLi.FlightNavigation.Editor.NoCollisionVolumeRetainsBoundsContainment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationNoCollisionContainmentTest::RunTest(const FString& Parameters)
{
	using namespace GuLiFlightNavigationEditorTests;
	FTransientEditorWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AGuLiFlightNavigationVolume* Volume = Fixture.SpawnNavigationVolume(*this);
	if (!Volume || !Volume->NavigationData)
	{
		return false;
	}

	const FBox AuthoredBounds = Volume->GetBounds().GetBox();
	Volume->NavigationData->Metadata.Bounds = AuthoredBounds;
	TestFalse(TEXT("The authored FlightNav Volume remains non-physical"),
		Volume->GetActorEnableCollision());
	TestTrue(TEXT("Disabling physical collision does not disable a baked-bounds query"),
		Volume->ContainsNavigationPoint(AuthoredBounds.GetCenter()));
	TestFalse(TEXT("A point outside current and baked bounds remains excluded"),
		Volume->ContainsNavigationPoint(AuthoredBounds.Max + FVector(1.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationDeterministicBakeTest,
	"GuLi.FlightNavigation.Editor.DeterministicBakeAndPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationDeterministicBakeTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-4000.0), FVector(4000.0));
	const FBox Pillar(FVector(-300.0, -300.0, -4000.0), FVector(300.0, 300.0, 4000.0));
	FGuLiFlightNavBakeSettings Settings;
	Settings.MinimumCellSize = 500.0f;
	Settings.AgentRadius = 100.0f;
	Settings.MaximumDepth = 5;
	Settings.MaximumNodes = 100000;
	Settings.MaximumCells = 50000;
	const auto IsBlocked = [&Pillar](const FBox& Candidate)
	{
		return Candidate.Intersect(Pillar);
	};

	UGuLiFlightNavigationData* FirstData = NewObject<UGuLiFlightNavigationData>();
	FString Error;
	TestTrue(
		TEXT("Synthetic pillar bake succeeds"),
		FGuLiFlightNavigationBaker::Build(Bounds, Settings, TEXT("/Test/FlightNav"), TEXT("Pillar"), 1, IsBlocked, *FirstData, Error));
	TestTrue(TEXT("Baked data validates"), FirstData->ValidateData(Error));
	TestTrue(TEXT("Adaptive octree contains internal nodes"), FirstData->Nodes.Num() > FirstData->Cells.Num());
	TestTrue(TEXT("Bake generated portals"), !FirstData->Portals.IsEmpty());

	const FGuLiFlightNavigationQuery Query(FirstData->CreateRuntimeGraph(&Error));
	FGuLiFlightNavPathQueryOptions Options;
	Options.bSmoothPath = true;
	const FGuLiFlightNavPathResult Path = Query.FindPath(
		FVector(-3000.0, 0.0, 0.0),
		FVector(3000.0, 0.0, 0.0),
		Options);
	TestEqual(TEXT("A* routes around the pillar"), Path.Status, EGuLiFlightNavPathStatus::Success);
	TestTrue(TEXT("Smoothed detour keeps at least one bend"), Path.Points.Num() >= 3);

	UGuLiFlightNavigationData* SecondData = NewObject<UGuLiFlightNavigationData>();
	TestTrue(
		TEXT("Second synthetic bake succeeds"),
		FGuLiFlightNavigationBaker::Build(Bounds, Settings, TEXT("/Test/FlightNav"), TEXT("Pillar"), 1, IsBlocked, *SecondData, Error));
	TestEqual(TEXT("Geometry signature is deterministic"), SecondData->Metadata.GeometrySignature, FirstData->Metadata.GeometrySignature);
	TestEqual(TEXT("Content checksum is deterministic"), SecondData->Metadata.ContentChecksum, FirstData->Metadata.ContentChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationDisconnectedBakeTest,
	"GuLi.FlightNavigation.Editor.DisconnectedComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationDisconnectedBakeTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-4000.0), FVector(4000.0));
	const FBox Wall(FVector(-300.0, -4000.0, -4000.0), FVector(300.0, 4000.0, 4000.0));
	FGuLiFlightNavBakeSettings Settings;
	Settings.MinimumCellSize = 500.0f;
	Settings.AgentRadius = 100.0f;
	Settings.MaximumDepth = 5;
	const auto IsBlocked = [&Wall](const FBox& Candidate)
	{
		return Candidate.Intersect(Wall);
	};

	UGuLiFlightNavigationData* Data = NewObject<UGuLiFlightNavigationData>();
	FString Error;
	TestTrue(
		TEXT("Synthetic wall bake succeeds"),
		FGuLiFlightNavigationBaker::Build(Bounds, Settings, TEXT("/Test/FlightNav"), TEXT("Wall"), 1, IsBlocked, *Data, Error));
	const FGuLiFlightNavigationQuery Query(Data->CreateRuntimeGraph(&Error));
	const FGuLiFlightNavPathResult Result = Query.FindPath(
		FVector(-3000.0, 0.0, 0.0),
		FVector(3000.0, 0.0, 0.0));
	TestEqual(TEXT("Wall creates separate components"), Result.Status, EGuLiFlightNavPathStatus::Disconnected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationSettingsHashTest,
	"GuLi.FlightNavigation.Editor.SettingsHashCoversBakeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationSettingsHashTest::RunTest(const FString& Parameters)
{
	using namespace GuLiFlightNavigationEditorTests;
	const FGuLiFlightNavBakeSettings Base = MakeCompactBakeSettings();
	const uint64 BaseHash = FGuLiFlightNavigationBaker::ComputeSettingsHash(Base);
	TestNotEqual(TEXT("The settings hash never uses the invalid zero sentinel"), BaseHash, 0ull);
	TestEqual(
		TEXT("Unchanged settings hash deterministically"),
		FGuLiFlightNavigationBaker::ComputeSettingsHash(Base),
		BaseHash);

	const auto VerifyMutation = [this, &Base, BaseHash](
		const TCHAR* FieldName,
		TFunctionRef<void(FGuLiFlightNavBakeSettings&)> Mutate)
	{
		FGuLiFlightNavBakeSettings Changed = Base;
		Mutate(Changed);
		TestNotEqual(
			FString::Printf(TEXT("%s participates in SettingsHash"), FieldName),
			FGuLiFlightNavigationBaker::ComputeSettingsHash(Changed),
			BaseHash);
	};

	VerifyMutation(TEXT("MinimumCellSize"), [](FGuLiFlightNavBakeSettings& Value) { Value.MinimumCellSize += 100.0f; });
	VerifyMutation(TEXT("AgentRadius"), [](FGuLiFlightNavBakeSettings& Value) { Value.AgentRadius += 25.0f; });
	VerifyMutation(TEXT("MaximumDepth"), [](FGuLiFlightNavBakeSettings& Value) { ++Value.MaximumDepth; });
	VerifyMutation(TEXT("MaximumNodes"), [](FGuLiFlightNavBakeSettings& Value) { ++Value.MaximumNodes; });
	VerifyMutation(TEXT("MaximumCells"), [](FGuLiFlightNavBakeSettings& Value) { ++Value.MaximumCells; });
	VerifyMutation(TEXT("FaceCoordinateTolerance"), [](FGuLiFlightNavBakeSettings& Value) { Value.FaceCoordinateTolerance += 0.01f; });
	VerifyMutation(TEXT("MinimumPortalSpan"), [](FGuLiFlightNavBakeSettings& Value) { Value.MinimumPortalSpan += 1.0f; });
	VerifyMutation(TEXT("CollisionChannel"), [](FGuLiFlightNavBakeSettings& Value) { Value.CollisionChannel = ECC_WorldDynamic; });
	VerifyMutation(TEXT("bTraceComplex"), [](FGuLiFlightNavBakeSettings& Value) { Value.bTraceComplex = !Value.bTraceComplex; });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationVolumeStalenessTest,
	"GuLi.FlightNavigation.Editor.VolumeSourceStaleness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationVolumeStalenessTest::RunTest(const FString& Parameters)
{
	using namespace GuLiFlightNavigationEditorTests;
	FTransientEditorWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AGuLiFlightNavigationVolume* Volume = Fixture.SpawnNavigationVolume(*this);
	if (Volume == nullptr)
	{
		return false;
	}
	UBoxComponent* StaticBlocker = Fixture.SpawnCollisionBox(
		*this,
		TEXT("StaticFlightObstacle"),
		FVector(-500.0, 0.0, 0.0),
		FVector(150.0, 150.0, 150.0),
		EComponentMobility::Static);
	UBoxComponent* DynamicBlocker = Fixture.SpawnCollisionBox(
		*this,
		TEXT("DynamicFlightObstacle"),
		FVector(500.0, 0.0, 0.0),
		FVector(150.0, 150.0, 150.0),
		EComponentMobility::Movable);
	if (StaticBlocker == nullptr || DynamicBlocker == nullptr)
	{
		return false;
	}

	const FGuLiFlightNavBakeSettings Settings = MakeCompactBakeSettings();
	FText Error;
	if (!TestTrue(
		FString::Printf(TEXT("Transient volume bake succeeds: %s"), *Error.ToString()),
		UGuLiFlightNavigationEditorLibrary::BakeVolume(Volume, Settings, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(TEXT("Bake records a non-zero source geometry signature"),
		Volume->NavigationData->Metadata.GeometrySignature != 0);
	TestEqual(TEXT("Bake records the exact settings hash"),
		Volume->NavigationData->Metadata.SettingsHash,
		FGuLiFlightNavigationBaker::ComputeSettingsHash(Settings));

	Error = FText::GetEmpty();
	TestTrue(
		FString::Printf(TEXT("Fresh bake validates against its source World: %s"), *Error.ToString()),
		UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Settings, Error));

	const uint64 ChecksumBeforeDynamicMove = Volume->NavigationData->Metadata.ContentChecksum;
	DynamicBlocker->SetWorldLocation(FVector(750.0, 100.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	Error = FText::GetEmpty();
	TestTrue(
		FString::Printf(TEXT("Movable collision does not contaminate source signature: %s"), *Error.ToString()),
		UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Settings, Error));
	Error = FText::GetEmpty();
	if (!TestTrue(
		TEXT("Rebake after moving only movable collision succeeds"),
		UGuLiFlightNavigationEditorLibrary::BakeVolume(Volume, Settings, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(
		TEXT("Movable collision does not alter baked topology/content"),
		Volume->NavigationData->Metadata.ContentChecksum,
		ChecksumBeforeDynamicMove);

	StaticBlocker->SetMobility(EComponentMobility::Movable);
	StaticBlocker->SetWorldLocation(FVector(-750.0, 0.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	StaticBlocker->SetMobility(EComponentMobility::Static);
	Error = FText::GetEmpty();
	TestFalse(
		TEXT("Relocating static blocking geometry invalidates the previous bake"),
		UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Settings, Error));
	TestTrue(
		FString::Printf(TEXT("Static geometry mismatch reports Stale (actual: %s)"), *Error.ToString()),
		Error.ToString().Contains(TEXT("Stale"), ESearchCase::IgnoreCase));

	FGuLiFlightNavBakeSettings ChangedSettings = Settings;
	ChangedSettings.AgentRadius += 25.0f;
	Error = FText::GetEmpty();
	TestFalse(
		TEXT("Changing bake settings invalidates the previous bake"),
		UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, ChangedSettings, Error));
	TestTrue(
		FString::Printf(TEXT("Settings mismatch reports Stale (actual: %s)"), *Error.ToString()),
		Error.ToString().Contains(TEXT("Stale"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationAgentRadiusFringeStalenessTest,
	"GuLi.FlightNavigation.Editor.AgentRadiusFringeSourceStaleness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationAgentRadiusFringeStalenessTest::RunTest(const FString& Parameters)
{
	using namespace GuLiFlightNavigationEditorTests;
	FTransientEditorWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AGuLiFlightNavigationVolume* Volume = Fixture.SpawnNavigationVolume(*this);
	if (Volume == nullptr)
	{
		return false;
	}
	const FBox VolumeBounds = Volume->GetComponentsBoundingBox(true);
	FGuLiFlightNavBakeSettings Settings = MakeCompactBakeSettings();
	Settings.AgentRadius = 100.0f;

	const FVector FringeExtent(10.0, 50.0, 50.0);
	const FVector FringeLocation(VolumeBounds.Max.X + 50.0, 0.0, 0.0);
	UBoxComponent* FringeBlocker = Fixture.SpawnCollisionBox(
		*this,
		TEXT("AgentRadiusFringeObstacle"),
		FringeLocation,
		FringeExtent,
		EComponentMobility::Static);
	if (FringeBlocker == nullptr)
	{
		return false;
	}
	TestFalse(
		TEXT("Fringe blocker bounds do not intersect the navigation volume itself"),
		FringeBlocker->Bounds.GetBox().Intersect(VolumeBounds));
	TestTrue(
		TEXT("Fringe blocker bounds intersect the AgentRadius-expanded influence region"),
		FringeBlocker->Bounds.GetBox().Intersect(VolumeBounds.ExpandBy(Settings.AgentRadius)));

	FText Error;
	if (!TestTrue(
		TEXT("Volume bakes with an external blocker inside the AgentRadius fringe"),
		UGuLiFlightNavigationEditorLibrary::BakeVolume(Volume, Settings, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(
		TEXT("Fresh AgentRadius-fringe source validates"),
		UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Settings, Error));

	FringeBlocker->SetMobility(EComponentMobility::Movable);
	FringeBlocker->SetWorldLocation(
		FVector(VolumeBounds.Max.X + Settings.AgentRadius + FringeExtent.X + 50.0, 0.0, 0.0),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	FringeBlocker->SetMobility(EComponentMobility::Static);
	TestFalse(
		TEXT("Moving a static fringe blocker out of the AgentRadius influence invalidates the bake"),
		UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Settings, Error));
	TestTrue(
		FString::Printf(TEXT("Fringe geometry mismatch reports Stale (actual: %s)"), *Error.ToString()),
		Error.ToString().Contains(TEXT("Stale"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationCookGateFailClosedTest,
	"GuLi.FlightNavigation.Editor.CookGateFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationCookGateFailClosedTest::RunTest(const FString& Parameters)
{
	using namespace GuLiFlightNavigationEditorTests;
	FTransientEditorWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	const TArray<FName> RequiredWorlds { Fixture.World->GetOutermost()->GetFName() };
	TArray<FGuLiFlightNavigationCookIssue> Issues;
	TestFalse(
		TEXT("A required map without an enabled FlightNav volume fails closed"),
		FGuLiFlightNavigationCookGate::ValidateWorld(Fixture.World, RequiredWorlds, Issues));
	TestEqual(TEXT("Missing map volume has one deterministic issue"), Issues.Num(), 1);
	if (!Issues.IsEmpty())
	{
		TestEqual(
			TEXT("Missing map volume uses the stable MissingVolume code"),
			Issues[0].Code,
			EGuLiFlightNavigationCookIssue::MissingVolume);
	}

	AGuLiFlightNavigationVolume* Volume = Fixture.SpawnNavigationVolume(*this);
	if (Volume == nullptr)
	{
		return false;
	}
	Volume->NavigationData = nullptr;
	TestFalse(
		TEXT("An enabled volume without Data fails closed"),
		FGuLiFlightNavigationCookGate::ValidateWorld(Fixture.World, RequiredWorlds, Issues));
	TestTrue(
		TEXT("Missing volume Data is identified explicitly"),
		Issues.ContainsByPredicate([](const FGuLiFlightNavigationCookIssue& Issue)
		{
			return Issue.Code == EGuLiFlightNavigationCookIssue::MissingData;
		}));

	Volume->NavigationData = NewObject<UGuLiFlightNavigationData>(Volume, TEXT("CookGateNavigationData"));
	const FGuLiFlightNavBakeSettings Settings = MakeCompactBakeSettings();
	FText Error;
	if (!TestTrue(
		TEXT("Cook-gate fixture bakes explicitly"),
		UGuLiFlightNavigationEditorLibrary::BakeVolume(Volume, Settings, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const uint32 BakedRevision = Volume->NavigationData->Metadata.DefinitionRevision;
	TestTrue(
		TEXT("A fresh required map passes the read-only cook gate"),
		FGuLiFlightNavigationCookGate::ValidateWorld(Fixture.World, RequiredWorlds, Issues));
	TestTrue(TEXT("Fresh validation returns no issues"), Issues.IsEmpty());
	TestEqual(
		TEXT("Bake persists the authoring settings needed for cook staleness checks"),
		FGuLiFlightNavigationBaker::ComputeSettingsHash(Volume->AuthoringBakeSettings),
		Volume->NavigationData->Metadata.SettingsHash);

	const uint32 OriginalFormat = Volume->NavigationData->Metadata.FormatVersion;
	Volume->NavigationData->Metadata.FormatVersion = OriginalFormat + 1;
	FGuLiFlightNavigationCookIssue Issue;
	TestFalse(TEXT("Schema mismatch is rejected"), FGuLiFlightNavigationCookGate::ValidateVolume(Volume, Issue));
	TestEqual(TEXT("Schema mismatch has a stable code"), Issue.Code, EGuLiFlightNavigationCookIssue::SchemaMismatch);
	Volume->NavigationData->Metadata.FormatVersion = OriginalFormat;

	const float OriginalClearance = Volume->NavigationData->Cells[0].Clearance;
	Volume->NavigationData->Cells[0].Clearance += 1.0f;
	TestFalse(TEXT("Payload checksum mutation is rejected"), FGuLiFlightNavigationCookGate::ValidateVolume(Volume, Issue));
	TestEqual(TEXT("Checksum mismatch has a stable code"), Issue.Code, EGuLiFlightNavigationCookIssue::ChecksumMismatch);
	Volume->NavigationData->Cells[0].Clearance = OriginalClearance;

	const FName OriginalSourceWorld = Volume->NavigationData->Metadata.SourceWorldPackage;
	Volume->NavigationData->Metadata.SourceWorldPackage = TEXT("/Game/Maps/WrongWorld");
	Volume->NavigationData->Metadata.ContentChecksum = Volume->NavigationData->ComputeContentChecksum();
	TestFalse(TEXT("Source-world mismatch is stale"), FGuLiFlightNavigationCookGate::ValidateVolume(Volume, Issue));
	TestEqual(TEXT("Source-world mismatch has a stable code"), Issue.Code, EGuLiFlightNavigationCookIssue::StaleSourceWorld);
	Volume->NavigationData->Metadata.SourceWorldPackage = OriginalSourceWorld;
	Volume->NavigationData->Metadata.ContentChecksum = Volume->NavigationData->ComputeContentChecksum();

	Volume->AuthoringBakeSettings.AgentRadius += 1.0f;
	TestFalse(TEXT("Authoring-setting mutation is stale"), FGuLiFlightNavigationCookGate::ValidateVolume(Volume, Issue));
	TestEqual(TEXT("Setting mismatch has a stable code"), Issue.Code, EGuLiFlightNavigationCookIssue::StaleSettings);
	Volume->AuthoringBakeSettings = Settings;

	UBoxComponent* NewStaticBlocker = Fixture.SpawnCollisionBox(
		*this,
		TEXT("CookGateLateStaticObstacle"),
		FVector(500.0, 0.0, 0.0),
		FVector(100.0, 100.0, 100.0),
		EComponentMobility::Static);
	if (!TestNotNull(TEXT("Late static obstacle exists"), NewStaticBlocker))
	{
		return false;
	}
	TestFalse(TEXT("Static source mutation is stale"), FGuLiFlightNavigationCookGate::ValidateVolume(Volume, Issue));
	TestEqual(TEXT("Geometry mismatch has a stable code"), Issue.Code, EGuLiFlightNavigationCookIssue::StaleGeometry);
	TestEqual(
		TEXT("Every cook-gate validation path remains read-only and never re-bakes"),
		Volume->NavigationData->Metadata.DefinitionRevision,
		BakedRevision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFlightNavigationCookGateConfigurationTest,
	"GuLi.FlightNavigation.Editor.CookGateRequiredMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFlightNavigationCookGateConfigurationTest::RunTest(const FString& Parameters)
{
	const UGuLiFlightNavigationCookSettings* Settings = GetDefault<UGuLiFlightNavigationCookSettings>();
	TestEqual(TEXT("Exactly three release maps are guarded"), Settings->RequiredWorldPackages.Num(), 3);
	TestTrue(TEXT("Commander Mass prototype is guarded"),
		Settings->RequiredWorldPackages.Contains(TEXT("/Game/Maps/LVL_CommanderMassPrototype")));
	TestTrue(TEXT("Main map is guarded"),
		Settings->RequiredWorldPackages.Contains(TEXT("/Game/Maps/LVL_Main")));
	TestTrue(TEXT("Ship test map is guarded"),
		Settings->RequiredWorldPackages.Contains(TEXT("/Game/Maps/LVL_ShipTest")));
	return true;
}

#endif

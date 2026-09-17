#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AI/NavigationSystemBase.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Gameplay/Building/GuLiGroundAccessRampComponent.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationVolume.h"
#include "GuLiNavigationBakeLibrary.h"
#include "GuLiNavigationSourceHash.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Landscape.h"
#include "LandscapeLayerInfoObject.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavModifierComponent.h"
#include "NavigationSystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace GuLiNavigationBakeTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		UNavigationSystemV1* Navigation = nullptr;
		~FWorldFixture()
		{
			if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		}
		void Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("GuLiNavigationBakeTest"));
			GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
		}
		UStaticMeshComponent* Floor(const TCHAR* Name, FVector Location, FVector Extent)
		{
			FActorSpawnParameters Params; Params.Name = Name;
			AActor* Actor = World->SpawnActor<AActor>(Params);
			auto* Mesh = NewObject<UStaticMeshComponent>(Actor, TEXT("Collision"));
			Actor->SetRootComponent(Mesh); Actor->AddInstanceComponent(Mesh);
			Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Mesh->SetCanEverAffectNavigation(true);
			Mesh->SetWorldLocation(Location); Mesh->SetWorldScale3D(Extent / 50.0);
			Mesh->RegisterComponent();
			return Mesh;
		}
		void InitializeNavigation()
		{
			ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
			UCubeBuilder* Builder = NewObject<UCubeBuilder>();
			Builder->X = 16000; Builder->Y = 8000; Builder->Z = 4000;
			UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
			FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode, nullptr, false);
			Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavDataConfig Agent;
			Agent.Name = TEXT("NavBakeTest"); Agent.AgentRadius = 34; Agent.AgentHeight = 144;
			Agent.SetNavDataClass(ARecastNavMesh::StaticClass());
			Navigation->OverrideSupportedAgents({ Agent });
			Navigation->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
			Navigation->OnNavigationBoundsUpdated(Bounds);
			Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInEditor,
				UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
			World->UpdateWorldComponents(true, false);
		}
		void FlushDirty()
		{
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				Navigation->Tick(0.1f);
				for (TActorIterator<ARecastNavMesh> It(World); It; ++It) It->EnsureBuildCompletion();
			}
		}
		bool HasPath() const
		{
			ANavigationData* Data = Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
			if (!Data) return false;
			FPathFindingQuery Query(nullptr, *Data, FVector(-4500, 0, 75), FVector(4500, 0, 75));
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = Navigation->FindPathSync(Query);
			return Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundNavigationBakeCacheTest,
	"GuLi.NavigationBake.GroundCacheAndDynamicReachability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGroundNavigationBakeCacheTest::RunTest(const FString& Parameters)
{
	using namespace GuLiNavigationBakeTests;
	FWorldFixture Fixture; Fixture.Initialize();
	Fixture.Floor(TEXT("LeftGround"), FVector(-4000, 0, 0), FVector(2000, 2500, 50));
	Fixture.Floor(TEXT("RightGround"), FVector(4000, 0, 0), FVector(2000, 2500, 50));
	Fixture.InitializeNavigation();
	auto Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	if (!TestTrue(*Result.Message, Result.bSuccess)) return false;
	TestTrue(TEXT("Missing ground bake is rebuilt"), Result.GroundRebuilds > 0);
	Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	TestTrue(TEXT("Unchanged source is a cache hit"), Result.bSuccess && Result.GroundRebuilds == 0);
	for (TActorIterator<ARecastNavMesh> It(Fixture.World); It; ++It)
		TestTrue(TEXT("Editor parallel bake restores configured runtime gathering"),
			It->bDoFullyAsyncNavDataGathering == GetDefault<ARecastNavMesh>()->bDoFullyAsyncNavDataGathering);
	TestFalse(TEXT("Separate ground islands are unreachable"), Fixture.HasPath());

	UStaticMeshComponent* Bridge = Fixture.Floor(TEXT("RuntimeBridge"), FVector::ZeroVector, FVector(2000, 2500, 50));
	TestFalse(TEXT("New walkable geometry invalidates baked source"),
		UGuLiNavigationBakeLibrary::ValidateWorldNavigation(Fixture.World).bSuccess);
	Fixture.FlushDirty();
	TestTrue(TEXT("Dynamic tiles create a new bridge connection"), Fixture.HasPath());
	// Keep the end steps below the existing agent's 35 cm climb limit.
	Bridge->SetWorldRotation(FRotator(0.5, 0, 0));
	Fixture.FlushDirty();
	TestTrue(TEXT("An inclined runtime ramp remains reachable after local geometry updates"), Fixture.HasPath());

	AGuLiOreClusterObstacleActor* Ore = Fixture.World->SpawnActor<AGuLiOreClusterObstacleActor>();
	Ore->InitializeObstacle(0, 3000);
	Fixture.FlushDirty();
	TestFalse(TEXT("Ore footprint blocks the bridge corridor"), Fixture.HasPath());
	Ore->SetObstacleEnabled(false);
	Fixture.FlushDirty();
	TestTrue(TEXT("Depleted ore restores the baked ground connection"), Fixture.HasPath());
	Ore->SetObstacleEnabled(true);
	Fixture.FlushDirty();
	TestFalse(TEXT("Re-enabled ore blocks again"), Fixture.HasPath());
	Fixture.World->DestroyActor(Ore);
	Fixture.FlushDirty();
	TestTrue(TEXT("Removing a dynamic obstacle restores access"), Fixture.HasPath());
	const FTransform BuildingTransform(FVector(0, 0, 500));
	AGuLiPlacedBuilding* Building = Fixture.World->SpawnActorDeferred<AGuLiPlacedBuilding>(
		AGuLiPlacedBuilding::StaticClass(), BuildingTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("The production building actor can be created"), Building)) return false;
	Building->GetBuildingCollision()->SetBoxExtent(FVector(400, 3000, 500));
	Building->FinishSpawning(BuildingTransform);
	// ApplyDefinition refreshes these bounds after assigning the catalog footprint.
	Building->GetNavigationModifier()->UpdateNavigationBounds();
	Building->GetNavigationModifier()->RefreshNavigationModifiers();
	Fixture.FlushDirty();
	TestFalse(TEXT("Production building footprint blocks the runtime ramp"), Fixture.HasPath());
	Fixture.World->DestroyActor(Building);
	Fixture.FlushDirty();
	TestTrue(TEXT("Demolishing the building restores the same ramp connection"), Fixture.HasPath());
	Fixture.World->DestroyActor(Bridge->GetOwner());
	Fixture.FlushDirty();
	TestFalse(TEXT("Removing the runtime ramp disconnects the islands"), Fixture.HasPath());

	Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	if (!TestTrue(TEXT("Current source can be prepared after dynamic changes"), Result.bSuccess)) return false;
	for (TActorIterator<ARecastNavMesh> It(Fixture.World); It; ++It)
	{
		It->GetPackage()->GetMetaData().SetValue(*It, TEXT("GuLi.GroundNavigation.Payload.v1"), TEXT("corrupt"));
		break;
	}
	TestFalse(TEXT("Matching source cannot conceal corrupt payload metadata"),
		UGuLiNavigationBakeLibrary::ValidateWorldNavigation(Fixture.World).bSuccess);
	Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	TestTrue(TEXT("Corrupt cache is regenerated"), Result.bSuccess && Result.GroundRebuilds > 0);
	// Removing a world navigation system cleans its generators. Test the cook-only
	// path last: a cleaned system cannot be reattached for subsequent dynamic builds.
	Fixture.World->SetNavigationSystem(nullptr);
	Result = UGuLiNavigationBakeLibrary::ValidateWorldNavigation(Fixture.World);
	TestFalse(TEXT("Cook without a live nav system rejects a missing configured second agent"), Result.bSuccess);
	TestTrue(TEXT("Cook failure specifically identifies the missing agent"), Result.Message.Contains(TEXT("required ground agent NavData is missing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiNavigationSourceHashTest,
	"GuLi.NavigationBake.CollisionTerrainAndSettingsSignature", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiNavigationSourceHashTest::RunTest(const FString& Parameters)
{
	using namespace GuLiNavigationBakeTests;
	FWorldFixture Fixture; Fixture.Initialize();
	UStaticMeshComponent* Floor = Fixture.Floor(TEXT("SourceFloor"), FVector::ZeroVector, FVector(2000, 2000, 50));
	ARecastNavMesh* Nav = Fixture.World->SpawnActor<ARecastNavMesh>();
	const auto Hash = [&]() { return UGuLiNavigationBakeLibrary::ComputeGroundSourceHash(Fixture.World, Nav); };
	const uint64 Original = Hash();
	TestEqual(TEXT("Identical source has deterministic signature"), Hash(), Original);
	Floor->SetVisibility(false);
	TestEqual(TEXT("Visual visibility does not invalidate navigation"), Hash(), Original);
	Floor->SetWorldLocation(FVector(100, 0, 0));
	TestNotEqual(TEXT("Moving collision invalidates navigation"), Hash(), Original);
	Floor->SetWorldLocation(FVector::ZeroVector);
	TestEqual(TEXT("Restoring source restores its signature"), Hash(), Original);
	Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestNotEqual(TEXT("Disabling collision invalidates navigation"), Hash(), Original);
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Nav->SetCellSize(ENavigationDataResolution::Default, Nav->GetCellSize(ENavigationDataResolution::Default) + 1.0f);
	TestNotEqual(TEXT("Changing actual Recast resolution invalidates navigation"), Hash(), Original);
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(Floor->GetOwner(), TEXT("InstanceFingerprint"));
	Instances->SetStaticMesh(Floor->GetStaticMesh());
	Instances->AddInstance(FTransform(FVector(-500, 0, 0)));
	Instances->AddInstance(FTransform(FVector(500, 0, 0)));
	Instances->AddInstance(FTransform(FVector::ZeroVector));
	const auto InstanceHash = [&]() { FGuLiNavigationSourceHash H; H.AddCollisionSource(Instances); return H.Get(); };
	const uint64 OriginalInstances = InstanceHash();
	Instances->UpdateInstanceTransform(2, FTransform(FVector(100, 0, 0)), false, false, false);
	TestNotEqual(TEXT("Moving an interior instance invalidates with unchanged outer bounds"), InstanceHash(), OriginalInstances);

	ULandscapeHeightfieldCollisionComponent* Terrain = NewObject<ULandscapeHeightfieldCollisionComponent>(
		Fixture.World->SpawnActor<ALandscape>(), TEXT("CollisionFingerprint"));
	Terrain->CollisionSizeQuads = 1;
	Terrain->CollisionHeightData.Lock(LOCK_READ_WRITE);
	uint16* Heights = static_cast<uint16*>(Terrain->CollisionHeightData.Realloc(4));
	for (int32 Index = 0; Index < 4; ++Index) Heights[Index] = 32768;
	Terrain->CollisionHeightData.Unlock();
	const auto TerrainHash = [&]() { FGuLiNavigationSourceHash H; H.AddCollisionSource(Terrain); return H.Get(); };
	const uint64 OriginalTerrain = TerrainHash();
	Heights = static_cast<uint16*>(Terrain->CollisionHeightData.Lock(LOCK_READ_WRITE));
	Heights[1] += 256;
	Terrain->CollisionHeightData.Unlock();
	TestNotEqual(TEXT("Sculpted terrain invalidates even if cached bounds are unchanged"), TerrainHash(), OriginalTerrain);
	const uint64 Sculpted = TerrainHash();
	Terrain->CollisionQuadFlags.Add(1);
	TestNotEqual(TEXT("Collision holes/quad topology participate in the signature"), TerrainHash(), Sculpted);
	Terrain->ComponentLayerInfos.Add(NewObject<ULandscapeLayerInfoObject>(Terrain->GetOwner()));
	Terrain->ComponentLayerInfos.Add(NewObject<ULandscapeLayerInfoObject>(Terrain->GetOwner()));
	if (!TestNotNull(TEXT("Engine landscape visibility layer is available"), ALandscapeProxy::VisibilityLayer)) return false;
	Terrain->ComponentLayerInfos.Add(ALandscapeProxy::VisibilityLayer);
	Terrain->DominantLayerData.Lock(LOCK_READ_WRITE);
	uint8* Layers = static_cast<uint8*>(Terrain->DominantLayerData.Realloc(4));
	for (int32 Index = 0; Index < 4; ++Index) Layers[Index] = 0;
	Terrain->DominantLayerData.Unlock();
	const uint64 PaintedTerrain = TerrainHash();
	Layers = static_cast<uint8*>(Terrain->DominantLayerData.Lock(LOCK_READ_WRITE));
	Layers[1] = 1;
	Terrain->DominantLayerData.Unlock();
	TestEqual(TEXT("Changing a visual terrain layer preserves navigation signature"), TerrainHash(), PaintedTerrain);
	Layers = static_cast<uint8*>(Terrain->DominantLayerData.Lock(LOCK_READ_WRITE));
	Layers[1] = 2;
	Terrain->DominantLayerData.Unlock();
	TestNotEqual(TEXT("Changing a sample to a visibility hole invalidates navigation"), TerrainHash(), PaintedTerrain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiAutomaticFlightBakeTest,
	"GuLi.NavigationBake.AutomaticFlightRebakeAndReachability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiAutomaticFlightBakeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiNavigationBakeTests;
	FWorldFixture Fixture; Fixture.Initialize();
	AGuLiFlightNavigationVolume* Volume = Fixture.World->SpawnActor<AGuLiFlightNavigationVolume>();
	UCubeBuilder* Builder = NewObject<UCubeBuilder>(); Builder->X = Builder->Y = Builder->Z = 4000;
	UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	Volume->NavigationData = NewObject<UGuLiFlightNavigationData>(Volume);
	Volume->AuthoringBakeSettings.MinimumCellSize = 250;
	Volume->AuthoringBakeSettings.AgentRadius = 25;
	Volume->AuthoringBakeSettings.MaximumDepth = 5;
	Fixture.World->UpdateWorldComponents(true, false);
	const auto Path = [&]()
	{
		FGuLiFlightNavigationQuery Query(Volume->NavigationData->CreateRuntimeGraph());
		return Query.ValidateEndpointsInSameComponent(FVector(-1000, 0, 0), FVector(1000, 0, 0), 25)
			== EGuLiFlightNavSegmentStatus::Valid;
	};
	auto Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	if (!TestTrue(*Result.Message, Result.bSuccess)) return false;
	TestEqual(TEXT("Empty flight payload automatically bakes once"), Result.FlightRebuilds, 1);
	TestTrue(TEXT("Initial open flight volume connects endpoints"), Path());
	Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	TestTrue(TEXT("Unchanged flight graph is reused"), Result.bSuccess && Result.FlightRebuilds == 0);
	UStaticMeshComponent* Wall = Fixture.Floor(TEXT("FlightWall"), FVector::ZeroVector, FVector(200, 2500, 2500));
	Wall->SetMobility(EComponentMobility::Static);
	TestFalse(TEXT("Map obstacle makes previous flight graph stale"),
		UGuLiNavigationBakeLibrary::ValidateWorldNavigation(Fixture.World).bSuccess);
	Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	TestTrue(TEXT("Changed map automatically rebakes flight navigation"), Result.bSuccess && Result.FlightRebuilds == 1);
	TestFalse(TEXT("Rebaked solid wall disconnects flight endpoints"), Path());
	Fixture.World->DestroyActor(Wall->GetOwner());
	Result = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(Fixture.World, false);
	TestTrue(TEXT("Removing static obstacle automatically restores flight reachability"), Result.bSuccess && Path());
	Volume->NavigationData->Metadata.ContentChecksum ^= 1;
	TestFalse(TEXT("Corrupt flight graph fails even with an unchanged source"),
		UGuLiNavigationBakeLibrary::ValidateWorldNavigation(Fixture.World).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiBuildingRampGroundContractTest,
	"GuLi.NavigationBake.FactoryRampGroundContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiBuildingRampGroundContractTest::RunTest(const FString& Parameters)
{
	using namespace GuLiNavigationBakeTests;
	FWorldFixture Fixture; Fixture.Initialize();
	UStaticMeshComponent* Floor = Fixture.Floor(TEXT("FactoryGround"), FVector(0, 0, -50), FVector(6000, 5000, 50));
	UClass* Presentation = LoadClass<AActor>(nullptr,
		TEXT("/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory.BP_ResourceProcessingFactory_C"));
	if (!TestNotNull(TEXT("The production factory presentation loads"), Presentation)) return false;
	FGuLiBuildingDefinition Definition;
	Definition.CollisionExtent = FVector(3000, 3250, 2000);
	FString Reason;
	TestTrue(TEXT("Full footprint and production SCS ramp accept flat ground"),
		GuLiBuildings::ValidateGroundPlacement(*Fixture.World, Definition, FTransform::Identity, Presentation, nullptr, Reason));
	Floor->SetWorldScale3D(FVector(3300, 5000, 50) / 50.0);
	TestTrue(TEXT("Building footprint alone remains supported"),
		GuLiBuildings::ValidateGroundPlacement(*Fixture.World, Definition, FTransform::Identity, nullptr, nullptr, Reason));
	TestFalse(TEXT("A supported center/footprint cannot hide a missing Blueprint ramp entrance"),
		GuLiBuildings::ValidateGroundPlacement(*Fixture.World, Definition, FTransform::Identity, Presentation, nullptr, Reason));

	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
	Owner->SetRootComponent(Root); Owner->AddInstanceComponent(Root); Root->RegisterComponent();
	auto* Ramp = NewObject<UGuLiGroundAccessRampComponent>(Owner, TEXT("AccessRamp"));
	Owner->AddInstanceComponent(Ramp); Ramp->SetupAttachment(Root); Ramp->RegisterComponent();
	AddExpectedMessagePlain(TEXT("[GULI_ACCESS_RAMP] Disabled"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("Missing entrance disables the runtime ramp without a fatal assertion"), Ramp->RefreshGroundFit());
	TestFalse(TEXT("An invalid ramp cannot export phantom navigation geometry"), Ramp->CanEverAffectNavigation());
	TestEqual(TEXT("An invalid ramp has no collision"), Ramp->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	Floor->SetWorldScale3D(FVector(6000, 5000, 50) / 50.0);
	TestTrue(TEXT("Restoring terrain allows the same component to recover"), Ramp->RefreshGroundFit());
	TestTrue(TEXT("Recovered ramp participates in dynamic navigation"), Ramp->CanEverAffectNavigation());
	UStaticMeshComponent* Uphill = Fixture.Floor(TEXT("UphillEntrance"), FVector(4000, 0, 1140.606), FVector(500, 2500, 50));
	TestFalse(TEXT("The reported +1190.606 cm entrance is rejected before factory spawn"),
		GuLiBuildings::ValidateGroundPlacement(*Fixture.World, Definition, FTransform::Identity, Presentation, Owner, Reason));
	TestTrue(TEXT("Failure identifies the entrance height contract"), Reason.Contains(TEXT("Ramp entrance height")));
	TestFalse(TEXT("The same uphill condition is nonfatal at runtime"), Ramp->RefreshGroundFit());
	Fixture.World->DestroyActor(Uphill->GetOwner());
	Floor->SetWorldScale3D(FVector(1000, 5000, 50) / 50.0);
	TestFalse(TEXT("A valid center cannot hide unsupported footprint corners"),
		GuLiBuildings::ValidateGroundPlacement(*Fixture.World, Definition, FTransform::Identity, nullptr, Owner, Reason));
	const auto Candidates = GuLiBuildings::GetGiftPlacementCandidates(FVector(112000, 0, 0), 2);
	TestEqual(TEXT("Original western slot is retained as first candidate"), Candidates[0].GetLocation().X, 105000.0);
	for (int32 Index = 1; Index < Candidates.Num(); ++Index)
		TestTrue(TEXT("Fallback order is nearest-first"),
			FVector::DistSquared2D(Candidates[Index].GetLocation(), Candidates[0].GetLocation()) + 0.01 >=
			FVector::DistSquared2D(Candidates[Index - 1].GetLocation(), Candidates[0].GetLocation()));
	return true;
}

#endif

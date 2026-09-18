// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace GuLiBuildingWorldTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		AGuLiBattleGameState* GameState = nullptr;
		AGuLiCommanderPlayerController* Controller = nullptr;
		AGuLiBattlePlayerState* PlayerState = nullptr;
		UGuLiBuildingPlacementComponent* Placement = nullptr;
		UGuLiBuildingCatalog* Catalog = nullptr;
		AActor* FloorActor = nullptr;
		bool bWorldContextRegistered = false;

		~FWorldFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (bWorldContextRegistered && GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
			if (Catalog && Catalog->IsRooted())
			{
				Catalog->RemoveFromRoot();
			}
		}

		bool Initialize(FAutomationTestBase& Test, const bool bCreateLevelFloor = true)
		{
			if (!Test.TestNotNull(TEXT("An engine exists for the building World fixture"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("An isolated authority building World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			World->InitializeActorsForPlay(FURL());

			GameState = World->SpawnActor<AGuLiBattleGameState>();
			if (!Test.TestNotNull(TEXT("The fixture has a BattleGameState"), GameState))
			{
				return false;
			}
			World->SetGameState(GameState);
			GameState->InitializeServerMatchState();

			Controller = World->SpawnActor<AGuLiCommanderPlayerController>();
			PlayerState = World->SpawnActor<AGuLiBattlePlayerState>();
			if (!Test.TestNotNull(TEXT("The fixture has the shared battle PlayerController"), Controller)
				|| !Test.TestNotNull(TEXT("The fixture has a BattlePlayerState"), PlayerState))
			{
				return false;
			}
			Controller->SetPlayerState(PlayerState);
			PlayerState->EnsureServerPlayerGuid();
			Placement = Controller->GetBuildingPlacementComponent();
			if (!Test.TestNotNull(TEXT("The shared controller owns its placement component"), Placement))
			{
				return false;
			}

			Catalog = NewObject<UGuLiBuildingCatalog>(GetTransientPackage());
			Catalog->AddToRoot();
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			UMaterialInterface* Preview = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
			if (!Test.TestNotNull(TEXT("The test catalog can use Engine Cube"), Cube)
				|| !Test.TestNotNull(TEXT("The test catalog can use the default material"), Preview))
			{
				return false;
			}
			Catalog->PreviewMaterial = Preview;
			for (const EGuLiBuildingType Type : {
				EGuLiBuildingType::MissileTurret,
				EGuLiBuildingType::SentryTurret,
				EGuLiBuildingType::Outpost})
			{
				FGuLiBuildingDefinition& Definition = Catalog->Definitions.AddDefaulted_GetRef();
				Definition.Type = Type;
				Definition.DisplayName = GetGuLiBuildingFallbackDisplayName(Type);
				Definition.Mesh = Cube;
				Definition.CollisionExtent = FVector(50.0f);
				Definition.VisualOffset = FVector(0.0f, 0.0f, 50.0f);
			}
			if (!Test.TestTrue(TEXT("The transient three-entry test catalog is usable"), Catalog->IsUsable()))
			{
				return false;
			}
			Placement->TestOnly_SetCatalog(Catalog);
			Placement->TestOnly_BypassConnectionGate(true);

			if (bCreateLevelFloor)
			{
				FloorActor = SpawnBox(
					Test,
					FVector(100000.0f, 100000.0f, 50.0f),
					FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -50.0f)),
					ECC_WorldStatic);
				if (!FloorActor)
				{
					return false;
				}
			}
			return true;
		}

		void SetRole(const EGuLiCommanderRole Role, const bool bReady = true)
		{
			const uint8 Slot = Role == EGuLiCommanderRole::Commander ? 0u
				: Role == EGuLiCommanderRole::Ground ? 1u : 3u;
			PlayerState->SetServerRoleAssignment(EGuLiTeam::Red, Role, Slot);
			PlayerState->SetServerBattleReady(bReady);
		}

		AActor* SpawnBox(
			FAutomationTestBase& Test,
			const FVector& Extent,
			const FTransform& Transform,
			const ECollisionChannel ObjectType)
		{
			AActor* Actor = World ? World->SpawnActor<AActor>() : nullptr;
			if (!Test.TestNotNull(TEXT("A collision fixture Actor exists"), Actor))
			{
				return nullptr;
			}
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionObjectType(ObjectType);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->SetGenerateOverlapEvents(false);
			Box->RegisterComponent();
			Actor->SetActorTransform(Transform);
			World->UpdateWorldComponents(true, false);
			return Actor;
		}

		APawn* SpawnAndPossessPawn(FAutomationTestBase& Test, const FVector& Location)
		{
			APawn* Pawn = World ? World->SpawnActor<APawn>(Location, FRotator::ZeroRotator) : nullptr;
			if (!Test.TestNotNull(TEXT("The Ground role has a Pawn"), Pawn))
			{
				return nullptr;
			}
			Controller->Possess(Pawn);
			return Test.TestTrue(TEXT("The Ground Pawn is possessed"), Controller->GetPawn() == Pawn)
				? Pawn : nullptr;
		}

		AGuLiPlacedBuilding* SpawnDirectBuilding(
			FAutomationTestBase& Test,
			const FVector& GroundLocation,
			const FGuid& BuilderGuid)
		{
			const FGuLiBuildingDefinition* Definition = Catalog->FindDefinition(EGuLiBuildingType::Outpost);
			if (!Test.TestNotNull(TEXT("Direct-spawn definition exists"), Definition))
			{
				return nullptr;
			}
			const FTransform SpawnTransform(
				FRotator::ZeroRotator,
				GroundLocation + FVector(0.0f, 0.0f, Definition->CollisionExtent.Z));
			AGuLiPlacedBuilding* Building = World->SpawnActorDeferred<AGuLiPlacedBuilding>(
				AGuLiPlacedBuilding::StaticClass(),
				SpawnTransform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Test.TestNotNull(TEXT("A direct capacity-fixture building begins spawning"), Building)
				|| !Test.TestTrue(TEXT("A direct capacity-fixture building initializes"),
					Building->InitializeBuilding(
						EGuLiBuildingType::Outpost,
						EGuLiTeam::Red,
						BuilderGuid,
						*Definition)))
			{
				if (Building)
				{
					Building->Destroy();
				}
				return nullptr;
			}
			Building->FinishSpawning(SpawnTransform);
			return Building;
		}
	};

	FGuLiBuildingPlacementRequest MakeRequest(
		const uint32 RequestId,
		const FVector& GroundLocation,
		const EGuLiBuildingType Type = EGuLiBuildingType::MissileTurret,
		const float YawDegrees = 0.0f)
	{
		FGuLiBuildingPlacementRequest Request;
		Request.ClientRequestId = RequestId;
		Request.Type = Type;
		Request.DesiredGroundLocation = GroundLocation;
		Request.CompressedYaw = FRotator::CompressAxisToShort(YawDegrees);
		return Request;
	}

	int32 CountBuildings(const UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<AGuLiPlacedBuilding> It(&World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				++Count;
			}
		}
		return Count;
	}

	AGuLiPlacedBuilding* FindFirstBuilding(UWorld& World)
	{
		for (TActorIterator<AGuLiPlacedBuilding> It(&World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	bool TestRejectReason(
		FAutomationTestBase& Test,
		const TCHAR* Description,
		const FGuLiBuildingPlacementResult& Result,
		const EGuLiBuildingPlacementRejectReason Expected)
	{
		return Test.TestEqual(Description, Result.RejectReason, Expected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBuildingWorldTests,
	"GuLiStrike.Building.BuildingWorldTests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingWorldTests::RunTest(const FString& Parameters)
{
	using namespace GuLiBuildingWorldTests;

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Commander, false);
		TestRejectReason(*this, TEXT("A battle-unready connection is rejected"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector::ZeroVector)),
			EGuLiBuildingPlacementRejectReason::NotReady);

		Fixture.Placement->TestOnly_ResetServerRequestState();
		Fixture.SetRole(EGuLiCommanderRole::Commander, true);
		UGuLiBuildingCatalog* ValidCatalog = Fixture.Catalog;
		UGuLiBuildingCatalog* EmptyCatalog = NewObject<UGuLiBuildingCatalog>(GetTransientPackage());
		Fixture.Placement->TestOnly_SetCatalog(EmptyCatalog);
		TestRejectReason(*this, TEXT("An unusable catalog is rejected"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector::ZeroVector)),
			EGuLiBuildingPlacementRejectReason::AssetUnavailable);

		Fixture.Placement->TestOnly_ResetServerRequestState();
		Fixture.Placement->TestOnly_SetCatalog(ValidCatalog);
		const FGuLiBuildingPlacementRequest AcceptedRequest = MakeRequest(
			2u, FVector::ZeroVector, EGuLiBuildingType::MissileTurret, 37.0f);
		const FGuLiBuildingPlacementResult Accepted =
			Fixture.Placement->TestOnly_ProcessServerRequest(AcceptedRequest);
		TestTrue(TEXT("A legal Commander request is accepted"), Accepted.WasAccepted());
		TestEqual(TEXT("The first accepted request spawns exactly one building"),
			CountBuildings(*Fixture.World), 1);

		const FGuLiBuildingPlacementResult Replay =
			Fixture.Placement->TestOnly_ProcessServerRequest(AcceptedRequest);
		TestTrue(TEXT("An identical reliable retry replays the accepted result"), Replay.WasAccepted());
		TestEqual(TEXT("An identical reliable retry does not spawn a duplicate"),
			CountBuildings(*Fixture.World), 1);

		FGuLiBuildingPlacementRequest MutatedRetry = AcceptedRequest;
		MutatedRetry.Type = EGuLiBuildingType::SentryTurret;
		TestRejectReason(*this, TEXT("Reusing an ID for changed data is invalid"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MutatedRetry),
			EGuLiBuildingPlacementRejectReason::InvalidRequest);
		TestRejectReason(*this, TEXT("An older request ID is rejected as duplicate"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector(1000.0f, 0.0f, 0.0f))),
			EGuLiBuildingPlacementRejectReason::Duplicate);
		TestRejectReason(*this, TEXT("A newer request cannot overlap the placed building"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(3u, FVector::ZeroVector)),
			EGuLiBuildingPlacementRejectReason::Blocked);
		TestEqual(TEXT("Every rejection leaves the accepted building count unchanged"),
			CountBuildings(*Fixture.World), 1);

		AGuLiPlacedBuilding* Building = FindFirstBuilding(*Fixture.World);
		if (!TestNotNull(TEXT("The accepted replicated building exists"), Building)) return false;
		UBoxComponent* Collision = Building->GetBuildingCollision();
		UStaticMeshComponent* Visual = Building->GetBuildingVisual();
		UNavModifierComponent* Navigation = Building->GetNavigationModifier();
		if (!TestNotNull(TEXT("The building owns deterministic Box collision"), Collision)
			|| !TestNotNull(TEXT("The building owns a visual mesh"), Visual)
			|| !TestNotNull(TEXT("The building owns a navigation modifier"), Navigation)) return false;

		TestTrue(TEXT("The placed Actor replicates"), Building->GetIsReplicated());
		TestTrue(TEXT("The placed Actor is globally relevant"), Building->bAlwaysRelevant);
		TestFalse(TEXT("The immutable placed Actor does not replicate movement"), Building->IsReplicatingMovement());
		TestEqual(TEXT("The placed Actor starts in initial dormancy"), Building->NetDormancy.GetValue(), DORM_Initial);
		TestTrue(TEXT("The placed Actor remains at Scale 1"), Building->GetActorScale3D().Equals(FVector::OneVector));
		TestEqual(TEXT("The collision root is static"), Collision->Mobility.GetValue(), EComponentMobility::Static);
		TestEqual(TEXT("The root uses query and physics collision"),
			Collision->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		TestEqual(TEXT("The root is WorldStatic"), Collision->GetCollisionObjectType(), ECC_WorldStatic);
		TestEqual(TEXT("The root uses the named BlockAll profile"),
			Collision->GetCollisionProfileName(), UCollisionProfile::BlockAll_ProfileName);
		TestEqual(TEXT("The root blocks Pawns"), Collision->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
		TestEqual(TEXT("The root blocks WorldDynamic Actors"),
			Collision->GetCollisionResponseToChannel(ECC_WorldDynamic), ECR_Block);
		TestEqual(TEXT("The root blocks visibility traces"),
			Collision->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
		TestFalse(TEXT("The collision root does not trigger a global runtime navigation rebuild"),
			Collision->CanEverAffectNavigation());
		TestEqual(TEXT("The visual mesh has no collision"), Visual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("The visual mesh does not contribute duplicate navigation geometry"),
			Visual->CanEverAffectNavigation());
		TestTrue(TEXT("The visual component remains at Scale 1"),
			Visual->GetRelativeScale3D().Equals(FVector::OneVector));
		TestTrue(TEXT("The accepted building applies its catalog mesh"),
			Visual->GetStaticMesh()
			== Fixture.Catalog->FindDefinition(EGuLiBuildingType::MissileTurret)->Mesh);
		TestEqual(TEXT("The initialized visual is locked to static mobility"),
			Visual->Mobility.GetValue(), EComponentMobility::Static);
		TestTrue(TEXT("The navigation modifier is registered"), Navigation->IsRegistered());
		TestTrue(TEXT("The navigation modifier applies NavArea_Null"),
			Navigation->AreaClass == UNavArea_Null::StaticClass());
		TestFalse(TEXT("Runtime Soldier building avoidance is intentionally inactive in this phase"),
			Navigation->IsNavigationRelevant());
		Navigation->CalcAndCacheBounds();
		TestTrue(TEXT("The navigation modifier derives valid bounds from the Box root"),
			Navigation->GetNavigationBounds().IsValid != 0);
		TestEqual(TEXT("The building retains its replicated type"),
			Building->GetBuildingType(), EGuLiBuildingType::MissileTurret);
		TestEqual(TEXT("The building retains its replicated team"), Building->GetBuildingTeam(), EGuLiTeam::Red);
		TestTrue(TEXT("The building retains the stable builder GUID"),
			Building->GetBuilderPlayerGuid() == Fixture.PlayerState->GetPlayerGuid());

		for (const FName PropertyName : {
			FName(TEXT("BuildingType")), FName(TEXT("Team")), FName(TEXT("BuilderPlayerGuid"))})
		{
			const FProperty* Property = FindFProperty<FProperty>(AGuLiPlacedBuilding::StaticClass(), PropertyName);
			TestTrue(
				FString::Printf(TEXT("%s is a replicated property"), *PropertyName.ToString()),
				Property && Property->HasAnyPropertyFlags(CPF_Net));
		}
	}

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Air, true);
		TestRejectReason(*this, TEXT("Air cannot place a building on the server"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector::ZeroVector)),
			EGuLiBuildingPlacementRejectReason::UnauthorizedRole);
		Fixture.Placement->TestOnly_ResetServerRequestState();
		Fixture.SetRole(EGuLiCommanderRole::Observer, true);
		TestRejectReason(*this, TEXT("Observer cannot place a building on the server"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector::ZeroVector)),
			EGuLiBuildingPlacementRejectReason::UnauthorizedRole);
	}

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Ground, true);
		if (!Fixture.SpawnAndPossessPawn(*this, FVector(0.0f, 0.0f, 100.0f))) return false;
		TestRejectReason(*this, TEXT("Ground cannot place beyond 100 meters from its Pawn"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector(10001.0f, 0.0f, 0.0f))),
			EGuLiBuildingPlacementRejectReason::OutOfRange);
		AActor* SightBlocker = Fixture.SpawnBox(
			*this,
			FVector(60.0f, 180.0f, 120.0f),
			FTransform(FRotator::ZeroRotator, FVector(500.0f, 0.0f, 75.0f)),
			ECC_WorldStatic);
		if (!SightBlocker) return false;
		TestRejectReason(*this, TEXT("Ground cannot place through a line-of-sight blocker"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(2u, FVector(1000.0f, 0.0f, 0.0f))),
			EGuLiBuildingPlacementRejectReason::NoLineOfSight);
		TestTrue(TEXT("Ground can place at an unobstructed in-range point"),
			Fixture.Placement->TestOnly_ProcessServerRequest(
				MakeRequest(3u, FVector(1000.0f, 1000.0f, 0.0f))).WasAccepted());
	}

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this, false)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Commander, true);
		if (!Fixture.SpawnBox(
			*this,
			FVector(5000.0f, 5000.0f, 50.0f),
			FTransform(FRotator(20.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, -50.0f)),
			ECC_WorldStatic)) return false;
		TestRejectReason(*this, TEXT("The server rejects a surface steeper than 15 degrees"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(1u, FVector::ZeroVector)),
			EGuLiBuildingPlacementRejectReason::SlopeTooSteep);
	}

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this, false)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Commander, true);
		for (uint32 RequestId = 1u; RequestId <= 10u; ++RequestId)
		{
			TestRejectReason(*this, TEXT("Each in-budget request reaches ground validation"),
				Fixture.Placement->TestOnly_ProcessServerRequest(
					MakeRequest(RequestId, FVector(RequestId * 100.0f, 0.0f, 0.0f))),
				EGuLiBuildingPlacementRejectReason::NoGround);
		}
		TestRejectReason(*this, TEXT("The eleventh request inside one second is rate limited"),
			Fixture.Placement->TestOnly_ProcessServerRequest(MakeRequest(11u, FVector(1100.0f, 0.0f, 0.0f))),
			EGuLiBuildingPlacementRejectReason::RateLimited);
	}

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Commander, true);
		for (uint32 Index = 0u; Index < 6u; ++Index)
		{
			TestTrue(TEXT("Each of the first six builder placements is accepted"),
				Fixture.Placement->TestOnly_ProcessServerRequest(
					MakeRequest(Index + 1u, FVector(Index * 500.0f, 0.0f, 0.0f), EGuLiBuildingType::Outpost))
				.WasAccepted());
		}
		TestEqual(TEXT("The builder owns six buildings"), CountBuildings(*Fixture.World), 6);
		TestRejectReason(*this, TEXT("The seventh clear placement reaches the per-builder cap"),
			Fixture.Placement->TestOnly_ProcessServerRequest(
				MakeRequest(7u, FVector(3000.0f, 0.0f, 0.0f), EGuLiBuildingType::Outpost)),
			EGuLiBuildingPlacementRejectReason::BuilderLimitReached);
	}

	{
		FWorldFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.SetRole(EGuLiCommanderRole::Commander, true);
		for (uint32 Index = 0u; Index < 24u; ++Index)
		{
			const FVector Location(
				-5000.0f + static_cast<float>(Index % 6u) * 500.0f,
				-5000.0f + static_cast<float>(Index / 6u) * 500.0f,
				0.0f);
			if (!Fixture.SpawnDirectBuilding(*this, Location, FGuid(17u, 23u, 29u, Index + 1u)))
			{
				return false;
			}
		}
		TestEqual(TEXT("The capacity fixture contains 24 buildings"), CountBuildings(*Fixture.World), 24);
		TestRejectReason(*this, TEXT("A clear placement is rejected at the global cap"),
			Fixture.Placement->TestOnly_ProcessServerRequest(
				MakeRequest(1u, FVector(5000.0f, 5000.0f, 0.0f), EGuLiBuildingType::Outpost)),
			EGuLiBuildingPlacementRejectReason::WorldLimitReached);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BrushComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "GuLiFlightNavigationVolume.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"

namespace GuLiWingmanInitialFormationNavigationTests
{
	constexpr float NavigationHalfExtentCentimeters = 1000.0f;
	constexpr float AgentRadiusCentimeters = 50.0f;

	struct FTransientGameWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FTransientGameWorldFixture()
		{
			if (!World)
			{
				return;
			}

			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for the initial-formation navigation test"), GEngine))
			{
				return false;
			}

			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				FName(TEXT("GuLiWingmanInitialFormationNavigationTestWorld")));
			if (!Test.TestNotNull(TEXT("Transient Standalone game World exists"), World))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			return Test.TestTrue(
				TEXT("Initial-formation test World is a Standalone game World"),
				World->IsGameWorld() && World->GetNetMode() == NM_Standalone);
		}
	};

	FGuLiWingmanGroupHandle MakeGroup(const uint32 IdentitySalt)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(
			0x1650cafe,
			0x20260903,
			0xabc00000u | (IdentitySalt & 0xffffu),
			0xdef00000u | (IdentitySalt & 0xffffu));
		Group.ShipGeneration = IdentitySalt + 1u;
		Group.GroupGeneration = IdentitySalt + 10u;
		return Group;
	}

	bool BuildUsableAbilityConfig(
		FAutomationTestBase& Test,
		UObject* Outer,
		const FGuLiWingmanGroupHandle& Group,
		FGuLiGroupAbilityConfigSnapshot& OutConfig)
	{
		UGuLiShipAbilitySet* AbilitySet = UGuLiShipAbilitySet::CreateNativeV1Transient(Outer);
		if (!Test.TestNotNull(TEXT("Native v1 Ship ability set exists"), AbilitySet))
		{
			return false;
		}

		TArray<FGuLiShipAbilityGrant> Grants;
		FString Error;
		if (!AbilitySet->ResolveLoadout(
			FGuLiShipAbilityLoadoutState::MakeNativeV1(), Grants, &Error))
		{
			Test.AddError(FString::Printf(TEXT("Native v1 loadout did not resolve: %s"), *Error));
			return false;
		}

		OutConfig = FGuLiGroupAbilityConfigSnapshot();
		OutConfig.ShipInstanceId = Group.ShipInstanceId;
		OutConfig.ShipGeneration = Group.ShipGeneration;
		OutConfig.GroupGeneration = Group.GroupGeneration;
		OutConfig.AbilitySetRevision = AbilitySet->Revision;
		OutConfig.SnapshotRevision = 1u;
		OutConfig.bGroupAbilitiesValid = true;
		for (const FGuLiShipAbilityGrant& Grant : Grants)
		{
			switch (Grant.Slot)
			{
			case EGuLiShipAbilitySlot::Formation:
				OutConfig.FormationAbilityId = Grant.AbilityId;
				OutConfig.FormationDefinitionRevision = Grant.GetDefinitionRevision();
				OutConfig.FormationDefinitionChecksum = Grant.GetDefinitionChecksum();
				break;
			case EGuLiShipAbilitySlot::BasicWeapon:
				OutConfig.BasicWeaponAbilityId = Grant.AbilityId;
				OutConfig.BasicWeaponDefinitionRevision = Grant.GetDefinitionRevision();
				OutConfig.BasicWeaponDefinitionChecksum = Grant.GetDefinitionChecksum();
				break;
			case EGuLiShipAbilitySlot::Missile:
				OutConfig.MissileAbilityId = Grant.AbilityId;
				OutConfig.MissileDefinitionRevision = Grant.GetDefinitionRevision();
				OutConfig.MissileDefinitionChecksum = Grant.GetDefinitionChecksum();
				break;
			default:
				break;
			}
		}

		OutConfig.FormationCommandRevision = 1u;
		OutConfig.EffectiveClientSimTick = 30u;
		OutConfig.FormationRuntime.InnerRingRadiusCentimeters = 200.0f;
		OutConfig.FormationRuntime.OuterRingRadiusCentimeters = 400.0f;
		OutConfig.FormationRuntime.InnerRingHeightCentimeters = 100.0f;
		OutConfig.FormationRuntime.OuterRingHeightCentimeters = -100.0f;
		OutConfig.FormationRuntime.AgentRadiusCentimeters = AgentRadiusCentimeters;
		OutConfig.FormationRuntime.SeparationRadiusCentimeters = AgentRadiusCentimeters * 3.0f;
		OutConfig.RefreshHash();
		return Test.TestTrue(
			TEXT("Initial-formation config is a usable protocol-v7 projection"),
			OutConfig.IsUsableByLeaseOwner());
	}

	UGuLiFlightNavigationData* MakeSingleCellNavigationData(UObject* Outer)
	{
		UGuLiFlightNavigationData* Data = NewObject<UGuLiFlightNavigationData>(Outer);
		Data->Metadata.FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;
		Data->Metadata.DefinitionRevision = 1u;
		Data->Metadata.BakeId = FGuid::NewGuid();
		Data->Metadata.Bounds = FBox::BuildAABB(
			FVector::ZeroVector, FVector(NavigationHalfExtentCentimeters));
		Data->Metadata.MinimumCellSize = NavigationHalfExtentCentimeters * 2.0f;
		Data->Metadata.BakedAgentRadius = AgentRadiusCentimeters;
		Data->Metadata.GeometrySignature = 0x1650a11u;
		Data->Metadata.SettingsHash = 0x20260903u;

		FGuLiFlightNavOctreeNode& Root = Data->Nodes.AddDefaulted_GetRef();
		Root.Center = FVector::ZeroVector;
		Root.Extent = FVector(NavigationHalfExtentCentimeters);
		Root.LeafCellIndex = 0;

		FGuLiFlightNavCell& Cell = Data->Cells.AddDefaulted_GetRef();
		Cell.Center = FVector::ZeroVector;
		Cell.Extent = FVector(NavigationHalfExtentCentimeters);
		Cell.Clearance = AgentRadiusCentimeters;
		Cell.ComponentId = 0;
		Cell.StableId = 1u;

		Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
		return Data;
	}

	AGuLiFlightNavigationVolume* RegisterSingleCellNavigation(
		FAutomationTestBase& Test,
		UWorld& World,
		UGuLiFlightNavigationSubsystem& Navigation)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("InitialFormationFlightNavigationVolume");
		SpawnParameters.ObjectFlags |= RF_Transient;
		AGuLiFlightNavigationVolume* Volume =
			World.SpawnActor<AGuLiFlightNavigationVolume>(SpawnParameters);
		if (!Test.TestNotNull(TEXT("Real FlightNav Volume was spawned"), Volume))
		{
			return nullptr;
		}

		UBrushComponent* NavigationBrush = Volume->GetBrushComponent();
		if (!Test.TestNotNull(TEXT("FlightNav Volume has its authoritative Brush bounds"), NavigationBrush))
		{
			return nullptr;
		}
		NavigationBrush->BrushBodySetup = NewObject<UBodySetup>(NavigationBrush);
		FKBoxElem& BoundsElement = NavigationBrush->BrushBodySetup->AggGeom.BoxElems.AddDefaulted_GetRef();
		BoundsElement.X = NavigationHalfExtentCentimeters * 2.0f;
		BoundsElement.Y = NavigationHalfExtentCentimeters * 2.0f;
		BoundsElement.Z = NavigationHalfExtentCentimeters * 2.0f;
		NavigationBrush->UpdateBounds();
		if (!Test.TestTrue(
			TEXT("FlightNav test Brush exposes the requested nonzero bounds"),
			Volume->GetBounds().BoxExtent.Equals(FVector(NavigationHalfExtentCentimeters))))
		{
			return nullptr;
		}

		Volume->NavigationData = MakeSingleCellNavigationData(Volume);
		FString ValidationError;
		const bool bDataValid = Volume->NavigationData->ValidateData(ValidationError);
		if (!Test.TestTrue(
			FString::Printf(TEXT("Single-cell FlightNav data validates: %s"), *ValidationError),
			bDataValid))
		{
			Test.AddError(ValidationError);
			return nullptr;
		}

		Navigation.RegisterVolume(Volume);
		if (!Test.TestTrue(
			TEXT("Registered FlightNav Volume contains the carrier"),
			Volume->ContainsNavigationPoint(FVector::ZeroVector)))
		{
			return nullptr;
		}
		return Volume;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanInitialFormationNavigationAllSlotsTest,
	"GuLiStrike.Wingman.Mass.InitialFormationNavigationValidatesAllSlots",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanInitialFormationNavigationAllSlotsTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanInitialFormationNavigationTests;

	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	UGuLiFlightNavigationSubsystem* Navigation =
		Fixture.World->GetSubsystem<UGuLiFlightNavigationSubsystem>();
	if (!TestNotNull(TEXT("Production owner Wingman simulation subsystem exists"), Simulation)
		|| !TestNotNull(TEXT("Production FlightNav subsystem exists"), Navigation))
	{
		return false;
	}
	Simulation->SetGroupBehaviorStateTreeForTests(nullptr);
	if (!RegisterSingleCellNavigation(*this, *Fixture.World, *Navigation))
	{
		return false;
	}

	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 1u;
	CarrierSource.MoveRevision = 1u;

	const FGuLiWingmanGroupHandle ValidGroup = MakeGroup(1u);
	FGuLiGroupAbilityConfigSnapshot ValidConfig;
	if (!BuildUsableAbilityConfig(*this, Simulation, ValidGroup, ValidConfig))
	{
		return false;
	}
	if (!TestTrue(
		TEXT("Production creation accepts a complete 25-slot formation inside FlightNav"),
		Simulation->CreateOrResetOwnedGroup(
			ValidGroup,
			ValidConfig,
			FTransform::Identity,
			FVector::ZeroVector,
			CarrierSource)))
	{
		return false;
	}
	TestEqual(
		TEXT("Accepted formation creates exactly 25 owner entities"),
		Simulation->GetOwnedEntityCount(ValidGroup),
		static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));

	const FGuLiWingmanGroupHandle RejectedGroup = MakeGroup(2u);
	FGuLiGroupAbilityConfigSnapshot RejectedConfig;
	if (!BuildUsableAbilityConfig(*this, Simulation, RejectedGroup, RejectedConfig))
	{
		return false;
	}
	RejectedConfig.FormationRuntime.OuterRingRadiusCentimeters = 960.0f;
	RejectedConfig.RefreshHash();
	if (!TestTrue(
		TEXT("Outer-ring rejection config remains structurally usable"),
		RejectedConfig.IsUsableByLeaseOwner()))
	{
		return false;
	}

	TestEqual(
		TEXT("Carrier alone remains valid in the registered production navigation"),
		Navigation->ValidateAuthoritativeSegment(
			FVector::ZeroVector,
			FVector::ZeroVector,
			AgentRadiusCentimeters),
		EGuLiFlightNavSegmentStatus::Valid);
	const FVector FirstOuterRingSlot(
		RejectedConfig.FormationRuntime.OuterRingRadiusCentimeters,
		0.0f,
		RejectedConfig.FormationRuntime.OuterRingHeightCentimeters);
	TestEqual(
		TEXT("At least the first outer-ring slot exceeds the radius-inset navigation bounds"),
		Navigation->ValidateAuthoritativeSegment(
			FirstOuterRingSlot,
			FirstOuterRingSlot,
			AgentRadiusCentimeters),
		EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation);

	const int32 EntityCountBeforeRejectedCreate = Simulation->GetTotalOwnedEntityCount();
	TestFalse(
		TEXT("Production creation rejects a group when any of its 25 initial slots is outside FlightNav"),
		Simulation->CreateOrResetOwnedGroup(
			RejectedGroup,
			RejectedConfig,
			FTransform::Identity,
			FVector::ZeroVector,
			CarrierSource));
	TestEqual(
		TEXT("Rejected group creates zero owner entities"),
		Simulation->GetOwnedEntityCount(RejectedGroup),
		0);
	TestEqual(
		TEXT("Rejected creation adds zero entities to the existing valid group"),
		Simulation->GetTotalOwnedEntityCount(),
		EntityCountBeforeRejectedCreate);
	TestEqual(
		TEXT("The previously accepted 25-member group remains intact"),
		Simulation->GetOwnedEntityCount(ValidGroup),
		static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));

	TestTrue(TEXT("Accepted group cleanup succeeds"), Simulation->DestroyOwnedGroup(ValidGroup));
	TestEqual(TEXT("Test cleanup removes all owner entities"), Simulation->GetTotalOwnedEntityCount(), 0);
	return true;
}

#endif

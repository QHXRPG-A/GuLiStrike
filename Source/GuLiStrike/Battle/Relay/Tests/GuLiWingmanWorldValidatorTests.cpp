// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanWorldValidator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace GuLiWingmanWorldValidatorTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FWorldFixture()
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
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for Candidate World validation"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Transient authority World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			const UPackage* WorldPackage = World->GetOutermost();
			const bool bTransientAutomationWorld = World->HasAnyFlags(RF_Transient)
				|| (WorldPackage && WorldPackage->GetName().StartsWith(TEXT("/Temp/")));
			return Test.TestTrue(TEXT("Validator override World is transient standalone Game"),
				bTransientAutomationWorld
					&& World->WorldType == EWorldType::Game
					&& World->GetNetMode() == NM_Standalone);
		}

		UBoxComponent* SpawnBox(
			FAutomationTestBase& Test,
			const ECollisionChannel ObjectType,
			const FVector& Location)
		{
			AActor* Actor = World ? World->SpawnActor<AActor>() : nullptr;
			if (!Test.TestNotNull(TEXT("World obstacle Actor exists"), Actor))
			{
				return nullptr;
			}
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->SetBoxExtent(FVector(20.0));
			Box->SetMobility(ObjectType == ECC_WorldStatic
				? EComponentMobility::Static : EComponentMobility::Movable);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ObjectType);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Actor->SetActorLocation(Location);
			World->UpdateWorldComponents(true, false);
			return Box;
		}
	};

	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(91u, 92u, 93u, 94u);
		Group.ShipGeneration = 1u;
		Group.GroupGeneration = 2u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.MatchEpoch = 1u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(1u, 2u, 3u, 4u);
		Config.WingmanTypeId = TEXT("WorldValidatorTestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 1u;
		Config.LoadoutRevision = 1u;
		Config.SnapshotRevision = 1u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 1u;
		Config.FormationDefinitionChecksum = 2u;
		Config.BasicWeaponDefinitionRevision = 1u;
		Config.BasicWeaponDefinitionChecksum = 3u;
		Config.MissileDefinitionRevision = 1u;
		Config.MissileDefinitionChecksum = 4u;
		Config.FormationCommandRevision = 1u;
		Config.EffectiveClientSimTick = 1u;
		Config.FormationRuntime.AgentRadiusCentimeters = 50.0f;
		Config.RefreshHash();
		return Config;
	}

	struct FContextFixture
	{
		FGuLiWingmanCandidateBatch Candidate;
		FGuLiRelayCarrierState Carrier;
		FGuLiGroupAbilityConfigSnapshot Config;
		FGuLiWingmanCandidateWorldValidationContext Context;

		FContextFixture()
		{
			const FGuLiWingmanGroupHandle Group = MakeGroup();
			Config = MakeConfig(Group);
			Candidate.MatchEpoch = 1u;
			Candidate.Group = Group;
			Candidate.LeaseEpoch = 1u;
			Candidate.CandidateSequence = 1u;
			Candidate.ClientSimTick = 1u;
			Candidate.CarrierSource.CanonicalEpoch = 1u;
			Candidate.CarrierSource.MoveRevision = 1u;
			Candidate.AbilitySetRevision = Config.AbilitySetRevision;
			Candidate.FormationCommandRevision = Config.FormationCommandRevision;
			Candidate.FormationDefinitionChecksum = Config.FormationDefinitionChecksum;
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman.Flight.Group = Group;
			Sample.Wingman.Flight.FlightIndex = 0u;
			Sample.Wingman.MemberIndex = 0u;
			Sample.Wingman.EntityGeneration = 1u;
			Sample.PositionCentimeters = FIntVector(200, 0, 0);
			Sample.VelocityCentimetersPerSecond = FIntVector(100, 0, 0);
			Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Follow);

			Carrier.Transform = FTransform::Identity;
			Carrier.ServerWorldTimeSeconds = 0.1;
			Context.Candidate = &Candidate;
			Context.Carrier = &Carrier;
			Context.ConfirmedAbilityConfig = &Config;
			FGuLiWingmanCandidateWorldSegment& Segment = Context.Segments.AddDefaulted_GetRef();
			Segment.Wingman = Sample.Wingman;
			Segment.PreviousPosition = FVector(-200.0, 0.0, 0.0);
			Segment.CurrentPosition = FVector(200.0, 0.0, 0.0);
			Segment.bHasPreviousAcceptedSample = true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanWorldStaticDynamicNavigationTest,
	"GuLiStrike.Wingman.Relay.Validator.WorldStaticDynamicAndNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanWorldStaticDynamicNavigationTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanWorldValidatorTests;
	FWorldFixture WorldFixture;
	if (!WorldFixture.Initialize(*this))
	{
		return false;
	}
	FContextFixture ContextFixture;
	int32 NavigationValidationCount = 0;
	const GuLiWingmanWorldValidation::FFlightNavSegmentValidatorForTests PermitNavigation =
		[&NavigationValidationCount](
			const FVector& Start, const FVector& End, const float AgentRadius)
		{
			++NavigationValidationCount;
			return Start.Equals(FVector(-200.0, 0.0, 0.0))
				&& End.Equals(FVector(200.0, 0.0, 0.0))
				&& FMath::IsNearlyEqual(AgentRadius, 50.0f);
		};
	const FGuLiCandidateWorldValidator Validator = GuLiWingmanWorldValidation::MakeValidatorForTests(
		WorldFixture.World, nullptr, PermitNavigation);
	TestEqual(TEXT("An obstacle-free segment with valid baked navigation is accepted"),
		Validator(ContextFixture.Context), EGuLiWingmanRejectReason::None);
	TestEqual(TEXT("The exact segment and projected agent radius reach FlightNav"),
		NavigationValidationCount, 1);

	UBoxComponent* StaticObstacle = WorldFixture.SpawnBox(
		*this, ECC_WorldStatic, FVector::ZeroVector);
	if (!StaticObstacle)
	{
		return false;
	}
	TestEqual(TEXT("An ECC_WorldStatic sphere sweep blocks the Candidate"),
		Validator(ContextFixture.Context), EGuLiWingmanRejectReason::InvalidIdentity);
	StaticObstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UBoxComponent* MovableStaticChannelObstacle = WorldFixture.SpawnBox(
		*this, ECC_WorldStatic, FVector::ZeroVector);
	if (!MovableStaticChannelObstacle)
	{
		return false;
	}
	MovableStaticChannelObstacle->SetMobility(EComponentMobility::Movable);
	TestEqual(TEXT("A movable hull on the WorldStatic channel is not treated as historical static geometry"),
		Validator(ContextFixture.Context), EGuLiWingmanRejectReason::None);
	MovableStaticChannelObstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UBoxComponent* DynamicObstacle = WorldFixture.SpawnBox(
		*this, ECC_WorldDynamic, FVector::ZeroVector);
	if (!DynamicObstacle)
	{
		return false;
	}
	TestEqual(TEXT("An ordinary WorldDynamic projectile/pickup does not block movement"),
		Validator(ContextFixture.Context), EGuLiWingmanRejectReason::None);
	DynamicObstacle->ComponentTags.Add(GuLiWingmanWorldValidation::GetDynamicObstacleTag());
	TestEqual(TEXT("The explicit component tag promotes WorldDynamic geometry to an obstacle"),
		Validator(ContextFixture.Context), EGuLiWingmanRejectReason::InvalidIdentity);
	DynamicObstacle->ComponentTags.Reset();
	DynamicObstacle->GetOwner()->Tags.Add(GuLiWingmanWorldValidation::GetDynamicObstacleTag());
	TestEqual(TEXT("The explicit Actor tag also promotes WorldDynamic geometry to an obstacle"),
		Validator(ContextFixture.Context), EGuLiWingmanRejectReason::InvalidIdentity);
	DynamicObstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	int32 RejectedNavigationCount = 0;
	const FGuLiCandidateWorldValidator RejectNavigation = GuLiWingmanWorldValidation::MakeValidatorForTests(
		WorldFixture.World,
		nullptr,
		[&RejectedNavigationCount](const FVector&, const FVector&, const float)
		{
			++RejectedNavigationCount;
			return false;
		});
	TestEqual(TEXT("A FlightNav endpoint/clearance/portal rejection blocks the Candidate"),
		RejectNavigation(ContextFixture.Context), EGuLiWingmanRejectReason::InvalidIdentity);
	TestEqual(TEXT("FlightNav validation runs once per member segment"), RejectedNavigationCount, 1);
	return true;
}

#endif

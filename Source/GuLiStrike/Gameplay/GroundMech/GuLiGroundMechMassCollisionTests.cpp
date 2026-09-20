#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementNetwork.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <limits>

namespace GuLiGroundMechMassCollisionTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

FGuLiGroundMassBody MakeBody(const uint32 SoldierId, const FVector &Location, const float Radius = 150.0f,
							 const float BottomZ = 0.0f, const float TopZ = 300.0f)
{
	FGuLiGroundMassBody Body;
	Body.SoldierId = FGuLiSoldierId(SoldierId);
	Body.Team = EGuLiTeam::Red;
	Body.UnitTypeId = 1u;
	Body.Location = Location;
	Body.RadiusCentimeters = Radius;
	Body.BottomZ = BottomZ;
	Body.TopZ = TopZ;
	return Body;
}

struct FTransientGameWorld
{
	UWorld *World = nullptr;
	bool bRegisteredContext = false;

	~FTransientGameWorld()
	{
		if (!World)
			return;
		World->DestroyWorld(false);
		if (bRegisteredContext && GEngine)
			GEngine->DestroyWorldContext(World);
	}

	bool Initialize(FAutomationTestBase &Test)
	{
		if (!Test.TestNotNull(TEXT("Engine exists"), GEngine))
			return false;
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("Transient game world exists"), World))
			return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		bRegisteredContext = true;
		return true;
	}
};
} // namespace GuLiGroundMechMassCollisionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassSideSweepTest, "GuLiStrike.GroundMech.MassCollision.SideSweep",
								 GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassSideSweepTest::RunTest(const FString &Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	const TArray<FGuLiGroundMassBody> Bodies{MakeBody(1u, FVector(500.0f, 0.0f, 0.0f))};

	const FGuLiGroundMassMoveResult Blocked = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 150.0f), FVector(100000.0f, 0.0f, 0.0f), 100.0f, 100.0f, 0.1f, Bodies);
	TestEqual(TEXT("High-speed sweep reports the blocking soldier"), Blocked.FirstHit.Value, 1u);
	TestTrue(TEXT("High-speed movement stops before the combined cylinder radius"),
			 Blocked.Delta.X >= 240.0f && Blocked.Delta.X <= 250.0f);
	TestTrue(TEXT("High-speed sweep never crosses the body"),
			 Blocked.Delta.X + 100.0f <= Bodies[0].Location.X - Bodies[0].RadiusCentimeters);

	const FGuLiGroundMassMoveResult Sliding = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 150.0f), FVector(1000.0f, 400.0f, 0.0f), 100.0f, 100.0f, 0.1f, Bodies);
	const FVector SlidingEnd = FVector(0.0f, 0.0f, 150.0f) + Sliding.Delta;
	TestTrue(TEXT("Oblique impact produces a side hit"), Sliding.SideHitCount > 0);
	TestTrue(TEXT("Oblique impact retains tangential movement"), Sliding.Delta.Y > 0.0f);
	TestTrue(TEXT("Sliding endpoint remains outside the combined radius"),
			 FVector::DistSquared2D(SlidingEnd, Bodies[0].Location) >= FMath::Square(248.0f));

	const TArray<FGuLiGroundMassBody> OverlapBodies{MakeBody(3u, FVector::ZeroVector, 150.0f)};
	const FGuLiGroundMassMoveResult Depenetrated = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(10.0f, 0.0f, 150.0f), FVector::ZeroVector, 100.0f, 100.0f, 0.1f, OverlapBodies);
	TestTrue(TEXT("Initial overlap is pushed along its shortest direction"),
			 Depenetrated.Delta.X > 0.0f && FMath::IsNearlyZero(Depenetrated.Delta.Y));
	TestTrue(TEXT("Initial overlap correction is bounded"),
			 Depenetrated.DepenetrationCentimeters > 0.0f &&
				 Depenetrated.DepenetrationCentimeters <= GuLiGroundMassCollision::MaximumDepenetrationCentimeters);

	const FGuLiGroundMassMoveResult Flying = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 1000.0f), FVector(1000.0f, 0.0f, 0.0f), 100.0f, 100.0f, 0.1f, Bodies);
	TestTrue(TEXT("A vertically separated flying mech passes freely"),
			 Flying.Delta.Equals(FVector(1000.0f, 0.0f, 0.0f), UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Vertical separation produces no side hit"), Flying.SideHitCount, 0);

	FGuLiGroundMassBody IncomingBody = MakeBody(5u, FVector(500.0f, 0.0f, 0.0f));
	IncomingBody.Velocity = FVector(-5000.0f, 0.0f, 0.0f);
	const TArray<FGuLiGroundMassBody> IncomingBodies{IncomingBody};
	const FGuLiGroundMassMoveResult MovingContact = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 150.0f), FVector::ZeroVector, 100.0f, 100.0f, 0.1f, IncomingBodies);
	TestEqual(TEXT("Relative sweep detects a Mass body moving into a stationary mech"), MovingContact.FirstHit.Value,
			  5u);
	TestTrue(TEXT("Post-contact relative projection keeps the moving body from crossing"),
			 MovingContact.Delta.X < -240.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassLandingAndSupportTest,
								 "GuLiStrike.GroundMech.MassCollision.LandingAndSupport",
								 GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassLandingAndSupportTest::RunTest(const FString &Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	TArray<FGuLiGroundMassBody> Bodies;
	Bodies.Add(MakeBody(8u, FVector::ZeroVector, 300.0f, 0.0f, 300.0f));
	Bodies.Add(MakeBody(9u, FVector::ZeroVector, 300.0f, 0.0f, 500.0f));
	Bodies.Add(MakeBody(3u, FVector::ZeroVector, 300.0f, 0.0f, 500.0f));
	Bodies.Add(MakeBody(1u, FVector::ZeroVector, 300.0f, 0.0f, 499.5f));
	const FGuLiGroundMassLandingResult Landing = GuLiGroundMassCollision::FindLandingSupport(
		FVector(0.0f, 0.0f, 800.0f), FVector(0.0f, 0.0f, -700.0f), 100.0f, 0.1f, Bodies);
	TestTrue(TEXT("Fast descent crosses a Mass top"), Landing.IsValid());
	TestEqual(TEXT("Highest top wins and stable ID breaks an equal-height tie"), Landing.SoldierId.Value, 3u);
	TestEqual(TEXT("Selected landing height is the highest top"), Landing.TopZ, 500.0f);

	FGuLiGroundMassBody Moving = Bodies[2];
	Moving.Velocity = FVector(100.0f, 50.0f, 20.0f);
	const FGuLiGroundMassBody Extrapolated = Moving.Extrapolated(1.0f);
	TestTrue(TEXT("Snapshot extrapolation is clamped to 100 ms"),
			 Extrapolated.Location.Equals(FVector(10.0f, 5.0f, 2.0f), UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Moving top follows the clamped vertical displacement"), Extrapolated.TopZ, 502.0f);

	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this))
		return false;
	UGuLiGroundMassContactSubsystem *Contacts = Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>();
	if (!TestNotNull(TEXT("Mass contact subsystem exists"), Contacts))
		return false;

	FGuLiGroundMassBody Support = MakeBody(31u, FVector::ZeroVector, 500.0f, 0.0f, 500.0f);
	Contacts->TestOnly_SetBodies(MakeArrayView(&Support, 1));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGuLiGroundMechCharacter *Mech = Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(
		AGuLiGroundMechCharacter::StaticClass(), FVector(0.0f, 0.0f, 1000.0f), FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("Ground mech exists"), Mech))
		return false;
	UGuLiGroundMechMovementComponent *Movement = Cast<UGuLiGroundMechMovementComponent>(Mech->GetCharacterMovement());
	if (!TestNotNull(TEXT("Ground mech uses the Mass-aware movement component"), Movement))
		return false;
	const float CapsuleHalfHeight = Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Mech->SetActorLocation(FVector(Support.Location.X, Support.Location.Y, Support.TopZ + CapsuleHalfHeight + 150.0f));
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.0f, 0.0f, -4000.0f);
	Movement->StartNewPhysics(0.1f, 0);
	TestEqual(TEXT("Landing enters Mass support with the selected soldier"), Movement->GetMassSupportSoldierId().Value,
			  31u);
	TestEqual(TEXT("Mass support uses the dedicated custom movement mode"), Movement->MovementMode, MOVE_Custom);
	TestEqual(TEXT("Mass support uses the reserved custom mode value"), Movement->CustomMovementMode,
			  GuLiGroundMechMovement::MassSupportCustomMode);
	TestTrue(TEXT("Falling physics stops at the virtual top during a substep"),
			 FMath::IsNearlyEqual(Mech->GetActorLocation().Z, Support.TopZ + CapsuleHalfHeight, 1.0f));

	const FVector LandedLocation = Mech->GetActorLocation();
	Support.Location += FVector(100.0f, 50.0f, 20.0f);
	Support.BottomZ += 20.0f;
	Support.TopZ += 20.0f;
	Contacts->TestOnly_SetBodies(MakeArrayView(&Support, 1));
	Movement->StartNewPhysics(0.1f, 0);
	const FVector CarriedLocation = Mech->GetActorLocation();
	TestTrue(TEXT("Moving support carries the mech horizontally without inheriting rotation"),
			 FVector2D(CarriedLocation - LandedLocation).Equals(FVector2D(100.0f, 50.0f), 1.0f) &&
				 Mech->GetActorRotation().Equals(FRotator::ZeroRotator, 0.1f));
	TestTrue(TEXT("Moving support carries the mech through top-height changes"),
			 FMath::IsNearlyEqual(CarriedLocation.Z - LandedLocation.Z, 20.0f, 1.0f));
	Movement->ApplyExternalDisplacement(
		FTransform(Mech->GetActorQuat(), CarriedLocation + FVector(1000.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("External displacement clears Mass support immediately"),
			  Movement->GetMassSupportSoldierId().IsValid());
	TestEqual(TEXT("External displacement leaves a former support rider falling"), Movement->MovementMode,
			  MOVE_Falling);

	Mech->SetActorLocation(FVector(Support.Location.X, Support.Location.Y, Support.TopZ + CapsuleHalfHeight + 50.0f));
	Movement->SetMovementMode(MOVE_Falling);
	FHitResult MoveHit(1.0f);
	Movement->Velocity = FVector(0.0f, 0.0f, -2000.0f);
	Movement->StartNewPhysics(0.05f, 0);
	TestEqual(TEXT("The mech can reacquire support after an external displacement"),
			  Movement->GetMassSupportSoldierId().Value, 31u);
	Movement->MoveUpdatedComponent(FVector(510.0f, 0.0f, 0.0f), Mech->GetActorQuat(), true, &MoveHit);
	Movement->Velocity = FVector::ZeroVector;
	Movement->StartNewPhysics(0.1f, 0);
	TestFalse(TEXT("Walking beyond the support top clears the support ID"),
			  Movement->GetMassSupportSoldierId().IsValid());
	TestEqual(TEXT("Walking off a support without ordinary ground starts falling"), Movement->MovementMode,
			  MOVE_Falling);

	Mech->SetActorLocation(FVector(Support.Location.X, Support.Location.Y, Support.TopZ + CapsuleHalfHeight + 50.0f));
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.0f, 0.0f, -2000.0f);
	Movement->StartNewPhysics(0.05f, 0);
	TestEqual(TEXT("The mech can land again after walking off a support"), Movement->GetMassSupportSoldierId().Value,
			  31u);

	Contacts->TestOnly_SetBodies(TConstArrayView<FGuLiGroundMassBody>());
	Movement->StartNewPhysics(0.1f, 0);
	TestFalse(TEXT("Missing, dead, phased, or retired support clears the support ID"),
			  Movement->GetMassSupportSoldierId().IsValid());
	TestEqual(TEXT("Losing support transitions immediately to falling"), Movement->MovementMode, MOVE_Falling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassFriendlyYieldTest, "GuLiStrike.GroundMech.MassCollision.FriendlyYield",
								 GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassFriendlyYieldTest::RunTest(const FString &Parameters)
{
	(void)Parameters;
	TArray<FGuLiGroundMassYieldCandidate> Candidates;
	for (uint32 Id = 1u; Id <= 20u; ++Id)
	{
		FGuLiGroundMassYieldCandidate &Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.StableSoldierId = Id;
		Candidate.Team = EGuLiTeam::Red;
		Candidate.Location = FVector(static_cast<float>(Id * 10u), 0.0f, 0.0f);
		Candidate.RadiusCentimeters = 50.0f;
		Candidate.bCanYield = true;
	}
	FGuLiGroundMassYieldCandidate &Enemy = Candidates.AddDefaulted_GetRef();
	Enemy.StableSoldierId = 21u;
	Enemy.Team = EGuLiTeam::Blue;
	Enemy.Location = FVector(1.0f, 0.0f, 0.0f);
	Enemy.RadiusCentimeters = 50.0f;
	Enemy.bCanYield = true;
	FGuLiGroundMassYieldCandidate &HighFriendly = Candidates.AddDefaulted_GetRef();
	HighFriendly.StableSoldierId = 22u;
	HighFriendly.Team = EGuLiTeam::Red;
	HighFriendly.Location = FVector(1.0f, 0.0f, 200.0f);
	HighFriendly.RadiusCentimeters = 50.0f;
	HighFriendly.bCanYield = true;

	TArray<int32> Selected;
	GuLiGroundMassCollision::SelectFriendlyYieldCandidates(Candidates, EGuLiTeam::Red, FVector::ZeroVector, 230.0f,
														   100.0f, 100.0f, 16, Selected);
	TestEqual(TEXT("One mech influences at most sixteen idle friendlies"), Selected.Num(), 16);
	for (int32 Rank = 0; Rank < Selected.Num(); ++Rank)
	{
		TestEqual(TEXT("Friendly yield candidates are nearest-first with stable ordering"),
				  Candidates[Selected[Rank]].StableSoldierId, static_cast<uint32>(Rank + 1));
		TestEqual(TEXT("Enemy soldiers never enter friendly yield"), Candidates[Selected[Rank]].Team, EGuLiTeam::Red);
	}

	const FVector Target = GuLiGroundMassCollision::ComputeYieldTarget(FVector::ZeroVector, FVector(10.0f, 0.0f, 0.0f),
																	   FVector::ZeroVector, 11u, 1u, 400.0f, 1250.0f);
	TestTrue(TEXT("Yield target clears the mech at the requested center distance"),
			 FMath::IsNearlyEqual(Target.Size2D(), 400.0f, 0.1f));
	const FVector ClampedTarget = GuLiGroundMassCollision::ComputeYieldTarget(
		FVector::ZeroVector, FVector(100.0f, 0.0f, 0.0f), FVector(-2000.0f, 0.0f, 0.0f), 11u, 1u, 5000.0f, 1250.0f);
	TestTrue(TEXT("Yield never exceeds 1250 cm from the captured anchor"),
			 ClampedTarget.Size2D() <= 1250.0f + UE_KINDA_SMALL_NUMBER);
	bool bReturning = false;
	const FVector HeldTarget = GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(FVector::ZeroVector, Target,
																						  10.49, 10.0, 0.5, bReturning);
	TestFalse(TEXT("Yield holds its clearance target before the return delay"), bReturning);
	TestTrue(TEXT("The held target remains unchanged before 0.5 seconds"), HeldTarget.Equals(Target));
	const FVector ReturnTarget = GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(
		FVector::ZeroVector, Target, 10.5, 10.0, 0.5, bReturning);
	TestTrue(TEXT("Yield starts returning when pressure has been absent for 0.5 seconds"), bReturning);
	TestTrue(TEXT("The delayed return target is the captured anchor"), ReturnTarget.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassObstacleRegistryTest,
								 "GuLiStrike.GroundMech.MassCollision.DynamicObstacleRegistry",
								 GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassObstacleRegistryTest::RunTest(const FString &Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this))
		return false;
	UGuLiDynamicObstacleRegistrySubsystem *Registry =
		Fixture.World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!TestNotNull(TEXT("Dynamic obstacle registry exists"), Registry))
		return false;

	const FGuLiDynamicObstacleHandle StaticHandle = Registry->RegisterObstacle(FVector(100.0f, 200.0f, 0.0f), 300.0f);
	FGuLiDynamicObstacle MechObstacle;
	MechObstacle.Location = FVector::ZeroVector;
	MechObstacle.RadiusCentimeters = 230.0f;
	MechObstacle.Kind = EGuLiDynamicObstacleKind::GroundMech;
	MechObstacle.Team = EGuLiTeam::Red;
	const FGuLiDynamicObstacleHandle MechHandle = Registry->RegisterObstacle(MechObstacle);
	const uint32 RevisionBeforeUpdate = Registry->GetRevision();
	MechObstacle.Location = FVector(50.0f, 75.0f, 0.0f);
	TestTrue(TEXT("A registered ground-mech obstacle can be updated"),
			 Registry->UpdateObstacle(MechHandle, MechObstacle) == EGuLiObstacleUpdateResult::Updated);
	TestEqual(TEXT("Obstacle update preserves its stable handle"), MechHandle.Value, 2u);
	TestTrue(TEXT("Changed obstacle data advances the registry revision"),
			 Registry->GetRevision() > RevisionBeforeUpdate);
	const uint32 RevisionAfterUpdate = Registry->GetRevision();
	TestTrue(TEXT("An identical update reports unchanged"),
			 Registry->UpdateObstacle(MechHandle, MechObstacle) == EGuLiObstacleUpdateResult::Unchanged);
	TestEqual(TEXT("An identical update does not publish a redundant revision"), Registry->GetRevision(),
			  RevisionAfterUpdate);

	FGuLiDynamicObstacle Invalid = MechObstacle;
	Invalid.Location.X = std::numeric_limits<double>::quiet_NaN();
	TestTrue(TEXT("Invalid data is distinguished from a missing handle"),
			 Registry->UpdateObstacle(MechHandle, Invalid) == EGuLiObstacleUpdateResult::InvalidData);
	TestTrue(TEXT("Validation precedes handle recovery"),
			 Registry->UpdateObstacle(FGuLiDynamicObstacleHandle{999u}, Invalid) ==
				 EGuLiObstacleUpdateResult::InvalidData);
	TestFalse(TEXT("Invalid registration cannot insert a second record"),
			  Registry->RegisterObstacle(Invalid).IsValid());
	TestEqual(TEXT("Invalid writes retain both valid obstacles"), Registry->GetObstacles().Num(), 2);
	TestEqual(TEXT("Invalid writes do not publish a revision"), Registry->GetRevision(), RevisionAfterUpdate);
	TestTrue(TEXT("Missing valid handle has a distinct recovery result"),
			 Registry->UpdateObstacle(FGuLiDynamicObstacleHandle{999u}, MechObstacle) ==
				 EGuLiObstacleUpdateResult::NotFound);

	const FGuLiDynamicObstacle *StaticObstacle = Registry->GetObstacles().FindByPredicate(
		[StaticHandle](const FGuLiDynamicObstacle &Obstacle) { return Obstacle.Handle == StaticHandle; });
	TestNotNull(TEXT("Legacy static resource obstacle remains registered"), StaticObstacle);
	if (StaticObstacle)
	{
		TestEqual(TEXT("Legacy obstacle keeps static-world behavior"), StaticObstacle->Kind,
				  EGuLiDynamicObstacleKind::StaticWorld);
		TestEqual(TEXT("Legacy obstacle has no team affinity"), StaticObstacle->Team, EGuLiTeam::Unassigned);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassSpatialIndexTest, "GuLiStrike.GroundMech.MassCollision.SpatialIndex500",
								 GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassSpatialIndexTest::RunTest(const FString &Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	TArray<FGuLiGroundMassBody> Bodies;
	Bodies.Reserve(500);
	for (int32 Y = 0; Y < 20; ++Y)
	{
		for (int32 X = 0; X < 25; ++X)
		{
			Bodies.Add(MakeBody(static_cast<uint32>(Bodies.Num() + 1),
								FVector(200.0f + X * 1500.0f, 200.0f + Y * 1500.0f, 0.0f), 100.0f));
		}
	}
	FGuLiGroundMassSpatialIndex Index;
	Index.Rebuild(Bodies);
	TestEqual(TEXT("All five hundred valid cylinders enter the spatial index"), Index.Num(), 500);
	TestTrue(TEXT("Sparse bodies occupy many grid buckets"), Index.GetBucketCount() > 400);

	const FVector2D QueryCenter(Bodies[262].Location);
	TArray<FGuLiGroundMassBody> Results;
	int32 RawCandidates = 0;
	Index.Query(FBox2D(QueryCenter - FVector2D(100.0f), QueryCenter + FVector2D(100.0f)), 0.1f, 0.1f, Results,
				&RawCandidates);
	TestEqual(TEXT("A local query returns only the intersecting body"), Results.Num(), 1);
	TestEqual(TEXT("The local query returns the expected stable soldier"), Results[0].SoldierId.Value,
			  Bodies[262].SoldierId.Value);
	TestTrue(TEXT("The grid bounds raw candidates far below a full 500-body scan"),
			 RawCandidates > 0 && RawCandidates < 25);
	AddInfo(FString::Printf(TEXT("500-body grid: buckets=%d, raw candidates=%d, intersecting bodies=%d"),
							Index.GetBucketCount(), RawCandidates, Results.Num()));

	FGuLiGroundMassBody Incoming = MakeBody(900u, FVector(700.0f, 0.0f, 0.0f), 100.0f);
	Incoming.Velocity = FVector(-5000.0f, 0.0f, 0.0f);
	Index.Rebuild(MakeArrayView(&Incoming, 1));
	Index.Query(FBox2D(FVector2D(-100.0f, -100.0f), FVector2D(300.0f, 100.0f)), 0.0f, 0.1f, Results);
	TestEqual(TEXT("Broad phase includes a moving body that enters during the step"), Results.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassContinuousTimeTest,
								 "GuLiStrike.GroundMech.MassCollision.ContinuousTime",
								 GuLiGroundMechMassCollisionTests::Flags)
bool FGuLiGroundMassContinuousTimeTest::RunTest(const FString &Parameters)
{
	using namespace GuLiGroundMechMassCollisionTests;
	auto Body = MakeBody(1, FVector(500, 0, 0));
	Body.Velocity = FVector(-720, 0, 0);
	const auto Aged = Body.Extrapolated(0.08f).Extrapolated(0.1f);
	TestTrue(TEXT("Snapshot age and movement share one 100ms extrapolation budget"),
			 Aged.Location.Equals(FVector(428, 0, 0), 0.001));
	TestTrue(TEXT("A stale snapshot remains stationary during another movement"),
			 Aged.TranslationDuring(0.05f).IsNearlyZero());
	const auto Whole = GuLiGroundMassCollision::ResolvePlanarMove(FVector(0, 0, 150), FVector(350, 0, 0), 100, 100,
																  0.05f, MakeArrayView(&Body, 1));
	const auto First = GuLiGroundMassCollision::ResolvePlanarMove(FVector(0, 0, 150), FVector(175, 0, 0), 100, 100,
																  0.025f, MakeArrayView(&Body, 1));
	const auto Advanced = Body.Extrapolated(0.025f);
	const auto Second = GuLiGroundMassCollision::ResolvePlanarMove(FVector(0, 0, 150) + First.Delta, FVector(175, 0, 0),
																   100, 100, 0.025f, MakeArrayView(&Advanced, 1));
	TestTrue(TEXT("Two physical substeps match one 50ms relative sweep"),
			 (First.Delta + Second.Delta).Equals(Whole.Delta, 0.01));
	TestTrue(TEXT("50ms accounts for 36cm of Mass motion"), FMath::IsNearlyEqual(Whole.Delta.X, 212.0, 0.01));

	Body.Velocity = FVector::ZeroVector;
	const FVector Start(0, 0, 700), Delta(600, 0, -600);
	const auto Hit = GuLiGroundMassCollision::Sweep(Start, Delta, 100, 100, 0.05f, MakeArrayView(&Body, 1), {}, true);
	TestTrue(TEXT("Entering XY above a unit then descending still collides"),
			 Hit.Kind == EGuLiGroundMassContactKind::Side);
	TestTrue(TEXT("The diagonal side begins at vertical entry, halfway through the move"),
			 FMath::IsNearlyEqual(Hit.Time, 0.5f, 0.001f));
	const auto Resolved =
		GuLiGroundMassCollision::ResolvePlanarMove(Start, Delta, 100, 100, 0.05f, MakeArrayView(&Body, 1));
	const FVector End = Start + Resolved.Delta;
	TestTrue(TEXT("Diagonal descent cannot end inside the cylinder"),
			 !GuLiGroundMassCollision::HasVerticalOverlap(End.Z, 100, Body.BottomZ, Body.TopZ) ||
				 FVector::DistSquared2D(End, Body.Location) >= FMath::Square(249.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassRawSnapshotTest, "GuLiStrike.GroundMech.MassCollision.RawSnapshot",
								 GuLiGroundMechMassCollisionTests::Flags)
bool FGuLiGroundMassRawSnapshotTest::RunTest(const FString &Parameters)
{
	using namespace GuLiGroundMechMassCollisionTests;
	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this))
		return false;
	auto *Roster = Fixture.World->SpawnActor<AGuLiSoldierStateReplicator>();
	auto *Contacts = Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>();
	TArray<FGuLiSoldierStateItem> States;
	States.SetNum(2);
	States[0].SoldierId = FGuLiSoldierId(1);
	States[1].SoldierId = FGuLiSoldierId(2);
	Roster->ApplyAuthoritySnapshot(States, 1);
	Contacts->TestOnly_SetRoster(Roster, 1);
	FGuLiSoldierPoseChunk Chunk;
	Chunk.AuthorityEpoch = 1;
	Chunk.FrameSequence = 10;
	Chunk.ServerTimeSeconds = 1.0f;
	Chunk.ChunkCount = 2;
	FGuLiQuantizedSoldierPose Pose;
	Pose.SoldierId = FGuLiSoldierId(1);
	Pose.SetWorldLocationCentimeters(FVector(200, 0, 0));
	Pose.SetVelocityCentimetersPerSecond(FVector(720, 0, 0));
	Chunk.Samples = {Pose};
	Contacts->TestOnly_ReceivePose(Chunk);
	Chunk.ChunkIndex = 1;
	Chunk.Samples[0].SoldierId = FGuLiSoldierId(2);
	Contacts->TestOnly_ReceivePose(Chunk);
	Contacts->TestOnly_RefreshSnapshot();
	auto Frame = Contacts->CaptureMove(0.05f);
	FGuLiGroundMassBody Body;
	TestTrue(TEXT("Decoded data works without a presentation actor"),
			 Frame.Snapshot && Frame.Snapshot->Find(FGuLiSoldierId(1), 1.0, Body));
	TestTrue(TEXT("The next chunk in the same frame is accepted"), Frame.Snapshot->Find(FGuLiSoldierId(2), 1.0, Body));
	Frame.Snapshot->Find(FGuLiSoldierId(1), 1.1, Body);
	TestTrue(TEXT("Physics uses transmitted velocity and sample time"), Body.Location.Equals(FVector(272, 0, 0), 0.01));
	Chunk.FrameSequence = 9;
	Chunk.Samples[0].SoldierId = FGuLiSoldierId(1);
	Chunk.Samples[0].SetWorldLocationCentimeters(FVector(9000, 0, 0));
	Contacts->TestOnly_ReceivePose(Chunk);
	Contacts->TestOnly_RefreshSnapshot();
	Contacts->FindMassBody(FGuLiSoldierId(1), Body);
	TestTrue(TEXT("An older pose cannot overwrite the latest sample"), Body.Location.X < 300.0);
	const auto Historical = Contacts->CaptureMove(0.0f);
	States[0].bPhased = true;
	Roster->ApplyAuthoritySnapshot(States, 1);
	TestFalse(TEXT("Reliable phase invalidates the current frame immediately"),
			  Contacts->FindMassBody(FGuLiSoldierId(1), Body));
	TestTrue(TEXT("Invalidation does not mutate a frame retained by a saved move"),
			 Historical.Snapshot->Find(FGuLiSoldierId(1), 1.0, Body));
	States[0].bPhased = false;
	States[0].DisplacementFrameFloor = 20;
	States[0].DisplacementLocation = FVector(5000, 100, 300);
	States[0].DisplacementSimulationTime = 1.1;
	Roster->ApplyAuthoritySnapshot(States, 1);
	TestTrue(TEXT("Reliable displacement seeds collision without an unreliable teleport packet"),
			 Contacts->FindMassBody(FGuLiSoldierId(1), Body));
	TestTrue(TEXT("Reliable displacement replaces the old location"),
			 Body.Location.Equals(States[0].DisplacementLocation));
	Chunk.FrameSequence = 19;
	Contacts->TestOnly_ReceivePose(Chunk);
	Contacts->TestOnly_RefreshSnapshot();
	Contacts->FindMassBody(FGuLiSoldierId(1), Body);
	TestTrue(TEXT("Pre-displacement poses cannot revive the previous location"),
			 Body.Location.Equals(States[0].DisplacementLocation));
	Contacts->MarkLocalContact(Body);
	FTransform Display(FVector(9999, 0, 0));
	Contacts->ApplyContactPresentation(Body.SoldierId, Display);
	TestTrue(TEXT("Contact display uses the collision pose immediately"), Display.GetLocation().Equals(Body.Location));
	Fixture.World->TimeSeconds += 0.1;
	Contacts->TestOnly_EndContactFrame();
	Display = FTransform(FVector(9999, 0, 0));
	Contacts->ApplyContactPresentation(Body.SoldierId, Display);
	TestTrue(TEXT("Leaving contact blends back over 200ms"),
			 Display.GetLocation().Equals(FMath::Lerp(Body.Location, FVector(9999, 0, 0), 0.5), 0.01));
	Roster->ApplyAuthoritySnapshot(States, 2);
	TestFalse(TEXT("A new match cannot reuse an old collision frame"), Contacts->CaptureMove(0).Snapshot.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassMovementReplayTest,
								 "GuLiStrike.GroundMech.MassCollision.MovementReplay",
								 GuLiGroundMechMassCollisionTests::Flags)
bool FGuLiGroundMassMovementReplayTest::RunTest(const FString &Parameters)
{
	using namespace GuLiGroundMechMassCollisionTests;
	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this))
		return false;
	auto *Contacts = Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>();
	auto Body = MakeBody(71, FVector::ZeroVector, 150, 0, 300);
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto *Mech =
		Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(FVector(-145, 0, 500), FRotator::ZeroRotator, Spawn);
	auto *Move = CastChecked<UGuLiGroundMechMovementComponent>(Mech->GetCharacterMovement());
	Mech->GetCapsuleComponent()->SetCapsuleSize(100, 100);
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(0, 0, -4000);
	Move->StartNewPhysics(0.05f, 0);
	if (!TestEqual(TEXT("Edge rider starts supported"), Move->GetMassSupportSoldierId().Value, 71u))
		return false;
	const FVector BeforeCarry = Mech->GetActorLocation();
	Fixture.World->TimeSeconds = 0.05;
	Body.Location.X = 20;
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	Move->StartNewPhysics(0.05f, 0);
	TestEqual(TEXT("A moving platform cannot invalidate the old edge position before carry"),
			  Move->GetMassSupportSoldierId().Value, 71u);
	TestTrue(TEXT("The edge rider receives the 20cm platform translation"),
			 Mech->GetActorLocation().Equals(BeforeCarry + FVector(20, 0, 0), 0.01));

	Fixture.World->TimeSeconds = 0.1;
	Body.Velocity = FVector(720, 0, 0);
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	const FVector PredictedStart = Mech->GetActorLocation();
	const auto Baseline = Move->TestOnly_GetSupport();
	Fixture.World->DeltaTimeSeconds = 1.0f / 60.0f;
	Move->StartNewPhysics(0.05f, 0);
	const auto Context = Move->TestOnly_GetMoveContext();
	const FVector PredictedEnd = Mech->GetActorLocation();
	TestTrue(TEXT("A 50ms server-style move carries 36cm despite a 60Hz world"),
			 (PredictedEnd - PredictedStart).Equals(FVector(36, 0, 0), 0.01));
	// Rewind to an authoritative corrected baseline and then replay the retained frame.
	auto Corrected = Baseline;
	Corrected.BodyLocation.X += 10;
	Corrected.RelativeLocation = PredictedStart + FVector(10, 0, 0) - Corrected.BodyLocation;
	Mech->SetActorLocation(PredictedStart + FVector(10, 0, 0));
	Move->TestOnly_ApplySupportBaseline(Corrected);
	Fixture.World->DeltaTimeSeconds = 0.2f;
	Body.Location.X = 9000;
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	Move->TestOnly_Replay(Context, 0.05f);
	TestTrue(TEXT("Replay advances once from the correction and ignores newer live poses"),
			 Mech->GetActorLocation().Equals(PredictedEnd + FVector(10, 0, 0), 0.01));
	Fixture.World->TimeSeconds = 0.15;
	Body.Location.X = 56;
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	Move->StartNewPhysics(0.05f, 0);
	TestTrue(TEXT("The next ordinary move preserves the corrected platform baseline"),
			 Mech->GetActorLocation().Equals(PredictedEnd + FVector(46, 0, 0), 0.01));

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Corrected.Serialize(Writer);
	FGuLiMassSupportState Decoded;
	FMemoryReader Reader(Bytes);
	Decoded.Serialize(Reader);
	TestFalse(TEXT("Support response decoding succeeds"), Reader.IsError());
	TestEqual(TEXT("Support response preserves stable ID"), Decoded.SoldierId.Value, Corrected.SoldierId.Value);
	TestTrue(TEXT("Support response preserves its reference pose"),
			 Decoded.BodyLocation.Equals(Corrected.BodyLocation));
	TestEqual(TEXT("Support response preserves simulation time"), Decoded.SimulationSeconds,
			  Corrected.SimulationSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassWorldLandingTest, "GuLiStrike.GroundMech.MassCollision.WorldLanding",
								 GuLiGroundMechMassCollisionTests::Flags)
bool FGuLiGroundMassWorldLandingTest::RunTest(const FString &Parameters)
{
	using namespace GuLiGroundMechMassCollisionTests;
	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this))
		return false;
	auto Body = MakeBody(81, FVector::ZeroVector, 500, 0, 300);
	Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>()->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	auto *Floor = Fixture.World->SpawnActor<AActor>();
	auto *Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(1000, 1000, 20));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Floor->SetActorLocation(FVector(0, 0, 600));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto *Mech = Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
	Mech->GetCapsuleComponent()->SetCapsuleSize(100, 100);
	auto *Move = CastChecked<UGuLiGroundMechMovementComponent>(Mech->GetCharacterMovement());
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(0, 0, -10000);
	Move->StartNewPhysics(0.1f, 0);
	TestFalse(TEXT("Earlier world collision prevents acquiring a lower Mass top"),
			  Move->GetMassSupportSoldierId().IsValid());
	TestTrue(TEXT("World floor wins the continuous sweep"), Mech->GetActorLocation().Z >= 720.0f);
	TestEqual(TEXT("Normal world landing retains CMC walking"), Move->MovementMode, MOVE_Walking);
	// A wall changes the trajectory before the proposed Mass landing. The old landing
	// candidate must be discarded and the deflected path queried again.
	Box->SetBoxExtent(FVector(10, 1000, 2000));
	Floor->SetActorLocation(FVector(-100, 0, 1000));
	Body.Location.X = 200;
	Body.RadiusCentimeters = 150;
	Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>()->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	Mech->SetActorLocation(FVector(-500, 0, 1100));
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(10000, 0, -10000);
	Move->StartNewPhysics(0.1f, 0);
	TestFalse(TEXT("World sliding discards the landing from the old path"), Move->GetMassSupportSoldierId().IsValid());
	TestTrue(TEXT("The deflected path stays on the near side of the world wall"), Mech->GetActorLocation().X <= -209.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiGroundMassNetworkMoveTest, "GuLiStrike.GroundMech.MassCollision.NetworkMove",
								 GuLiGroundMechMassCollisionTests::Flags)
bool FGuLiGroundMassNetworkMoveTest::RunTest(const FString &Parameters)
{
	using namespace GuLiGroundMechMassCollisionTests;
	const UClass *FormalMech = LoadClass<AGuLiGroundMechCharacter>(
		nullptr, TEXT("/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C"));
	if (!TestNotNull(TEXT("Formal ground mech Blueprint loads"), FormalMech))
		return false;
	TestTrue(TEXT("Formal Blueprint uses the Mass-aware movement component"),
			 FormalMech->GetDefaultObject<AGuLiGroundMechCharacter>()
				 ->GetCharacterMovement()
				 ->IsA<UGuLiGroundMechMovementComponent>());
	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this))
		return false;
	auto *Contacts = Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>();
	auto Body = MakeBody(91, FVector::ZeroVector, 500, 0, 300);
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto *Mech = Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(FVector(0, 0, 500), FRotator::ZeroRotator, Spawn);
	Mech->GetCapsuleComponent()->SetCapsuleSize(100, 100);
	auto *Move = CastChecked<UGuLiGroundMechMovementComponent>(Mech->GetCharacterMovement());
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(0, 0, -4000);
	Move->StartNewPhysics(0.05f, 0);
	if (!TestEqual(TEXT("Network fixture starts on a Mass support"), Move->GetMassSupportSoldierId().Value, 91u))
		return false;
	const FVector Before = Mech->GetActorLocation();
	Fixture.World->TimeSeconds = 0.05;
	Body.Velocity = FVector(720, 0, 0);
	Contacts->TestOnly_SetBodies(MakeArrayView(&Body, 1));
	Fixture.World->DeltaTimeSeconds = 1.0f / 60.0f;
	FGuLiGroundMechMoveContainer Packet;
	Move->Activate();
	auto &Data = Packet.Moves[0];
	Data.TimeStamp = 0.05f;
	Data.Revision = Move->GetDisplacementRevision();
	Data.Location = Before + FVector(36, 0, 0);
	Data.MovementMode = Move->PackNetworkMovementMode();
	// Incorrect support identity with an otherwise correct position must still be corrected.
	Data.SupportId = 0;
	Data.SupportEpoch = 0;
	Data.SupportDisplacement = 0;
	Move->ServerMove_HandleMoveData(Packet);
	TestTrue(TEXT("Real server move handler consumes client timestamp delta, not world delta"),
			 Mech->GetActorLocation().Equals(Before + FVector(36, 0, 0), 0.01));
	auto *ServerData = Move->GetPredictionData_Server_Character();
	TestFalse(TEXT("Wrong support identity forces correction even at the correct position"),
			  ServerData->PendingAdjustment.bAckGoodMove);
	FGuLiGroundMechMoveResponse Sent;
	Sent.ServerFillResponseData(*Move, ServerData->PendingAdjustment);
	TestEqual(TEXT("Correction is bound to the acknowledged support"), Sent.Support.SoldierId.Value, 91u);
	const FVector Captured = Sent.Support.BodyLocation;
	Move->SupportState.BodyLocation.X += 500;
	FGuLiGroundMechMoveResponse Delayed;
	Delayed.ServerFillResponseData(*Move, ServerData->PendingAdjustment);
	TestTrue(TEXT("Delayed serialization uses the captured baseline, not live support"),
			 Delayed.Support.BodyLocation.Equals(Captured));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Extended movement response serializes through CMC"), Sent.Serialize(*Move, Writer, nullptr));
	FGuLiGroundMechMoveResponse Received;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Extended movement response deserializes through CMC"), Received.Serialize(*Move, Reader, nullptr));
	TestEqual(TEXT("External displacement revision survives the specialized response"), Received.Revision,
			  Sent.Revision);
	TestTrue(TEXT("Support reference survives the complete movement response"),
			 Received.Support.BodyLocation.Equals(Captured));

	auto *Client =
		Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(FVector(2000, 0, 700), FRotator::ZeroRotator, Spawn);
	Client->GetCapsuleComponent()->SetCapsuleSize(100, 100);
	auto *ClientMove = CastChecked<UGuLiGroundMechMovementComponent>(Client->GetCharacterMovement());
	ClientMove->Activate();
	auto *Prediction = ClientMove->GetPredictionData_Client_Character();
	FSavedMovePtr Acked = Prediction->AllocateNewMove();
	Acked->SetMoveFor(Client, 0.05f, FVector::ZeroVector, *Prediction);
	Acked->TimeStamp = Data.TimeStamp;
	Prediction->SavedMoves.Add(Acked);
	ClientMove->ClientHandleMoveResponse(Received);
	TestEqual(TEXT("Correction installs the authoritative support before replay"),
			  ClientMove->GetMassSupportSoldierId().Value, 91u);
	TestTrue(TEXT("Correction installs the corresponding reference pose"),
			 ClientMove->SupportState.BodyLocation.Equals(Captured));
	ClientMove->MassSupportSoldierId = FGuLiSoldierId(999u);
	TestEqual(TEXT("An independently replicated support mirror cannot rewind physics"),
			  ClientMove->GetMassSupportSoldierId().Value, 91u);
	const FVector BeforeStale = ClientMove->SupportState.BodyLocation;
	Received.Revision += 1;
	Received.Support.BodyLocation.X += 123;
	ClientMove->ClientHandleMoveResponse(Received);
	TestTrue(TEXT("Existing external revision guard also protects support corrections"),
			 ClientMove->SupportState.BodyLocation.Equals(BeforeStale));
	// The fixture hosts both peers in one transient world; their capsules must not
	// collide with one another after the correction places them at the same position.
	Client->SetActorEnableCollision(false);
	Move->SupportState = Sent.Support;
	Data.TimeStamp = 0.1f;
	Data.Location = Before + FVector(72, 0, 0);
	Move->ServerMove_HandleMoveData(Packet);
	TestTrue(TEXT("A second packet in the same world frame consumes the next 50ms"),
			 Mech->GetActorLocation().Equals(Before + FVector(72, 0, 0), 0.01));
	Data.TimeStamp = 0.15f;
	Move->ServerMove_HandleMoveData(Packet);
	TestTrue(TEXT("Additional packets cannot advance beyond the original 100ms sample horizon"),
			 Mech->GetActorLocation().Equals(Before + FVector(72, 0, 0), 0.01));
	TestFalse(TEXT("An acknowledged move releases its immutable collision frame"),
			  static_cast<FGuLiGroundMechSavedMove &>(*Acked).Context.Snapshot.IsValid());
	Client->SetReplicates(true);
	Client->SwapRoles();
	TestEqual(TEXT("Remote movement fixture is a simulated proxy"), Client->GetLocalRole(), ROLE_SimulatedProxy);
	ClientMove->SetMovementMode(MOVE_Walking);
	ClientMove->CurrentFloor.bBlockingHit = true;
	ClientMove->CurrentFloor.bWalkableFloor = true;
	ClientMove->CurrentFloor.HitResult.ImpactNormal = FVector::UpVector;
	ClientMove->CurrentFloor.HitResult.Normal = FVector::UpVector;
	const FVector BeforeProxySmooth = Client->GetActorLocation();
	ClientMove->MoveSmooth(FVector(100, 0, 0), 0.05f);
	TestTrue(TEXT("Remote proxy smoothing uses CMC without a local collision time context"),
			 Client->GetActorLocation().Equals(BeforeProxySmooth + FVector(5, 0, 0), 0.01));
	return true;
}

#endif

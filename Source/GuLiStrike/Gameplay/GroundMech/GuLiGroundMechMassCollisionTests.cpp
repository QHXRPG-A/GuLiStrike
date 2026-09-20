#include "Gameplay/GroundMech/GuLiGroundMassCollisionTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Misc/AutomationTest.h"

namespace GuLiGroundMechMassCollisionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter;

	FGuLiGroundMassBody MakeBody(
		const uint32 SoldierId,
		const FVector& Location,
		const float Radius = 150.0f,
		const float BottomZ = 0.0f,
		const float TopZ = 300.0f)
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
		UWorld* World = nullptr;
		bool bRegisteredContext = false;

		~FTransientGameWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (bRegisteredContext && GEngine) GEngine->DestroyWorldContext(World);
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists"), GEngine)) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Transient game world exists"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bRegisteredContext = true;
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGroundMassSideSweepTest,
	"GuLiStrike.GroundMech.MassCollision.SideSweep",
	GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassSideSweepTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	const TArray<FGuLiGroundMassBody> Bodies{MakeBody(1u, FVector(500.0f, 0.0f, 0.0f))};

	const FGuLiGroundMassMoveResult Blocked = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 150.0f),
		FVector(100000.0f, 0.0f, 0.0f),
		100.0f,
		100.0f,
		0.1f,
		Bodies);
	TestEqual(TEXT("High-speed sweep reports the blocking soldier"), Blocked.FirstHit.Value, 1u);
	TestTrue(TEXT("High-speed movement stops before the combined cylinder radius"),
		Blocked.Delta.X >= 240.0f && Blocked.Delta.X <= 250.0f);
	TestTrue(TEXT("High-speed sweep never crosses the body"),
		Blocked.Delta.X + 100.0f <= Bodies[0].Location.X - Bodies[0].RadiusCentimeters);

	const FGuLiGroundMassMoveResult Sliding = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 150.0f),
		FVector(1000.0f, 400.0f, 0.0f),
		100.0f,
		100.0f,
		0.1f,
		Bodies);
	const FVector SlidingEnd = FVector(0.0f, 0.0f, 150.0f) + Sliding.Delta;
	TestTrue(TEXT("Oblique impact produces a side hit"), Sliding.SideHitCount > 0);
	TestTrue(TEXT("Oblique impact retains tangential movement"), Sliding.Delta.Y > 0.0f);
	TestTrue(TEXT("Sliding endpoint remains outside the combined radius"),
		FVector::DistSquared2D(SlidingEnd, Bodies[0].Location)
			>= FMath::Square(248.0f));

	const TArray<FGuLiGroundMassBody> OverlapBodies{
		MakeBody(3u, FVector::ZeroVector, 150.0f)};
	const FGuLiGroundMassMoveResult Depenetrated = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(10.0f, 0.0f, 150.0f),
		FVector::ZeroVector,
		100.0f,
		100.0f,
		0.1f,
		OverlapBodies);
	TestTrue(TEXT("Initial overlap is pushed along its shortest direction"),
		Depenetrated.Delta.X > 0.0f && FMath::IsNearlyZero(Depenetrated.Delta.Y));
	TestTrue(TEXT("Initial overlap correction is bounded"),
		Depenetrated.DepenetrationCentimeters > 0.0f
			&& Depenetrated.DepenetrationCentimeters
				<= GuLiGroundMassCollision::MaximumDepenetrationCentimeters);

	const FGuLiGroundMassMoveResult Flying = GuLiGroundMassCollision::ResolvePlanarMove(
		FVector(0.0f, 0.0f, 1000.0f),
		FVector(1000.0f, 0.0f, 0.0f),
		100.0f,
		100.0f,
		0.1f,
		Bodies);
	TestTrue(TEXT("A vertically separated flying mech passes freely"),
		Flying.Delta.Equals(FVector(1000.0f, 0.0f, 0.0f), UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Vertical separation produces no side hit"), Flying.SideHitCount, 0);

	FGuLiGroundMassBody IncomingBody = MakeBody(
		5u,
		FVector(500.0f, 0.0f, 0.0f));
	IncomingBody.Velocity = FVector(-5000.0f, 0.0f, 0.0f);
	const TArray<FGuLiGroundMassBody> IncomingBodies{IncomingBody};
	const FGuLiGroundMassMoveResult MovingContact =
		GuLiGroundMassCollision::ResolvePlanarMove(
			FVector(0.0f, 0.0f, 150.0f),
			FVector::ZeroVector,
			100.0f,
			100.0f,
			0.1f,
			IncomingBodies);
	TestEqual(TEXT("Relative sweep detects a Mass body moving into a stationary mech"),
		MovingContact.FirstHit.Value,
		5u);
	TestTrue(TEXT("Post-contact relative projection keeps the moving body from crossing"),
		MovingContact.Delta.X < -240.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGroundMassLandingAndSupportTest,
	"GuLiStrike.GroundMech.MassCollision.LandingAndSupport",
	GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassLandingAndSupportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	TArray<FGuLiGroundMassBody> Bodies;
	Bodies.Add(MakeBody(8u, FVector::ZeroVector, 300.0f, 0.0f, 300.0f));
	Bodies.Add(MakeBody(9u, FVector::ZeroVector, 300.0f, 0.0f, 500.0f));
	Bodies.Add(MakeBody(3u, FVector::ZeroVector, 300.0f, 0.0f, 500.0f));
	Bodies.Add(MakeBody(1u, FVector::ZeroVector, 300.0f, 0.0f, 499.5f));
	const FGuLiGroundMassLandingResult Landing = GuLiGroundMassCollision::FindLandingSupport(
		FVector(0.0f, 0.0f, 800.0f),
		FVector(0.0f, 0.0f, -700.0f),
		100.0f,
		0.1f,
		Bodies);
	TestTrue(TEXT("Fast descent crosses a Mass top"), Landing.IsValid());
	TestEqual(TEXT("Highest top wins and stable ID breaks an equal-height tie"),
		Landing.SoldierId.Value,
		3u);
	TestEqual(TEXT("Selected landing height is the highest top"), Landing.TopZ, 500.0f);

	FGuLiGroundMassBody Moving = Bodies[2];
	Moving.Velocity = FVector(100.0f, 50.0f, 20.0f);
	const FGuLiGroundMassBody Extrapolated = Moving.Extrapolated(1.0f);
	TestTrue(TEXT("Snapshot extrapolation is clamped to 100 ms"),
		Extrapolated.Location.Equals(FVector(10.0f, 5.0f, 2.0f), UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Moving top follows the clamped vertical displacement"),
		Extrapolated.TopZ,
		502.0f);

	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiGroundMassContactSubsystem* Contacts =
		Fixture.World->GetSubsystem<UGuLiGroundMassContactSubsystem>();
	if (!TestNotNull(TEXT("Mass contact subsystem exists"), Contacts)) return false;

	FGuLiGroundMassBody Support = MakeBody(
		31u,
		FVector::ZeroVector,
		500.0f,
		0.0f,
		500.0f);
	Contacts->TestOnly_SetBodies(MakeArrayView(&Support, 1));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGuLiGroundMechCharacter* Mech = Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(
		AGuLiGroundMechCharacter::StaticClass(),
		FVector(0.0f, 0.0f, 1000.0f),
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!TestNotNull(TEXT("Ground mech exists"), Mech)) return false;
	UGuLiGroundMechMovementComponent* Movement =
		Cast<UGuLiGroundMechMovementComponent>(Mech->GetCharacterMovement());
	if (!TestNotNull(TEXT("Ground mech uses the Mass-aware movement component"), Movement)) return false;
	const float CapsuleHalfHeight = Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Mech->SetActorLocation(FVector(
		Support.Location.X,
		Support.Location.Y,
		Support.TopZ + CapsuleHalfHeight + 150.0f));
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.0f, 0.0f, -4000.0f);
	Movement->StartNewPhysics(0.1f, 0);
	TestEqual(TEXT("Landing enters Mass support with the selected soldier"),
		Movement->GetMassSupportSoldierId().Value,
		31u);
	TestEqual(TEXT("Mass support uses the dedicated custom movement mode"),
		Movement->MovementMode,
		MOVE_Custom);
	TestEqual(TEXT("Mass support uses the reserved custom mode value"),
		Movement->CustomMovementMode,
		GuLiGroundMechMovement::MassSupportCustomMode);
	TestTrue(TEXT("Falling physics stops at the virtual top during a substep"),
		FMath::IsNearlyEqual(
			Mech->GetActorLocation().Z,
			Support.TopZ + CapsuleHalfHeight,
			1.0f));

	const FVector LandedLocation = Mech->GetActorLocation();
	Support.Location += FVector(100.0f, 50.0f, 20.0f);
	Support.BottomZ += 20.0f;
	Support.TopZ += 20.0f;
	Contacts->TestOnly_SetBodies(MakeArrayView(&Support, 1));
	Movement->StartNewPhysics(0.1f, 0);
	const FVector CarriedLocation = Mech->GetActorLocation();
	TestTrue(TEXT("Moving support carries the mech horizontally without inheriting rotation"),
		FVector2D(CarriedLocation - LandedLocation).Equals(FVector2D(100.0f, 50.0f), 1.0f)
			&& Mech->GetActorRotation().Equals(FRotator::ZeroRotator, 0.1f));
	TestTrue(TEXT("Moving support carries the mech through top-height changes"),
		FMath::IsNearlyEqual(CarriedLocation.Z - LandedLocation.Z, 20.0f, 1.0f));
	Movement->ApplyExternalDisplacement(FTransform(
		Mech->GetActorQuat(),
		CarriedLocation + FVector(1000.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("External displacement clears Mass support immediately"),
		Movement->GetMassSupportSoldierId().IsValid());
	TestEqual(TEXT("External displacement leaves a former support rider falling"),
		Movement->MovementMode,
		MOVE_Falling);

	Mech->SetActorLocation(FVector(
		Support.Location.X,
		Support.Location.Y,
		Support.TopZ + CapsuleHalfHeight + 50.0f));
	Movement->SetMovementMode(MOVE_Falling);
	FHitResult MoveHit(1.0f);
	Movement->MoveUpdatedComponent(
		FVector(0.0f, 0.0f, -100.0f),
		Mech->GetActorQuat(),
		true,
		&MoveHit);
	TestEqual(TEXT("The mech can reacquire support after an external displacement"),
		Movement->GetMassSupportSoldierId().Value,
		31u);
	Movement->MoveUpdatedComponent(
		FVector(510.0f, 0.0f, 0.0f),
		Mech->GetActorQuat(),
		true,
		&MoveHit);
	Movement->Velocity = FVector::ZeroVector;
	Movement->StartNewPhysics(0.1f, 0);
	TestFalse(TEXT("Walking beyond the support top clears the support ID"),
		Movement->GetMassSupportSoldierId().IsValid());
	TestEqual(TEXT("Walking off a support without ordinary ground starts falling"),
		Movement->MovementMode,
		MOVE_Falling);

	Mech->SetActorLocation(FVector(
		Support.Location.X,
		Support.Location.Y,
		Support.TopZ + CapsuleHalfHeight + 50.0f));
	Movement->SetMovementMode(MOVE_Falling);
	Movement->MoveUpdatedComponent(
		FVector(0.0f, 0.0f, -100.0f),
		Mech->GetActorQuat(),
		true,
		&MoveHit);
	TestEqual(TEXT("The mech can land again after walking off a support"),
		Movement->GetMassSupportSoldierId().Value,
		31u);

	Contacts->TestOnly_SetBodies(TConstArrayView<FGuLiGroundMassBody>());
	Movement->StartNewPhysics(0.1f, 0);
	TestFalse(TEXT("Missing, dead, phased, or retired support clears the support ID"),
		Movement->GetMassSupportSoldierId().IsValid());
	TestEqual(TEXT("Losing support transitions immediately to falling"),
		Movement->MovementMode,
		MOVE_Falling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGroundMassFriendlyYieldTest,
	"GuLiStrike.GroundMech.MassCollision.FriendlyYield",
	GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassFriendlyYieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FGuLiGroundMassYieldCandidate> Candidates;
	for (uint32 Id = 1u; Id <= 20u; ++Id)
	{
		FGuLiGroundMassYieldCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.StableSoldierId = Id;
		Candidate.Team = EGuLiTeam::Red;
		Candidate.Location = FVector(static_cast<float>(Id * 10u), 0.0f, 0.0f);
		Candidate.RadiusCentimeters = 50.0f;
		Candidate.bCanYield = true;
	}
	FGuLiGroundMassYieldCandidate& Enemy = Candidates.AddDefaulted_GetRef();
	Enemy.StableSoldierId = 21u;
	Enemy.Team = EGuLiTeam::Blue;
	Enemy.Location = FVector(1.0f, 0.0f, 0.0f);
	Enemy.RadiusCentimeters = 50.0f;
	Enemy.bCanYield = true;
	FGuLiGroundMassYieldCandidate& HighFriendly = Candidates.AddDefaulted_GetRef();
	HighFriendly.StableSoldierId = 22u;
	HighFriendly.Team = EGuLiTeam::Red;
	HighFriendly.Location = FVector(1.0f, 0.0f, 200.0f);
	HighFriendly.RadiusCentimeters = 50.0f;
	HighFriendly.bCanYield = true;

	TArray<int32> Selected;
	GuLiGroundMassCollision::SelectFriendlyYieldCandidates(
		Candidates,
		EGuLiTeam::Red,
		FVector::ZeroVector,
		230.0f,
		100.0f,
		100.0f,
		16,
		Selected);
	TestEqual(TEXT("One mech influences at most sixteen idle friendlies"), Selected.Num(), 16);
	for (int32 Rank = 0; Rank < Selected.Num(); ++Rank)
	{
		TestEqual(TEXT("Friendly yield candidates are nearest-first with stable ordering"),
			Candidates[Selected[Rank]].StableSoldierId,
			static_cast<uint32>(Rank + 1));
		TestEqual(TEXT("Enemy soldiers never enter friendly yield"),
			Candidates[Selected[Rank]].Team,
			EGuLiTeam::Red);
	}

	const FVector Target = GuLiGroundMassCollision::ComputeYieldTarget(
		FVector::ZeroVector,
		FVector(10.0f, 0.0f, 0.0f),
		FVector::ZeroVector,
		11u,
		1u,
		400.0f,
		1250.0f);
	TestTrue(TEXT("Yield target clears the mech at the requested center distance"),
		FMath::IsNearlyEqual(Target.Size2D(), 400.0f, 0.1f));
	const FVector ClampedTarget = GuLiGroundMassCollision::ComputeYieldTarget(
		FVector::ZeroVector,
		FVector(100.0f, 0.0f, 0.0f),
		FVector(-2000.0f, 0.0f, 0.0f),
		11u,
		1u,
		5000.0f,
		1250.0f);
	TestTrue(TEXT("Yield never exceeds 1250 cm from the captured anchor"),
		ClampedTarget.Size2D() <= 1250.0f + UE_KINDA_SMALL_NUMBER);
	bool bReturning = false;
	const FVector HeldTarget = GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(
		FVector::ZeroVector,
		Target,
		10.49,
		10.0,
		0.5,
		bReturning);
	TestFalse(TEXT("Yield holds its clearance target before the return delay"), bReturning);
	TestTrue(TEXT("The held target remains unchanged before 0.5 seconds"),
		HeldTarget.Equals(Target));
	const FVector ReturnTarget = GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(
		FVector::ZeroVector,
		Target,
		10.5,
		10.0,
		0.5,
		bReturning);
	TestTrue(TEXT("Yield starts returning when pressure has been absent for 0.5 seconds"),
		bReturning);
	TestTrue(TEXT("The delayed return target is the captured anchor"),
		ReturnTarget.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGroundMassObstacleRegistryTest,
	"GuLiStrike.GroundMech.MassCollision.DynamicObstacleRegistry",
	GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassObstacleRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	FTransientGameWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiDynamicObstacleRegistrySubsystem* Registry =
		Fixture.World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!TestNotNull(TEXT("Dynamic obstacle registry exists"), Registry)) return false;

	const FGuLiDynamicObstacleHandle StaticHandle = Registry->RegisterObstacle(
		FVector(100.0f, 200.0f, 0.0f),
		300.0f);
	FGuLiDynamicObstacle MechObstacle;
	MechObstacle.Location = FVector::ZeroVector;
	MechObstacle.RadiusCentimeters = 230.0f;
	MechObstacle.Kind = EGuLiDynamicObstacleKind::GroundMech;
	MechObstacle.Team = EGuLiTeam::Red;
	const FGuLiDynamicObstacleHandle MechHandle = Registry->RegisterObstacle(MechObstacle);
	const uint32 RevisionBeforeUpdate = Registry->GetRevision();
	MechObstacle.Location = FVector(50.0f, 75.0f, 0.0f);
	TestTrue(TEXT("A registered ground-mech obstacle can be updated"),
		Registry->UpdateObstacle(MechHandle, MechObstacle));
	TestEqual(TEXT("Obstacle update preserves its stable handle"), MechHandle.Value, 2u);
	TestTrue(TEXT("Changed obstacle data advances the registry revision"),
		Registry->GetRevision() > RevisionBeforeUpdate);
	const uint32 RevisionAfterUpdate = Registry->GetRevision();
	TestTrue(TEXT("An identical update succeeds"), Registry->UpdateObstacle(MechHandle, MechObstacle));
	TestEqual(TEXT("An identical update does not publish a redundant revision"),
		Registry->GetRevision(),
		RevisionAfterUpdate);

	const FGuLiDynamicObstacle* StaticObstacle = Registry->GetObstacles().FindByPredicate(
		[StaticHandle](const FGuLiDynamicObstacle& Obstacle)
		{
			return Obstacle.Handle == StaticHandle;
		});
	TestNotNull(TEXT("Legacy static resource obstacle remains registered"), StaticObstacle);
	if (StaticObstacle)
	{
		TestEqual(TEXT("Legacy obstacle keeps static-world behavior"),
			StaticObstacle->Kind,
			EGuLiDynamicObstacleKind::StaticWorld);
		TestEqual(TEXT("Legacy obstacle has no team affinity"),
			StaticObstacle->Team,
			EGuLiTeam::Unassigned);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGroundMassSpatialIndexTest,
	"GuLiStrike.GroundMech.MassCollision.SpatialIndex500",
	GuLiGroundMechMassCollisionTests::Flags)

bool FGuLiGroundMassSpatialIndexTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiGroundMechMassCollisionTests;
	TArray<FGuLiGroundMassBody> Bodies;
	Bodies.Reserve(500);
	for (int32 Y = 0; Y < 20; ++Y)
	{
		for (int32 X = 0; X < 25; ++X)
		{
			Bodies.Add(MakeBody(
				static_cast<uint32>(Bodies.Num() + 1),
				FVector(200.0f + X * 1500.0f, 200.0f + Y * 1500.0f, 0.0f),
				100.0f));
		}
	}
	FGuLiGroundMassSpatialIndex Index;
	Index.Rebuild(Bodies);
	TestEqual(TEXT("All five hundred valid cylinders enter the spatial index"), Index.Num(), 500);
	TestTrue(TEXT("Sparse bodies occupy many grid buckets"), Index.GetBucketCount() > 400);

	const FVector2D QueryCenter(Bodies[262].Location);
	TArray<FGuLiGroundMassBody> Results;
	int32 RawCandidates = 0;
	Index.Query(
		FBox2D(QueryCenter - FVector2D(100.0f), QueryCenter + FVector2D(100.0f)),
		0.1f,
		0.1f,
		Results,
		&RawCandidates);
	TestEqual(TEXT("A local query returns only the intersecting body"), Results.Num(), 1);
	TestEqual(TEXT("The local query returns the expected stable soldier"),
		Results[0].SoldierId.Value,
		Bodies[262].SoldierId.Value);
	TestTrue(TEXT("The grid bounds raw candidates far below a full 500-body scan"),
		RawCandidates > 0 && RawCandidates < 25);

	FGuLiGroundMassBody Incoming = MakeBody(
		900u,
		FVector(700.0f, 0.0f, 0.0f),
		100.0f);
	Incoming.Velocity = FVector(-5000.0f, 0.0f, 0.0f);
	Index.Rebuild(MakeArrayView(&Incoming, 1));
	Index.Query(
		FBox2D(FVector2D(-100.0f, -100.0f), FVector2D(300.0f, 100.0f)),
		0.0f,
		0.1f,
		Results);
	TestEqual(TEXT("Broad phase includes a moving body that enters during the step"),
		Results.Num(),
		1);
	return true;
}

#endif

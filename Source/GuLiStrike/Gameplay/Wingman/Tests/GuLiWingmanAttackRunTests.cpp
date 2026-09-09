#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Data/Generated/GuLiStrikeShipTableRows.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanGroundRunTest, "GuLiStrike.Wingman.Attack.GroundPathAndCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanGroundRunTest::RunTest(const FString&)
{
	FGuLiWingmanAttackProfile Profile; Profile.Pattern = EGuLiWingmanAttackPattern::GroundDive; Profile.ExecutorId = TEXT("WingmanGroundMissile");
	FGuLiWingmanGroundRunPath Path;
	TestTrue(TEXT("Default path is physically feasible"), GuLiWingmanAttack::BuildGroundPath(FVector(100,200,300), FVector(1,0,0), Profile, 20, Path));
	const float ExpectedTurnDrop = Path.TurnRadius * (1.0f - UE_INV_SQRT_2);
	const float ExpectedLeg = Profile.FlightSpeed * Profile.DiveSeconds * UE_INV_SQRT_2;
	TestTrue(TEXT("Entry includes enough turn lift to preserve the authored minimum clearance"),
		FMath::IsNearlyEqual(Path.Entry.Z - 300, Profile.PullUpHeight + ExpectedTurnDrop + ExpectedLeg, 0.1));
	TestTrue(TEXT("Dive lasts exactly 1.5s and reaches pull-up anchor"), Path.PositionAt(1.5).Equals(Path.PullUp, 0.01));
	TestTrue(TEXT("Both straight legs have 45 degree pitch"), FMath::IsNearlyEqual(Path.DirectionAt(0).Z, -UE_INV_SQRT_2, 0.0001)
		&& FMath::IsNearlyEqual(Path.DirectionAt(Path.TotalSeconds()).Z, UE_INV_SQRT_2, 0.0001));
	double Lowest = DBL_MAX;
	for (int32 I = 0; I <= 600; ++I) Lowest = FMath::Min(Lowest, Path.PositionAt(Path.TotalSeconds() * I / 600.0f).Z - 300);
	TestTrue(TEXT("Finite-radius turn bottoms out at the authored 100m clearance"),
		FMath::IsNearlyEqual(Lowest, Profile.PullUpHeight, 0.1));
	TestTrue(TEXT("Path exits at the declared final point"), Path.PositionAt(Path.TotalSeconds()).Equals(Path.Exit, 0.01));
	TestEqual(TEXT("Last of ten shots occurs at dive end"), GuLiWingmanAttack::ShotTime(9,10,1.5f),1.5f);
	TestEqual(TEXT("Single missile emits at dive start"), GuLiWingmanAttack::ShotTime(0,1,1.5f),0.0f);
	TestTrue(TEXT("Strip starts on target"), GuLiWingmanAttack::StripPoint(Path,12000,0,10).Equals(Path.Target));
	TestTrue(TEXT("Strip ends 120m along own approach"), GuLiWingmanAttack::StripPoint(Path,12000,9,10).Equals(Path.Target + FVector(12000,0,0)));
	TestEqual(TEXT("Five planes have at most ten shots in a 200ms batch"),Profile.MaximumShotsPerFlightBatch(),10);
	TArray<FVector> ApproachCandidates;
	for (int32 CandidateIndex = 0; CandidateIndex < GuLiWingmanAttack::MaximumGroundApproachCandidates; ++CandidateIndex)
	{
		const FVector Candidate = GuLiWingmanAttack::BuildGroundApproachCandidate(
			FVector::ForwardVector, 0u, CandidateIndex);
		TestTrue(TEXT("Every bounded ground approach is normalized"), Candidate.IsNormalized());
		TestFalse(TEXT("Every bounded ground approach is unique"), ApproachCandidates.ContainsByPredicate(
			[&](const FVector& Existing) { return Existing.Equals(Candidate, 0.001); }));
		ApproachCandidates.Add(Candidate);
	}
	TestTrue(TEXT("First ground approach remains the direct route"), ApproachCandidates[0].Equals(FVector::ForwardVector));
	TestTrue(TEXT("Stable seed mirrors the first alternative"),
		GuLiWingmanAttack::BuildGroundApproachCandidate(FVector::ForwardVector, 0u, 1).Y > 0.0
		&& GuLiWingmanAttack::BuildGroundApproachCandidate(FVector::ForwardVector, 1u, 1).Y < 0.0);
	TestEqual(TEXT("Eight relative plus eight world-stable approaches are available"),
		ApproachCandidates.Num(), 16);
	TestTrue(TEXT("World-stable fallbacks fill the 22.5 degree gaps"),
		FMath::IsNearlyEqual(FMath::RadiansToDegrees(FMath::Atan2(
			ApproachCandidates[8].Y, ApproachCandidates[8].X)), 22.5f, 0.01f));
	TestTrue(TEXT("Setup point supplies the complete six-second lead-in"),
		GuLiWingmanAttack::GroundRunSetupPoint(Path).Equals(
			Path.Entry - Path.DirectionAt(0.0f)
				* (Path.Speed * GuLiWingmanAttack::GroundIngressLeadSeconds), 0.01));
	TestTrue(TEXT("Out-of-range ground approach index fails closed"),
		GuLiWingmanAttack::BuildGroundApproachCandidate(FVector::ForwardVector, 0u,
			GuLiWingmanAttack::MaximumGroundApproachCandidates).IsNearlyZero());
	FGuLiWingmanGroundRunPath Oblique;
	GuLiWingmanAttack::BuildGroundPath(FVector(150, 250, 350), FVector(1, 2, 0), Profile, 20, Oblique);
	const FGuLiWingmanGroundRunPath BeforeFreeze = Oblique;
	TestTrue(TEXT("Freezing a run in place preserves each aircraft's approach"),
		GuLiWingmanAttack::BuildGroundPath(Oblique.Target, Oblique.Direction, Profile, 20, Oblique)
		&& Oblique.Entry.Equals(BeforeFreeze.Entry, 0.001) && Oblique.Direction.Equals(BeforeFreeze.Direction, 0.001));
	Profile.PullUpHeight=5000;
	TestTrue(TEXT("Minimum 50m clearance raises the turn anchors instead of penetrating terrain"),
		GuLiWingmanAttack::BuildGroundPath(FVector::ZeroVector,FVector::ForwardVector,Profile,20,Path));
	Lowest = DBL_MAX;
	for (int32 I = 0; I <= 600; ++I)
		Lowest = FMath::Min(Lowest, Path.PositionAt(Path.TotalSeconds() * I / 600.0f).Z);
	TestTrue(TEXT("Minimum authored clearance is preserved across the complete turn"),
		FMath::IsNearlyEqual(Lowest, GuLiWingmanAttack::MinimumGroundHeight, 0.1));
	Profile.PullUpHeight=10000; Profile.MissileCount=64;
	TestFalse(TEXT("A source row exceeding bounded packet capacity is rejected"),Profile.IsWellFormed());
	auto* SourceTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons.DT_GuLiStrikeShip_WingmanWeapons"));
	const auto* SourceRow = SourceTable ? SourceTable->FindRow<FGuLiStrikeShipWingmanWeaponsRow>(TEXT("WingmanGroundMissile"), TEXT("Wingman attack acceptance")) : nullptr;
	if (!TestNotNull(TEXT("Authored attack source table is deployed"), SourceRow)) return false;
	TestEqual(TEXT("Authored default is ten missiles"), SourceRow->MissileCount, 10);
	TestEqual(TEXT("Authored default dive is 1.5 seconds"), SourceRow->DiveSeconds, 1.5f);
	auto* TransientTable = NewObject<UDataTable>();
	TransientTable->RowStruct = FGuLiStrikeShipWingmanWeaponsRow::StaticStruct();
	auto EditedRow = *SourceRow; EditedRow.MissileCount = 6; EditedRow.DiveSeconds = 2.0f;
	TransientTable->AddRow(TEXT("Changed"), EditedRow);
	auto* Definition = NewObject<UGuLiWingmanWeaponDefinition>();
	Definition->AttackProfileRow.DataTable = TransientTable; Definition->AttackProfileRow.RowName = TEXT("Changed");
	FGuLiWingmanWeaponRuntimeConfig Resolved;
	TestTrue(TEXT("A changed source row resolves through the real weapon definition"), Definition->BuildRuntimeConfig(Resolved));
	TestEqual(TEXT("Missile count comes from the changed table"), Resolved.Attack.MissileCount, 6);
	TestEqual(TEXT("Duration comes from the changed table"), Resolved.Attack.DiveSeconds, 2.0f);
	TestEqual(TEXT("New cadence ends at the configured duration"), GuLiWingmanAttack::ShotTime(5, Resolved.Attack.MissileCount, Resolved.Attack.DiveSeconds), 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAirRunTest, "GuLiStrike.Wingman.Attack.ThreeDimensionalDogfightStateMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanAirRunTest::RunTest(const FString&)
{
	using namespace GuLiWingmanAttack;
	FGuLiWingmanAttackProfile Profile;
	Profile.Pattern = EGuLiWingmanAttackPattern::AirDogfight;
	Profile.ExecutorId = TEXT("WingmanMachineGun");
	TestTrue(TEXT("Default dogfight profile is valid"), Profile.IsWellFormed());

	EGuLiWingmanAttackPhase Phase = EGuLiWingmanAttackPhase::AirApproachFire;
	Phase = NextAirDogfightPhase(Phase);
	TestEqual(TEXT("Approach enters breakaway turn"), Phase, EGuLiWingmanAttackPhase::AirBreakawayTurn);
	Phase = NextAirDogfightPhase(Phase);
	TestEqual(TEXT("Breakaway turn enters retreat"), Phase, EGuLiWingmanAttackPhase::AirRetreat);
	Phase = NextAirDogfightPhase(Phase);
	TestEqual(TEXT("Retreat enters return turn"), Phase, EGuLiWingmanAttackPhase::AirReturnTurn);
	Phase = NextAirDogfightPhase(Phase);
	TestEqual(TEXT("Return turn closes the loop through approach"), Phase, EGuLiWingmanAttackPhase::AirApproachFire);
	TestTrue(TEXT("Machine gun can queue in approach"), CanQueueAirGun(EGuLiWingmanAttackPhase::AirApproachFire));
	for (EGuLiWingmanAttackPhase NonFiring : {EGuLiWingmanAttackPhase::AirBreakawayTurn,
		EGuLiWingmanAttackPhase::AirRetreat, EGuLiWingmanAttackPhase::AirReturnTurn})
		TestFalse(TEXT("Machine gun is blocked outside approach"), CanQueueAirGun(NonFiring));

	TestTrue(TEXT("Nose-on target can fire"),IsInsideForwardArc(FVector::ZeroVector,FVector::ForwardVector,FVector(1000,0,0),0,2000,20));
	TestFalse(TEXT("Side target cannot fire"),IsInsideForwardArc(FVector::ZeroVector,FVector::ForwardVector,FVector(0,1000,0),0,2000,20));
	TestFalse(TEXT("Rear target cannot fire"),IsInsideForwardArc(FVector::ZeroVector,FVector::ForwardVector,FVector(-1000,0,0),0,2000,20));

	const FVector Position(-40000, 0, 0), Target = FVector::ZeroVector, Ship(200000, 0, 0);
	const uint32 Seed = MakeAirManeuverSeed(17, 3, 1, 900, BreakawayTurnSalt, 0);
	const FVector Retreat = BuildRetreatCandidate(Position, Target, Ship, 33000, Profile, Seed);
	const double Longitudinal = Retreat.X;
	const double Ellipse = FMath::Square(Retreat.Y / Profile.RetreatLateralRadius)
		+ FMath::Square(Retreat.Z / Profile.RetreatVerticalRadius);
	TestTrue(TEXT("Retreat point uses the 35-60 percent target-to-Ship interval"),
		Longitudinal >= 70000.0 && Longitudinal <= 120000.0);
	TestTrue(TEXT("Retreat point lies in the authored 3D elliptical cross section"), Ellipse <= 1.0001);
	const FVector ShortCorridorRetreat = BuildRetreatCandidate(Position, Target, FVector(30000,0,0),
		33000, Profile, Seed);
	TestTrue(TEXT("Short corridor extends through and behind Ship to the 450m minimum"),
		ShortCorridorRetreat.X >= Profile.RetreatMinimumDistance && ShortCorridorRetreat.X > 30000.0);
	const FVector FrozenRetreat = Retreat;
	const FVector RebuiltAfterMovement = BuildRetreatCandidate(Position, FVector(10000,0,0), FVector(250000,0,0),
		33000, Profile, Seed);
	TestTrue(TEXT("A run keeps its frozen retreat point while live endpoints move"), FrozenRetreat.Equals(Retreat));
	TestFalse(TEXT("Resampling against moved endpoints would be a different point"), FrozenRetreat.Equals(RebuiltAfterMovement, 0.01));

	FGuLiWingmanAirTurnPlan Turn, Repeat, NextRound;
	TestTrue(TEXT("Deterministic turn plan builds"), BuildAirTurnPlan(Position, Retreat, 4500, 20, Profile, Seed, Turn));
	TestTrue(TEXT("Same entry seed reproduces the exact control point"),
		BuildAirTurnPlan(Position, Retreat, 4500, 20, Profile, Seed, Repeat)
		&& Repeat.ControlPoint.Equals(Turn.ControlPoint, 0.001)
		&& Repeat.SignedYawDegrees == Turn.SignedYawDegrees && Repeat.PitchDegrees == Turn.PitchDegrees);
	const uint32 NextSeed = MakeAirManeuverSeed(17, 3, 2, 930, BreakawayTurnSalt, 0);
	TestTrue(TEXT("A later state entry builds another legal turn"),
		BuildAirTurnPlan(Position, Retreat, 4500, 20, Profile, NextSeed, NextRound));
	TestFalse(TEXT("Different rounds do not reuse the same turn"), NextRound.ControlPoint.Equals(Turn.ControlPoint, 0.001));
	TestTrue(TEXT("Random yaw respects authored magnitude bounds"),
		FMath::Abs(Turn.SignedYawDegrees) >= 40.0f && FMath::Abs(Turn.SignedYawDegrees) <= 90.0f);
	TestTrue(TEXT("Random pitch respects authored bounds"), FMath::Abs(Turn.PitchDegrees) <= 30.0f);
	const float PhysicalTurnRadius = 4500.0f / FMath::DegreesToRadians(20.0f);
	TestTrue(TEXT("Control point is two physical turn radii away"),
		FMath::IsNearlyEqual(FVector::Distance(Turn.Origin, Turn.ControlPoint), 2.0f * PhysicalTurnRadius, 0.1f));
	bool bSawLeft = false, bSawRight = false, bSawClimb = false, bSawDive = false;
	for (uint32 Entry = 1; Entry <= 64; ++Entry)
	{
		FGuLiWingmanAirTurnPlan Sample;
		const uint32 SampleSeed = MakeAirManeuverSeed(17 + Entry, 3, Entry, 900 + Entry, ReturnTurnSalt, 0);
		if (!BuildAirTurnPlan(Position, Retreat, 4500, 20, Profile, SampleSeed, Sample)) continue;
		bSawLeft |= Sample.SignedYawDegrees < 0; bSawRight |= Sample.SignedYawDegrees > 0;
		bSawClimb |= Sample.PitchDegrees > 0; bSawDive |= Sample.PitchDegrees < 0;
	}
	TestTrue(TEXT("Deterministic entries cover left and right turns"), bSawLeft && bSawRight);
	TestTrue(TEXT("Deterministic entries cover climbs and dives"), bSawClimb && bSawDive);
	TestFalse(TEXT("Turn remains active before reaching its point or timeout"),
		ShouldFinishAirTurn(Turn.Origin, Turn, Profile.ManeuverArrivalRadius, 9.99));
	TestTrue(TEXT("Ten-second timeout switches the turn to direct guidance"),
		ShouldFinishAirTurn(Turn.Origin, Turn, Profile.ManeuverArrivalRadius, 10.0));
	TestTrue(TEXT("Passing a control point completes the turn"),
		ShouldFinishAirTurn(Turn.ControlPoint + (Turn.ControlPoint - Turn.Origin).GetSafeNormal() * 100,
			Turn, Profile.ManeuverArrivalRadius, 1.0));

	auto* SourceTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons.DT_GuLiStrikeShip_WingmanWeapons"));
	const auto* SourceRow = SourceTable ? SourceTable->FindRow<FGuLiStrikeShipWingmanWeaponsRow>(
		TEXT("WingmanMachineGun"), TEXT("Dogfight source acceptance")) : nullptr;
	if (!TestNotNull(TEXT("Dogfight source table is deployed"), SourceRow)) return false;
	TestEqual(TEXT("Authored attack pattern is AirDogfight"), SourceRow->AttackPattern, FString(TEXT("AirDogfight")));
	TestEqual(TEXT("Authored breakaway distance is 300m"), SourceRow->BreakawayDistanceCentimeters, 30000.0f);
	TestEqual(TEXT("Authored retreat minimum is 450m"), SourceRow->RetreatMinimumDistanceCentimeters, 45000.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAttackWireTest, "GuLiStrike.Wingman.Attack.BoundedCapturedFireWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanAttackWireTest::RunTest(const FString&)
{
	FGuLiWingmanCandidateBatch Batch;
	Batch.MatchEpoch=1; Batch.ConnectionGeneration=1; Batch.Group.ShipInstanceId=FGuid(1,2,3,4);
	Batch.Group.ShipGeneration=1; Batch.Group.GroupGeneration=1; Batch.LeaseEpoch=1; Batch.RosterRevision=1;
	Batch.FlightIndex=0; Batch.RequiredMemberMask=1; Batch.ObservedGrantRevision=1; Batch.CandidateSequence=1;
	Batch.FrameSequence=1; Batch.ClientSimTick=30; Batch.CaptureEstimatedServerTimeSeconds=1;
	Batch.NavSchemaRevision=1; Batch.NavDataChecksum=1; Batch.TuningRevision=1; Batch.ObstacleRevision=1;
	Batch.CarrierSource.CanonicalEpoch=1; Batch.CarrierSource.MoveRevision=1; Batch.AbilitySetRevision=1;
	Batch.FormationCommandRevision=1; Batch.FormationDefinitionChecksum=1;
	auto& Sample=Batch.Samples.AddDefaulted_GetRef(); Sample.Wingman.Flight.Group=Batch.Group;
	Sample.Wingman.Flight.FlightIndex=0; Sample.Wingman.MemberIndex=0; Sample.Wingman.EntityGeneration=1;
	auto& Shot=Batch.AttackFireRecords.AddDefaulted_GetRef(); Shot.MemberIndex=0; Shot.ClientSimTick=30;
	Shot.SlotId=TEXT("GroundWeapon"); Shot.ProfileRevision=1; Shot.LoadoutRevision=1; Shot.RunId=30;
	Shot.Target.Target.Kind=EGuLiTargetKind::Ship; Shot.Target.Target.AuthorityId=FGuid(5,6,7,8); Shot.Target.Target.Generation=1;
	Shot.Target.Revision=1; Shot.Target.bGround=true;
	TestTrue(TEXT("Shot references complete current pose"),Batch.IsWellFormed());
	FNetBitWriter Writer(nullptr,65536); bool Success=false; Batch.NetSerialize(Writer,nullptr,Success);
	TestTrue(TEXT("Bounded batch serializes"),Success);
	FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits()); FGuLiWingmanCandidateBatch Copy;
	Copy.NetSerialize(Reader,nullptr,Success); TestTrue(TEXT("Bounded batch round-trips"),Success);
	TestEqual(TEXT("Fire record participates in stable payload hash"),Copy.ComputeStablePayloadHash(),Batch.ComputeStablePayloadHash());
	Copy.AttackFireRecords[0].ShotIndex=1; TestNotEqual(TEXT("Changing an ordinal changes payload hash"),Copy.ComputeStablePayloadHash(),Batch.ComputeStablePayloadHash());
	Copy=Batch; Copy.AttackFireRecords[0].ClientSimTick=29;
	TestFalse(TEXT("No shot may invent an uncaptured pose tick"),Copy.IsWellFormed());
	Copy=Batch; while(Copy.AttackFireRecords.Num()<17) Copy.AttackFireRecords.Add(Batch.AttackFireRecords[0]);
	TestFalse(TEXT("Per-flight batch is capped at 16 records"),Copy.IsWellFormed());
	FGuLiWingmanAttackTarget EmptyTarget;
	FNetBitWriter EmptyWriter(nullptr,4096); EmptyTarget.NetSerialize(EmptyWriter,nullptr,Success);
	TestTrue(TEXT("An empty replicated target is a legal no-target command"),Success);
	FNetBitReader EmptyReader(nullptr,EmptyWriter.GetData(),EmptyWriter.GetNumBits());
	FGuLiWingmanAttackTarget EmptyCopy=Shot.Target; EmptyCopy.NetSerialize(EmptyReader,nullptr,Success);
	TestTrue(TEXT("Clearing a target survives replication"),Success && !EmptyCopy.Target.IsValid());
	FGuLiWingmanAttackCheckpoint Checkpoint;
	Checkpoint.Emitter=Sample.Wingman; Checkpoint.SlotId=Shot.SlotId; Checkpoint.SkillId=TEXT("Wingman.GroundMissile");
	Checkpoint.FrozenTargetHandle=Shot.Target.Target; Checkpoint.DefinitionChecksum=7531;
	Checkpoint.ProfileRevision=1; Checkpoint.RunId=30; Checkpoint.LeaseEpoch=1; Checkpoint.LastShotIndex=7;
	Checkpoint.StartTime=27.456789123; Checkpoint.NextFireTime=35.456789123;
	Checkpoint.FrozenTarget=FVector(125.123456789,346.987654321,6499.593639);
	Checkpoint.ApproachDirection=FVector(1,2,0).GetSafeNormal();
	FNetBitWriter CheckpointWriter(nullptr,4096); Checkpoint.NetSerialize(CheckpointWriter,nullptr,Success);
	TestTrue(TEXT("Authority checkpoint serializes"),Success);
	FNetBitReader CheckpointReader(nullptr,CheckpointWriter.GetData(),CheckpointWriter.GetNumBits());
	FGuLiWingmanAttackCheckpoint CheckpointCopy; CheckpointCopy.NetSerialize(CheckpointReader,nullptr,Success);
	FGuLiWingmanAttackAuthorityState Before,After; Before.Checkpoints.Add(Checkpoint); After.Checkpoints.Add(CheckpointCopy);
	TestTrue(TEXT("Takeover checkpoint preserves cooldown and frozen geometry hash exactly"),Success && Before.ComputeStableHash()==After.ComputeStableHash());
	After.Checkpoints[0].DefinitionChecksum++;
	TestNotEqual(TEXT("A weapon replacement invalidates the prior attack checkpoint"),Before.ComputeStableHash(),After.ComputeStableHash());
	return true;
}
#endif

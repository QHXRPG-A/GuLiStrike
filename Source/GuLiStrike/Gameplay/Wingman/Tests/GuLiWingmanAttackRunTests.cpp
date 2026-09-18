#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Data/Generated/GuLiStrikeShipTableRows.h"
#include "Misc/AutomationTest.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeSpellFieldsTableRows.h"
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
	TestTrue(TEXT("Ground ingress has no fixed speed-scaled lead-in"),
		GuLiWingmanAttack::GroundRunSetupPoint(Path).Equals(Path.Entry, 0.01));
	TestTrue(TEXT("Out-of-range ground approach index fails closed"),
		GuLiWingmanAttack::BuildGroundApproachCandidate(FVector::ForwardVector, 0u,
			GuLiWingmanAttack::MaximumGroundApproachCandidates).IsNearlyZero());
	FGuLiWingmanGroundRunPath Oblique;
	GuLiWingmanAttack::BuildGroundPath(FVector(150, 250, 350), FVector(1, 2, 0), Profile, 20, Oblique);
	const FGuLiWingmanGroundRunPath BeforeFreeze = Oblique;
	TestTrue(TEXT("Freezing a run in place preserves each aircraft's approach"),
		GuLiWingmanAttack::BuildGroundPath(Oblique.Target, Oblique.Direction, Profile, 20, Oblique)
		&& Oblique.Entry.Equals(BeforeFreeze.Entry, 0.001) && Oblique.Direction.Equals(BeforeFreeze.Direction, 0.001));
	Profile.PullUpHeight=1000;
	TestTrue(TEXT("Minimum 50m clearance raises the turn anchors instead of penetrating terrain"),
		GuLiWingmanAttack::BuildGroundPath(FVector::ZeroVector,FVector::ForwardVector,Profile,20,Path));
	Lowest = DBL_MAX;
	for (int32 I = 0; I <= 600; ++I)
		Lowest = FMath::Min(Lowest, Path.PositionAt(Path.TotalSeconds() * I / 600.0f).Z);
	TestTrue(TEXT("Minimum authored clearance is preserved across the complete turn"),
		FMath::IsNearlyEqual(Lowest, GuLiWingmanAttack::MinimumGroundHeight, 0.1));
	Profile.PullUpHeight=2000; Profile.MissileCount=64;
	TestFalse(TEXT("A source row exceeding bounded packet capacity is rejected"),Profile.IsWellFormed());
	auto* SourceTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons.DT_GuLiStrikeShip_WingmanWeapons"));
	const auto* SourceRow = SourceTable ? SourceTable->FindRow<FGuLiStrikeShipWingmanWeaponsRow>(TEXT("WingmanGroundMissile"), TEXT("Wingman attack acceptance")) : nullptr;
	if (!TestNotNull(TEXT("Authored attack source table is deployed"), SourceRow)) return false;
	TestEqual(TEXT("Authored default is ten missiles"), SourceRow->MissileCount, 10);
	TestEqual(TEXT("Authored default dive is 1.5 seconds"), SourceRow->DiveSeconds, 1.5f);
	TestEqual(TEXT("Authored ground attack flight speed is doubled"),
		SourceRow->FlightSpeedCentimetersPerSecond, 1800.0f);
	FGuLiSpellFieldConfig Field;
	TestTrue(TEXT("Ship ground weapon links the global field row"),
		UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(FName(*SourceRow->EffectConfigId), Field));
	TestEqual(TEXT("Global ground blast radius is 40m"), Field.Radius, 800.0f);
	TestEqual(TEXT("Ground damage is authored only in the global table"), SourceRow->Damage, 0.0f);
	auto* TransientTable = NewObject<UDataTable>();
	TransientTable->RowStruct = FGuLiStrikeShipWingmanWeaponsRow::StaticStruct();
	auto EditedRow = *SourceRow; EditedRow.MissileCount = 6; EditedRow.DiveSeconds = 2.0f;
	TransientTable->AddRow(TEXT("Changed"), EditedRow);
	auto* Definition = NewObject<UGuLiWingmanWeaponDefinition>();
	Definition->AttackProfileRow.DataTable = TransientTable; Definition->AttackProfileRow.RowName = TEXT("Changed");
	FGuLiWingmanWeaponRuntimeConfig Resolved;
	TestTrue(TEXT("A changed source row resolves through the real weapon definition"), Definition->BuildRuntimeConfig(Resolved));
	TestEqual(TEXT("Missile count comes from the changed table"), Resolved.Attack.MissileCount, 6);
	TestEqual(TEXT("Resolved damage uses the global field"), Resolved.Damage, Field.Damage);
	TestEqual(TEXT("Resolved radius uses the global field"), Resolved.Attack.ExplosionRadius, Field.Radius);
	auto* Fields = GetDefault<UGuLiSpellFieldDataSettings>()->DataTable.LoadSynchronous();
	auto* Authored = Fields->FindRow<FGuLiStrikeSpellFieldsFieldsRow>(Field.ConfigId, TEXT("Global field edit acceptance"));
	const auto Original = *Authored;
	Authored->Damage = 47; Authored->RadiusCentimeters = 3200;
	FGuLiWingmanWeaponRuntimeConfig ChangedField;
	const bool bChanged = Definition->BuildRuntimeConfig(ChangedField);
	*Authored = Original;
	TestTrue(TEXT("Editing the global field changes the real wingman weapon without editing Ship"), bChanged);
	TestEqual(TEXT("Global damage edit reaches wingman"), ChangedField.Damage, 47.0f);
	TestEqual(TEXT("Global radius edit reaches wingman"), ChangedField.Attack.ExplosionRadius, 3200.0f);
	TestEqual(TEXT("Duration comes from the changed table"), Resolved.Attack.DiveSeconds, 2.0f);
	TestEqual(TEXT("New cadence ends at the configured duration"), GuLiWingmanAttack::ShotTime(5, Resolved.Attack.MissileCount, Resolved.Attack.DiveSeconds), 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAirRunTest, "GuLiStrike.Wingman.Attack.AirBurstOrbitStateMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanAirRunTest::RunTest(const FString&)
{
	using namespace GuLiWingmanAttack;
	FGuLiWingmanAttackProfile Profile;
	Profile.Pattern = EGuLiWingmanAttackPattern::AirBurstOrbit;
	Profile.ExecutorId = TEXT("WingmanMachineGun");
	TestEqual(TEXT("Burst-start wire semantics require protocol version fourteen"),
		GULI_WINGMAN_PROTOCOL_VERSION, 14u);
	TestTrue(TEXT("Default burst-orbit profile is valid"), Profile.IsWellFormed());
	TestEqual(TEXT("Below 100m first separates"), SelectAirEntryPhase(1999.0f, Profile),
		EGuLiWingmanAttackPhase::AirSeparate);
	TestEqual(TEXT("Exactly 100m can start a burst"), SelectAirEntryPhase(2000.0f, Profile),
		EGuLiWingmanAttackPhase::AirApproachFire);
	TestFalse(TEXT("Exactly 50m keeps firing"), ShouldEndAirBurst(1000.0f, 1.0, Profile));
	TestTrue(TEXT("Strictly below 50m stops firing"), ShouldEndAirBurst(999.0f, 1.0, Profile));
	TestFalse(TEXT("Burst remains active before five seconds"), ShouldEndAirBurst(1600.0f, 4.999, Profile));
	TestTrue(TEXT("Burst ends at five seconds"), ShouldEndAirBurst(1600.0f, 5.0, Profile));
	TestFalse(TEXT("Cooldown does not start outside the 520m orbit"),
		ShouldBeginAirOrbitCooldown(52000.1f, 52000.0f));
	TestTrue(TEXT("Cooldown starts on the 520m boundary"),
		ShouldBeginAirOrbitCooldown(52000.0f, 52000.0f));
	TestFalse(TEXT("A full three seconds is required in orbit"),
		IsAirOrbitCooldownComplete(2.999, Profile));
	TestTrue(TEXT("Three seconds in orbit completes cooldown"),
		IsAirOrbitCooldownComplete(3.0, Profile));
	TestEqual(TEXT("Five-hertz burst includes the shot at t=0"),
		AirBurstShotsDue(0.2f, 5.0f, 0.0), 1);
	TestEqual(TEXT("Five-hertz five-second half-open burst has 25 shots"),
		AirBurstShotsDue(0.2f, 5.0f, 5.0), 25);
	TestEqual(TEXT("Thirty-hertz five-second half-open burst has 150 shots"),
		AirBurstShotsDue(MinimumAirShotIntervalSeconds, 5.0f, 5.0), 150);
	TestEqual(TEXT("A rate above 30Hz is invalid rather than clamped"),
		AirBurstShotsDue(0.03f, 5.0f, 5.0), 0);
	FGuLiWingmanWeaponRuntimeConfig Runtime;
	Runtime.Attack = Profile;
	Runtime.Damage = 10.0f;
	Runtime.CooldownSeconds = MinimumAirShotIntervalSeconds;
	TestTrue(TEXT("Exactly 30Hz is a valid sustained-fire configuration"), Runtime.IsWellFormed());
	Runtime.CooldownSeconds = 0.03f;
	TestFalse(TEXT("A sustained-fire configuration above 30Hz is invalid"), Runtime.IsWellFormed());

	auto* SourceTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons.DT_GuLiStrikeShip_WingmanWeapons"));
	const auto* SourceRow = SourceTable ? SourceTable->FindRow<FGuLiStrikeShipWingmanWeaponsRow>(
		TEXT("WingmanMachineGun"), TEXT("Burst-orbit source acceptance")) : nullptr;
	if (!TestNotNull(TEXT("Burst-orbit source table is deployed"), SourceRow)) return false;
	TestEqual(TEXT("Authored attack pattern is AirBurstOrbit"), SourceRow->AttackPattern, FString(TEXT("AirBurstOrbit")));
	TestEqual(TEXT("Authored air attack flight speed is doubled"),
		SourceRow->FlightSpeedCentimetersPerSecond, 1800.0f);
	TestEqual(TEXT("Authored fire start distance is 100m"), SourceRow->AirFireStartDistanceCentimeters, 2000.0f);
	TestEqual(TEXT("Authored fire stop distance is 50m"), SourceRow->AirFireStopDistanceCentimeters, 1000.0f);
	TestEqual(TEXT("Authored burst duration is five seconds"), SourceRow->AirBurstDurationSeconds, 5.0f);
	TestEqual(TEXT("Authored orbit cooldown is three seconds"), SourceRow->AirOrbitCooldownSeconds, 3.0f);
	TestEqual(TEXT("Authored machine-gun logical interval is 0.2 seconds"), SourceRow->CooldownSeconds, 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanNativeV3AttackFallbackTest,
	"GuLiStrike.Wingman.Attack.NativeV3SpeedRadiusAndProjectile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanNativeV3AttackFallbackTest::RunTest(const FString&)
{
	const UGuLiShipAbilitySet* Set = UGuLiShipAbilitySet::CreateNativeV3Transient(GetTransientPackage());
	if (!TestNotNull(TEXT("Native V3 ability set exists"), Set)) return false;
	const UGuLiWingmanWeaponDefinition* MachineGun = nullptr;
	const UGuLiWingmanWeaponDefinition* GroundMissile = nullptr;
	for (const FGuLiShipAbilityGrant& Grant : Set->Grants)
	{
		if (Grant.AbilityId == TAG_GuLi_ShipAbility_Weapon_Wingman_MachineGun)
		{
			MachineGun = Grant.WeaponDefinition;
		}
		else if (Grant.AbilityId == TAG_GuLi_ShipAbility_Weapon_Wingman_GroundMissile)
		{
			GroundMissile = Grant.WeaponDefinition;
		}
	}
	if (!TestNotNull(TEXT("Native V3 machine-gun definition exists"), MachineGun)
		|| !TestNotNull(TEXT("Native V3 ground-missile definition exists"), GroundMissile))
	{
		return false;
	}
	TestEqual(TEXT("Native V3 catalog carries ability-set revision four"), Set->Revision, 4u);
	TestEqual(TEXT("Machine-gun definition revision advances"), MachineGun->Revision, 3u);
	TestEqual(TEXT("Ground-missile definition revision advances"), GroundMissile->Revision, 2u);
	TestEqual(TEXT("Native machine gun uses burst-orbit"), MachineGun->Attack.Pattern,
		EGuLiWingmanAttackPattern::AirBurstOrbit);
	TestEqual(TEXT("Native machine-gun logical interval is 0.2 seconds"), MachineGun->CooldownSeconds, 0.2f);
	TestEqual(TEXT("Native machine-gun burst lasts five seconds"), MachineGun->Attack.AirBurstDurationSeconds, 5.0f);
	TestEqual(TEXT("Native machine-gun attack speed is doubled"), MachineGun->Attack.FlightSpeed, 1800.0f);
	TestEqual(TEXT("Native ground-run attack speed is doubled"), GroundMissile->Attack.FlightSpeed, 1800.0f);
	TestEqual(TEXT("Native ground blast radius is five times the shared field radius"),
		GroundMissile->Attack.ExplosionRadius, 800.0f);
	TestEqual(TEXT("Ground attack uses the Wingman-only projectile definition"),
		GroundMissile->AttackProjectile.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile.DA_WingmanGroundMissile")));
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

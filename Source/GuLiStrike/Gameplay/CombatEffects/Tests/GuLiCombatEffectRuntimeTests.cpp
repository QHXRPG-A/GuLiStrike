#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Components/DecalComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "UObject/CoreNet.h"

namespace GuLiCombatEffectTests
{
	struct FTarget { FGuLiCombatTargetSnapshot Snapshot; };
	struct FFixture
	{
		TArray<TUniquePtr<FTarget>> Targets;
		UWorld* World = nullptr;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UGuLiCombatEffectRuntimeSubsystem* Runtime = nullptr;
		bool Initialize(FAutomationTestBase& Test)
		{
			if (!GEngine) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("isolated game world"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
			Runtime = World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
			return Test.TestNotNull(TEXT("independent runtime"), Runtime) && Ledger && Ledger->BindMatchEpoch(9);
		}
		FTarget& Add(EGuLiTeam Team, FVector Position, bool bMass = false, float Radius = 50)
		{
			auto Target = MakeUnique<FTarget>(); FTarget* Ptr = Target.Get();
			const uint32 Id = Targets.Num() + 1;
			Ptr->Snapshot.Handle = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, Id);
			if (!bMass)
			{
				Ptr->Snapshot.Handle.Kind = EGuLiTargetKind::Ship;
				Ptr->Snapshot.Handle.AuthorityId = FGuid(0, 0, 0, Id);
				Ptr->Snapshot.Handle.LocalId = 0;
			}
			Ptr->Snapshot.Team = Team; Ptr->Snapshot.Location = Position; Ptr->Snapshot.CollisionRadius = Radius;
			Ptr->Snapshot.Health = 100; Ptr->Snapshot.bAlive = true;
			FGuLiCombatTargetAdapter Adapter; Adapter.LifetimeOwner = World;
			Adapter.ReadSnapshot = [Ptr](FGuLiCombatTargetSnapshot& Out) { Out = Ptr->Snapshot; return true; };

			check(Ledger->RegisterTarget(Ptr->Snapshot.Handle, MoveTemp(Adapter)));
			Targets.Add(MoveTemp(Target)); return *Ptr;
		}
		FGuLiCombatEffectContext Context(const FTarget& Source, const FTarget* Target = nullptr)
		{
			FGuLiCombatEffectContext Value; Value.Source = Source.Snapshot.Handle;
			if (Target) Value.Target = Target->Snapshot.Handle;
			Value.Damage = 10; Value.MatchEpoch = 9; Value.SkillId = TEXT("TestSpell");
			return Value;
		}
		~FFixture()
		{
			if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCombatEffectMathTest, "GuLiStrike.CombatEffects.MathAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCombatEffectMathTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffects;
	FGuLiCombatEffectState A; A.RandomSeed = 142;
	A.LaunchLocation = FVector(100, 200, 50); A.Location = A.LaunchLocation;
	A.LastTargetLocation = FVector(6000, 0, 50); A.LaunchDirection = FVector::ForwardVector;
	FGuLiCombatEffectState B = A;
	FGuLiCombatEffectState Cached = A;
	FGuLiProjectileCurveCoefficients Coefficients; Coefficients.Initialize(Cached);
	for (int32 Step = 1; Step < 45; ++Step)
	{
		AdvanceProjectile(A, Step / 30.0f, 1 / 30.0f); AdvanceProjectile(B, Step / 30.0f, 1 / 30.0f);
		AdvanceProjectile(Cached, Step / 30.0f, 1 / 30.0f, &Coefficients);
		TestTrue(TEXT("same seed and input reproduce every flight sample"), FVector(A.Location).Equals(B.Location, 0.001));
		TestTrue(TEXT("frozen coefficients preserve trajectory and velocity"), FVector(A.Location).Equals(Cached.Location, 0.001) && FVector(A.Velocity).Equals(Cached.Velocity, 0.001));
	}
	// Exercise the curved branch and both launch-normalization fallbacks in this existing case.
	for (int32 Seed : {0, -421, 142}) for (bool bZeroDirection : {false, true})
	{
		FGuLiCombatEffectState Original;
		Original.RandomSeed = Seed; Original.LastTargetLocation = FVector(6000,2000,50);
		Original.LaunchDirection = bZeroDirection ? FVector::ZeroVector : FVector(1,2,.3);
		Original.Motion.VerticalCurve = 300; Original.Motion.LongitudinalCurve = 200;
		auto Frozen = Original; FGuLiProjectileCurveCoefficients Curve; Curve.Initialize(Frozen);
		for (int32 Step = 1; Step < 128; ++Step)
		{
			Original.LastTargetLocation = Frozen.LastTargetLocation = FVector(6000,2000+Step*3,50);
			AdvanceProjectile(Original, Step / 30.f, 1 / 30.f);
			AdvanceProjectile(Frozen, Step / 30.f, 1 / 30.f, &Curve);
			TestTrue(TEXT("cached seeded curves retain moving-target samples"), FVector(Original.Location).Equals(Frozen.Location,.001) && FVector(Original.Velocity).Equals(Frozen.Velocity,.001));
		}
	}
	B.RandomSeed = 421;
	TestFalse(TEXT("different seeds vary the lift"), LiftPosition(A, 0.25f).Equals(LiftPosition(B, 0.25f)));
	TestTrue(TEXT("lift starts at the authored launch point"), LiftPosition(A, 0).Equals(A.LaunchLocation));
	TestTrue(TEXT("lift raises the missile"), LiftPosition(A, 0.25f).Z > A.LaunchLocation.Z + 100);
	TestEqual(TEXT("instant is not due before activation"), PulsesDue(EGuLiSpellFieldTiming::Instant, 1, 0, 1, .99), 0);
	TestEqual(TEXT("instant remains one pulse"), PulsesDue(EGuLiSpellFieldTiming::Instant, 1, 0, 1, 99), 1);
	TestEqual(TEXT("delayed activates at the exact boundary"), PulsesDue(EGuLiSpellFieldTiming::Delayed, 2, 0, 1, 2), 1);
	TestEqual(TEXT("periodic includes activation"), PulsesDue(EGuLiSpellFieldTiming::Periodic, 2, 2, .5, 2), 1);
	TestEqual(TEXT("periodic excludes a pulse at the exclusive end"), PulsesDue(EGuLiSpellFieldTiming::Periodic, 2, 2, .5, 4), 4);
	FGuLiCombatTargetSnapshot Target; Target.Handle = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, 2);
	Target.bAlive = true; Target.Location = FVector(850, 0, 0); Target.CollisionRadius = 50;
	TestTrue(TEXT("sphere intersects target collision radius at the boundary"), IntersectsSphere(FVector::ZeroVector, 800, Target));
	Target.Location.X += .01;
	TestFalse(TEXT("just outside is excluded"), IntersectsSphere(FVector::ZeroVector, 800, Target));
	const FGuid Effect(1, 2, 3, 4);
	TestEqual(TEXT("one target and pulse have a stable damage identity"), DamageId(Effect, 0, Target.Handle), DamageId(Effect, 0, Target.Handle));
	TestNotEqual(TEXT("different pulses stack"), DamageId(Effect, 0, Target.Handle), DamageId(Effect, 1, Target.Handle));
	// Instant tracers use two full endpoints, not velocity integration. Check local scale, pivot and yaw.
	const FTransform Pose(FRotator(0, 90, 0), FVector(100, 200, 30), FVector(2));
	TestTrue(TEXT("muzzle transform includes scale and pivot"), Pose.TransformPosition(FVector(50, 10, 20)).Equals(FVector(80, 300, 70), .001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSpellFieldTableConfigTest, "GuLiStrike.CombatEffects.SpellFieldTableConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSpellFieldTableConfigTest::RunTest(const FString& Parameters)
{
	GuLiCombatEffectTests::FFixture F; if (!F.Initialize(*this)) return false;
	const UGuLiCommanderDataSubsystem* Data = F.World->GetSubsystem<UGuLiCommanderDataSubsystem>();
	if (!TestNotNull(TEXT("Commander table cache"), Data)
		|| !TestTrue(TEXT("SpellFields table is valid"), Data->IsSpellFieldCatalogValid())
		|| !TestTrue(TEXT("WeaponMounts table is valid"), Data->IsWeaponMountCatalogValid())) return false;
	const auto* Global = F.World->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
	const FGuLiSpellFieldConfig* Field = Global->FindCombatField(TEXT("WM01_MissileExplosion"));
	TestTrue(TEXT("Commander reads the same global catalog"), Field == Data->FindSpellFieldConfig(TEXT("WM01_MissileExplosion")));
	TestNotNull(TEXT("Ship bombardment lives alongside WM fields"), Global->FindCombatField(TEXT("WingmanGroundMissile")));
	if (!TestNotNull(TEXT("WM01 table field"), Field)) return false;
	TestEqual(TEXT("field base damage comes from SpellFields"), Field->Damage, 30.0f);
	TestEqual(TEXT("current WM01 field radius comes from SpellFields"), Field->Radius, 300.0f);
	TestEqual(TEXT("field timing comes from SpellFields"), Field->Timing, EGuLiSpellFieldTiming::Instant);
	const FGuLiSkillDefinition* Skill = Data->FindSkillDefinition(TEXT("WM01_HomingMissile"));
	TestTrue(TEXT("missile skill links the field row"), Skill && Skill->EffectConfigId == Field->ConfigId);
	const FGuLiUnitSkillConfig* Weapon = Data->GetUnitSkillConfigs().FindByPredicate([](const auto& Config)
	{
		return Config.UnitTypeId == 2 && Config.SlotId == TEXT("MissileLauncher");
	});
	TestTrue(TEXT("field damage is injected into the resolved weapon base without a duplicate authored value"),
		Weapon && Weapon->Damage == Field->Damage);
	const FGuLiWeaponMountConfig* FourFGun = Data->FindWeaponMountConfig(5, TEXT("BasicAttack"));
	TestTrue(TEXT("summon Sweeper ID 5 muzzle is loaded from WeaponMounts"), FourFGun && FourFGun->Muzzles.Num() == 1
		&& FourFGun->Muzzles[0].Equals(FVector(757.2613716, 0, 588.1241798) * .2, 0.01)
		&& FourFGun->AimOffset.Equals(FVector(0, 0, 130), 0.01));
	const FGuLiWeaponMountConfig* WM01Missiles = Data->FindWeaponMountConfig(2, TEXT("MissileLauncher"));
	TestTrue(TEXT("WM01 twin missile sockets retain table order"), WM01Missiles && WM01Missiles->Muzzles.Num() == 2
		&& WM01Missiles->Muzzles[0].Equals(FVector(-729.1603565, 932.1683884, 3877.2838232) * .2, 0.01)
		&& WM01Missiles->Muzzles[1].Equals(FVector(-729.1603565, -932.1683884, 3877.2838232) * .2, 0.01)
		&& WM01Missiles->AimOffset.Equals(FVector(0, 0, 470.99032636), 0.01));
	auto& Source = F.Add(EGuLiTeam::Red, FVector(-5000, 0, 0));
	auto& Target = F.Add(EGuLiTeam::Blue, FVector::ZeroVector);
	auto* Visual = NewObject<UGuLiSpellFieldDefinition>(F.World);
	Visual->ConfigId = Field->ConfigId;
	auto ResolvedContext = F.Context(Source, &Target);
	ResolvedContext.EffectConfigId = Field->ConfigId;
	ResolvedContext.Damage = 45.0f;
	TestTrue(TEXT("Authority-resolved global field launches through the shared runtime"),
		F.Runtime->CreateSpellField(Visual, ResolvedContext, FVector::ZeroVector).IsValid());
	TestEqual(TEXT("Shared runtime preserves skill-modified damage with an explicit field id"), Target.Snapshot.Health, 55.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020WarningPoolTest, "GuLiStrike.Scale020.GroundWarningPoolingAndRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiScale020WarningPoolTest::RunTest(const FString& Parameters)
{
	GuLiCombatEffectTests::FFixture F; if (!F.Initialize(*this)) return false;
	auto* Warnings = F.World->GetSubsystem<UGuLiGroundWarningSubsystem>();
	auto* Style = LoadObject<UGuLiGroundWarningStyle>(nullptr,
		TEXT("/Game/GuLiStrike/FX/GroundWarning/DA_GroundWarning_Red.DA_GroundWarning_Red"));
	if (!TestNotNull(TEXT("render-world warning subsystem"), Warnings)
		|| !TestNotNull(TEXT("production warning style"), Style)) return false;
	FGuLiGroundWarningParams Params;
	Params.Style = Style; Params.Location = FVector(125000, -240000, 12650);
	Params.Radius = 160; Params.ExpireServerSeconds = 6;
	const FGuid Id = FGuid::NewGuid(), Shared = FGuid::NewGuid();
	TestTrue(TEXT("first warning"), Warnings->UpsertWarning(Id, Params));
	TestTrue(TEXT("same ID is idempotent"), Warnings->UpsertWarning(Id, Params));
	TestTrue(TEXT("matching warnings share one circle"), Warnings->UpsertWarning(Shared, Params));
	TestEqual(TEXT("two callers"), Warnings->GetActiveWarningCount(), 2);
	TestEqual(TEXT("one decal circle"), Warnings->GetActiveCircleCount(), 1);
	TInlineComponentArray<UDecalComponent*> Decals(F.World->GetWorldSettings());
	if (!TestEqual(TEXT("only one pooled decal allocated"), Decals.Num(), 1)) return false;
	auto* Decal = Decals[0];
	TestTrue(TEXT("visible edge equals frozen 160 cm radius"), FMath::IsNearlyEqual(Decal->DecalSize.Y * .96, 160.0, .001));
	TestEqual(TEXT("projection depth migrated once"), Decal->DecalSize.X, 80.0);
	TestTrue(TEXT("absolute map position is unchanged"), Decal->GetComponentLocation().Equals(Params.Location));
	Warnings->RemoveWarning(Id);
	TestEqual(TEXT("shared circle remains until final caller leaves"), Warnings->GetActiveCircleCount(), 1);
	Warnings->RemoveWarning(Shared);
	TestFalse(TEXT("released decal is hidden"), Decal->IsVisible());
	Params.Location += FVector(200, 0, 0);
	TestTrue(TEXT("pooled component reused"), Warnings->UpsertWarning(FGuid::NewGuid(), Params));
	TestTrue(TEXT("reuse cannot compound .2 into .04"), FMath::IsNearlyEqual(Decal->DecalSize.Y * .96, 160.0, .001));
	TestTrue(TEXT("reuse resets world anchor"), Decal->GetComponentLocation().Equals(Params.Location));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCombatEffectNetworkOrderTest, "GuLiStrike.CombatEffects.NetworkOrderAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCombatEffectNetworkOrderTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffectTests;
	// The 500-unit fixture must not resend long asset paths/frozen motion on each
	// terminal event. Exercise the real bit serializer, identities and byte budget.
	FGuLiCombatEffectState Wire; Wire.MatchEpoch=9; Wire.EffectId=FGuid(1,2,3,4); Wire.Sequence=3;
	Wire.bAcceptanceAdditional=true;
	Wire.Source=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9,17);
	Wire.Target=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9,500,2);
	Wire.Location=FVector(123,456,789); Wire.EndTime=8;
	bool bSerialized=false; FNetBitWriter StateWriter(nullptr,0);
	Wire.NetSerialize(StateWriter,nullptr,bSerialized);
	TestTrue(TEXT("compact create bit stream is valid and under 200 bytes"),bSerialized && StateWriter.GetNumBytes()<200);
	FNetBitReader StateReader(nullptr,StateWriter.GetData(),StateWriter.GetNumBits()); FGuLiCombatEffectState Decoded;
	Decoded.NetSerialize(StateReader,nullptr,bSerialized);
	TestTrue(TEXT("compact targets round trip the complete stable identities"),bSerialized && Decoded.Source==Wire.Source && Decoded.Target==Wire.Target);
	TestTrue(TEXT("quantized create preserves position"),FVector(Decoded.Location).Equals(Wire.Location,1));
	TestTrue(TEXT("additional-flight cohort survives the actual state codec"),Decoded.bAcceptanceAdditional);
	Wire.Phase=EGuLiCombatEffectPhase::Finished; Wire.EndReason=EGuLiCombatEffectEndReason::Impact;
	FNetBitWriter EndWriter(nullptr,0); Wire.NetSerialize(EndWriter,nullptr,bSerialized);
	TestTrue(TEXT("terminal carries its impact point without repeated cast payload, under 44 bytes"),bSerialized && EndWriter.GetNumBytes()<44);
	FNetBitReader EndReader(nullptr,EndWriter.GetData(),EndWriter.GetNumBits()); Decoded.NetSerialize(EndReader,nullptr,bSerialized);
	TestTrue(TEXT("terminal remains a well-formed removal at the authoritative impact point"),bSerialized && Decoded.IsWellFormed()
		&& Decoded.Phase==EGuLiCombatEffectPhase::Finished && FVector(Decoded.Location).Equals(Wire.Location,1));
	TestTrue(TEXT("terminal keeps its cohort classification"),Decoded.bAcceptanceAdditional);
	FGuLiCombatShotCue WireShot; WireShot.MatchEpoch=9; WireShot.ShotId=FGuid(4,3,2,1);
	WireShot.Source=Wire.Source; WireShot.Target=Wire.Target; WireShot.SlotId=TEXT("BasicAttack"); WireShot.UnitTypeId=2;
	WireShot.Start=FVector(123,456,789); WireShot.End=FVector(12000,500,800);
	FNetBitWriter ShotWriter(nullptr,0); WireShot.NetSerialize(ShotWriter,nullptr,bSerialized);
	TestTrue(TEXT("full-endpoint shot is under 65 bytes"),bSerialized && ShotWriter.GetNumBytes()<65);
	FNetBitReader ShotReader(nullptr,ShotWriter.GetData(),ShotWriter.GetNumBits()); FGuLiCombatShotCue DecodedShot;
	DecodedShot.NetSerialize(ShotReader,nullptr,bSerialized);
	TestTrue(TEXT("shot wire round trip preserves endpoints and mount"),bSerialized && DecodedShot.Source==WireShot.Source
		&& DecodedShot.Target==WireShot.Target && DecodedShot.SlotId==WireShot.SlotId
		&& FVector(DecodedShot.Start).Equals(WireShot.Start,1) && FVector(DecodedShot.End).Equals(WireShot.End,1));
	FFixture F; if (!F.Initialize(*this)) return false;
	auto* Visuals = F.World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>();
	if (!TestNotNull(TEXT("standalone client presentation exists"), Visuals)) return false;
	FGuLiCombatEffectState State; State.EffectId = FGuid(1, 2, 3, 4); State.MatchEpoch = 9; State.Sequence = 1;
	State.Source = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, 1); State.EndTime = 8;
	Visuals->ApplyState(State, true); Visuals->ApplyState(State);
	TestEqual(TEXT("duplicate create has one active instance"), Visuals->GetActiveVisualCount(), 1);
	State.Sequence = 5; Visuals->ApplyState(State);
	State.Sequence = 3; Visuals->ApplyState(State);
	TestEqual(TEXT("out of order correction cannot roll state back"), Visuals->GetEffectStates()[0].Sequence, 5u);
	FGuLiCombatEffectCorrection Correction; Correction.MatchEpoch=9; Correction.EffectId=State.EffectId;
	Correction.Sequence=6; Correction.SampleTime=.2f; Correction.Location=FVector(100,200,300);
	Visuals->ApplyCorrection(Correction);
	TestTrue(TEXT("compact correction updates an existing projectile"),FVector(Visuals->GetEffectStates()[0].Location).Equals(Correction.Location));
	Correction.Sequence=4; Correction.Location=FVector::ZeroVector; Visuals->ApplyCorrection(Correction);
	TestEqual(TEXT("stale compact correction is ignored"),Visuals->GetEffectStates()[0].Sequence,6u);
	State.Sequence = 7; State.Phase = EGuLiCombatEffectPhase::Finished; Visuals->ApplyState(State);
	State.Sequence = 8; State.Phase = EGuLiCombatEffectPhase::Active; Visuals->ApplyState(State);
	Correction.Sequence=9; Visuals->ApplyCorrection(Correction);
	TestEqual(TEXT("terminal tombstone prevents resurrection"), Visuals->GetActiveVisualCount(), 0);
	FGuLiCombatShotCue Cue; Cue.MatchEpoch = 9; Cue.ShotId = FGuid(4, 3, 2, 1);
	Visuals->ApplyShots({Cue, Cue}); TestEqual(TEXT("duplicate shot is rejected"), Visuals->GetCounters().DroppedShots, static_cast<int64>(1));
	Visuals->BeginEpoch(10); State.EffectId = FGuid::NewGuid(); Visuals->ApplyState(State);
	TestEqual(TEXT("old epoch cannot repopulate the new match"), Visuals->GetActiveVisualCount(), 0);
	TestEqual(TEXT("epoch leaves no Niagara components"), Visuals->GetCounters().ComponentCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiFlightBatchLifecycleTest, "GuLiStrike.CombatEffects.FlightBatchBootstrapAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiFlightBatchLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffectTests;
	FGuLiFlightEvent Event;
	auto& State=Event.State;
	State.MatchEpoch=MAX_uint32; State.EffectId=FGuid::NewGuid(); State.Sequence=1;
	State.Kind=EGuLiCombatEffectKind::LinearProjectile;
	State.Source=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(State.MatchEpoch,1);
	State.SourceTeam=EGuLiTeam::Red; State.Motion.Speed=100; State.Motion.SweepRadius=1;
	State.LaunchDirection=FVector::ForwardVector; State.Velocity=FVector(100,0,0); State.EndTime=8;
	TArray<FGuLiFlightEvent> Inputs; Inputs.Init(Event,32);
	for (auto& Input : Inputs) Input.State.EffectId=FGuid::NewGuid();
	TArray<uint8> Payload; TArray<FGuLiFlightEvent> Decoded;
	const int32 Count=GuLiFlightWire::EncodeBatch(Inputs,Payload);
	TestTrue(TEXT("batch is bounded by count and actual encoded size"),Count>0 && Count<=16 && Payload.Num()<=1000);
	TestTrue(TEXT("accepted batch decodes complete identities"),GuLiFlightWire::DecodeBatch(Payload,Decoded)
		&& Decoded.Num()==Count && Decoded[0].State.EffectId==Inputs[0].State.EffectId);
	Payload.Pop();
	TestFalse(TEXT("truncated reliable batch cannot be applied"),GuLiFlightWire::DecodeBatch(Payload,Decoded));
	FFixture F; if (!F.Initialize(*this)) return false;
	auto* Visuals=F.World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>();
	if (!TestNotNull(TEXT("local presentation exists"),Visuals)) return false;
	Visuals->ApplyFlightEvent(Event); Visuals->ApplyFlightEvent(Event);
	TestEqual(TEXT("duplicate launch acquires only one prediction entry"),
		Visuals->GetCounters().ClientFlightActorActive+Visuals->GetCounters().ClientFlightDataActive,1);
	const int32 Capacity=Visuals->GetCounters().ClientFlightActorCapacity+Visuals->GetCounters().ClientFlightDataCapacity;
	Event.State.Phase=EGuLiCombatEffectPhase::Finished; Event.State.Sequence=2;
	Visuals->ApplyFlightEvent(Event); Visuals->ApplyFlightEvent(Event);
	Event.State.Phase=EGuLiCombatEffectPhase::Active; Event.State.Sequence=3;
	Visuals->ApplyFlightEvent(Event);
	TestTrue(TEXT("terminal tombstone prevents delayed launch resurrection"),
		Visuals->GetEffectStates().Num()==1 && Visuals->GetEffectStates()[0].Phase==EGuLiCombatEffectPhase::Finished);
	Event.State.EffectId=FGuid::NewGuid(); Event.State.MatchEpoch=7;
	Event.State.Source=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(7,1);
	Visuals->BeginEpoch(7);
	TestEqual(TEXT("epoch reset returns retained terminal trail to pool"),
		Visuals->GetCounters().ClientFlightActorActive+Visuals->GetCounters().ClientFlightDataActive,0);
	Visuals->ApplyFlightEvent(Event);
	TestEqual(TEXT("wrapped opaque epoch accepts new match"),
		Visuals->GetCounters().ClientFlightActorActive+Visuals->GetCounters().ClientFlightDataActive,1);
	Event.State.MatchEpoch=MAX_uint32; Visuals->ApplyFlightEvent(Event);
	TestEqual(TEXT("retired epoch cannot replace current match"),Visuals->GetEffectStates()[0].MatchEpoch,7u);
	TestEqual(TEXT("pool reused across epoch reset"),
		Visuals->GetCounters().ClientFlightActorCapacity+Visuals->GetCounters().ClientFlightDataCapacity,Capacity);
	Event.State.MatchEpoch=7; Event.State.StartTime=3; Event.State.SampleTime=3;
	Event.State.Location=Event.State.LaunchLocation=FVector(300,0,0); Event.bBootstrap=true;
	auto* Actor=F.World->SpawnActor<AGuLiFlightVisualActor>();
	Actor->ActivateFlight(Event); Actor->AdvanceFlight(5,nullptr);
	TestTrue(TEXT("late bootstrap advances from current point with remaining lifetime"),Actor->GetDisplayLocation(5).Equals(FVector(500,0,0),1));
	TestFalse(TEXT("client pooled Actor never replicates"),Actor->GetIsReplicated());
	TestFalse(TEXT("client pooled Actor never replicates movement"),Actor->IsReplicatingMovement());
	Actor->Destroy();
	return true;
}
#endif

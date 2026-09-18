"""Mechanical, reviewed test-fixture/expected-value migration; never changes runtime code."""
import json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
edits=[]
def change(path,pairs):
    p=ROOT/path; source=p.read_text(encoding='utf-8'); original=source
    for old,new in pairs:
        if old not in source:
            if new in source: continue
            raise RuntimeError('Test source changed: '+path+' '+old)
        source=source.replace(old,new)
        edits.append({'path':path,'old':old,'target':new})
    if source!=original: p.write_text(source,encoding='utf-8')
base='Source/GuLiStrike/'
change(base+'Gameplay/CombatEffects/Tests/GuLiCombatEffectRuntimeTests.cpp',[
 ('A.LaunchLocation.Z + 500','A.LaunchLocation.Z + 100'),
 ('Field->Radius, 800.0f','Field->Radius, 160.0f'),
 ('FVector(-480, 5, 935)','FVector(-96, 1, 187)'),('FVector(0, 0, 650)','FVector(0, 0, 130)'),
 ('FVector(-260, 1257, 2440)','FVector(-52, 251.4, 488)'),('FVector(-260, -326, 2440)','FVector(-52, -65.2, 488)'),
 ('FVector(0, 495, 1600)','FVector(0, 99, 320)'),
 ('F.Add(EGuLiTeam::Blue, FVector(850, 0, 0), true)','F.Add(EGuLiTeam::Blue, FVector(210, 0, 0), true)'),
 ('F.Add(EGuLiTeam::Blue, FVector(850.1, 0, 0), true)','F.Add(EGuLiTeam::Blue, FVector(210.1, 0, 0), true)')])
change(base+'Commander/Presentation/Tests/GuLiCommanderCameraPolicyTests.cpp',[
 ('default camera arm remains 800m','default camera arm is 160m after scale020'),('80000.0f','16000.0f')])
change(base+'Gameplay/Data/Tests/GuLiCommanderSoldierResolverTests.cpp',[
 ('Fallback.MovementSpeedCmPerSecond, 3600.0f','Fallback.MovementSpeedCmPerSecond, 720.0f'),
 ('Resolved.MovementSpeedCmPerSecond, 3600.0f','Resolved.MovementSpeedCmPerSecond, 720.0f'),
 ('Miner.MovementSpeedCmPerSecond, 4500.0f','Miner.MovementSpeedCmPerSecond, 900.0f'),
 ('Miner.PresentationScale * 591.6596f, 1800.0f','Miner.PresentationScale * 591.6596f, 360.0f'),
 ('36 m/s baseline','7.2 m/s baseline'),('three times the previous 1500 cm/s','900 cm/s after scale020'),
 ('uniformly scaled to 18 metres','uniformly scaled to 3.6 metres')])
change(base+'Gameplay/Building/Tests/BuildingAssetTests.cpp',[
 ('Expected.Mesh->GetBounds().BoxExtent,','Expected.Mesh->GetBounds().BoxExtent * Definition->MeshScale,')])
change(base+'Gameplay/Building/Tests/BuildingPlacementPolicyTests.cpp',[
 ('Placement clearance is exactly 100 cm','Placement clearance is exactly 20 cm'),
 ('PlacementClearanceCentimeters, 100.0f','PlacementClearanceCentimeters, 20.0f')])
change(base+'Gameplay/Resources/Tests/GuLiResourceTests.cpp',[
 ('Factory speed triples to 15m/s','Factory speed is 3m/s after scale020'),
 ('Config->FactoryManeuverSpeedCentimetersPerSecond, 1500.0f','Config->FactoryManeuverSpeedCentimetersPerSecond, 300.0f')])
change(base+'Gameplay/Ship/Abilities/Tests/GuLiShipAbilityTests.cpp',[
 ('Snapshot.FormationRuntime.MinimumSpeedCentimetersPerSecond, 6000.0f','Snapshot.FormationRuntime.MinimumSpeedCentimetersPerSecond, 1200.0f'),
 ('Snapshot.FormationRuntime.CruiseSpeedCentimetersPerSecond, 9000.0f','Snapshot.FormationRuntime.CruiseSpeedCentimetersPerSecond, 1800.0f'),
 ('Snapshot.FormationRuntime.CatchUpSpeedCentimetersPerSecond, 15000.0f','Snapshot.FormationRuntime.CatchUpSpeedCentimetersPerSecond, 3000.0f'),
 ('Snapshot.FormationRuntime.MaximumAccelerationCentimetersPerSecondSquared, 4000.0f','Snapshot.FormationRuntime.MaximumAccelerationCentimetersPerSecondSquared, 800.0f'),
 ('Snapshot.FormationRuntime.MaximumDecelerationCentimetersPerSecondSquared, 3200.0f','Snapshot.FormationRuntime.MaximumDecelerationCentimetersPerSecondSquared, 640.0f'),
 ('Snapshot.BasicWeaponRuntime.RangeCentimeters, 150000.0f','Snapshot.BasicWeaponRuntime.RangeCentimeters, 30000.0f'),
 ('Snapshot.MissileRuntime.RangeCentimeters, 250000.0f','Snapshot.MissileRuntime.RangeCentimeters, 50000.0f')])
change(base+'Gameplay/Skills/Tests/GuLiSkillLifecycleTests.cpp',[
 ('WM01 integration range remains 150 metres','WM01 integration range is 30 metres'),
 ('WM01->RangeCentimeters, 15000.0f','WM01->RangeCentimeters, 3000.0f')])
change(base+'Gameplay/Teleport/Tests/GuLiTeleportTests.cpp',[
 ('{4000.f, 10000.f, 20000.f, 50000.f}','{800.f, 2000.f, 4000.f, 10000.f}'),
 ('exactly 100m','exactly 20m'),('Ship above 100m','Ship above 20m'),
 ('CanCollectVehicle(Config,true,10000)','CanCollectVehicle(Config,true,2000)'),
 ('CanCollectVehicle(Config,true,10000.1)','CanCollectVehicle(Config,true,2000.1)'),
 ('Beam height is 500m','Beam height is 100m'),('Config.BeamHeightCentimeters,50000.f','Config.BeamHeightCentimeters,10000.f')])
change(base+'Gameplay/Wingman/Tests/GuLiWingmanAttackRunTests.cpp',[
 ('Profile.PullUpHeight=5000','Profile.PullUpHeight=1000'),('Profile.PullUpHeight=10000','Profile.PullUpHeight=2000'),
 ('SourceRow->FlightSpeedCentimetersPerSecond, 9000.0f','SourceRow->FlightSpeedCentimetersPerSecond, 1800.0f'),
 ('Field.Radius, 4000.0f','Field.Radius, 800.0f'),
 ('SelectAirEntryPhase(9999.0f','SelectAirEntryPhase(1999.0f'),('SelectAirEntryPhase(10000.0f','SelectAirEntryPhase(2000.0f'),
 ('ShouldEndAirBurst(5000.0f','ShouldEndAirBurst(1000.0f'),('ShouldEndAirBurst(4999.0f','ShouldEndAirBurst(999.0f'),
 ('ShouldEndAirBurst(8000.0f','ShouldEndAirBurst(1600.0f'),
 ('SourceRow->AirFireStartDistanceCentimeters, 10000.0f','SourceRow->AirFireStartDistanceCentimeters, 2000.0f'),
 ('SourceRow->AirFireStopDistanceCentimeters, 5000.0f','SourceRow->AirFireStopDistanceCentimeters, 1000.0f'),
 ('MachineGun->Attack.FlightSpeed, 9000.0f','MachineGun->Attack.FlightSpeed, 1800.0f'),
 ('GroundMissile->Attack.FlightSpeed, 9000.0f','GroundMissile->Attack.FlightSpeed, 1800.0f'),
 ('GroundMissile->Attack.ExplosionRadius, 4000.0f','GroundMissile->Attack.ExplosionRadius, 800.0f')])
def section(path,start,end,pairs):
    p=ROOT/path; source=p.read_text(encoding='utf-8')
    begin=source.index(start); finish=source.index(end,begin)
    body=source[begin:finish]
    marker='// Scale020 fixture: spatial values use final centimeters.\n'
    if marker in body: return
    for old,new in pairs:
        if old not in body: raise RuntimeError(path+' missing fixture '+old)
        body=body.replace(old,new)
    body=body.replace('{','{\n\t'+marker,1)
    p.write_text(source[:begin]+body+source[finish:],encoding='utf-8')
nav=base+'Commander/Mass/Navigation/Tests/GuLiCommanderNavigationPolicyTests.cpp'
section(nav,'bool FCommanderSharedTargetProjectionTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('FVector(750.0, 0.0, 5000.0)','FVector(150.0, 0.0, 5000.0)'),('FVector(751.0, 0.0, 0.0)','FVector(151.0, 0.0, 0.0)')])
section(nav,'bool FCommanderLooseArrivalRadiusTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('5000.0f','1000.0f'),('6500.0f','1300.0f'),('9500.0f','1900.0f'),('4500.0f','900.0f'),
 ('500.0f','100.0f'),('3600.0f','720.0f'),('3750.0f','750.0f')])
section(nav,'bool FCommanderSurfaceMoveAcceptanceTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('250.01','50.01'),('250.0','50.0'),('250cm','50cm')])
section(nav,'bool FCommanderMeaningfulProgressTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('970.01f','994.01f'),('970.0f','994.0f'),('29.99cm','5.99cm'),('30cm','6cm')])
section(base+'Commander/Mass/Navigation/Tests/GuLiCommanderAvoidancePolicyTests.cpp',
 'bool FCommanderPredictiveAvoidanceGridBoundaryTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('1499.0','299.0'),('7499.1','1499.1'),('7499.0','1499.0'),
 ('1500.1','300.1'),('750.0f','150.0f'),('6000 cm','1200 cm')])
section(base+'Commander/Mass/Tests/GuLiCommanderSelectionQueryTests.cpp',
 'bool FGuLiCommanderPointHintValidationTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('FVector(-3000.0, 0.0, 1000.0)','FVector(-600.0, 0.0, 200.0)'),
 ('FVector(-3000.0, 750.0, 500.0)','FVector(-600.0, 150.0, 100.0)'),
 ('FVector(-3000.0, 2000.0, 500.0)','FVector(-600.0, 400.0, 100.0)'),
 ('FVector(1200.0, 0.0, 0.0)','FVector(240.0, 0.0, 0.0)'),('FVector(1000.0, 0.0, 0.0)','FVector(200.0, 0.0, 0.0)'),
 ('Request = MakePointRequest();','Request = MakePointRequest(); Request.RayOrigin *= 0.2;')])
section(base+'Battle/Combat/Tests/GuLiWingmanAttackCombatTests.cpp',
 'bool FGuLiWingmanTargetHysteresisAndGuardTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('150000.0','30000.0'),('150001.0','30001.0'),('180000.0','36000.0'),('180001.0','36001.0'),
 ('170000.0','34000.0')])

# These two isolated combat fixtures model LOCAL encounters around (0,0,0), not map
# anchors. Bring their positions/velocities along with authored ranges; keep unit
# direction vectors, health, time, angular and packed milli-direction values intact.
for relative in ['Battle/Combat/Tests/GuLiWingmanCombatCoordinatorTests.cpp',
                 'Battle/Combat/Tests/GuLiWingmanAttackCombatTests.cpp']:
    p=ROOT/base/relative
    source=p.read_text(encoding='utf-8')
    marker='// Scale020: isolated encounter positions and velocities migrated.\n'
    if marker in source: continue
    # Only literal position constructors, never expressions or normalized directions.
    def vector(m):
        v=[float(x.strip().rstrip('f')) for x in m[1].split(',')]
        if max(abs(n) for n in v)<=1: return m[0]
        return 'FVector('+', '.join(format(n*.2,'.8g') for n in v)+')'
    source=re.sub(r'FVector\(\s*((?:-?\d+(?:\.\d+)?f?\s*,\s*){2}-?\d+(?:\.\d+)?f?\s*)\)',vector,source)
    if 'Coordinator' in relative:
        source=source.replace('FlightIndex) * 1000','FlightIndex) * 200').replace('MemberIndex) * 100','MemberIndex) * 20')
        source=source.replace('FIntVector(4500, 0, 0)','FIntVector(900, 0, 0)')
    else:
        source=source.replace('CollisionRadius=50','CollisionRadius=10').replace('R.FrozenField.Radius=4000','R.FrozenField.Radius=800')
        source=source.replace('5000.0f,52000.0f','1000.0f,10400.0f')
    p.write_text(marker+source,encoding='utf-8')
section(nav,'bool FCommanderPerMemberLooseArrivalStateTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('5000.0f','1000.0f'),('4000.0f','800.0f'),('4600.0f','920.0f'),('4500.0f','900.0f'),
 ('5100.0f','1020.0f'),('4800.0f','960.0f'),('500.0f','100.0f'),('3600.0f','720.0f'),
 ('6000.0f','1200.0f'),('120.0f','24.0f')])
section(base+'Battle/Combat/Tests/GuLiWingmanAttackCombatTests.cpp',
 'bool FGuLiWingmanServerGunGateTest::RunTest','IMPLEMENT_SIMPLE_AUTOMATION_TEST',[
 ('Target,10000)','Target,2000)'),('100m','20m')])
print(json.dumps({'replacements':len(edits)}))

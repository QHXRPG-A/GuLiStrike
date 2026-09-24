"""Author the Mass navigation review scene in its existing map; never runs gameplay.

Editor Python: runpy.run_path(script).
The canonical map requires profile 0: original armies, 250 soldiers per team.
Population overrides are rejected before edits; select 25 units in game for the small case.
Only actors tagged MassNavigationReview20260923 are changed. The script saves the map.
"""
import json
import math
import traceback
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MassNavigationReview20260923'
FOLDER = 'GuLiStrike/Review/MassNavigation'
PROFILE = int(globals().get('MASS_NAV_PROFILE', 0))
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/MassNavigation20260923'
report = {'success': False, 'map': MAP, 'profile': PROFILE, 'gameplay_started': False,
          'native_build_executed': False, 'runtime_verified': False}


def vec(v):
    return list(v.to_tuple())


def record(a):
    return {'name': a.get_name(), 'label': a.get_actor_label(), 'class': a.get_class().get_path_name(),
            'location': vec(a.get_actor_location()), 'rotation': list(a.get_actor_rotation().to_tuple()),
            'scale': vec(a.get_actor_scale3d())}


def run():
    assert PROFILE == 0, ('LVL_CommanderMassPrototype only supports the default 250 soldiers per team. '
                          'Population override profiles require shared spawn/bake support and are not available.')
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'Finish the game session before editing the scene.'
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = api.get_all_level_actors()
    owned = {a.get_actor_label(): a for a in actors if TAG in [str(t) for t in a.tags]}
    original = {a.get_path_name(): record(a) for a in actors if a not in owned.values()}
    assert not any(a.get_class().get_name() == 'GuLiCommanderDeploymentPoint'
                   for a in actors if a not in owned.values()), 'Preserve unrelated authored deployments.'
    definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    economy = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    assert definition and economy
    layout_hash = definition.get_editor_property('layout_hash')
    anchors = definition.get_editor_property('spawn_anchors')
    assembly = anchors.get_editor_property('red_assembly')
    clusters = [(c.get_editor_property('center'), c.get_editor_property('obstacle_radius_centimeters'))
                for c in definition.get_editor_property('clusters')]
    markers = [a for a in actors if a.get_actor_label().startswith('Outpost_')]
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    assert cube
    authored = []

    def ground(x, y):
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 30000),
                unreal.Vector(x, y, -15000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True,
                list(owned.values()), unreal.DrawDebugTrace.NONE)
        assert hit is not None, f'No ground below ({x},{y})'
        values = hit.to_tuple()  # NativeBreakFunc: blocking, overlap, time, distance, location, impact point.
        assert values[0], f'No blocking ground at ({x},{y})'
        return values[5]

    def get_actor(label, cls, location):
        actor = owned.get(label)
        if not actor:
            actor = api.spawn_actor_from_class(cls, location)
            owned[label] = actor
        assert actor.get_class() == cls.static_class(), label
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER)
        actor.set_editor_property('tags', [TAG])
        actor.set_actor_location(location, False, False)
        authored.append(actor)
        return actor

    # Keep the fixtures clear of actual default spawn slots, facilities and ore bodies.
    wall_specs = [
        ('CornerLong', (11200, 70500), (2600, 200, 1400)),
        ('CornerReturn', (12600, 71500), (200, 2200, 1400)),
        ('NarrowLeft', (5200, 69000), (200, 2400, 1400)),
        ('NarrowRight', (6200, 69000), (200, 2400, 1400)),
        ('ClosedNorth', (16500, 68500), (3000, 200, 1400)),
        ('ClosedSouth', (16500, 65500), (3000, 200, 1400)),
        ('ClosedWest', (15000, 67000), (200, 3200, 1400)),
        ('ClosedEast', (18000, 67000), (200, 3200, 1400)),
    ]
    preview = unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()
    assert preview, 'Default army preview failed; fix the reported authoring issue before editing.'
    reservations = json.loads(preview)['reservations']
    # Validate the real army/facility reservation footprint, not just resource positions.
    for name, (x, y), size in wall_specs:
        for c, r in clusters:
            dx = max(0., abs(c.x-x)-size[0]/2)
            dy = max(0., abs(c.y-y)-size[1]/2)
            assert dx*dx+dy*dy > (r+250)**2, f'{name} overlaps baked ore; relocate this fixture.'
        for reserve in reservations:
            dx = max(0., abs(reserve['x']-x)-size[0]/2)
            dy = max(0., abs(reserve['y']-y)-size[1]/2)
            assert dx*dx+dy*dy > (reserve['radius_cm']+250)**2, f'{name} overlaps {reserve["label"]}'

    with unreal.ScopedEditorTransaction('Prepare Mass incremental navigation scene'):
        for name, (x, y), size in wall_specs:
            ground_point = ground(x, y)
            a = get_actor('MassNav_' + name, unreal.StaticMeshActor,
                          ground_point + unreal.Vector(0, 0, size[2]/2-100))
            component = a.static_mesh_component
            component.set_static_mesh(cube)
            component.set_mobility(unreal.ComponentMobility.STATIC)
            component.set_collision_profile_name('BlockAll')
            a.set_actor_scale3d(unreal.Vector(size[0]/100, size[1]/100, size[2]/100))

        # Rebuild only this utility's deployments; runtime behavior and unit definitions are reused.
        for label, a in list(owned.items()):
            if label.startswith('MassNav_Deployment_'):
                api.destroy_actor(a)
                del owned[label]

        nearest = min(clusters, key=lambda cr: (cr[0]-assembly).length())[0]
        notes = [
            ('Entry', assembly+unreal.Vector(0,0,500),
             'Mass分帧寻路验收入口：原图默认双方各250名，共500名。\n'
             '使用原图合法初始军队；专项DeploymentPoint会被本图资源校验拒绝。\n'
             '源码已编译。玩家开局后，S停止自动推进；选择25人或更多，右键25米外下令，再连续改令、S、减选。首批先走，绿线逐批保留。\n'
             '2000/10000人口档位尚未接入本图合法出生校验，暂不提供覆盖部署。'),
            ('CornerObserve', ground(11000,68500)+unreal.Vector(0,0,500),
             '从拐角西侧向东北右键移动，观察贴墙时中心线回退、侧向归位暂停、稳定绕行。不可穿墙投影。'),
            ('NarrowObserve', ground(5700,67000)+unreal.Vector(0,0,500),
             '向(5700,71000)下令穿过800cm净宽通道，观察缩列、出口恢复与反复改令。'),
            ('CapacityObserve', ground(16500,67000)+unreal.Vector(0,0,500),
             '封闭区用于不可达候选与部分失败；向围墙中心下令。后续失败不得回滚先前已提交批次。'),
            ('OreMechObserve', nearest+unreal.Vector(0,0,500),
             '现有520cm矿体：向矿体另一侧下令。切到地面机甲角色穿行队伍，观察环境预测、贴障减速及友方让行。\n'
             '建筑/导航重建使用本图正常B建造/拆除入口；运输用既有T指挥官传送。重连使用新客户端晚加入。'),
        ]
        for label, location, message in notes:
            a = get_actor('MassNav_'+label, unreal.Note, location)
            a.set_editor_property('is_editor_only_actor', True)
            a.set_editor_property('text', message)
        camera = get_actor('MassNav_Overview', unreal.CameraActor, unreal.Vector(-12000,85000,28000))
        camera.set_actor_rotation(unreal.Rotator(-52,-90,0),False)
        camera.set_editor_property('is_editor_only_actor', True)

    current = {a.get_path_name(): record(a) for a in api.get_all_level_actors() if TAG not in [str(t) for t in a.tags]}
    assert original == current, 'Unrelated actor transforms changed.'
    assert definition.get_editor_property('layout_hash') == layout_hash
    navigation = unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world, True)
    report['navigation'] = {k: str(navigation.get_editor_property(k)) for k in ['success','message','ground_rebuilds','flight_rebuilds','total_seconds']}
    assert navigation.get_editor_property('success'), navigation.get_editor_property('message')
    bake = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    report['resource_validation'] = {k: str(bake.get_editor_property(k)) for k in ['success','message','source_hash','initial_soldier_count','validated_initial_soldier_count']}
    assert bake.get_editor_property('success'), bake.get_editor_property('message')
    saved = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    assert saved, 'Map save failed.'
    # Read authored entity data after the save; this is not a play/performance validation.
    data = []
    for a in api.get_all_level_actors():
        if TAG not in [str(t) for t in a.tags]: continue
        r = record(a)
        if isinstance(a, unreal.GuLiCommanderDeploymentPoint):
            r['deployment'] = {p: str(a.get_editor_property(p)) if p == 'team' else a.get_editor_property(p)
                               for p in ['team','unit_type_id','rows','columns','spacing_centimeters','allow_automatic_fire']}
        if isinstance(a, unreal.StaticMeshActor):
            r['mesh'] = a.static_mesh_component.static_mesh.get_path_name()
            r['collision'] = str(a.static_mesh_component.get_collision_profile_name())
            r['mobility'] = str(a.static_mesh_component.mobility)
        if isinstance(a, unreal.Note): r['text'] = a.get_editor_property('text')
        data.append(r)
    count = sum(r['deployment']['rows']*r['deployment']['columns'] for r in data if 'deployment' in r)
    assert count == PROFILE
    report.update(success=True, saved=saved, actors=data, configured_red_soldiers=250, configured_blue_soldiers=250, overriding_deployment_soldiers=count,
                  default_population_restored=not PROFILE, existing_actors_unchanged=len(original),
                  resource_layout_hash=layout_hash, clusters=len(clusters),
                  game_mode=str(world.get_world_settings().get_editor_property('default_game_mode')),
                  miners_per_team=economy.get_editor_property('initial_mining_vehicles_per_team'),
                  builders_per_team=economy.get_editor_property('initial_construction_vehicles_per_team'),
                  nav_actors=[record(a) for a in actors if a.get_class().get_name() in ['RecastNavMesh','NavMeshBoundsVolume']])


try:
    run()
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'scene-delivery-latest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k:report[k] for k in ['success','saved','profile','configured_red_soldiers','error'] if k in report}))

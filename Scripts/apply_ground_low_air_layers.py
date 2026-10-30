"""Import ground/low-air unit data, export startup manifests and save review markers.

Run through Scripts/ue_exec.py in the idle source editor. Never starts gameplay.
"""
import hashlib
import json
import math
import os
import re
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundLowAir20261026'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TABLE = 'DT_GuLiStrikeCommander_Soldiers'
TAG = 'GroundLowAirReview20261026'


def apply_data(report):
    namespace = {'__name__': 'ground_low_air_import'}
    source = (ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    source = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
    exec(compile(source, 'import_data_to_engine.py', 'exec'), namespace)
    namespace['PROGRESS'] = str(OUT / 'table-import.log')
    manifest = json.loads((ROOT / 'data/Json/manifest.json').read_text(encoding='utf-8'))
    # This update only adds movement fields; the general preflight also runs an
    # obsolete model-migration baseline. Check the selected native schema here.
    struct_path = manifest['tables'][TABLE]['struct']
    assert unreal.find_object(None, struct_path), 'Native soldier row struct must be loaded'
    row_type = getattr(unreal, struct_path.rsplit('.', 1)[-1])
    native_row = row_type()
    source_rows = json.loads((ROOT / f'data/Json/{TABLE}.json').read_text(encoding='utf-8'))
    for field in dict.fromkeys(key for row in source_rows for key in row if key != 'Name'):
        name = field[1:] if field.startswith('b') and len(field) > 1 and field[1].isupper() else field
        name = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2', name)
        name = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name).lower()
        native_row.get_editor_property(name)
    result = namespace['import_table'](TABLE, manifest['tables'][TABLE])
    report['table'] = result
    assert result.get('imported'), result
    table = unreal.load_asset('/Game/GuLiStrike/Data/' + TABLE)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    report['unit_layers'] = [dict(name=r['Name'], unit_type_id=r['Id'],
                                  movement_layer=r['MovementLayer'], altitude_meters=r['AltitudeMeters'])
                             for r in rows]
    for row in report['unit_layers']:
        expected = (1, 30) if row['unit_type_id'] == 2 else (0, 0)
        assert (row['movement_layer'], row['altitude_meters']) == expected, row
    assert len(rows) == 6
    report['data_applied'] = True


def flight_snapshot(manifest):
    def flight_key(key):
        parts = key.lower().split('/')
        return ('gulistrikeship' in key.lower() or 'wingman' in key.lower()
                or any(p in ('ship', 'flight', 'linearflight', 'shipplayer') for p in parts)
                or any(p.startswith('ship_') or p.endswith('_flight_adapter') for p in parts))

    return dict(numbers={k: v for k, v in manifest['numbers'].items() if flight_key(k)},
                strings={k: v for k, v in manifest['strings'].items() if flight_key(k)},
                objects=[o for o in manifest['objects'] if flight_key(o['definition'])])


def export_manifests(report):
    report['manifests'] = []
    candidates = []
    for population in (200, 600):
        destination = ROOT / f'Config/Hybrid/RuntimeManifest-{population}.json'
        previous = json.loads(destination.read_text(encoding='utf-8-sig'))
        candidate = OUT / destination.name
        exported = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(candidate)))
        assert exported.get('success'), exported
        validated = json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(candidate)))
        assert validated.get('success'), validated
        manifest = json.loads(candidate.read_text(encoding='utf-8-sig'))
        before, after = flight_snapshot(previous), flight_snapshot(manifest)
        assert before == after, 'Flight data changed during export; preserve existing manifests'
        assert previous['strings']['ground_grid/source_hash'] == manifest['strings']['ground_grid/source_hash']
        previous_objects = {obj['id']: obj for obj in previous['objects']}
        numbers = manifest['numbers']
        for unit in report['unit_layers']:
            prefix = f"unit_definition/unit/{unit['unit_type_id']}"
            assert float(numbers[prefix + '/movement_layer']) == unit['movement_layer']
            assert float(numbers[prefix + '/altitude_cm']) == unit['altitude_meters'] * 100
        altitude_samples = {}
        for obj in manifest['objects']:
            if obj['definition'] not in ('unit/1', 'unit/2'):
                continue
            altitude = 3000 if obj['definition'] == 'unit/2' else 0
            old = previous_objects[obj['id']]
            assert (obj['x'], obj['y']) == (old['x'], old['y']), 'Authored spawn XY changed'
            # The exporter uses the unchanged authored grid surface. Recast's
            # quantized Z differs slightly, so compare the exact same ground anchor.
            old_altitude = float(previous['numbers'].get('unit_definition/' + obj['definition'] + '/altitude_cm', 0))
            ground_z = float(old['z']) - old_altitude
            actual_altitude = float(obj['z']) - ground_z
            assert math.isclose(actual_altitude, altitude, abs_tol=1e-4), obj['id']
            altitude_samples.setdefault(obj['definition'], []).append(actual_altitude)
        assert len(altitude_samples.get('unit/2', [])) > 0
        summary = dict(population=population, file=str(destination), objects=len(manifest['objects']),
                       config_hash=manifest['config_hash'], map_hash=manifest['map_hash'],
                       sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),
                       flight_data_unchanged=True, export=exported, readback=validated,
                       heights_cm={key: dict(count=len(values), minimum=min(values), maximum=max(values))
                                   for key, values in altitude_samples.items()})
        report['manifests'].append(summary)
        candidates.append((candidate, destination))
    for candidate, destination in candidates:
        destination.write_bytes(candidate.read_bytes())
    report['manifests_applied'] = True


def configure_review_ring(component):
    # Persist the complete profile, including channel responses. A live
    # NoCollision override with a Custom profile can revert when the map reloads.
    component.modify()
    component.set_collision_profile_name('NoCollision')
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_editor_property('can_ever_affect_navigation', False)
    component.set_editor_property('generate_overlap_events', False)


def author_scene(report, level, world, nav):
    ed = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = {a.get_actor_label(): a for a in ed.get_all_level_actors()}
    center = existing['MassSoft_C'].get_actor_location()

    def ground(position):
        result = unreal.NavigationSystemV1.project_point_to_navigation(
            world, position, nav, None, unreal.Vector(150, 150, 50000))
        assert result is not None and math.hypot(result.x - position.x, result.y - position.y) <= 150.01
        return result

    origin = ground(center)
    positions = {'Ground': origin, 'Low30m': origin + unreal.Vector(0, 0, 3000),
                 'Low99m': origin + unreal.Vector(0, 0, 9900)}
    for suffix in ('W', 'E', 'N', 'S'):
        positions[suffix] = ground(existing['MassSoft_' + suffix].get_actor_location())

    def actor(cls, suffix, location, editor_only=True):
        label = 'GroundLowAir_' + suffix
        matches = [a for a in ed.get_all_level_actors() if a.get_actor_label() == label]
        assert len(matches) <= 1, label
        result = matches[0] if matches else ed.spawn_actor_from_class(cls, location)
        assert isinstance(result, cls), label
        result.set_actor_label(label)
        result.set_actor_location(location, False, True)
        result.set_folder_path('Review/GroundLowAir')
        result.set_editor_property('tags', [unreal.Name(TAG)])
        result.set_editor_property('is_editor_only_actor', editor_only)
        return result

    for name, position in positions.items():
        actor(unreal.TargetPoint, name, position)
    # The old MassSoft rings predate the terrain-height migration. Reuse their
    # exact mesh, scale and material in five non-colliding copies on today's floor.
    for suffix in ('C', 'W', 'E', 'N', 'S'):
        source_actor = existing['MassSoft_' + suffix]
        source_component = source_actor.get_component_by_class(unreal.StaticMeshComponent)
        ring = actor(unreal.StaticMeshActor, 'Ring_' + suffix,
                     positions['Ground' if suffix == 'C' else suffix] + unreal.Vector(0, 0, 8), False)
        component = ring.get_component_by_class(unreal.StaticMeshComponent)
        component.set_static_mesh(source_component.static_mesh)
        for index, material in enumerate(source_component.get_materials()):
            component.set_material(index, material)
        configure_review_ring(component)
        ring.set_actor_scale3d(source_actor.get_actor_scale3d())
        ring.set_actor_rotation(source_actor.get_actor_rotation(), True)
    instructions = (
        '地面 / 低空移动层核验，重防号 WM01（UnitTypeId=2）=低空、离当地地面30米。\n'
        '入口：本图现有默认600兵Hybrid对局；重新开始一局以读取已更新的出生清单。\n'
        '场地为GroundLowAir_Ring_C/W/E/N/S五个地面圆环，复用MassSoft原圆环资产；'
        '地面中心和30米、99米高度参考点同XY。\n'
        '1. 观察初始重防号在地面上方30米，原有悬浮盘动画继续在该根位置上播放。\n'
        '2. 分别选地面兵与重防号，从西/东圆环相向穿过中心：跨层不让路、不推挤；'
        '再让两组重防号相向移动，确认同层仍遵守原避障与让路。\n'
        '3. 混选地面兵与重防号移动到中心：两层独立分配站位，允许同XY；'
        '相同层的占地仍独立排开，工程车同类互通的原规则继续生效。\n'
        '4. 向已有坡地、建筑边界及矿体移动，再停止、改令：重防号仍沿地面导航，'
        '根位置保持当地地面+30米，不因恢复或转移重复叠加高度。\n'
        '填表：GuLiStrikeCommander.xlsx / Soldiers：MovementLayer 0=地面、1=低空；'
        'AltitudeMeters 地面填0，低空填30–99。改变表值后需要按原数据管线重新导入和导出清单。\n'
        '本轮已完成编译、数据和实体回读；助手未启动游戏、未读图核验，实际运动和视觉待玩家确认。')
    note = actor(unreal.Note, 'PlayerInstructions', origin + unreal.Vector(0, 1200, 400))
    note.set_editor_property('text', instructions)
    camera_location = origin + unreal.Vector(14000, -14000, 10000)
    camera = actor(unreal.CameraActor, 'ReviewCamera', camera_location)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
        camera_location, origin + unreal.Vector(0, 0, 3500)), True)
    report['scene_saved'] = bool(level.save_current_level())
    assert report['scene_saved']
    entities = []
    for item in ed.get_all_level_actors():
        if not item.actor_has_tag(TAG):
            continue
        entry = dict(label=item.get_actor_label(), object=item.get_path_name(),
                     cls=item.get_class().get_name(), location=list(item.get_actor_location().to_tuple()),
                     editor_only=item.get_editor_property('is_editor_only_actor'),
                     tags=[str(t) for t in item.tags])
        if isinstance(item, unreal.Note):
            entry['text'] = str(item.get_editor_property('text'))
        if isinstance(item, unreal.CameraActor):
            entry['rotation'] = list(item.get_actor_rotation().to_tuple())
        if isinstance(item, unreal.StaticMeshActor):
            component = item.get_component_by_class(unreal.StaticMeshComponent)
            entry.update(mesh=component.static_mesh.get_path_name(),
                         materials=[m.get_path_name() if m else None for m in component.get_materials()],
                         collision=str(component.get_collision_enabled()),
                         collision_profile=str(component.get_collision_profile_name()),
                         affects_navigation=component.get_editor_property('can_ever_affect_navigation'))
            assert component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            assert entry['collision_profile'] == 'NoCollision'
            assert not entry['affects_navigation']
        entities.append(entry)
    assert len(entities) == 14
    assert sum(item['editor_only'] for item in entities) == 9
    points = {item['label']: item['location'] for item in entities}
    for label, altitude in (('Low30m', 3000), ('Low99m', 9900)):
        delta = [a - b for a, b in zip(points['GroundLowAir_' + label], points['GroundLowAir_Ground'])]
        assert delta == [0, 0, altitude]
    report.update(entities=sorted(entities, key=lambda item: item['label']),
                  map=world.get_path_name().split('.')[0],
                  center=list(origin.to_tuple()), runtime_validation='not_run', visual_validation='player_pending')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(success=False, pid=os.getpid(), data_applied=False, manifests_applied=False,
                  scene_saved=False, runtime_validation='not_run')
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Preserve the player session'
        assert world.get_path_name().split('.')[0] == MAP
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved asset edits'
        ed = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        navs = [a for a in ed.get_all_level_actors()
                if isinstance(a, unreal.RecastNavMesh) and 'CommanderSoldier' in a.get_name()]
        assert len(navs) == 1, 'Use the CommanderSoldier navigation data'
        assert not unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world)
        apply_data(report)
        bake = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
        report['resource_bake'] = dict(success=bool(bake.success), message=str(bake.message),
                                       source_hash=str(bake.source_hash), layout_hash=str(bake.layout_hash),
                                       initial_soldiers=bake.initial_soldier_count,
                                       validated_initial_soldiers=bake.validated_initial_soldier_count)
        assert bake.success, str(bake.message)
        export_manifests(report)
        author_scene(report, level, world, navs[0])
        navigation = unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
        report['navigation_bake'] = dict(
            success=bool(navigation.success), message=str(navigation.message),
            entries=[dict(kind=str(item.kind), status=str(item.status), message=str(item.message))
                     for item in navigation.entries])
        assert navigation.success, str(navigation.message)
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    path = OUT / 'scene-delivery.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(
        success=report['success'], data_applied=report['data_applied'], manifests_applied=report['manifests_applied'],
        scene_saved=report['scene_saved'], entities=len(report.get('entities', [])),
        error=report.get('error'), report=str(path))))


main()

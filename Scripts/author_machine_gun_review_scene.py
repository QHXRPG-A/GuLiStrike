"""Persist a scoped editor comparison stage in the existing commander map.

The stage is editor-only and inactive by default. Real combat acceptance uses
the map's existing armies. Run again after the native/asset update to populate
the new light arrays on the comparison components.
"""
import json
from pathlib import Path
import sys
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/MachineGunEffects_20260930'
sys.path.insert(0, str(ROOT / 'Scripts/Vfx'))
from vfx_registry import resource

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MachineGunReview20260930'
BASE = unreal.Vector(-10000, -12000, 1000)
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
report = {'success': False, 'runtime_verified': False, 'actors': []}
try:
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    assert not dirty or (globals().get('GULI_MACHINE_GUN_REVIEW_RESUME', False)
                         and all(p.get_name() == MAP for p in dirty)), 'Save existing map work before authoring this stage'
    owned = {a.get_actor_label(): a for a in api.get_all_level_actors() if TAG in list(map(str, a.tags))}

    def spawn(cls, label, offset):
        actor = owned.get(label) or api.spawn_actor_from_class(cls, BASE + unreal.Vector(*offset))
        actor.set_actor_label(label)
        actor.set_editor_property('tags', [TAG])
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_folder_path('Review/MachineGunEffects_20260930')
        report['actors'].append({'label': label, 'class': actor.get_class().get_name(), 'path': actor.get_path_name()})
        return actor

    floor = spawn(unreal.StaticMeshActor, 'MachineGunReview_Ground', (1500, 2000, -40))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(90, 80, 0.5))
    floor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    floor.static_mesh_component.set_cast_shadow(False)
    flash = unreal.load_asset(resource('GroundMachineGunMuzzle'))
    for label, offset, scale in [
        ('MachineGunReview_MuzzleBefore', (0, 0, 467.17), 1.0),
        ('MachineGunReview_MuzzleWM01After', (1800, 0, 467.17), 2.0),
        ('MachineGunReview_MuzzleSweeperAfter', (3600, 0, 117.62), 1.0),
        ('MachineGunReview_ImpactBefore', (0, 2200, 100), 1.0),
        ('MachineGunReview_ImpactAfter', (1800, 2200, 100), 2.0)]:
        actor = spawn(unreal.NiagaraActor, label, offset)
        component = actor.get_component_by_class(unreal.NiagaraComponent)
        component.set_auto_activate(False)
        component.set_asset(flash)
        component.deactivate()
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        component.set_cast_shadow(False)
        report['actors'][-1].update(scale=scale, asset=component.get_editor_property('asset').get_path_name())

    system_path = '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool'
    system = unreal.load_asset(system_path)
    ready = 'User.LaserLightEnabled' in list(map(str, unreal.NiagaraService.summarize(system_path).user_parameter_names))
    for label, offset, enabled in [('MachineGunReview_TracerBefore', (0, 4400, 0), False),
                                    ('MachineGunReview_TracerAfter', (2500, 4400, 0), True)]:
        actor = spawn(unreal.NiagaraActor, label, offset)
        component = actor.get_component_by_class(unreal.NiagaraComponent)
        component.set_auto_activate(False)
        component.set_asset(system)
        component.deactivate()
        component.set_cast_shadow(False)
        component.set_editor_property('allow_cull_distance_volume', False)
        if ready:
            arrays = unreal.NiagaraDataInterfaceArrayFunctionLibrary
            positions = [unreal.Vector(0, 0, 0) for _ in range(1024)]
            directions = [unreal.Vector(1, 0, 0) for _ in range(1024)]
            sizes = [unreal.Vector2D(0, 0) for _ in range(1024)]
            colors = [unreal.LinearColor(0, 0, 0, 0) for _ in range(1024)]
            light_colors = colors.copy()
            radii, lights = [0.0] * 1024, [False] * 1024
            for i in range(6):
                positions[i] = actor.get_actor_location() + unreal.Vector((i % 3) * 400, (i // 3) * 850, 117.62 if i < 3 else 467.17)
                sizes[i] = unreal.Vector2D(20, 180)
                tint = (1, 1, 0) if i % 2 == 0 else (1, 0.025, 0.015)
                colors[i] = unreal.LinearColor(*(v * 24 for v in tint), 1)
                light_colors[i] = unreal.LinearColor(*tint, 25 if enabled else 0)
                radii[i], lights[i] = (800.0, True) if enabled else (0.0, False)
            arrays.set_niagara_array_position(component, 'User.LaserPositions', positions)
            arrays.set_niagara_array_vector(component, 'User.LaserDirections', directions)
            arrays.set_niagara_array_vector2d(component, 'User.LaserSizes', sizes)
            arrays.set_niagara_array_color(component, 'User.LaserColors', colors)
            arrays.set_niagara_array_position(component, 'User.LaserLightPositions', positions)
            arrays.set_niagara_array_color(component, 'User.LaserLightColors', light_colors)
            arrays.set_niagara_array_float(component, 'User.LaserLightRadii', radii)
            arrays.set_niagara_array_bool(component, 'User.LaserLightEnabled', lights)
            # Niagara instance bounds are component-local; the uploaded particles are world-space.
            component.set_system_fixed_bounds(unreal.Box(min=unreal.Vector(-800, -800, -800),
                                                        max=unreal.Vector(1600, 1650, 1300)))
        report['actors'][-1].update(light_arrays_bound=ready, requested_light_count=6 if enabled else 0)

    camera = spawn(unreal.CameraActor, 'MachineGunReview_Camera', (1500, -4500, 7800))
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), BASE + unreal.Vector(1500, 2400, 0)), False)
    camera.camera_component.set_field_of_view(60)
    note = spawn(unreal.Note, 'MachineGunReview_Instructions', (-1200, -1000, 300))
    note.set_editor_property('text', '机枪对照：枪口原版/重防号2倍/扫荡者原尺寸；命中原版/全部机枪2倍；弹道无灯/6盏800cm灯。样本仅编辑器显示且默认停播。新C++加载并运行apply_machine_gun_effects.py后重跑本场景脚本。实战使用本Map原有双方部队，检查出膛、移动照明、命中和停止后的清理；僚机及玩家机枪仅命中放大。')
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    report.update(success=True, map=world.get_path_name(), lighting_arrays_ready=ready,
                  readback=[{'label': a.get_actor_label(), 'position': list(a.get_actor_location().to_tuple()),
                             'editor_only': a.get_editor_property('is_editor_only_actor')}
                            for a in api.get_all_level_actors() if TAG in list(map(str, a.tags))])
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k: report.get(k) for k in ('success', 'map', 'lighting_arrays_ready', 'error')}, ensure_ascii=False))

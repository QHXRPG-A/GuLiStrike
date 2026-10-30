"""Refresh existing editor-only outposts using the production native floating layout."""
import json
import math
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUTPUT = ROOT / 'Artifacts/OutpostFloat20261029'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
POSE_TAG = 'GuLi.CommanderIsland.EditorPose'
OWNER_TAG = 'GuLi.OutpostPreview.Owner.'
DELIVERY_TAG = 'GuLi.OutpostScale.Reference.20261028'


def point(value):
    return list(value.to_tuple())


def load_layout():
    native = getattr(unreal.GuLiCommanderIslandAuthoringLibrary,
                     'get_outpost_presentation_layout_json', None)
    if not native:
        raise RuntimeError('Load the current outpost native editor module before refreshing the scene.')
    layout = json.loads(native())
    if (not layout['success'] or not layout['using_reference_size'] or layout['warning']
            or not layout.get('always_floating')):
        raise RuntimeError('The current always-floating outpost module is required: ' + repr(layout))
    layout['source'] = 'native_shared_layout'
    layout['native_code_loaded'] = True
    return layout


def sample_float_height_cm(layout, elapsed):
    """Analytical metadata reference; no gameplay or automated test is run."""
    return layout['hover_height_cm'] - layout['float_amplitude_cm'] * math.cos(
        2 * math.pi * max(0.0, elapsed) / layout['float_period_seconds'])


def _owner(actor):
    for tag in actor.tags:
        if str(tag).startswith(OWNER_TAG):
            return str(tag)[len(OWNER_TAG):]
    mesh = actor.static_mesh_component
    for index in range(mesh.get_num_materials()):
        material = mesh.get_material(index)
        if material:
            for name in ('Red', 'Blue', 'Neutral'):
                if material.get_name() == 'MI_OutpostGlow_' + name:
                    return name
    raise RuntimeError('Missing initial-owner metadata on ' + actor.get_actor_label())


def _preview_color(component, model_id, owner):
    # Saved CPD defaults mirror the existing model paint contract, with Red as the view team.
    models = json.loads((ROOT / 'data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf-8-sig'))
    row = next(r for r in models if r['Id'] == model_id)
    if not row['bTeamColorEnabled']:
        return
    for offset, suffix in ((8, 'PrimaryHex'), (12, 'SecondaryHex')):
        color = row[('Blue' if owner == 'Red' else 'Enemy') + suffix] if owner != 'Neutral' else '#2C3735'
        rgb = [int(color[i:i+2], 16) / 255 for i in (1, 3, 5)]
        linear = [v / 12.92 if v <= 0.04045 else ((v + .055) / 1.055) ** 2.4 for v in rgb]
        for index, value in enumerate([*linear, 1.0]):
            component.set_default_custom_primitive_data_float(offset + index, value)
    component.set_default_custom_primitive_data_float(16, 1.0)
    component.set_default_custom_primitive_data_float(17, 0.0 if owner == 'Neutral' else .35)
    if model_id == 2003:
        component.set_default_custom_primitive_data_float(18, 0.0 if owner == 'Neutral' else 1.0)


def prepare_scene():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors_api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = editor.get_editor_world()
    if world.get_path_name().split('.')[0] != MAP or editor.get_game_world():
        raise RuntimeError('Open the corresponding commander map outside PIE before preparing references.')
    layout = load_layout()
    actors = actors_api.get_all_level_actors()
    markers = {a.get_actor_label(): a for a in actors if a.get_class().get_name() == 'GuLiMapMarker'
               and a.get_actor_label().startswith('Outpost_R')}
    poses = [a for a in actors if POSE_TAG in [str(t) for t in a.tags]]
    if not poses or len(poses) != len(markers) or not all(k in markers for k in ('Outpost_R1C4', 'Outpost_R4C4')):
        raise RuntimeError('Existing outpost pose/marker identities are incomplete.')
    # Validate identities and owner metadata before the first scene mutation.
    owners = {}
    for actor in poses:
        key = actor.get_actor_label().removeprefix('EditorPose_')
        if key not in markers or not isinstance(actor, unreal.StaticMeshActor):
            raise RuntimeError('Invalid existing outpost reference: ' + key)
        owners[key] = _owner(actor)
    mesh = unreal.load_object(None, layout['mesh'])
    for actor in poses:
        key = actor.get_actor_label().removeprefix('EditorPose_')
        owner = owners[key]
        height = layout['hover_height_cm']
        actor.modify()
        actor.set_actor_scale3d(unreal.Vector(*layout['mesh_scale']))
        actor.set_actor_location_and_rotation(markers[key].get_actor_location()
            + unreal.Vector(*layout['ground_offset_cm']) + unreal.Vector(0, 0, height), unreal.Rotator(), False, True)
        component = actor.static_mesh_component
        component.set_static_mesh(mesh)
        component.set_editor_property('override_materials', [])
        component.set_overlay_material(unreal.load_asset(
            '/Game/GuLiStrike/Buildings/OutpostSymmetric/Materials/M_OutpostSymmetric_Outline'))
        _preview_color(component, layout['model_id'], owner)
        component.set_collision_profile_name('NoCollision', True)
        component.set_editor_property('can_ever_affect_navigation', False)
        actor.set_actor_enable_collision(False)
        actor.set_actor_hidden_in_game(True)
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_editor_property('tags', [t for t in actor.tags if not str(t).startswith(OWNER_TAG)] + [OWNER_TAG + owner])

    def fixture(label, cls, location):
        found = [a for a in actors if a.get_actor_label() == label]
        if len(found) > 1 or (found and DELIVERY_TAG not in [str(t) for t in found[0].tags]):
            raise RuntimeError('Duplicate or unowned scene reference: ' + label)
        actor = found[0] if found else actors_api.spawn_actor_from_class(cls, location)
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path('CommanderIsland/Review/OutpostScale_20261028')
        actor.set_editor_property('tags', [DELIVERY_TAG])
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_actor_hidden_in_game(True)
        actor.set_actor_enable_collision(False)
        return actor

    anchor = markers['Outpost_R1C4'].get_actor_location()
    reference_ground = anchor + unreal.Vector(4000, 0, 0)
    landscape = next(a for a in actors if isinstance(a, unreal.Landscape))
    height = unreal.LandscapeService.get_height_at_location(
        landscape.get_actor_label(), reference_ground.x, reference_ground.y)
    if not height.valid:
        raise RuntimeError('No terrain under the shield size reference.')
    reference_ground.z = height.height
    reference = fixture('OutpostScale_ShieldReference_R1C4', unreal.StaticMeshActor, reference_ground)
    reference.set_actor_scale3d(unreal.Vector(*layout['reference_mesh_scale']))
    reference.set_actor_location_and_rotation(reference_ground + unreal.Vector(*layout['reference_ground_offset_cm']),
                                               unreal.Rotator(), False, True)
    reference.static_mesh_component.set_static_mesh(unreal.load_object(None, layout['reference_mesh']))
    reference.static_mesh_component.set_collision_profile_name('NoCollision', True)
    reference.static_mesh_component.set_editor_property('can_ever_affect_navigation', False)
    _preview_color(reference.static_mesh_component, 2005, 'Red')
    note = fixture('OutpostScale_Readme', unreal.Note, anchor + unreal.Vector(0, 2500, 500))
    size_m = [v / 100 for v in layout['size_cm']]
    center_m = layout['hover_height_cm'] / 100
    amplitude_m = layout['float_amplitude_cm'] / 100
    period = layout['float_period_seconds']
    motion_text = (f'全部据点平均离地{center_m:g}米，上下各{amplitude_m:g}米，{period:g}秒一周期。'
                   f'绕竖直轴{layout["self_rotation_period_seconds"]:g}秒一圈。'
                   '中立、己方、敌方始终悬浮并自转；换主不落地、不重置相位。')
    note.set_actor_location(anchor + unreal.Vector(0, 2500, 500), False, True)
    note.set_editor_property('text',
        '据点巨物尺寸与轻微浮动\n'
        f'长宽高各为护盾发生器{layout["size_multiplier"]:g}倍；约{size_m[0]:.1f} × {size_m[1]:.1f} × {size_m[2]:.1f}米。\n'
        'R1C4：' + motion_text + '\n'
        'R4C4：中立也保持同样的悬浮高度；占领和恢复中立只改变阵营表现。\n'
        '右侧40米处护盾模型仅作编辑器尺寸参照；全部预览在游戏中隐藏。\n'
        '正常指挥官入口验证。点选悬浮模型后运输目标仍为地面；玩法碰撞与占领范围不变。\n'
        + '上下对称新模型；中央光柱中立白、己方蓝、敌方红，光柱与灯槽增强HDR并使用现有Bloom。请玩家验收近远景、浮空自转和换主效果。')
    updated_notes = [note.get_actor_label()]
    for existing in actors:
        if not isinstance(existing, unreal.Note):
            continue
        text = str(existing.get_editor_property('text'))
        replacement = None
        if text.startswith('据点占领与浮动'):
            replacement = ('据点占领与浮动\n全部状态始终悬浮，尺寸为护盾发生器三轴各'
                f'{layout["size_multiplier"]:g}倍。派兵占领R2C4，确认阵营变化时高度连续。\n'
                + motion_text + '\n点击悬浮模型，命令终点仍在地面。详见 OutpostScale_Readme。')
        elif text.startswith('中央据点R4C4：换主/中立验收'):
            replacement = ('中央据点R4C4：换主/中立验收\n中立即悬浮，派兵占领前后均有明显离地间隙；'
                '红蓝换主和恢复中立不落地、不重置相位。\n' + motion_text)
        if replacement is not None:
            existing.modify()
            existing.set_editor_property('text', replacement)
            updated_notes.append(existing.get_actor_label())
    if not levels.save_current_level():
        raise RuntimeError('Unable to save the commander map.')

    entities = []
    max_size_error = 0.0
    max_bottom_error = 0.0
    bottom_clearances = []
    for actor in [*poses, reference]:
        origin, extent = actor.get_actor_bounds(False)
        expected_size = layout['reference_size_cm'] if actor == reference else layout['size_cm']
        size = point(extent * 2)
        size_error = max(abs(a - b) for a, b in zip(size, expected_size))
        max_size_error = max(max_size_error, size_error)
        if actor != reference:
            key = actor.get_actor_label().removeprefix('EditorPose_')
            expected_bottom = markers[key].get_actor_location().z + layout['hover_height_cm']
            max_bottom_error = max(max_bottom_error, abs(origin.z - extent.z - expected_bottom))
            bottom_clearances.append(origin.z - extent.z - markers[key].get_actor_location().z)
        entities.append(dict(label=actor.get_actor_label(), actor=actor.get_path_name(),
            mesh=actor.static_mesh_component.get_editor_property('static_mesh').get_path_name(),
            location_cm=point(actor.get_actor_location()), scale=point(actor.get_actor_scale3d()), size_cm=size,
            editor_only=actor.get_editor_property('is_editor_only_actor'), hidden_in_game=actor.get_editor_property('hidden'),
            actor_collision=actor.get_actor_enable_collision()))
    if max_size_error > 0.1 or max_bottom_error > 0.1:
        raise RuntimeError(f'Saved scene dimensions/ground alignment differ: {max_size_error}, {max_bottom_error}')
    report = dict(success=True, map=MAP, saved=True, layout=layout, outpost_count=len(poses),
        entities=entities, note=note.get_actor_label(), updated_notes=updated_notes, max_size_error_cm=max_size_error,
        max_bottom_error_cm=max_bottom_error, minimum_preview_bottom_clearance_cm=min(bottom_clearances),
        maximum_preview_bottom_clearance_cm=max(bottom_clearances), pie_run=False, automated_tests_run=False,
        visual_verification='Awaiting player acceptance',
        native_runtime_verification='Not run; player acceptance pending')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'scene-delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return {k: report[k] for k in ('success', 'map', 'saved', 'outpost_count', 'max_size_error_cm', 'max_bottom_error_cm')} | {
        'native_code_loaded': layout['native_code_loaded'], 'evidence': str(OUTPUT / 'scene-delivery.json')}

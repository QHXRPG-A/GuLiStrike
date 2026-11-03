"""Save and read back the existing Ground HUD player entry; never start PIE."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundToonIcons_20261103'
report = {'success': False, 'pie_started': False, 'visual_review': 'pending_user'}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    map_path = '/Game/Maps/LVL_CommanderMassPrototype'
    assert world.get_path_name().split('.')[0] == map_path
    dirty_maps = [package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    assert not dirty_maps, 'Preserve existing unsaved maps: ' + str(dirty_maps)
    actors = {actor.get_actor_label(): actor for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    names = ['GroundHUD_v1_PlayerEntry', 'GroundHUD_ShieldTarget', 'GroundHUD_KillTarget', 'GroundRogue_Entry']
    assert all(name in actors for name in names)
    note = actors[names[0]]
    original = str(note.get_editor_property('text'))
    line = '图标 A_v4：3D三渲二三档描边；子弹铜金/深灰统一，喷气跳跃与容量共用双喷口，闪现为淡蓝机甲/闪电/残影。F4取得强化后查看图标及等级；Space喷气、Shift闪现，键位/CD/动效保持。图标表唯一资源7张，均按原比例显示。视觉由玩家核验。'
    lines = [value for value in original.splitlines() if not value.startswith('图标 A_v4：')]
    note.set_editor_property('text', '\n'.join(lines + [line]))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, map_path)
    report['map'] = map_path
    report['entities'] = []
    for name in names:
        actor = actors[name]
        p = actor.get_actor_location()
        report['entities'].append({'label': name, 'path': actor.get_path_name(),
                                   'class': actor.get_class().get_name(), 'location': [p.x, p.y, p.z]})
    report['note'] = str(note.get_editor_property('text'))
    pawn = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light'))
    assert pawn.get_editor_property('model_id') == 4001
    mesh = pawn.get_editor_property('mesh').get_skeletal_mesh_asset()
    assert '/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2.' in mesh.get_path_name()
    report['ground_pawn'] = {'class': pawn.get_class().get_path_name(), 'model_id': 4001, 'mesh': mesh.get_path_name()}
    report['dirty_maps_after'] = [package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    assert not report['dirty_maps_after']
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    (OUT / 'scene_readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'report': str(OUT / 'scene_readback.json'), 'error': report.get('error', '')}))

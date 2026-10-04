"""Save native WM01 v3 production routing and existing map notes; never start PIE."""
import json
import math
import runpy
import traceback
from pathlib import Path

import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/WM01MissileCards/Visual_v3/ProductionActivation'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
PROJECTILE = '/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile'
BASE = '/Game/GuLiStrike/FX/WM01Missiles/'
LIB = unreal.EditorAssetLibrary
NS = unreal.NiagaraService
report = {'success': False, 'authorization': '赶紧切换啊；代码层面彻底替换啊',
          'systems': [], 'actors': [], 'gameplay_validation': 'not_run',
          'performance_measurement': 'not_run'}

try:
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert not level.is_in_play_in_editor(), 'Finish PIE before asset authoring'
    assert world.get_path_name().split('.')[0] == MAP
    definition = unreal.load_asset(PROJECTILE)
    assert hasattr(definition, 'uses_missile_cluster_rendering'), 'Load the authorized native build first'
    assert definition.uses_missile_cluster_rendering(), 'The WM01 table must select production GPU rendering'
    read_profile = runpy.run_path(str(ROOT / 'Scripts/wm01_missile_visual_config.py'))['read_profile']
    profile, parameters = read_profile(unreal)
    report['projectile_profile'] = profile
    report['visual_parameters'] = parameters
    registry = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(registry))
    for quality, lanes, vfx_id in [('Full', 24, 49), ('Lite', 8, 50), ('Minimal', 0, 51)]:
        path = BASE + 'NS_WM01MissileCluster_' + quality
        system = unreal.load_asset(path)
        assert system, path
        row = next(r for r in rows if int(r['Id']) == vfx_id)
        assert row['ResourcePath'] == system.get_path_name(), row
        assert LIB.get_metadata_tag(system, 'GuLi.WM01Missiles.VisualRevision').startswith('3;')
        assert int(NS.get_parameter(path, 'User.MissileContractVersion').current_value) == 2
        actual = {name: float(NS.get_parameter(path, name).current_value) for name in parameters}
        assert all(math.isclose(actual[name], value, abs_tol=1e-3) for name, value in parameters.items())
        diagnostics = unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
        entry = {'path': path, 'vfx_id': vfx_id, 'lanes': lanes, 'parameters': actual, 'compiled_now': False}
        report['systems'].append(entry)
        if 'System valid=1 ready=1 gpu=1' not in diagnostics or diagnostics.count('GPU finished=1 complete=1') != (3 if lanes else 2):
            result = NS.compile_with_results(path)
            entry['compiled_now'] = True
            entry['compile_errors'] = [str(error) for error in result.errors]
            assert result.success and not result.errors, str(result)
            diagnostics = unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
        entry['diagnostics'] = diagnostics
        entry['runtime_ready'] = ('System valid=1 ready=1 gpu=1' in diagnostics
            and diagnostics.count('GPU finished=1 complete=1') == (3 if lanes else 2))
        assert entry['runtime_ready'], entry

    report['fallback_vfx_id'] = definition.get_editor_property('flight_vfx_id')
    report['impact_field'] = definition.get_editor_property('impact_field').get_path_name()
    LIB.set_metadata_tag(definition, 'GuLi.WM01Missiles.ProductionRevision', '3;native WM01/MissileLauncher routing;authorized 2026-09-30')
    assert LIB.save_loaded_asset(definition, False)
    # Reload the saved package rather than reporting only the current in-memory object.
    reloaded, error = unreal.EditorLoadingAndSavingUtils.reload_packages(
        [definition.get_outer()], unreal.ReloadPackagesInteractionMode.ASSUME_NEGATIVE)
    assert reloaded and not str(error), str(error)
    definition = unreal.load_asset(PROJECTILE)
    report['production_route_after_package_reload'] = bool(definition.uses_missile_cluster_rendering())
    assert report['production_route_after_package_reload']
    report['production_revision'] = LIB.get_metadata_tag(definition, 'GuLi.WM01Missiles.ProductionRevision')
    assert report['production_revision'].startswith('3;native')
    assert definition.get_editor_property('flight_vfx_id') == report['fallback_vfx_id']
    assert definition.get_editor_property('impact_field').get_path_name() == report['impact_field']

    replacements = [
        ('导弹候选 v3；加载本轮原生视觉字段及三张表后使用。', '导弹 v3；已按用户授权接入原生默认表现。'),
        ('候选开关：gs.MissileCluster.Candidate 1；旧表现对照：gs.MissileCluster.Candidate 0。正式 DA 开关保持关闭，待用户可播放候选审核。',
         '原生代码按WM01/MissileLauncher表记录默认使用v3；无需候选或资产开关，所有游戏地图生效，重开仍生效。旧表现对照：gs.MissileCluster.Enabled 0；恢复v3：gs.MissileCluster.Enabled 1。'),
        ('新旧对照仅切 Candidate 0/1', '新旧对照仅切 gs.MissileCluster.Enabled 0/1'),
        ('先 gs.MissileCluster.Candidate 1，再 gs.MissileFixture.Build 100 1', 'v3默认已启用，执行 gs.MissileFixture.Build 100 1'),
        ('正式 DA 引用保持候选审核前状态。', 'WM01原生默认使用v3，无需候选或资产开关；旧表现对照用 gs.MissileCluster.Enabled 0，恢复用1。'),
    ]
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    related = [actor for actor in editor.get_all_level_actors()
        if actor.actor_has_tag('WM01MissileReview20260929')
        or actor.get_actor_label() in ['RogueCards_F4_Entry', 'CommanderLOD_Instructions']]
    labels = {actor.get_actor_label() for actor in related}
    assert {'RogueCards_F4_Entry', 'WM01Missile_CandidateInstructions', 'WM01Missile_Camera_Near', 'WM01Missile_Camera_Overview'} <= labels
    assert all(f'WM01Missile_Case_{count}x{salvo}' in labels for count in (100, 500) for salvo in (1, 4, 8))
    for actor in related:
        if isinstance(actor, unreal.Note):
            before = str(actor.get_editor_property('text'))
            text = before
            for old, new in replacements:
                text = text.replace(old, new)
            if actor.get_actor_label() == 'WM01Missile_CandidateInstructions':
                marker = '\n[WM01 v3 原生正式接入 20260930]'
                text = text.split(marker)[0] + marker + '\n代码按Projectiles表UnitTypeId=2和MissileLauncher选择v3，已移除候选命令、原型地图限制及资产勾选开关；资源未就绪时保留可见回退。功能及性能仍按既有验收记录。'
            assert 'gs.MissileCluster.Candidate' not in text, actor.get_actor_label()
            if text != before:
                actor.set_editor_property('text', text)
    report['map_saved'] = bool(level.save_current_level())
    assert report['map_saved']
    for actor in related:
        entry = {'label': actor.get_actor_label(), 'object': actor.get_path_name(),
                 'location': list(actor.get_actor_location().to_tuple()), 'tags': [str(tag) for tag in actor.tags]}
        if isinstance(actor, unreal.Note):
            entry['text'] = str(actor.get_editor_property('text'))
        report['actors'].append(entry)
    report['map'] = world.get_path_name()
    report['default_cluster_enabled'] = unreal.SystemLibrary.get_console_variable_int_value('gs.MissileCluster.Enabled')
    report['default_visuals_enabled'] = unreal.SystemLibrary.get_console_variable_int_value('gs.CombatEffects.Visuals')
    assert report['default_cluster_enabled'] == 1 and report['default_visuals_enabled'] == 1
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()

OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'production-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'error': report.get('error'),
    'systems': len(report['systems']), 'actors': len(report['actors']),
    'native_production_route': report.get('production_route_after_package_reload')}))

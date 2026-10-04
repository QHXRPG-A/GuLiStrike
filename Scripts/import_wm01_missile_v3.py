"""Apply v3 through the existing importer/catalog author; verify production routing from the table."""
import ast
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'Artifacts/WM01MissileCards/Visual_v3'
OUT.mkdir(parents=True, exist_ok=True)
report = {'success': False, 'tables': [], 'profiles': []}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    definition = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
    assert definition.uses_missile_cluster_rendering(), 'WM01 must use production GPU rendering'
    field = definition.get_editor_property('impact_field')
    reference = float(field.get_editor_property('visual_reference_radius'))
    visual_table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    visual_before = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(visual_table)
    source = (ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8-sig')
    scope = {'__name__': 'wm01_v3_table_import'}
    exec(compile(source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0],
                 'import_data_to_engine.py', 'exec'), scope)
    scope['PROGRESS'] = str(OUT/'table-import.log')
    manifest = json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf-8'))
    for name in ['DT_GuLiStrikeSecondaryWeapons_Projectiles', 'DT_GuLiStrikeSpellFields_Fields',
                 'DT_GuLiStrikeCommander_UnitSkills']:
        result = scope['import_table'](name, manifest['tables'][name])
        report['tables'].append(result)
        assert result.get('imported'), result
    report['profiles'] = scope['wire_secondary_projectile_profiles']()
    tree = ast.parse((ROOT/'Scripts/author_secondary_unit_skill_assets.py').read_text(encoding='utf-8-sig'))
    tree.body = [node for node in tree.body if not (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                 and isinstance(node.value.func, ast.Name) and node.value.func.id == 'author')]
    scope = {'__name__': 'wm01_v3_skill_author'}
    exec(compile(tree, 'author_secondary_unit_skill_assets.py', 'exec'), scope)
    scope['OUT'] = OUT
    scope['author']()
    assert float(field.get_editor_property('visual_reference_radius')) == reference
    assert unreal.DataTableFunctionLibrary.export_data_table_to_json_string(visual_table) == visual_before
    production_after = bool(definition.uses_missile_cluster_rendering())
    assert production_after, 'Data import must preserve WM01 production rendering'
    report.update(success=True, explosion_visual_reference_radius=reference,
                  vfx_registry_unchanged=True, production_cluster_enabled=production_after,
                  production_routing_verified=True)
except Exception:
    report['error'] = traceback.format_exc()
(OUT/'data-import.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'error': report.get('error'),
    'tables': len(report['tables']), 'profiles': len(report['profiles'])}))

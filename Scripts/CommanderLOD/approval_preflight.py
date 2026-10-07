"""Read loaded editor capabilities and the candidate dependency graph."""
import json
import unreal
from pathlib import Path

root = Path('D:/UE5.7/test1')
art = root / 'ArtSource/CommanderLOD_20261005'
registry = unreal.AssetRegistryHelpers.get_asset_registry()
lib = unreal.EditorAssetLibrary
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assets = lib.list_assets('/Game/GuLiStrike/Commander/LODReview_20261005', True, False)
details = []
options = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,
    include_soft_package_references=True, include_searchable_names=False,
    include_soft_management_references=False, include_hard_management_references=False)
for path in assets:
    asset = lib.load_asset(path)
    package = asset.get_path_name().split('.')[0]
    details.append(dict(path=asset.get_path_name(), class_name=asset.get_class().get_name(),
        dependencies=[str(p) for p in registry.get_dependencies(package, options)]))
vat = unreal.load_asset('/Game/GuLiStrike/Commander/Units/BiZhiMao/VAT/DA_BiZhiMao_VAT')
if vat is None and hasattr(unreal, 'GuLiVATDefinition'):
    vat = unreal.get_default_object(unreal.GuLiVATDefinition)
fields = {}
for name in ['vertex_animation', 'vertex_lo_ds', 'vertex_lods', 'directional_blend', 'bones', 'bone_deltas', 'clips']:
    try:
        value = vat.get_editor_property(name)
        fields[name] = dict(available=True, size=len(value) if hasattr(value, '__len__') else None,
                           value=str(value) if isinstance(value, (bool, int, float)) else None)
    except Exception as error:
        fields[name] = dict(available=False, error=str(error))
dirty = [a.get_path_name() for a in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report = dict(success=True, world=editor.get_editor_world().get_path_name(),
    game_world=editor.get_game_world().get_path_name() if editor.get_game_world() else None,
    candidate_assets=details, vertex_fields=fields, dirty_packages=dirty)
report['native_tables'] = {}
for name in ['DT_GuLiStrikeCommander_Soldiers', 'DT_GuLiStrikeBuildings_Buildings']:
    table = lib.load_asset('/Game/GuLiStrike/Data/' + name)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    report['native_tables'][name] = rows
(art / 'Reports/native_tables_before_switch.json').write_text(
    json.dumps(report['native_tables'], ensure_ascii=False, indent=2), encoding='utf8')
(art / 'Reports/approval_preflight.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, asset_count=len(details),
    vertex_fields=fields, dirty_packages=dirty, game_world=report['game_world'],
    native_soldier_fields=list(report['native_tables']['DT_GuLiStrikeCommander_Soldiers'][0]))))

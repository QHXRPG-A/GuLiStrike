"""Check the restarted editor's reflected fields; no gameplay execution."""
import json
from pathlib import Path
import unreal

ART = Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005')
ed = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
report = dict(success=True, editor_world=ed.get_editor_world().get_path_name(),
    game_world=str(ed.get_game_world()), engine=unreal.SystemLibrary.get_engine_version(),
    dirty_packages=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    definition_documentation=unreal.GuLiVATDefinition.__doc__,
    vertex_lod_documentation=unreal.GuLiVertexVATLOD.__doc__)
for name in ('DT_GuLiStrikeCommander_Soldiers', 'DT_GuLiStrikeBuildings_Buildings'):
    dt = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
    report[name] = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
assert all(f in report['DT_GuLiStrikeCommander_Soldiers'][0] for f in ('FacingPolicy','MassAvoidanceRadiusMeters','bConstructionOnly'))
(ART / 'Reports/native_loaded_fields.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({k:v for k,v in report.items() if not k.startswith('DT_')}))

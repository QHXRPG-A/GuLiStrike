import json
import os
import runpy
from pathlib import Path
import unreal

project = Path('D:/UE5.7/test1')
assert os.getpid() == json.loads((project / 'Artifacts/CommanderPerformanceHUD/20260923/editor-launch.json').read_text(encoding='utf-8-sig'))['pid']
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
runpy.run_path(str(project / 'Scripts/import_data_to_engine.py'),
              init_globals={'GULI_TABLE_FILTER': {'DT_GuLiStrikeGameTexts_Texts'}})
report = json.loads((project / 'Data/tmp_import_report.json').read_text(encoding='utf-8'))
assert not report['errors'] and len(report['tables']) == 1 and report['tables'][0]['imported'], report
source = json.loads((project / 'Data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8'))
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert {r['Name']: r for r in actual} == {r['Name']: r for r in source}
report['exact_readback_match'] = True
report['dirty_maps'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
report['dirty_content'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
(project / 'Artifacts/CommanderPerformanceHUD/20260923/text-import.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'rows': len(actual), 'exact_readback_match': True,
    'dirty_maps': report['dirty_maps'], 'dirty_content': report['dirty_content']}))

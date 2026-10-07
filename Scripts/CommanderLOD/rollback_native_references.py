"""Restore the exact native pre-switch rows through the established CSV factory."""
import csv
import json
from pathlib import Path
import unreal
ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
before = json.loads((ART / 'Reports/native_tables_before_switch.json').read_text(encoding='utf8'))
pipeline = json.loads((ROOT / 'Data/tmp_import_report.json').read_text(encoding='utf8'))
(ART / 'Reports/native_import_before_compile_failed.json').write_text(json.dumps(pipeline, ensure_ascii=False, indent=2), encoding='utf8')
name = 'DT_GuLiStrikeCommander_Soldiers'
rows = before[name]
file = ART / 'Reports/native_reference_rollback.csv'
with file.open('w', encoding='utf8', newline='') as stream:
    writer = csv.DictWriter(stream, list(rows[0]), lineterminator='\n')
    writer.writeheader()
    writer.writerows({key: 'True' if value is True else 'False' if value is False else value for key, value in row.items()} for row in rows)
table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
settings = unreal.CSVImportSettings()
settings.set_editor_property('import_row_struct', table.get_editor_property('row_struct'))
settings.set_editor_property('import_type', unreal.CSVImportType.ECSV_DATA_TABLE)
factory = unreal.CSVImportFactory()
factory.set_editor_property('automated_import_settings', settings)
task = unreal.AssetImportTask()
for key, value in dict(factory=factory, filename=str(file), destination_path='/Game/GuLiStrike/Data',
    destination_name=name, automated=True, replace_existing=True, save=False).items():
    task.set_editor_property(key, value)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert actual == rows, 'Native rollback does not match the pre-switch snapshot.'
assert unreal.EditorAssetLibrary.save_loaded_asset(table, False)
source = json.loads((ART / 'Reports/source_reference_switch.json').read_text(encoding='utf8'))
source.update(state='rolled_back_source_and_native_verified', native_rollback_verified=True,
    formal_references_switched=False)
(ART / 'Reports/source_reference_switch.json').write_text(json.dumps(source, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, native_references_restored=True, formal_references_switched=False)))

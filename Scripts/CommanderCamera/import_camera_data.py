"""Use the standard pipeline to import only the commander camera table after the native row is loaded."""
from pathlib import Path
import json
import traceback
import unreal


def main():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    destination = root / globals().get('GULI_CAMERA_REPORT_DIR', 'Artifacts/CommanderCamera/20260930') / 'import-readback.json'
    report = {'success': False, 'source_sheet': 'Camera', 'runtime_validation': 'not_run'}
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            raise RuntimeError('End the player session before importing camera data.')
        row_struct = unreal.find_object(None, '/Script/GuLiStrike.GuLiStrikeCommanderCameraRow')
        if row_struct is None:
            raise RuntimeError('Camera row structure is not loaded. First obtain permission for a native build and editor reload.')
        source = root / 'Scripts/import_data_to_engine.py'
        namespace = {'__name__': '__main__', '__file__': str(source),
                     'GULI_TABLE_FILTER': {'DT_GuLiStrikeCommander_Camera'}}
        exec(compile(source.read_text(encoding='utf-8-sig'), str(source), 'exec'), namespace)
        pipeline = namespace['report']
        report['pipeline'] = pipeline
        assert not pipeline['errors'] and len(pipeline['tables']) == 1, 'Camera import pipeline failed'
        assert pipeline['tables'][0].get('imported'), pipeline['tables'][0]
        table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Camera')
        assert table and table.get_editor_property('row_struct') == row_struct
        report['saved'] = bool(unreal.EditorAssetLibrary.save_loaded_asset(table, False))
        assert report['saved'], 'Camera DataTable save failed'
        report['asset'] = table.get_path_name()
        report['struct'] = row_struct.get_path_name()
        report['rows'] = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        settings_class = unreal.load_class(None, '/Script/GuLiStrike.GuLiCommanderDataSettings')
        assert settings_class, 'Commander data settings class is not loaded'
        settings = unreal.get_default_object(settings_class)
        reference = settings.get_editor_property('CameraDataTable')
        report['settings_reference'] = reference.get_path_name() if hasattr(reference, 'get_path_name') else str(reference)
        assert 'DT_GuLiStrikeCommander_Camera' in report['settings_reference'], 'Commander data settings reference is incorrect'
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'report': str(destination),
                                                  'error': report.get('error')}, ensure_ascii=False))


main()

"""Feature-scoped stage 2 import. Run in the idle editor after the Excel exporter."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before importing match configuration'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype', 'Open the prototype map before exporting Hybrid manifests'
root = Path(unreal.Paths.project_dir()).resolve()
import sys
sys.path.insert(0, str(root / 'Scripts'))
soldiers = json.loads((root / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8-sig'))
report = Path(globals().get('REPORT', root / 'data/avoidance-import-report.json'))
scope = {'__name__': '__main__',
         'GULI_MODEL_VALIDATION_IDS': sorted({row['ModelId'] for row in soldiers}),
         'GULI_TABLE_FILTER': {'DT_GuLiStrikeCommander_Soldiers', 'DT_GuLiStrikeAvoidance_Units',
                              'DT_GuLiStrikeAvoidance_BuildingCircles', 'DT_GuLiStrikeAvoidance_OreClustering'},
         'GULI_IMPORT_REPORT': str(report),
         'GULI_IMPORT_PROGRESS': str(report.with_suffix('.log'))}
source = root / 'Scripts/import_data_to_engine.py'
exec(compile(source.read_text(encoding='utf-8'), str(source), 'exec'), scope)
result = json.loads(report.read_text(encoding='utf-8'))
assert not result.get('errors'), result.get('errors')
result['runtime_manifests'] = []
for population in (200, 600):
    target = root / f'Config/Hybrid/RuntimeManifest-{population}.json'
    exported = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(target)))
    assert exported.get('success'), exported
    validated = json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(target)))
    assert validated.get('success'), validated
    manifest = json.loads(target.read_text(encoding='utf-8-sig'))
    result['runtime_manifests'].append({'population': population, 'path': str(target),
                                      'config_hash': manifest['config_hash'], 'validated': True})
report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'report': str(report), 'import': result}))

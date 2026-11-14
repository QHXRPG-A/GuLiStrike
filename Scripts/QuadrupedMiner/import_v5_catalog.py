"""Import the five approved tables through the existing scoped data pipeline."""
import json
import runpy
import sys
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
TABLES = ['DT_GuLiStrikeModels_' + s for s in ['Models', 'Parts', 'MaterialParameters', 'ColorRegions']]
TABLES.append('DT_GuLiStrikeCommander_Soldiers')
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world()
sys.path.insert(0, str(ROOT / 'Scripts'))
result = runpy.run_path(str(ROOT / 'Scripts/import_data_to_engine.py'), init_globals={
    'GULI_TABLE_FILTER': TABLES, 'GULI_MODEL_VALIDATION_IDS': [1003, 8017],
    'GULI_IMPORT_REPORT': str(OUT / 'catalog-import.json'),
    'GULI_IMPORT_PROGRESS': str(OUT / 'catalog-import.log')})['report']
assert not result['errors'], result['errors']
assert len(result['tables']) == 5 and all(x.get('imported') for x in result['tables']), result
registries = list(unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem))
assert registries
for registry in registries: assert registry.reload_catalog()
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, tables=result['tables'], preflight=result['preflight'])))

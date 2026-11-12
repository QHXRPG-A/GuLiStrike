"""Import shield delivery, Pioneer barracks and their match configuration in an idle editor."""
import importlib.util
import json
import sys
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(root / 'Scripts'))
report_dir = root / 'Artifacts/ShieldDelivery20261112'
report_dir.mkdir(parents=True, exist_ok=True)
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'
source = root / 'Scripts/import_data_to_engine.py'
scope = {'__name__': '__main__',
         'GULI_MODEL_VALIDATION_IDS': [2006, 8007, 8008, 8015],
         'GULI_TABLE_FILTER': {'DT_GuLiStrikeModels_Models', 'DT_GuLiStrikeBuildings_Buildings',
                              'DT_GuLiStrikeAvoidance_BuildingCircles', 'DT_GuLiStrikeGameTexts_Texts',
                              'DT_GuLiStrikeVfx_Effects'},
         'GULI_IMPORT_REPORT': str(report_dir / 'data-import.json'),
         'GULI_IMPORT_PROGRESS': str(report_dir / 'data-import.log')}
exec(compile(source.read_text(encoding='utf-8'), str(source), 'exec'), scope)
result = json.loads((report_dir / 'data-import.json').read_text(encoding='utf-8'))
assert not result.get('errors'), result.get('errors')

# Keep the existing acceptance-only combinations, while refreshing production rows.
spec = importlib.util.spec_from_file_location('guli_acceptance_variants', root / 'Scripts/Network/prepare_integrated_content.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
source, rows = m.table('GuLiModelRegistrySettings', 'ModelsTable')
models = unreal.load_asset('/Game/Development/HybridAcceptance/DT_AcceptanceModels')
existing = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(models))
replacements = {row['Id']: row for row in rows if row['Id'] in (2006, 8007, 8008, 8015)}
m.save_table('GuLiModelRegistrySettings', 'ModelsTable', source,
             [replacements.get(row['Id'], row) for row in existing], 'DT_AcceptanceModels')
source, rows = m.table('GuLiBuildingDataSettings', 'BuildingsTable')
m.clone(rows, 'BasicBarracks', Name='AcceptanceActorProduction', Id=101, ProductionUnitId=102)
next(row for row in rows if row['Name'] == 'BasicBarracks')['ProductionUnitId'] = 101
m.clone(rows, 'ResourceFactory', Name='AcceptanceCombinedBuilding', Id=102,
        ProductionUnitId=0, ProductionSeconds=0, ProductionCount=0)
m.save_table('GuLiBuildingDataSettings', 'BuildingsTable', source, rows, 'DT_AcceptanceBuildings')
source, rows = m.table('GuLiAvoidanceDataSettings', 'BuildingCirclesTable')
for original in list(rows):
    target = {4: 101, 6: 102}.get(original['BuildingDefinitionId'])
    if target:
        m.clone(rows, original['Name'], Name='Acceptance_' + original['Name'],
                Id=max(r['Id'] for r in rows) + 1, BuildingDefinitionId=target)
m.save_table('GuLiAvoidanceDataSettings', 'BuildingCirclesTable', source, rows, 'DT_AcceptanceBuildingCircles')
path = root / 'Config/Hybrid/AcceptanceProfile.json'
profile = json.loads(path.read_text(encoding='utf-8-sig'))
keys = {(v['class'], v['property']) for v in m.overrides}
profile['overrides'] = [v for v in profile['overrides'] if (v['class'], v['property']) not in keys] + m.overrides
profile.setdefault('assets', {}).update(m.assets)
profile.setdefault('source_tables', {}).update(m.sources)
path.write_text(json.dumps(profile, ensure_ascii=False, indent=2), encoding='utf-8')
source = root / 'Scripts/Network/refresh_integrated_manifests.py'
scope = {'REPORT': report_dir / 'manifests.json'}
exec(compile(source.read_text(encoding='utf-8'), str(source), 'exec'), scope)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'import': str(report_dir / 'data-import.json'), 'manifests': scope['result']}))

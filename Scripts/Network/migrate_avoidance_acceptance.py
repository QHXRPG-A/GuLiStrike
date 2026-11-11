"""Refresh only the existing unit test variants for the independent circle catalog.

No gameplay is launched. The two synthetic units inherit their source unit's
circle profile; production Excel still contains exactly the six production units.
"""
import importlib.util
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
spec = importlib.util.spec_from_file_location('guli_acceptance_variants', root / 'Scripts/Network/prepare_integrated_content.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
source, rows = m.table('GuLiUnitDataSettings', 'SoldierDataTable')
m.clone(rows, 'DefaultSoldier', Name='AcceptanceCombinedMass', Id=101, ModelId=101001)
m.clone(rows, 'ElectromagneticMiner', Name='AcceptanceCombinedActor', Id=102)
m.save_table('GuLiUnitDataSettings', 'SoldierDataTable', source, rows, 'DT_AcceptanceUnits')
source, rows = m.table('GuLiAvoidanceDataSettings', 'UnitsTable')
m.clone(rows, 'Unit_1', Name='AcceptanceCircle101', Id=101, UnitTypeId=101)
m.clone(rows, 'Unit_3', Name='AcceptanceCircle102', Id=102, UnitTypeId=102)
m.save_table('GuLiAvoidanceDataSettings', 'UnitsTable', source, rows, 'DT_AcceptanceAvoidance')
source, rows = m.table('GuLiAvoidanceDataSettings', 'BuildingCirclesTable')
for original in list(rows):
    target = {4: 101, 6: 102}.get(original['BuildingDefinitionId'])
    if target:
        m.clone(rows, original['Name'], Name='Acceptance_'+original['Name'],
                Id=max(r['Id'] for r in rows)+1, BuildingDefinitionId=target)
m.save_table('GuLiAvoidanceDataSettings', 'BuildingCirclesTable', source, rows, 'DT_AcceptanceBuildingCircles')
path = root / 'Config/Hybrid/AcceptanceProfile.json'
profile = json.loads(path.read_text(encoding='utf-8-sig'))
keys = {(v['class'], v['property']) for v in m.overrides}
profile['overrides'] = [v for v in profile['overrides'] if (v['class'], v['property']) not in keys] + m.overrides
profile.setdefault('assets', {}).update(m.assets)
profile.setdefault('source_tables', {}).update(m.sources)
path.write_text(json.dumps(profile, ensure_ascii=False, indent=2), encoding='utf-8')
scope = {'REPORT': Path(globals().get('REPORT', root / 'Artifacts/CircularAvoidance20261110/new-all-manifests.json'))}
source = root / 'Scripts/Network/refresh_integrated_manifests.py'
exec(compile(source.read_text(encoding='utf-8'), str(source), 'exec'), scope)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'variants': list(m.assets), 'manifests': scope['result']}))

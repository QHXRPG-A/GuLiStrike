"""Read UE asset contents through the editor and seal the scenario inputs."""
from pathlib import Path
import hashlib
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
profile=json.loads((root/'Config/Hybrid/AcceptanceProfile.json').read_text(encoding='utf-8'))
tables={}
for path,record in profile['assets'].items():
    table=unreal.load_asset(path)
    if not isinstance(table,unreal.DataTable): raise RuntimeError('Profile table is missing: '+path)
    raw=unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    value=hashlib.sha256(raw.encode()).hexdigest()
    if value!=record['sha256']: raise RuntimeError('Saved profile table differs from its export: '+path)
    tables[path]=value
migration=json.loads((root/'Config/Hybrid/ProductionBlueprintMigration.json').read_text(encoding='utf-8'))
review=json.loads((root/migration['semantic_review']).read_text(encoding='utf-8'))
blueprints=json.loads(unreal.GuLiHybridAuthoringLibrary.inspect_production_blueprints([v['asset'] for v in review['assets']]))
# Readback must not save unrelated packages that merely loaded with the editor.
# The authoring step saves its own map and reviewed assets before this inspection.
entry=json.loads(unreal.GuLiHybridAuthoringLibrary.read_hybrid_review_entry())
result={'schema':'guli-integrated-assets/v1','tables':tables,'blueprints':blueprints,'map_entry':entry,
        'success':bool(blueprints.get('success') and entry.get('success')),'gameplay_executed':False}
path=root/'Config/Hybrid/AcceptanceAssetReadback.json'
path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':result['success'],'report':str(path),'tables':len(tables)}))

"""Switch B-approved LOD resources in Models; gameplay tables keep their IDs/values."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'Scripts'))
from Models import model_catalog
ART = ROOT/'ArtSource/CommanderLOD_20261005'
assert json.loads((ART/'approval_B.json').read_text(encoding='utf8'))['approval_B'] == 'approved'
book = ROOT/'Data/Excel/GuLiStrikeModels.xlsx'
soldiers = json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8'))
buildings = json.loads((ROOT/'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf8'))
selected = sys.argv[1:] or [r['Name'] for r in soldiers]
changes = []
for name in selected:
    row = next(r for r in soldiers if r['Name'] == name)
    ready = json.loads((ART/('Reports/formal_prepared_'+name+'.json')).read_text(encoding='utf8'))
    assert ready['success'] and ready['state'] in ('prepared_verified','native_configured_verified'), name
    before = model_catalog.definition(row['ModelId'])
    path = ready.get('presentation_class') or ready['model_asset']
    expected_type = 'PresentationClass' if path.endswith('_C') else 'StaticMesh'
    assert before['ResourceType'] == expected_type, (name,'resource type changed')
    changes.append(dict(id=row['ModelId'],name=name,before=before,after=dict(ResourcePath=path,VATDefinition=ready['vat_definition'])))
    if name == 'BiZhiMao':
        construction = next(r for r in buildings if r['Id'] == 8)
        old = model_catalog.definition(construction['ModelId'])
        path = ready['formal_root']+'/Meshes/SM_BiZhiMao_Construction.SM_BiZhiMao_Construction'
        changes.append(dict(id=construction['ModelId'],name='BiZhiMaoConstruction',before=old,after=dict(ResourcePath=path,VATDefinition=old['VATDefinition'])))
report_path = ROOT/'Data/Models/commander-lod-model-switch.json'
report = dict(state='prepared',groups=changes,gameplay_values_unchanged=True,native_imported=False,approval_B='approved LOD only; no paint approval')
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
try:
    for change in changes:
        model_catalog.write_binding_cells(book,change['id'],change['after']['ResourcePath'],change['after']['VATDefinition'])
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'Tools/DataPipeline/export_data_from_excel.py')],cwd=ROOT,check=True)
    assert json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8')) == soldiers
    assert json.loads((ROOT/'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf8')) == buildings
    for change in changes:
        updated = model_catalog.definition(change['id'])
        assert all(updated[k] == v for k,v in change['after'].items()), change['name']
    report['state'] = 'source_exported_verified'
except Exception:
    for change in changes:
        model_catalog.write_binding_cells(book,change['id'],change['before']['ResourcePath'],change['before']['VATDefinition'])
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'Tools/DataPipeline/export_data_from_excel.py')],cwd=ROOT,check=True)
    report['state']='rolled_back'
    report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    raise
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(dict(success=True,model_ids=[c['id'] for c in changes],gameplay_values_unchanged=True)))

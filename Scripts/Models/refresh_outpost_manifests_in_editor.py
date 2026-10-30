"""Refresh only the replaced model's copied inputs and their manifest hashes."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
profile_file=ROOT/'Config/Hybrid/AcceptanceProfile.json'
profile=json.loads(profile_file.read_text(encoding='utf-8-sig'))
changes=[]
for suffix,stable_id,fields in [('Models_Models',2003,None),('Buildings_Buildings',3,['MeshScale','VisualOffset'])]:
    source=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrike'+suffix)
    raw=unreal.DataTableFunctionLibrary.export_data_table_to_json_string(source)
    source_rows=json.loads(raw);current=next(r for r in source_rows if r['Id']==stable_id)
    kind='Models' if stable_id==2003 else 'Buildings'
    asset=unreal.load_asset('/Game/Development/HybridAcceptance/DT_Acceptance'+kind)
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset))
    target=next(r for r in rows if r['Id']==stable_id)
    if fields is None:
        target.update(current)
    else:
        for field in fields:target[field]=current[field]
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(asset,json.dumps(rows))
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    saved=unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset)
    profile['source_tables'][source.get_path_name()]=hashlib.sha256(raw.encode()).hexdigest()
    profile['assets'][asset.get_path_name()]={'sha256':hashlib.sha256(saved.encode()).hexdigest(),'rows':json.loads(saved)}
    changes.append({'source':source.get_path_name(),'asset':asset.get_path_name(),'stable_id':stable_id})
profile_file.write_text(json.dumps(profile,ensure_ascii=False,indent=2),encoding='utf8')
scope={'__name__':'outpost_manifest_refresh','REPORT':OUT/'manifest-refresh.json'}
exec(compile((ROOT/'Scripts/Network/refresh_integrated_manifests.py').read_text(encoding='utf8'),'outpost_manifest_refresh','exec'),scope)
result=scope['result']
assert result['passed']
entry=json.loads(unreal.GuLiHybridAuthoringLibrary.read_hybrid_review_entry())
saved=json.loads(unreal.GuLiHybridAuthoringLibrary.save_hybrid_review_entry(entry['manifest'],entry['room'],entry['identity'],600))
assert saved.get('success'),saved
readback_file=ROOT/'Config/Hybrid/AcceptanceAssetReadback.json'
if readback_file.exists():
    readback=json.loads(readback_file.read_text(encoding='utf-8-sig'))
    profile=json.loads(profile_file.read_text(encoding='utf8'))
    for change in changes:
        if change['asset'] in readback.get('tables',{}):readback['tables'][change['asset']]=profile['assets'][change['asset']]['sha256']
    readback['map_entry']=json.loads(unreal.GuLiHybridAuthoringLibrary.read_hybrid_review_entry())
    readback_file.write_text(json.dumps(readback,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'changes':changes,'manifests':len(result['loads']),'entry_saved':True}))

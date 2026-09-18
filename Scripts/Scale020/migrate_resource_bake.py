"""Rebake only internal ore layouts; fail before saving if any map anchor changes."""
import json, math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
PATH='/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap'
old=json.loads((ROOT/'TestResults/Scale020/editor-baseline.json').read_text(encoding='utf-8'))['assets'][PATH]['properties']
economy=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
if not economy:
    # Resolve the actual project asset name without guessing a second package to save.
    found=[a.get_asset() for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/GuLiStrike/Data/Resources',False)
           if str(a.asset_class_path.asset_name)=='GuLiResourceEconomyConfig']
    assert len(found)==1,len(found)
    economy=found[0]
economy_before=properties(economy)
result=unreal.GuLiResourceAuthoringLibrary.bake_current_map(False)
if not result.success:
    raise RuntimeError(result.message+' '+str(result.issues))
asset=unreal.load_asset(PATH)
new=properties(asset)
checks={k:old[k]==new[k] for k in ['deterministic_seed','map_package','playable_maximum','playable_minimum','source_hash','spawn_anchors','territories']}
checks['counts']=len(old['clusters'])==len(new['clusters']) and len(old['nodes'])==len(new['nodes'])
checks['centers']=all(a['center']==b['center'] for a,b in zip(old['clusters'],new['clusters']))
errors=[]
max_ground_change=0
for index,(a,b) in enumerate(zip(old['nodes'],new['nodes'])):
    # Native IDs are authoritative; do not assume clusters' array packing.
    raw=asset.get_editor_property('nodes')[index]
    cluster_id=raw.get_editor_property('cluster_id')
    cluster=asset.get_editor_property('clusters')[cluster_id-1]
    c=value(cluster.get_editor_property('center'))
    at,bt=a['world_transform'],b['world_transform']
    expected_xy=[c[d]+(at['location'][d]-c[d])*.2 for d in range(2)]
    valid=all(math.isclose(bt['location'][d],expected_xy[d],abs_tol=.05) for d in range(2))
    valid &= all(math.isclose(x*.2,y,abs_tol=1e-5) for x,y in zip(at['scale'],bt['scale']))
    valid &= at['rotation']==bt['rotation']
    valid &= all(a[k]==b[k] for k in ['family_index','initial_amount','resource_type'])
    if not valid: errors.append(index)
    max_ground_change=max(max_ground_change,abs(at['location'][2]-bt['location'][2]))
checks['local_layout_and_economy']=not errors
checks['layout_version']=new['layout_version']==2
checks['baked_object_scale']=math.isclose(new['baked_object_scale'],.2,abs_tol=1e-6)
checks['economy_unchanged']=properties(economy)==economy_before
report={'checks':checks,'failed_node_indices':errors[:50],'node_count':len(new['nodes']),
        'cluster_count':len(new['clusters']),'max_reground_delta_cm':max_ground_change,
        'old_layout_hash':old['layout_hash'],'new_layout_hash':new['layout_hash'],
        'bake':value(result),'saved':False}
out=ROOT/'TestResults/Scale020/resource-bake-readback.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
if not all(checks.values()):
    raise RuntimeError('Resource layout invariant failed; not saved: '+str(checks))
assert unreal.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
# Native bake marks this managed companion dirty even though this migration changes no economy values.
assert unreal.EditorAssetLibrary.save_loaded_asset(economy,only_if_is_dirty=True)
report['saved']=True
report['validation']=value(unreal.GuLiResourceAuthoringLibrary.validate_current_bake())
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))

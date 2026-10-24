"""Editor-only application of the reviewed Grid300 payload. Requires the compiled project bridge.

Run through commander_editor_python.py after the implementation's compilation gate.
The original on-disk packages and the currently edited map are backed up separately.
All transforms are absolute target values; a resumed run never multiplies an instance twice.
"""
import datetime
import hashlib
import json
import shutil
import runpy
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
DATA=ROOT/'ArtSource/Navigation/Grid300'
RECORD=DATA/'applied_migration.json'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
RESOURCE='/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap'

def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,v):
    temporary=p.with_suffix('.pending.json')
    temporary.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    temporary.replace(p)
def check(result):
    if isinstance(result,tuple):
        assert result[0],str(result)
    else:assert result,str(result)
def transform(v):
    return unreal.Transform(location=unreal.Vector(*v['location']),
                            rotation=unreal.Rotator(pitch=v['rotation'][0],yaw=v['rotation'][1],roll=v['rotation'][2]),scale=unreal.Vector(*v['scale']))
def values(t):
    r=t.rotation.rotator()
    return {'location':list(t.translation.to_tuple()),'rotation':[r.pitch,r.yaw,r.roll],
            'scale':list(t.scale3d.to_tuple())}
def close(a,b):
    return all(abs(x-y)<.02 for k in ('location','rotation','scale') for x,y in zip(a[k],b[k]))
def same(a,b):
    if isinstance(a,(float,int)) and isinstance(b,(float,int)):return abs(a-b)<.02
    if isinstance(a,list) and isinstance(b,list):return len(a)==len(b) and all(same(x,y) for x,y in zip(a,b))
    if isinstance(a,dict) and isinstance(b,dict):return a.keys()==b.keys() and all(same(a[k],b[k]) for k in a)
    return a==b
def vector(v):return list(v.to_tuple())
def like(v,reference):
    if isinstance(reference,dict) and 'location' in reference:return values(v)
    if isinstance(reference,list):
        if isinstance(v,unreal.Vector):return vector(v)
        if isinstance(v,unreal.Vector2D):return [v.x,v.y]
        return [like(item,reference[0]) for item in v] if reference else list(v)
    if isinstance(reference,str):return str(v)
    return v
def verify_source_fields(final=False):
    baseline=read(DATA/'Baseline/editor.json')
    original=baseline['assets'][RESOURCE];target=read(DATA/'layout_resource.json')
    asset=unreal.load_asset(RESOURCE);assert asset
    ids=read(DATA/'Baseline/definitions.json')
    for name,table in ids['tables'].items():
        current=unreal.load_asset(table['path']);assert current,'Missing definition table: '+name
        rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(current))
        assert len(rows)==len(table['rows']),'Definition count changed: '+name
        for actual,source in zip(rows,table['rows']):
            assert all(actual.get(key)==value for key,value in source.items()),'Definition changed after capture: '+name
    def allowed(value,src,dst):return same(value,dst) or not final and same(value,src)
    for field in ('territories','clusters','nodes'):
        actual=list(asset.get_editor_property(field))
        assert len(actual)==len(original[field])==len(target[field]),'Changed resource count: '+field
        for i,(current,src,dst) in enumerate(zip(actual,original[field],target[field])):
            for key in src:
                value=like(current.get_editor_property(key),src[key])
                assert allowed(value,src[key],dst[key]),f'Resource changed after capture: {field}[{i}].{key}'
            if field in ids:
                for key,identity in ids[field][i].items():
                    assert current.get_editor_property(key)==identity,f'Resource identity changed: {field}[{i}].{key}'
    anchors=asset.get_editor_property('spawn_anchors')
    for key,src in original['spawn_anchors'].items():
        assert allowed(vector(anchors.get_editor_property(key)),src,target['spawn_anchors'][key]),'Spawn anchor changed: '+key
    for key in ('playable_minimum','playable_maximum','layout_version','baked_object_scale','deterministic_seed','map_package'):
        assert allowed(like(asset.get_editor_property(key),original[key]),original[key],target[key]),'Resource setting changed: '+key
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    current=json.loads(service.get_snapshot().get_editor_property('json'))
    src={m['marker_id']:m for m in json.loads(baseline['authoring']['json'])['markers']}
    dst={m['marker_id']:m for m in read(DATA/'layout_markers.json')['markers']}
    assert set(src)==set(dst)=={m['marker_id'] for m in current['markers']},'Marker identities changed after capture'
    for marker in current['markers']:
        a,b=src[marker['marker_id']],dst[marker['marker_id']]
        for key in ('display_name','enabled','marker_key','note','parameters'):
            assert marker.get(key)==a.get(key),'Marker metadata changed after capture: '+marker['marker_id']
        assert allowed(marker['world_transform'],a['world_transform'],b['world_transform'])
        ar={r['region_id']:r for r in a['regions']};br={r['region_id']:r for r in b['regions']}
        assert set(ar)==set(br)=={r['region_id'] for r in marker['regions']}
        for r in marker['regions']:
            for key in ('display_name','region_key'):
                assert r.get(key)==ar[r['region_id']].get(key),'Region metadata changed after capture: '+r['region_id']
            for key in ('relative_transform','shape_type','geometry','enabled'):
                assert allowed(r[key],ar[r['region_id']][key],br[r['region_id']][key]),'Region changed after capture: '+r['region_id']
    density=json.loads(service.get_density_snapshot().get_editor_property('json'))
    old=json.loads(baseline['density']['json']);new=read(DATA/'layout_density.json')
    assert density['density_map_id']==old['density_map_id']==new['density_map_id']
    assert density['cell_size_cm']==old['cell_size_cm']==new['cell_size_cm']
    assert {v['layer_id'] for v in density['layers']}=={v['layer_id'] for v in old['layers']}=={v['layer_id'] for v in new['layers']}
    normal=lambda layer:sorted((v['cell_x'],v['cell_y'],v['density_u8']) for v in layer['cells'] if v['density_u8'])
    for layer in density['layers']:
        a=next(v for v in old['layers'] if v['layer_id']==layer['layer_id'])
        b=next(v for v in new['layers'] if v['layer_id']==layer['layer_id'])
        assert normal(layer)==normal(b) or not final and normal(layer)==normal(a),'Density changed: '+layer['layer_key']
def package_file(package):
    assert package.startswith('/Game/')
    return ROOT/'Content'/(package[6:]+('.umap' if package==MAP else '.uasset'))
def backup(package,directory):
    p=package_file(package)
    if p.exists():
        dest=directory/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.exists():shutil.copy2(p,dest)

def preflight():
    assert hasattr(unreal,'GuLiGridAuthoringLibrary'),'Compile and reload the project bridge before migration.'
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor(),'Migration cannot run during PIE.'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()==MAP+'.LVL_CommanderMassPrototype'
    manifest=read(DATA/'layout_manifest.json');terrain=read(DATA/'Terrain/manifest.json')
    for name,expected in manifest['source_hashes'].items():
        p=DATA/'Terrain/manifest.json' if name=='manifest.json' else DATA/name if name=='dimensions.json' else DATA/'Baseline'/name
        assert sha(p)==expected,'Changed preparation input: '+name
    for name,expected in manifest['output_hashes'].items():assert sha(DATA/name)==expected,name
    for name,expected in terrain['files'].items():assert sha(DATA/'Terrain'/name)==expected,name
    verify_source_fields()
    actors={a.get_path_name():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    foliage=read(DATA/'Baseline/foliage.json')['result']['foliage']
    assert {a.get_path_name() for a in actors.values() if a.get_class().get_name()=='InstancedFoliageActor'}=={a['path'] for a in foliage}
    for row in foliage:
        # The captured foliage actor is empty. New instances require preparing their transforms first.
        assert not row['components'] and not actors[row['path']].get_components_by_class(unreal.InstancedStaticMeshComponent),'Foliage changed after capture'
    for row in read(DATA/'layout_actors.json'):
        a=actors.get(row['path']);assert a is not None,'Missing original actor: '+row['path']
        current=values(a.get_actor_transform())
        assert close(current,row['source']) or close(current,row['target']),'Actor changed after capture: '+row['path']
    return level,world,actors,manifest,terrain

def apply_resources():
    source=read(DATA/'layout_resource.json');ids=read(DATA/'Baseline/definitions.json')
    asset=unreal.load_asset(RESOURCE);assert asset
    territories=list(asset.get_editor_property('territories'))
    clusters=list(asset.get_editor_property('clusters'));nodes=list(asset.get_editor_property('nodes'))
    assert len(territories)==49 and len(clusters)==240 and len(nodes)==6240
    for t,v in zip(territories,source['territories']):
        assert str(t.get_editor_property('territory_id'))==v['territory_id']
        for key in ('center','outpost_ground_location'):t.set_editor_property(key,unreal.Vector(*v[key]))
        t.set_editor_property('local_polygon',[unreal.Vector2D(*p) for p in v['local_polygon']])
    for c,v,identity in zip(clusters,source['clusters'],ids['clusters']):
        assert c.get_editor_property('cluster_id')==identity['cluster_id']
        assert c.get_editor_property('first_node_index')==identity['first_node_index']
        c.set_editor_property('center',unreal.Vector(*v['center']))
        for key in ('obstacle_radius_centimeters','footprint_cells','source_collision_diameter_cm'):c.set_editor_property(key,v[key])
    for n,v,identity in zip(nodes,source['nodes'],ids['nodes']):
        assert n.get_editor_property('node_id')==identity['node_id'] and n.get_editor_property('cluster_id')==identity['cluster_id']
        assert n.get_editor_property('initial_amount')==v['initial_amount']
        n.set_editor_property('world_transform',transform(v['world_transform']))
    anchors=asset.get_editor_property('spawn_anchors')
    for key,value in source['spawn_anchors'].items():anchors.set_editor_property(key,unreal.Vector(*value))
    for key,value in (('territories',territories),('clusters',clusters),('nodes',nodes),('spawn_anchors',anchors)):
        asset.set_editor_property(key,value)
    for key in ('layout_version','baked_grid_cell_cm','baked_object_scale'):asset.set_editor_property(key,source[key])
    for key in ('playable_minimum','playable_maximum'):asset.set_editor_property(key,unreal.Vector2D(*source[key]))
    check(unreal.GuLiGridAuthoringLibrary.finalize_migrated_resources(False))

def apply_authoring():
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    for row in read(DATA/'layout_markers.json')['markers']:
        # Do not rewrite labels or property bags: retain exact existing Unicode and user metadata.
        patch={'schema_version':1,'position':row['world_transform']['position'],'regions':[]}
        for r in row['regions']:
            patch['regions'].append({'region_id':r['region_id'],'position':r['relative_transform']['position'],
                'rotation_pitch_yaw_roll':r['relative_transform']['rotation_pitch_yaw_roll'],'shape_type':r['shape_type'],'geometry':r['geometry']})
        result=service.update_marker(row['marker_id'],json.dumps(patch,ensure_ascii=False))
        assert result.get_editor_property('success'),str(result)
    old=json.loads(service.get_density_snapshot().get_editor_property('json'))
    target=read(DATA/'layout_density.json')
    assert old['density_map_id']==target['density_map_id'] and old['cell_size_cm']==target['cell_size_cm']
    for src,dst in zip(old['layers'],target['layers']):
        assert src['layer_id']==dst['layer_id']
        cells={(v['cell_x'],v['cell_y']):{'cell_x':v['cell_x'],'cell_y':v['cell_y'],'density_u8':0} for v in src['cells']}
        cells.update({(v['cell_x'],v['cell_y']):v for v in dst['cells']})
        result=service.update_density_cells(json.dumps({'schema_version':1,'layer_key':dst['layer_key'],'cells':list(cells.values())}))
        assert result.get_editor_property('success'),str(result)

def refresh_flight_navigation(directory):
    # Layout/terrain changes invalidate the existing flight bake; retain its own settings and rules.
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    report_path=DATA/'flight_migration.json'
    previous={r['volume']:r for r in read(report_path)['volumes']} if report_path.exists() else {}
    report=[]
    for volume in actors:
        if not isinstance(volume,unreal.GuLiFlightNavigationVolume):continue
        asset=volume.get_editor_property('navigation_data');assert asset
        settings=volume.get_editor_property('authoring_bake_settings')
        package=asset.get_path_name().split('.')[0]
        backup(package,directory/'flight_before_bake')
        valid=unreal.GuLiFlightNavigationEditorLibrary.validate_volume(volume,settings)
        changed=valid is None
        if changed:
            result=unreal.GuLiFlightNavigationEditorLibrary.bake_volume(volume,settings)
            assert result is not None,'Flight bake failed: '+volume.get_path_name()
        assert unreal.GuLiFlightNavigationEditorLibrary.validate_volume(volume,settings) is not None
        check(unreal.EditorAssetLibrary.save_loaded_asset(asset,False))
        metadata=asset.get_editor_property('metadata')
        report.append({'volume':volume.get_path_name(),'asset':asset.get_path_name(),'rebaked':changed,
            'ever_rebaked':changed or previous.get(volume.get_path_name(),{}).get('ever_rebaked',False),
            'bake_id':metadata.get_editor_property('bake_id').to_string(),
            'settings_hash':metadata.get_editor_property('settings_hash'),
            'geometry_signature':metadata.get_editor_property('geometry_signature')})
    write(DATA/'flight_migration.json',{'success':True,'volumes':report,'gameplay_run':False})

def main():
    level,world,actors,manifest,terrain=preflight()
    digest=sha(DATA/'layout_manifest.json')
    record=read(RECORD) if RECORD.exists() else {'schema':'guli-grid300/applied-v1','payload_sha256':digest,'completed':[],
        'backup':str(DATA/'Backups'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))}
    assert record['payload_sha256']==digest,'Migration payload changed after application started.'
    if record.get('finished'):
        runpy.run_path(str(ROOT/'Scripts/Grid300/verify_migration.py'),run_name='grid300_verify')['readback']()
        return {'success':True,'already_applied':True,'record':str(RECORD)}
    directory=Path(record['backup']);directory.mkdir(parents=True,exist_ok=True)
    def phase(name,action):
        # Only the immutable backup is skipped. Reapply absolute values after a crash/reload;
        # a phase recorded in the JSON may have completed before the final map save.
        if name=='backup' and name in record['completed']:return
        record['in_progress']=name;write(RECORD,record)
        action()
        if name not in record['completed']:record['completed'].append(name)
        record.pop('in_progress',None);write(RECORD,record)
    def preserve():
        backup(MAP,directory/'disk_before_save');backup(RESOURCE,directory/'disk_before_save')
        check(level.save_current_level())
        check(unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(RESOURCE),False))
        backup(MAP,directory/'edited_baseline');backup(RESOURCE,directory/'edited_baseline')
        shutil.copy2(DATA/'Baseline/editor.json',directory/'editor_baseline.json')
    phase('backup',preserve)
    phase('terrain',lambda:check(unreal.GuLiGridAuthoringLibrary.import_prepared_landscape(str(DATA/'Terrain'))))
    def layout():
        for row in read(DATA/'layout_actors.json'):
            check(actors[row['path']].set_actor_transform(transform(row['target']),False,True))
    phase('actors',layout)
    phase('authoring',apply_authoring)
    phase('grid',lambda:check(unreal.GuLiGridAuthoringLibrary.import_prepared_grid(str(DATA/'Terrain'),True)))
    phase('resources',apply_resources)
    phase('navigation',lambda:check(unreal.GuLiGridAuthoringLibrary.select_grid_navigation()))
    phase('flight',lambda:refresh_flight_navigation(directory))
    phase('review',lambda:runpy.run_path(str(ROOT/'Scripts/Grid300/author_review.py'),run_name='grid300_author_review')['author']())
    def save():
        check(unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(RESOURCE),False))
        check(level.save_current_level())
    phase('save',save)
    phase('readback',lambda:runpy.run_path(str(ROOT/'Scripts/Grid300/verify_migration.py'),run_name='grid300_verify')['readback']())
    record['finished']=True;record['terrain_hash']=terrain['source_hash'];write(RECORD,record)
    return {'success':True,'record':str(RECORD),'backup':record['backup'],'player_visual_acceptance':'pending'}

if __name__=='__main__':
    try:result=main()
    except Exception as exc:result={'success':False,'error':str(exc),'traceback':traceback.format_exc(),'record':str(RECORD),'automatic_rollback':False}
    unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))

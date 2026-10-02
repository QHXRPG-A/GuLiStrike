"""Author exact road corridors as stable, editor-only map regions."""
import json,math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1'
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
SERVICE=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert WORLD.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()

def snapshot():
    result=SERVICE.get_snapshot();assert result.success,str(result.issues)
    return json.loads(result.json)

def main():
    before=snapshot()
    types=[t for t in before['types'] if t['type_id']=='ResourceClearance']
    if ISLAND_ACTION=='create':
        assert not types and not any(m['type_id']=='ResourceClearance' for m in before['markers'])
        type_asset=SERVICE.create_type('ResourceClearance');assert type_asset
        type_asset.set_editor_property('display_name','道路矿簇预留')
        type_asset.set_editor_property('allowed_shapes',['Box','Cylinder'])
        type_asset.set_editor_property('default_regions',[])
        type_asset.set_editor_property('color',unreal.LinearColor(.75,.55,.08,1))
        actor=SERVICE.create_marker(type_asset,unreal.Vector(0,0,0));assert actor
        marker_id=next(m['marker_id'] for m in snapshot()['markers'] if m['type_id']=='ResourceClearance')
        old_regions={}
    else:
        assert ISLAND_ACTION=='update' and len(types)==1
        marker=next(m for m in before['markers'] if m['type_id']=='ResourceClearance')
        marker_id=marker['marker_id']
        old_regions={r['region_key']:r['region_id'] for r in marker['regions']}
        actors=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.GuLiMapMarker)
                and a.get_editor_property('record').get_editor_property('marker_id').to_string().replace('-','').lower()==marker_id.replace('-','').lower()]
        assert len(actors)==1;actor=actors[0]
    layout=json.loads((FILES/'layout.json').read_text(encoding='utf-8'))
    regions=[]
    for route in layout['routes']:
        radius=(route['minimum_width_m']/2+5.2)*100
        for index,(a,b) in enumerate(zip(route['points'][:-1],route['points'][1:])):
            dx,dy=b[0]-a[0],b[1]-a[1]
            regions.append({'region_key':route['name']+f'_Segment_{index:02d}','display_name':route['name'],
                'enabled':True,'position':[(a[0]+b[0])*50,(a[1]+b[1])*50,0],
                'rotation_pitch_yaw_roll':[0,math.degrees(math.atan2(dy,dx)),0],
                'shape_type':'Box','geometry':{'half_extents':[math.hypot(dx,dy)*50,radius,50000]}})
        for index,p in enumerate(route['points']):
            regions.append({'region_key':route['name']+f'_Cap_{index:02d}','display_name':route['name'],
                'enabled':True,'position':[p[0]*100,p[1]*100,0],'rotation_pitch_yaw_roll':[0,0,0],
                'shape_type':'Cylinder','geometry':{'radius':radius,'min_z':-50000,'max_z':50000}})
    assert len({r['region_key'] for r in regions})==len(regions)
    if old_regions:
        assert set(old_regions)=={r['region_key'] for r in regions}
        for r in regions:r['region_id']=old_regions[r['region_key']]
    patch={'schema_version':1,'marker_id':marker_id,'marker_key':'CommanderIsland_Roads',
        'display_name':'海岛道路矿簇预留','enabled':True,'position':[0,0,0],'rotation_pitch_yaw_roll':[0,0,0],
        'note':'53条路段布局；每侧半宽额外预留5.2米矿簇外径，烘焙按实际候选点排除。','regions':regions}
    result=SERVICE.update_marker(marker_id,json.dumps(patch,ensure_ascii=False));assert result.success,str(result.issues)
    actor.set_actor_label('CommanderIsland_Roads');actor.set_folder_path('GuLi/MapAuthoring/ResourceClearance')
    assert SERVICE.save_authoring_packages()
    after=snapshot();markers=[m for m in after['markers'] if m['type_id']=='ResourceClearance']
    assert len(markers)==1 and len(markers[0]['regions'])==len(regions)
    (FILES/'road_clearance_regions.json').write_text(json.dumps(markers[0],ensure_ascii=False,indent=2),encoding='utf-8')
    return {'success':True,'type_asset':str(actor.get_editor_property('record').get_editor_property('type').get_path_name()),
        'marker_id':markers[0]['marker_id'],'route_count':len(layout['routes']),'region_count':len(regions),
        'cluster_footprint_margin_m':5.2,'saved':True}

unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

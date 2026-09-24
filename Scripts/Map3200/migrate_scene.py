"""Migrate captured scene content while preserving authoring identities and object size."""
import json
import math
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map3200/20260923'


def ground(x,y):
    hit=unreal.LandscapeService.batch_line_trace([unreal.Vector(x,y,1000000)],[unreal.Vector(x,y,-1000000)])[0]
    assert hit.hit and hit.actor_name.startswith('Landscape'),(x,y,str(hit))
    slope=math.degrees(math.acos(max(-1,min(1,hit.hit_normal.z))))
    assert slope<=15,(x,y,slope)
    return hit.hit_location


def main():
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert json.loads((OUT/'landscape-stage.json').read_text())['stage']=='terrain_ready'
    assert not (OUT/'scene-migration.json').exists(),'Inspect the existing migration before repeating it.'
    baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
    layout=json.loads((OUT/'layout-before.json').read_text(encoding='utf-8'))
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    by_name={a.get_name():a for a in actors}
    records={a.get_editor_property('record').get_editor_property('marker_id').to_string().replace('-','').lower():a
             for a in actors if isinstance(a,unreal.GuLiMapMarker)}
    assert len(records)==49
    mapping=[1,2,4,5,6,8,9]
    identities=[]
    with unreal.ScopedEditorTransaction('Migrate 49 stable outposts to the 9x9 board'):
        for entry in layout['markers']:
            actor=records[entry['marker_id'].replace('-','').lower()]
            record=actor.get_editor_property('record')
            record.set_editor_property('marker_key','Migration_'+entry['marker_id'].replace('-',''))
            actor.set_editor_property('record',record)
        for entry in layout['markers']:
            actor=records[entry['marker_id'].replace('-','').lower()]
            row,col=mapping[entry['parameters']['BoardRow']-1],mapping[entry['parameters']['BoardColumn']-1]
            record=actor.get_editor_property('record')
            key=f'Outpost_R{row}C{col}'
            record.set_editor_property('marker_key',key)
            actor.set_editor_property('record',record)
            actor.set_actor_location(ground((col-5)*30000,(5-row)*30000),False,False)
            identities.append({'marker_id':entry['marker_id'],'old_key':entry['marker_key'],'new_key':key,
                               'region_ids':[r['region_id'] for r in entry['regions']]})
    (OUT/'marker-migration-identities.json').write_text(json.dumps(identities,indent=2),encoding='utf-8')
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    for patch in json.loads((OUT/'density-patches-3200.json').read_text(encoding='utf-8')):
        result=service.update_density_cells(json.dumps(patch))
        assert result.success,str(result.issues)
    old_props={a['name']:a for a in baseline['actors']}
    props=[old_props[f'StaticMeshActor_{i}'] for i in (1,2,3)]
    centroid=[sum(a['transform']['location'][i] for a in props)/len(props) for i in range(2)]
    for old in props:
        actor=by_name[old['name']];position=old['transform']['location']
        p=ground(-80000+position[0]-centroid[0],45000+position[1]-centroid[1])
        p.z+=old['bounds']['extent'][2]-10
        actor.set_actor_location(p,False,False)
    assembly,factory=ground(0,112500),ground(0,127500)
    by_name['PlayerStart_0'].set_actor_location(assembly+unreal.Vector(0,0,2000),False,False)
    by_name['PlayerStart_0'].set_actor_rotation(unreal.Rotator(yaw=-90),False)
    specs={
        'Note_0':(assembly+unreal.Vector(0,0,500),'3.2 km / 9×9 地图验收入口\n整张地形3.2×3.2 km，战场2.7×2.7 km；81据点，每格300m。红方R1C5，蓝方R9C5。\n正常指挥官玩法入口。双方各250个Mass、8矿车、8建造车；初始编队向战场内展开。\n先确认部队出现与框选就绪，再验证移动、S停止、B施工及运输。'),
        'Note_1':(factory+unreal.Vector(0,-1400,500),'采矿返厂\n工厂锚点(0,±127500 cm)，集合点(0,±112500 cm)。\n观察矿车采矿、返厂直接卸货、接续任务；采空矿簇应解除导航阻挡。'),
        'Note_2':(assembly+unreal.Vector(1800,0,500),'施工验收\n每队8建造车；B进入建造，在基地合法平地放置可负担建筑。\n验证15°限制、到位施工、四车协作、S停止保留进度和完工通行。'),
        'Note_3':(ground(0,90000)+unreal.Vector(0,0,500),'据点推进与运输：R2C5\n红方从R1C5推进，沿用相邻占领、围合及运输规则。\n检查中央四向坡道、四角据点和易主后的运输通道。'),
        'Note_4':(assembly+unreal.Vector(2200,0,500),'动态阻挡\n沿用建筑尺寸、价格和15°落位限制。道路保留50m走廊。\n施工后局部导航更新；拆除或采空后恢复通行。'),
        'Note_5':(assembly+unreal.Vector(2600,0,500),'Mass与框选验收\n完整出生位预检应为500/500，双方各250。\n正常框选己方Mass和工程车并移动、停止。若启动失败，检查含阵营/编队/槽位的错误；不能以工程车出现代替部队就绪。'),
        'Note_6':(factory+unreal.Vector(1800,-1400,500),'矿区与飞行边界\n200蓝矿簇、40红矿簇、6240节点；55m簇间距，据点100m中心避让。\n完整出生队形、工厂、工程车和设施参与避让。检查僚机边缘编队与高台避障；PIE及内存实测单独验收。'),
    }
    for name,(position,text) in specs.items():
        by_name[name].set_actor_location(position,False,False)
        by_name[name].set_editor_property('text',text)
    camera=by_name['CameraActor_2']
    camera_pos=assembly+unreal.Vector(0,math.cos(math.radians(55))*16000,math.sin(math.radians(55))*16000)
    camera.set_actor_location_and_rotation(camera_pos,unreal.MathLibrary.find_look_at_rotation(camera_pos,assembly),False,False)
    camera.camera_component.set_field_of_view(45)
    by_name['Actor_3'].set_actor_location(unreal.Vector(0,0,220000),False,False)
    for name in ('DirectionalLight_0','SkyLight_1','PostProcessVolume_2'):
        old=old_props[name]['transform']['location']
        by_name[name].set_actor_location(unreal.Vector(old[0]*32/42,old[1]*32/42,old[2]),False,False)
    fog=by_name['ExponentialHeightFog_0'];p=fog.get_actor_location()
    fog.set_actor_location(unreal.Vector(0,0,p.z),False,False)
    formations=[]
    for name in ('DoubleRing','SwarmOrbit'):
        asset=unreal.load_asset('/Game/GuLiStrike/Ship/Abilities/Formations/DA_WingmanFormation_'+name)
        inner,outer=asset.inner_ring_radius_centimeters,asset.outer_ring_radius_centimeters
        ih,oh=asset.inner_ring_height_centimeters,asset.outer_ring_height_centimeters
        swarm=asset.swarm_orbit;radius=asset.agent_radius_centimeters
        probe=max(2000,asset.obstacle_look_ahead_centimeters,radius*2)
        radial=max(inner,outer,swarm.outer_soft_radius_centimeters,asset.recovery_distance_centimeters)
        vertical=max(math.hypot(inner,ih),math.hypot(outer,oh),math.hypot(swarm.outer_soft_radius_centimeters,swarm.vertical_half_extent_centimeters))
        formations.append({'asset':asset.get_path_name(),'xy_padding_cm':radial+radius+probe,'lower_padding_cm':vertical+radius+probe})
    land=next(a for a in actors if isinstance(a,unreal.LandscapeProxy))
    origin,extent=land.get_actor_bounds(False)
    half=max(extent.x,extent.y)+max(f['xy_padding_cm'] for f in formations)
    low=math.floor((origin.z-extent.z-max(f['lower_padding_cm'] for f in formations))/1000)*1000
    flight=by_name['GuLiFlightNavigationVolume_0']
    old_origin,old_extent=flight.get_actor_bounds(False)
    high=old_origin.z+old_extent.z;scale=flight.get_actor_scale3d()
    flight.set_actor_location(unreal.Vector(0,0,(low+high)/2),False,False)
    flight.set_actor_scale3d(unreal.Vector(scale.x*half/old_extent.x,scale.y*half/old_extent.y,scale.z*(high-low)/2/old_extent.z))
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera_pos,camera.get_actor_rotation())
    instance_count=sum(len(c.get('instances_world',[])) for a in baseline['actors'] for c in a['components'])
    assert instance_count==0,'Additional foliage needs explicit relocation from the captured baseline.'
    result={'success':True,'preserved_markers':len(identities),'new_markers_pending_canonical_prepare':32,
            'original_instance_count':instance_count,'prop_group_spacing_preserved':True,'object_sizes_preserved':True,
            'notes_updated':list(specs),'flight_half_extent_xy_cm':half,'flight_z_range':[low,high],'formations':formations,'saved':False}
    (OUT/'scene-migration.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return result


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

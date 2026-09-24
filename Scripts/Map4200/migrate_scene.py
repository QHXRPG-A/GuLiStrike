"""Move captured Commander authoring content; preserve identity and object sizes."""
import json
import math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Artifacts/Map4200/20260922'
MAP='/Game/Maps/LVL_CommanderMassPrototype'


def write(name,value):
    (OUT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')


def ground(x,y):
    hit=unreal.LandscapeService.batch_line_trace([unreal.Vector(x,y,1000000)], [unreal.Vector(x,y,-1000000)])[0]
    assert hit.hit and hit.actor_name.startswith('Landscape'), (x,y,str(hit))
    slope=math.degrees(math.acos(max(-1,min(1,hit.hit_normal.z))))
    assert slope<=15, (x,y,slope)
    return hit.hit_location


def main():
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]==MAP
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert json.loads((OUT/'landscape-stage.json').read_text())['stage']=='terrain_ready'
    baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
    layout=json.loads((OUT/'layout-before.json').read_text(encoding='utf-8'))
    actors_api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors=actors_api.get_all_level_actors()
    by_name={a.get_name():a for a in actors}
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    records={a.get_editor_property('record').get_editor_property('marker_id').to_string().replace('-','').lower():a
             for a in actors if isinstance(a,unreal.GuLiMapMarker)}
    mapping=[1,2,4,6,7]
    identities=[]
    with unreal.ScopedEditorTransaction('Migrate 25 stable Commander outposts to the 7x7 board'):
        for entry in layout['markers']:
            actor=records[entry['marker_id'].replace('-','')]
            old_row,old_col=entry['parameters']['BoardRow'],entry['parameters']['BoardColumn']
            row,col=mapping[old_row-1],mapping[old_col-1]
            record=actor.get_editor_property('record')
            record.set_editor_property('marker_key',f'Outpost_R{row}C{col}')
            actor.set_editor_property('record',record)
            identities.append({'marker_id':entry['marker_id'],'old_key':entry['marker_key'],'new_key':f'Outpost_R{row}C{col}',
                               'region_ids':[r['region_id'] for r in entry['regions']]})
    # The native canonical adapter will synchronize parameter bags, regions and the additional 24 markers after ground navigation.
    write('marker-migration-identities.json',identities)
    for patch in json.loads((OUT/'density-patches-4200.json').read_text(encoding='utf-8')):
        result=service.update_density_cells(json.dumps(patch))
        assert result.success,str(result.issues)
    old_props={a['name']:a for a in baseline['actors']}
    cylinders=[old_props[f'StaticMeshActor_{i}'] for i in (1,2,3)]
    centroid=[sum(a['transform']['location'][i] for a in cylinders)/3 for i in range(2)]
    for old in cylinders:
        a=by_name[old['name']]
        pos=old['transform']['location']
        # Keep this prop group's spacing and every mesh's scale; move its anchor clear of the new road.
        x=centroid[0]*.5+(pos[0]-centroid[0])+5000
        y=centroid[1]*.5+(pos[1]-centroid[1])
        p=ground(x,y)
        p.z+=old['bounds']['extent'][2]-10
        a.set_actor_location(p,False,False)
    red_assembly=ground(0,110000)
    red_factory=ground(0,130000)
    start=by_name['PlayerStart_0']
    start.set_actor_location(red_assembly+unreal.Vector(0,0,2000),False,False)
    start.set_actor_rotation(unreal.Rotator(yaw=-90),False)
    specs={
        'Note_0':(red_assembly+unreal.Vector(0,0,500),
            '4.2 km / 7×7 地图验收入口\n完整地形4.2×4.2 km，内部战场2.8×2.8 km，49据点。红方R1C4，蓝方R7C4。\n'
            '使用本地图正常指挥官入口；双方各250士兵、8矿车和8建造车。B进入建造，S停止，原选择与命令流程保留。\n'
            '检查四角据点、中央四向坡道、基地出口、矿区通路；采矿返厂、施工、推进运输及动态阻挡须玩家实际验收。'),
        'Note_1':(red_factory+unreal.Vector(0,-1400,500),
            '采矿返厂验收\n红方矿厂锚点(0,130000)，蓝方(0,-130000)。观察矿车采矿、四点直接卸货并接续任务。\n'
            '每队8矿车，货仓10；不改变采矿速率和工程车规则。矿簇最后节点采空后应恢复地面通行。'),
        'Note_2':(red_assembly+unreal.Vector(1800,0,500),
            '施工验收\n每队8建造车；B进入正常建造流程，在基地附近合法平地放置可负担建筑。\n'
            '检查15°坡度限制、寻路到施工位、最多四车施工、S停止保留进度，完工后矿厂出入口可走。'),
        'Note_3':(ground(0,80000)+unreal.Vector(0,0,500),
            '据点推进与运输验收：R2C4\n红方从R1C4向内部推进，实际目标由原邻接与围合规则选择。\n'
            '检查相邻占领、友方据点运输、易主后的通道更新；中央与四角均应连续可达。'),
        'Note_4':(red_assembly+unreal.Vector(2200,0,500),
            '建筑落位与动态阻挡\n沿用现有建筑尺寸、价格及15°放置限制。道路预留50m宽。\n'
            '在合法区域施工后观察局部导航更新；拆除/采空恢复通行，不应阻断整条基地出口。'),
        'Note_5':(red_assembly+unreal.Vector(2600,0,500),
            '施工进度与取消\n选择建造车进入B建造界面；观察待建碰撞、施工位、进度保留及完工真实坡道。\n'
            '本次只交付编辑器场景与导航资产，运行效果及30分钟容量测试独立验收。'),
        'Note_6':(red_factory+unreal.Vector(1800,-1400,500),
            '矿区通路与僚机边界\n240矿簇、6240矿节点；矿簇55m间距、据点130m避让、基地120m避让。\n'
            '用原飞船/僚机流程检查边缘编队、中央高台避障和飞行高度；FlightNav按当前编队参数重建。'),
    }
    for name,(location,description) in specs.items():
        a=by_name[name]
        a.set_actor_location(location,False,False)
        a.set_editor_property('text',description)
    camera=by_name['CameraActor_2']
    target=red_assembly
    camera_pos=target+unreal.Vector(0,math.cos(math.radians(55))*16000,math.sin(math.radians(55))*16000)
    camera.set_actor_location_and_rotation(camera_pos,unreal.MathLibrary.find_look_at_rotation(camera_pos,target),False,False)
    camera.camera_component.set_field_of_view(45)
    top=by_name['Actor_3']
    top.set_actor_location(unreal.Vector(0,0,300000),False,False)
    # Derive horizontal padding from current authored formations, including their recovery envelope.
    formations=json.loads((OUT/'formation-baseline.json').read_text())['formations']
    assert formations
    # Ring and recovery distances are both measured from the carrier, not added.
    # The subsequent tighten_flight_bounds.py pass also reads live probe tuning
    # and clips the lower Z envelope after the final terrain has been imported.
    padding=max(max(f['inner_radius'],f['outer_radius'],f['swarm_radius'],f['recovery_distance'])
                +f['agent_radius']+max(2000,f['agent_radius']*2) for f in formations)
    half=210000+padding
    flight=by_name['GuLiFlightNavigationVolume_0']
    _,extent=flight.get_actor_bounds(False)
    scale=flight.get_actor_scale3d()
    old_flight=old_props[flight.get_name()]
    center=old_flight['bounds']['origin']
    flight.set_actor_location(unreal.Vector(0,0,center[2]),False,False)
    flight.set_actor_scale3d(unreal.Vector(scale.x*half/extent.x,scale.y*half/extent.y,scale.z))
    # Focus the normal Commander observation pose without starting a play session.
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera_pos,camera.get_actor_rotation())
    result={'success':True,'preserved_markers':len(identities),'density_schema':1,'density_cell_cm':2500,
            'flight_half_extent_xy_cm':half,'flight_padding_cm':padding,'formation_sources':formations,
            'notes_updated':list(specs),'prop_group_spacing_preserved':True,'object_sizes_preserved':True,'saved':False}
    write('scene-migration.json',result)
    return result


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

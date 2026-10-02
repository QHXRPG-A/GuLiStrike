"""Prepare editor-only pose references and the real commander acceptance entrance."""
import json,math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1'
LAYOUT=json.loads((FILES/'layout.json').read_text(encoding='utf-8'))
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
assert not LEVELS.is_in_play_in_editor()
TAG='GuLi.CommanderIsland.EditorPose'
LAND='Landscape_CommanderIsland_1800m_v1'

def ground(x,y):
    h=unreal.LandscapeService.get_height_at_location(LAND,x,y)
    assert h.valid and h.height>0,(x,y)
    return unreal.Vector(x,y,h.height)

def fixtures():
    actors=ACTORS.get_all_level_actors()
    by_name={a.get_name():a for a in actors}
    for a in actors:
        if TAG in [str(t) for t in a.tags]:assert ACTORS.destroy_actor(a)
    mesh=unreal.load_asset('/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder')
    bounds=mesh.get_bounds();scale=1000/(bounds.box_extent.z*2)
    assert abs(scale-1/30)<1e-6
    base='/Game/GuLiStrike/FX/StrongholdOutpost/Candidate_v1'
    data=[]
    for p in LAYOUT['outposts']:
        anchor=ground(*(v*100 for v in p['center_m']))
        # These editor-only snapshots never replace or move the runtime actor.
        owned=p['initial_owner']!='Neutral';height=5000 if owned else 0
        a=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,
            anchor+unreal.Vector(-bounds.origin.x*scale,-bounds.origin.y*scale,height-(bounds.origin.z-bounds.box_extent.z)*scale))
        a.set_actor_label('EditorPose_'+p['name']);a.set_folder_path('CommanderIsland/Review/OutpostPoses')
        a.set_editor_property('tags',[TAG]);a.set_editor_property('is_editor_only_actor',True)
        a.set_actor_scale3d(unreal.Vector(scale,scale,scale))
        c=a.static_mesh_component;assert c.set_static_mesh(mesh)
        c.set_material(0,unreal.load_asset(base+'/MI_OutpostGlow_'+p['initial_owner']))
        c.set_collision_profile_name('NoCollision',True);c.set_editor_property('can_ever_affect_navigation',False)
        a.set_actor_enable_collision(False);a.set_actor_hidden_in_game(True)
        data.append({'key':p['name'],'owner':p['initial_owner'],'ground_cm':list(anchor.to_tuple()),
            'editor_pose_float_height_cm':height,'model_height_cm':1000,'scale':scale,'runtime_hidden':True})
    assembly,factory=ground(0,69000),ground(0,74000)
    specs={
        'Note_0':(assembly+unreal.Vector(0,0,500),'1.8 km海岛 / 7×7 据点验收入口\n完整地图1800×1800m，主陆地约1600×1600m；49据点，每格257.14m。红方R1C4，蓝方R7C4。\n双方各250 Mass、8矿车、8建造车；保持指挥官正常玩法入口。'),
        'Note_1':(factory+unreal.Vector(0,-1400,500),'基地与经济\n工厂(0,±740m)，集合点(0,±690m)。200蓝矿簇、40红矿簇、6240节点。\n检查采矿、返厂卸货、设施和运输都使用地面锚点。'),
        'Note_2':(assembly+unreal.Vector(1800,0,500),'据点占领与浮动\n中立白光贴地。派兵占领R2C4，检查颜色随阵营变化；可见模型周期5s，高度0～100m。\n0/1.25/2.5/3.75/5s应离地0/50/100/50/0m。点击悬浮模型，命令终点仍在地面。'),
        'Note_3':(ground(*[v*100 for v in LAYOUT['outposts'][24]['center_m']])+unreal.Vector(0,0,500),'中央据点R4C4：换主/中立验收\n服务器控制台：gs.Resources.SetTerritoryOwner 4 4 Red，然后Blue；换主只变色，相位连续。\n再执行 gs.Resources.SetTerritoryOwner 4 4 Neutral，检查0.5s平滑落地后停止。'),
        'Note_4':(assembly+unreal.Vector(2200,0,500),'地面玩法保持\nActor碰撞、占领中心、设施位置、运输端点固定。可见模型只接受点击查询，不阻挡物理或导航。\n三条主路与49据点均连接；海域NavArea_Null。检查施工后局部导航及采空恢复。'),
        'Note_5':(assembly+unreal.Vector(2600,0,500),'联机晚加入验收\n服务器先占领中央据点，再让新客户端进入本关卡。双方应按服务器时间看到相同浮动高度和阵营。\n专用服务器只运行玩法，不创建可见模型、材质或动画Tick。本轮仅准备场景，未运行PIE/自动化测试。'),
        'Note_6':(factory+unreal.Vector(1800,-1400,500),'地形与出生\n49据点落地区至少30×30m、坡度≤3°；主路≥50m，侧路≥40m，关键坡度≤12°。\n原500单位队形重新贴地，不调整单位尺寸、资源经济或阵营玩法。'),
    }
    for name,(position,text) in specs.items():
        a=by_name[name];a.set_actor_location(position,False,False);a.set_editor_property('text',text)
    camera=by_name['CameraActor_2']
    position=assembly+unreal.Vector(0,math.cos(math.radians(55))*16000,math.sin(math.radians(55))*16000)
    camera.set_actor_location_and_rotation(position,unreal.MathLibrary.find_look_at_rotation(position,assembly),False,False)
    camera.camera_component.set_field_of_view(45)
    (FILES/'EditorEvidence/editor_pose_references.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    return {'success':True,'editor_pose_count':49,'runtime_hidden':True,'notes_updated':list(specs),
            'runtime_commands':'Existing gs.Resources.SetTerritoryOwner','gameplay_run':False}

def flight_bounds():
    volumes=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.GuLiFlightNavigationVolume)]
    assert len(volumes)==1
    padding=[]
    for name in ('DoubleRing','SwarmOrbit'):
        f=unreal.load_asset('/Game/GuLiStrike/Ship/Abilities/Formations/DA_WingmanFormation_'+name)
        radius=f.agent_radius_centimeters
        padding.append(max(f.inner_ring_radius_centimeters,f.outer_ring_radius_centimeters,
            f.swarm_orbit.outer_soft_radius_centimeters,f.recovery_distance_centimeters)+radius+
            max(2000,f.obstacle_look_ahead_centimeters,radius*2))
    half=90000+max(padding)
    a=volumes[0];origin,extent=a.get_actor_bounds(False);scale=a.get_actor_scale3d()
    a.set_actor_scale3d(unreal.Vector(scale.x*half/extent.x,scale.y*half/extent.y,scale.z))
    after_origin,after_extent=a.get_actor_bounds(False)
    assert abs(after_extent.x-half)<1 and abs(after_extent.y-half)<1 and abs(after_extent.z-extent.z)<1
    return {'success':True,'physical_map_half_extent_cm':90000,'flight_half_extent_xy_cm':half,
        'formation_padding_cm':max(padding),'vertical_range_cm':[origin.z-extent.z,origin.z+extent.z],
        'navigation_asset':a.navigation_data.get_path_name()}

def navigation():
    r=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(WORLD,True)
    entries=[{k:getattr(e,k) for k in ('object_path','kind','status','source_hash','message','check_seconds','build_seconds')} for e in r.entries]
    return {**{k:getattr(r,k) for k in ('success','message','ground_rebuilds','flight_rebuilds','save_seconds','total_seconds')},'entries':entries}

def navigation_query_budget():
    meshes=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.RecastNavMesh)]
    assert len(meshes)==2
    entries=[]
    for a in meshes:
        for key in ('DefaultMaxSearchNodes','DefaultMaxHierarchicalSearchNodes'):
            assert unreal.ActorService.set_property(a.get_name(),key,'16384')
        entries.append({'actor':a.get_actor_label(),'maximum_search_nodes':16384})
    return {'success':True,'entries':entries,'map_scope':WORLD.get_path_name()}

def preview_visibility():
    visible=ISLAND_ACTION=='preview_on';count=0
    for a in ACTORS.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            a.set_editor_property('is_editor_only_actor',not visible)
            a.set_actor_hidden_in_game(not visible);count+=1
    assert count==49
    return {'success':True,'editor_poses':count,'preview_visible':visible}

unreal.MCPythonHelper.submit_result(json.dumps({'fixtures':fixtures,'flight_bounds':flight_bounds,'navigation':navigation,'navigation_query_budget':navigation_query_budget,
    'preview_on':preview_visibility,'preview_off':preview_visibility}[ISLAND_ACTION](),ensure_ascii=False))

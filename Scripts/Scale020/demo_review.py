"""Removable scale-review actors IN the supplied Demo_Map; no environment edits.

This is a size/presentation fixture, not a complete match or proof of runtime gameplay.
Call build(), gallery(), audit() or remove_owned(); only the explicit map is saved.
"""
import hashlib
import json
import math
import time
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
OUT=ROOT/'TestResults/Scale020/DemoReview'
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'Previews').mkdir(exist_ok=True)
MAP='/Game/StylizedPineEnvironment/Maps/Demo_Map'
TAG='GuLi.Scale020.Review.v1'
FOLDER='GuLiScale020_Review_REMOVE_AS_GROUP'
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

def vec(v): return list(v.to_tuple())
def transform(t): return [vec(t.translation),vec(t.rotation.rotator()),vec(t.scale3d)]
def write(name,data): (OUT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def owned(a): return TAG in [str(t) for t in a.tags]
def guard():
    assert not EDITOR.get_game_world(),'Not during PIE'
    w=EDITOR.get_editor_world()
    assert w.get_path_name().split('.')[0]==MAP,w.get_path_name()
    return w

def snapshot():
    result={}
    for a in ACTORS.get_all_level_actors():
        if owned(a): continue
        row={'class':a.get_class().get_path_name(),'transform':transform(a.get_actor_transform()),'components':{}}
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            # Landscape Grass components are generated on demand as cameras move, not authored layout.
            if c.get_class().get_name()=='GrassInstancedStaticMeshComponent': continue
            r={'mesh':c.static_mesh.get_path_name() if c.static_mesh else None,
               'materials':[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())],
               'transform':transform(c.get_world_transform())}
            if isinstance(c,unreal.InstancedStaticMeshComponent):
                count=c.get_instance_count()
                digest=hashlib.sha256()
                for i in range(count):
                    digest.update(json.dumps(transform(c.get_instance_transform(i,False)),separators=(',',':')).encode('utf-8'))
                r.update(instances=count,instance_sha256=digest.hexdigest())
            row['components'][c.get_name()]=r
        result[a.get_name()]=row
    return result

def spawn(cls,name,location,rotation=unreal.Rotator()):
    assert not any(a.get_actor_label()=='Scale020_'+name for a in ACTORS.get_all_level_actors()),name
    a=ACTORS.spawn_actor_from_class(cls,location,rotation)
    assert a,name
    a.set_actor_label('Scale020_'+name)
    a.set_folder_path(FOLDER)
    a.set_editor_property('tags',[unreal.Name(TAG)])
    a.set_editor_property('is_editor_only_actor',False)
    a.set_actor_hidden_in_game(False)
    a.set_actor_enable_collision(False)
    a.set_actor_tick_enabled(False)
    return a

def ground(x,y):
    # Vertical queries cover real unscaled terrain; review geometry is ignored.
    ignore=[a for a in ACTORS.get_all_level_actors() if owned(a)]
    hit=unreal.SystemLibrary.line_trace_single(guard(),unreal.Vector(x,y,100000),unreal.Vector(x,y,-100000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True)
    assert hit,'No ground at '+str((x,y))
    values=hit.to_tuple()
    assert values[0] and not values[1],'Ground trace did not produce an unobstructed blocking hit'
    return values[5]

def fit(a,x,y,clearance=0):
    at=ground(x,y)
    center,extent=a.get_actor_bounds(False,True)
    a.set_actor_location(a.get_actor_location()+unreal.Vector(x-center.x,y-center.y,at.z+clearance-center.z+extent.z),False,False)
    return at

def mesh_actor(name,mesh_path,xy,scale,yaw=0,clearance=0):
    a=spawn(unreal.StaticMeshActor,name,unreal.Vector(),unreal.Rotator(yaw=yaw))
    mesh=unreal.load_asset(mesh_path)
    assert mesh,mesh_path
    a.static_mesh_component.set_static_mesh(mesh)
    a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(unreal.Vector(*scale) if isinstance(scale,(list,tuple)) else unreal.Vector(scale,scale,scale))
    fit(a,*xy,clearance)
    return a

def audit():
    guard()
    original=json.loads((OUT/'environment_before.json').read_text(encoding='utf-8'))
    current=snapshot()
    write('environment_after.json',current)
    differences=[key for key in original if current.get(key)!=original[key]]
    differences += [key for key in current if key not in original]
    rows=[]
    for a in ACTORS.get_all_level_actors():
        if owned(a):
            center,extent=a.get_actor_bounds(False,True)
            rows.append({'name':a.get_actor_label(),'class':a.get_class().get_path_name(),
                'transform':transform(a.get_actor_transform()),'bounds_center_cm':vec(center),'bounds_size_cm':vec(extent*2),
                'editor_only':a.get_editor_property('is_editor_only_actor')})
    result={'success':not differences,'original_actors':len(original),'environment_differences':differences,
        'review_actors':rows,'scope':'Non-simulating geometry fixture, not a runtime match','user_visual_approval':'pending'}
    write('audit.json',result)
    return result

def build():
    guard()
    assert not any(owned(a) for a in ACTORS.get_all_level_actors()),'Review already exists; audit or remove_owned explicitly'
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Protect unsaved maps'
    before=OUT/'environment_before.json'
    if before.exists():
        current=snapshot()
        previous=json.loads(before.read_text(encoding='utf-8'))
        write('environment_current_diagnostic.json',current)
        assert current==previous,'Source map changed or legacy non-deterministic instance hash; inspect diagnostic before merging'
    else: write('environment_before.json',snapshot())
    # This clearing is part of the original map; none of its terrain/foliage is changed.
    positions={'DefaultSoldier':(17000,-4300),'WM01':(14800,-5400),
               'ElectromagneticMiner':(17900,-5400),'ConstructionVehicle':(17900,-6400)}
    units=json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8'))
    for row in units:
        xy=positions[row['Name']]
        if row['ModelAsset']:
            mesh_actor(row['Name'],row['ModelAsset'],xy,row['PresentationScale'],yaw=155,clearance=2)
        else:
            cls=unreal.load_class(None,row['PresentationClass'])
            assert cls,row['PresentationClass']
            a=spawn(cls,row['Name'],unreal.Vector(),unreal.Rotator(yaw=155))
            a.set_actor_scale3d(unreal.Vector(row['PresentationScale'],row['PresentationScale'],row['PresentationScale']))
            fit(a,*xy,2)
    # Spawn the real Ship blueprint at actor Scale 1; its common model parent already owns 0.2.
    ship_cls=unreal.EditorAssetLibrary.load_blueprint_class('/Game/GuLiStrike/Ship/BP_CombatAvatarFly01')
    ship=spawn(ship_cls,'Ship',unreal.Vector(12500,-10500,3800),unreal.Rotator(yaw=65))
    ship.set_actor_scale3d(unreal.Vector(1,1,1))
    # Source BP is only a presentation mesh in this fixture; no match/player logic is started.
    factory_cls=unreal.EditorAssetLibrary.load_blueprint_class('/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory')
    factory=spawn(factory_cls,'Factory',unreal.Vector(),unreal.Rotator(yaw=90))
    factory.set_actor_scale3d(unreal.Vector(.2,.2,.2))
    fit(factory,20500,-8500,0)
    rows=json.loads((ROOT/'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf-8'))
    for row,xy in zip([r for r in rows if r['Name'] in ['MissileTurret','SentryTurret','ManualOutpost']],
                      [(19900,-5500),(19800,-4100),(22000,-5300)]):
        mesh_actor(row['Name'],row['Mesh'],xy,[row['MeshScale'][c] for c in ['X','Y','Z']],yaw=155)
    anchor=ground(17000,-5200)
    write('anchor.json',{'target':vec(anchor),'map':MAP,'folder':FOLDER,'owner_tag':TAG})
    spawn(unreal.CameraActor,'Camera',anchor+unreal.Vector(-4000,0,5000))
    result=audit()
    assert result['success'],result['environment_differences']
    assert LEVEL.save_current_level()
    return {'success':True,'review_actors':len(result['review_actors']),'environment_actors_unchanged':len(snapshot())}

def views():
    t=json.loads((OUT/'anchor.json').read_text(encoding='utf-8'))['target']
    presets={
      '01_close':{'location':[t[0]-3900,t[1]-3200,t[2]+2200],'target':t,'fov':45},
      '02_overview':{'location':[t[0]-12000,t[1]-17000,t[2]+16000],'target':[t[0]-1000,t[1]-2500,t[2]+1000],'fov':45},
      '03_top':{'location':[t[0],t[1]-2500,t[2]+28000],'target':[t[0],t[1]-2500,t[2]],'fov':45}}
    for meters in (40,160,360):
        arm=meters*100
        presets['commander_%03dm'%meters]={'location':[t[0]-arm*math.cos(math.radians(55)),t[1],t[2]+arm*math.sin(math.radians(55))],
            'target':t,'fov':45,'arm_m':meters,'pitch_degrees':55}
    return presets

def camera(name):
    guard()
    cam=next(a for a in ACTORS.get_all_level_actors() if owned(a) and a.get_actor_label()=='Scale020_Camera')
    v=views()[name];pos=unreal.Vector(*v['location'])
    rot=unreal.MathLibrary.find_look_at_rotation(pos,unreal.Vector(*v['target']))
    cam.set_actor_location_and_rotation(pos,rot,False,False)
    cc=cam.get_component_by_class(unreal.CameraComponent)
    cc.set_field_of_view(v['fov']);cc.set_editor_property('aspect_ratio',16/9)
    EDITOR.set_level_viewport_camera_info(pos,rot)
    unreal.ViewportService.set_view_mode('lit');unreal.ViewportService.set_realtime(True)
    unreal.ViewportService.set_game_view(True);unreal.ViewportService.set_exposure(True,0.)
    ACTORS.set_selected_level_actors([])
    return cam

def gallery():
    global SCALE_GALLERY_HANDLE,SCALE_GALLERY_TASK,SCALE_GALLERY_STATE
    guard()
    assert not globals().get('SCALE_GALLERY_HANDLE')
    names=list(views())
    SCALE_GALLERY_STATE={'index':0,'phase':'camera','images':[],'start':time.monotonic(),'last':time.monotonic()}
    def tick(dt):
        global SCALE_GALLERY_HANDLE,SCALE_GALLERY_TASK
        s=SCALE_GALLERY_STATE
        try:
            if time.monotonic()-s['start']>300: raise RuntimeError('Capture timeout')
            if s['index']>=len(names):
                unreal.unregister_slate_post_tick_callback(SCALE_GALLERY_HANDLE);SCALE_GALLERY_HANDLE=None
                write('gallery.json',{'success':True,'images':s['images'],'views':views()})
                assert audit()['success']
                assert LEVEL.save_current_level()
                return
            name=names[s['index']]
            if s['phase']=='camera':
                camera(name);s.update(phase='settle',last=time.monotonic())
            elif s['phase']=='settle' and time.monotonic()-s['last']>4:
                SCALE_GALLERY_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/'Previews'/(name+'.png')),camera(name),delay=1.)
                s.update(phase='capture')
            elif s['phase']=='capture' and SCALE_GALLERY_TASK.is_task_done():
                p=OUT/'Previews'/(name+'.png');assert p.is_file() and p.stat().st_size>10000,str(p)
                s['images'].append(str(p));s.update(phase='camera',index=s['index']+1)
        except Exception:
            write('gallery.json',{'success':False,'state':s,'error':traceback.format_exc()})
            if SCALE_GALLERY_HANDLE: unreal.unregister_slate_post_tick_callback(SCALE_GALLERY_HANDLE)
            SCALE_GALLERY_HANDLE=None
    SCALE_GALLERY_HANDLE=unreal.register_slate_post_tick_callback(tick)
    return {'queued':names}

def repair_review_visibility():
    """Only owned fixture actors; never changes source materials or environment components."""
    guard()
    # The base Ship BP is a collision/socket carrier. The actual player uses this cel child.
    ship=next(a for a in ACTORS.get_all_level_actors() if owned(a) and a.get_actor_label()=='Scale020_Ship')
    cls=unreal.EditorAssetLibrary.load_blueprint_class('/Game/GuLiStrike/Ship/BP_CombatAvatarFly01')
    assert cls
    if ship.get_class()!=cls:
        old_transform=ship.get_actor_transform()
        assert ACTORS.destroy_actor(ship)  # Only this script's tagged review fixture.
        ship=spawn(cls,'Ship',old_transform.translation,old_transform.rotation.rotator())
        ship.set_actor_scale3d(unreal.Vector(1,1,1))
    # Center the closest camera on a model, not on the empty space between models.
    wm=next(a for a in ACTORS.get_all_level_actors() if owned(a) and a.get_actor_label()=='Scale020_WM01')
    center,extent=wm.get_actor_bounds(False,True)
    write('anchor.json',{'target':vec(center),'map':MAP,'folder':FOLDER,'owner_tag':TAG})
    report=[]
    for a in ACTORS.get_all_level_actors():
        if not owned(a): continue
        a.set_editor_property('is_editor_only_actor',False)
        a.set_actor_hidden_in_game(False)
        a.set_actor_enable_collision(False)
        a.set_actor_tick_enabled(False)
        components=[]
        for c in a.get_components_by_class(unreal.MeshComponent):
            components.append({'name':c.get_name(),'visible':c.get_editor_property('visible'),
                'hidden_in_game':c.get_editor_property('hidden_in_game'),
                'transform':transform(c.get_world_transform()),
                'materials':[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())]})
        report.append({'actor':a.get_actor_label(),'components':components})
    write('visibility-diagnostic.json',report)
    return report

def remove_owned():
    guard()
    targets=[a for a in ACTORS.get_all_level_actors() if owned(a)]
    assert all(str(a.get_folder_path())==FOLDER and a.get_actor_label().startswith('Scale020_') for a in targets)
    for a in targets: assert ACTORS.destroy_actor(a)
    assert audit()['success']
    assert LEVEL.save_current_level()
    return {'removed_review_actors':len(targets),'environment_unchanged':True}

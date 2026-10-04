"""Real Editor/ISM art captures. Transient actors only; no gameplay session or map save."""
import json,math,time,traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/ReviewImages'
OUT.mkdir(parents=True,exist_ok=True)
META=json.loads((OUT.parent/'vat_metadata.json').read_text(encoding='utf8'))
CAMERA=json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Camera.json').read_text(encoding='utf8'))[0]
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
WORLD=EDITOR.get_editor_world()
assert not EDITOR.get_game_world(),'Art capture requires an idle editor'
BASE='/Game/GuLiStrike/Robots/RSGMech'
STATE={'actors':[],'frames':[],'start':time.time(),'success':False}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def spawn(cls,location):
    a=ACTORS.spawn_actor_from_class(cls,location,transient=True)
    STATE['actors'].append(a);return a

def finish(error=None):
    if STATE.get('handle'):unreal.unregister_slate_post_tick_callback(STATE['handle'])
    for a in reversed(STATE['actors']):ACTORS.destroy_actor(a)
    report={k:v for k,v in STATE.items() if k not in ['actors','handle']}
    report.update(success=error is None,error=error,scope='Editor art preview; no PIE, authoritative gameplay or frame-rate acceptance')
    (OUT.parent/'Reports/ue_art_capture.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')

try:
    origin=unreal.Vector(0,0,150000)
    actor=spawn(unreal.GuLiCommanderPresentationActor,origin)
    component=next(c for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.get_name()=='UnitInstances')
    component.set_static_mesh(unreal.load_asset(BASE+'/Meshes/SM_Pioneer_VAT'))
    component.set_num_custom_data_floats(59)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_editor_property('can_ever_affect_navigation',False)
    component.add_instance(unreal.Transform(),False)
    backdrop=spawn(unreal.StaticMeshActor,origin+unreal.Vector(0,0,-55))
    floor=backdrop.static_mesh_component
    floor.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
    backdrop.set_actor_scale3d(unreal.Vector(10000,10000,1))
    floor.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    floor.set_editor_property('can_ever_affect_navigation',False)
    mat=unreal.new_object(unreal.Material)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    color=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',unreal.LinearColor(.87,.85,.8,1))
    unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat);floor.set_material(0,mat)
    capture=spawn(unreal.SceneCapture2D,origin)
    cap=capture.capture_component2d
    for key,value in dict(capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,
        capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,primitive_render_mode=unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST,
        fov_angle=CAMERA['FieldOfViewDegrees']).items():cap.set_editor_property(key,value)
    cap.show_only_component(component);cap.show_only_component(floor)
    pp=cap.get_editor_property('post_process_settings')
    for key,value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,auto_exposure_bias=0,
        auto_exposure_apply_physical_camera_exposure=False,motion_blur_amount=0,bloom_intensity=0).items():
        pp.set_editor_property('override_'+key,True);pp.set_editor_property(key,value)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1)
    rt=unreal.RenderingLibrary.create_render_target2d(WORLD,2048,2048,unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,
        unreal.LinearColor(0,0,0,1),False,False)
    cap.set_editor_property('texture_target',rt)
    forward=next(c for c in META['clips'] if c['name']=='Forward')
    death=next(c for c in META['clips'] if c['name']=='Death')
    jobs=[{'name':'UE_Pioneer_Hero','height':950,'pitch':40,'yaw':135,'lod':1,'frame':0},
          {'name':'UE_Pioneer_WalkAim','height':950,'pitch':40,'yaw':135,'lod':1,'frame':forward['first_frame']+7.5,'aim':(.28,.12,-.1)},
          {'name':'UE_Pioneer_LOD1','height':950,'pitch':40,'yaw':135,'lod':2,'frame':0},
          {'name':'UE_Pioneer_LOD2','height':950,'pitch':40,'yaw':135,'lod':3,'frame':0},
          {'name':'UE_Pioneer_LOD3','height':950,'pitch':40,'yaw':135,'lod':4,'frame':0},
          {'name':'UE_Pioneer_Death','height':950,'pitch':40,'yaw':135,'lod':1,'frame':death['first_frame']+30},
          {'name':'UE_Commander_Near','height':CAMERA['MinimumHeightMeters']*100,'pitch':CAMERA['NearPitchDegrees'],'yaw':135,'lod':0,'frame':0},
          {'name':'UE_Commander_Tactical','height':CAMERA['TacticalStartHeightMeters']*100,'pitch':CAMERA['TacticalPitchDegrees'],'yaw':135,'lod':0,'frame':0},
          {'name':'UE_Commander_Overview','height':CAMERA['TacticalMaximumHeightMeters']*100,'pitch':CAMERA['OverviewPitchDegrees'],'yaw':135,'lod':0,'frame':0}]
    playback={'job':0,'ticks':0}
    def apply(job):
        component.set_forced_lod_model(job['lod'])
        values=[job['frame'],*job.get('aim',(0,0,0))]
        for i,value in enumerate(values):
            component.set_custom_data_value(0,51+i,value,False)
            component.set_custom_data_value(0,55+i,value,i==3)
        target=origin+unreal.Vector(0,0,150)
        pitch=math.radians(job['pitch']);yaw=math.radians(job['yaw'])
        horizontal=job['height']/math.tan(pitch)
        pos=target+unreal.Vector(-math.cos(yaw)*horizontal,-math.sin(yaw)*horizontal,job['height'])
        capture.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,target),False,True)
        cap.set_editor_property('camera_cut_this_frame',True)
    apply(jobs[0])
    def tick(dt):
        try:
            if time.time()-STATE['start']>180:raise RuntimeError('Art render timeout')
            if playback['ticks']<12:
                cap.capture_scene();playback['ticks']+=1;return
            job=jobs[playback['job']]
            unreal.RenderingLibrary.export_render_target(WORLD,rt,str(OUT),job['name']+'.png')
            STATE['frames'].append(job)
            playback['job']+=1;playback['ticks']=0
            if playback['job']==len(jobs):finish();return
            apply(jobs[playback['job']])
        except Exception:finish(traceback.format_exc())
    STATE['handle']=unreal.register_slate_post_tick_callback(tick)
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'state':'capturing','output':str(OUT)}))
except Exception:
    finish(traceback.format_exc())
    raise

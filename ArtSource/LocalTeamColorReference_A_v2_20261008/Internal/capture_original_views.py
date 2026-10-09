"""Actual isolated UE model renders, with one frozen pose/camera across all three variants."""
import json
import math
import runpy
import time
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/LocalTeamColorReference_A_v2_20261008'
OLD=ROOT/'ArtSource/LocalTeamColorReview_20261008'
report=json.loads((OLD/'asset-authoring.json').read_text(encoding='utf8'))
report['models']=[m for m in report['models'] if m['name'] in ('ShieldGenerator','ManualOutpost')]
assert report['success'] and len(report['models'])==2

editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=editor.get_editor_world()
assert editor.get_game_world() is None
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
OUT.joinpath('Sources').mkdir(exist_ok=True)
state={'success':False,'gameplay_started':False,'started':time.time(),'frames':[],'actors':[],'job':0,'ticks':0}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def spawn(cls,position):
    actor=api.spawn_actor_from_class(cls,position,transient=True)
    assert actor,cls
    state['actors'].append(actor)
    return actor

def finish(error=None):
    if state.get('handle'): unreal.unregister_slate_post_tick_callback(state['handle'])
    for actor in reversed(state['actors']): api.destroy_actor(actor)
    data={k:v for k,v in state.items() if k not in ('actors','handle')}
    data.update(success=error is None,error=error,source_assets_saved=False,
                source_geometry_views_only=True,geometry_animation_lod_unchanged=True)
    (OUT/'source-capture.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
    if error is None and globals().get('PREPARE_SCENE_AFTER_CAPTURE',False):
        runpy.run_path(str(ROOT/'Scripts/SceneUI/prepare_acceptance_scene.py'))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

try:
    origin=unreal.Vector(50000,0,20000)
    capture=spawn(unreal.SceneCapture2D,origin)
    cap=capture.capture_component2d
    for key,value in dict(capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,
            capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,projection_type=unreal.CameraProjectionMode.ORTHOGRAPHIC,
            primitive_render_mode=unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():cap.set_editor_property(key,value)
    cap.set_editor_property('show_flag_settings',[
        unreal.EngineShowFlagsSetting(show_flag_name='Fog',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='Atmosphere',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='VolumetricFog',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='DynamicShadows',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='GlobalIllumination',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='LumenGlobalIllumination',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='LumenReflections',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='ReflectionEnvironment',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='ScreenSpaceReflections',enabled=False),
        unreal.EngineShowFlagsSetting(show_flag_name='MotionBlur',enabled=False)])
    pp=cap.get_editor_property('post_process_settings')
    for key,value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
            auto_exposure_apply_physical_camera_exposure=False,auto_exposure_bias=0.0,bloom_intensity=0.0,
            motion_blur_amount=0.0,tone_curve_amount=0.0,blue_correction=0.0,expand_gamut=0.0).items():
        pp.set_editor_property('override_'+key,True);pp.set_editor_property(key,value)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1)
    target=unreal.RenderingLibrary.create_render_target2d(world,1280,1280,unreal.TextureRenderTargetFormat.RTF_RGBA8,
                    unreal.LinearColor(.02,.025,.03,1),False,False)
    cap.set_editor_property('texture_target',target)
    light=spawn(unreal.DirectionalLight,origin+unreal.Vector(0,0,5000))
    light.set_actor_rotation(unreal.Rotator(-50,-35,0),False)
    light.light_component.set_editor_property('intensity',8.0)
    fill=spawn(unreal.DirectionalLight,origin+unreal.Vector(0,0,5000))
    fill.set_actor_rotation(unreal.Rotator(-40,145,0),False)
    fill.light_component.set_editor_property('intensity',2.0)
    soldiers={r['Name']:r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8-sig'))}
    buildings={r['Name']:r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf-8-sig'))}
    groups=[];jobs=[]
    for model in report['models']:
        key=model['name']; mesh=unreal.load_asset(model['mesh'])
        if model['group']=='Mass' or key=='BiZhiMaoConstruction':
            actor=spawn(unreal.GuLiCommanderPresentationActor,origin)
            component=next(c for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.get_name()=='UnitInstances')
            component.set_static_mesh(mesh);component.clear_instances()
            component.set_editor_property('num_custom_data_floats',63)
            scale=soldiers[key]['PresentationScale'] if model['group']=='Mass' else 2.0
            component.add_instance(unreal.Transform(scale=unreal.Vector(scale,scale,scale)),False)
            for i in range(63):component.set_custom_data_value(0,i,-1000 if i in (0,29) else 1 if i==30 else 0,i==62)
            component.set_forced_lod_model(1);components=[component]
        elif key=='ResourceFactory':
            cls=unreal.load_class(None,'/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory.BP_ResourceProcessingFactory_C')
            actor=spawn(cls,origin)
            actor.set_actor_scale3d(unreal.Vector(.2,.2,.2))
            components=actor.get_components_by_class(unreal.MeshComponent)
            component=next(c for c in components if c.get_name()=='Body')
        elif model['kind']=='SkeletalMesh':
            actor=spawn(unreal.SkeletalMeshActor,origin);component=actor.skeletal_mesh_component
            component.set_skeletal_mesh_asset(mesh);component.set_forced_lod(1);components=[component]
        else:
            actor=spawn(unreal.StaticMeshActor,origin);component=actor.static_mesh_component
            component.set_static_mesh(mesh)
            row=buildings[key];s=row['MeshScale'];component.set_world_scale3d(unreal.Vector(s['X'],s['Y'],s['Z']))
            component.set_forced_lod_model(1);components=[component]
        for c in components:
            c.set_visibility(True);c.set_hidden_in_game(False);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        center,extent=actor.get_actor_bounds(False)
        diameter=max(extent.to_tuple())*2.9
        if model['group']=='Mass':
            # The presentation actor also has a HALF_WORLD_MAX route component. It is not model geometry.
            b=mesh.get_bounds();center=origin+b.origin*scale;diameter=max(b.box_extent.to_tuple())*scale*2.9
        if key=='WM01':center=origin+unreal.Vector(0,0,150);diameter=1750
        if key=='SweeperSummon':center=origin+unreal.Vector(0,0,120);diameter=650
        if key in ('BiZhiMao','BiZhiMaoConstruction'):
            center=origin+unreal.Vector(306,0,689);diameter=3970
        if model['group']=='SSF':diameter*=1.3
        group={'model':model,'actor':actor,'body':component,'components':components,
               'center':center,'diameter':max(200,diameter)}
        groups.append(group)
        # Hidden comparison subjects must not cast shadows or reflect into another subject.
        for c in components:c.set_visibility(False)
        for variant in ('Hero','Front','Left','Back'):jobs.append((len(groups)-1,variant))

    def apply():
        if 'visible_group' in state:
            for c in groups[state['visible_group']]['components']:c.set_visibility(False)
        group_index,variant=jobs[state['job']];group=groups[group_index]
        state['visible_group']=group_index
        for c in group['components']:c.set_visibility(True)
        model=group['model'];body=group['body']
        for slot in model['materials']:
            path='/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial'
            if path:body.set_material(slot['slot'],unreal.load_asset(path))
        cap.clear_show_only_components()
        for c in group['components']:cap.show_only_component(c)
        center=group['center'];diameter=group['diameter']
        eye=center+unreal.Vector(diameter*.9,-diameter*1.1,diameter*.85)
        if variant=='Front':eye=center+unreal.Vector(diameter*2,0,0)
        elif variant=='Left':eye=center+unreal.Vector(0,-diameter*2,0)
        elif variant=='Back':eye=center+unreal.Vector(-diameter*2,0,0)
        cap.set_editor_property('ortho_width',diameter)
        capture.set_actor_location_and_rotation(eye,unreal.MathLibrary.find_look_at_rotation(eye,center),False,True)
        cap.set_editor_property('camera_cut_this_frame',True)
        state['ticks']=0
    apply()

    def tick(dt):
        try:
            if time.time()-state['started']>900:raise RuntimeError('Art capture exceeded 15 minute limit')
            if state['ticks']<6:cap.capture_scene();state['ticks']+=1;return
            group_index,variant=jobs[state['job']];group=groups[group_index]
            key=group['model']['name'];filename=key+'_'+variant+'.png'
            unreal.RenderingLibrary.export_render_target(world,target,str(OUT/'Sources'),filename)
            state['frames'].append({'model':key,'variant':variant,'file':'Sources/'+filename,
                                   'camera':str(capture.get_actor_transform()),'ortho_width_cm':group['diameter'],
                                   'camera_location_cm':list(capture.get_actor_location().to_tuple()),
                                   'camera_rotation_deg':list(capture.get_actor_rotation().to_tuple())})
            state['job']+=1
            if state['job']==len(jobs):finish();return
            apply()
        except Exception:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    unreal._local_team_color_capture=state
except Exception:finish(traceback.format_exc())

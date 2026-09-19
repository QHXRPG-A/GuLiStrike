"""Capture original UE meshes using temporary preview actors; no asset saves."""
import json, time, traceback, math
from pathlib import Path
import unreal

if '-MechStyleReferenceWorker' not in unreal.SystemLibrary.get_command_line():
    raise RuntimeError('Use capture_mech_style_sources_worker.py in an isolated preview editor; live capture is disabled.')

OUT = Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/Source')
OUT.mkdir(parents=True, exist_ok=True)
MECH_CAPTURE_ACTORS = []
MECH_CAPTURE_REPORT = {'captures': [], 'components': {}, 'errors': []}
MECH_CAPTURE_ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MECH_CAPTURE_ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
MECH_CAPTURE_CAMERA_BEFORE = MECH_CAPTURE_ED.get_level_viewport_camera_info()
MECH_CAPTURE_VIEW_BEFORE = unreal.ViewportService.get_viewport_info()
MECH_CAPTURE_MODE_BEFORE = unreal.ViewportService.get_view_mode()
MECH_CAPTURE_SELECTION = MECH_CAPTURE_ACT.get_selected_level_actors()
MECH_CAPTURE_BASE = unreal.Vector(100000, 100000, 0)

def mech_spawn(cls, name, offset=(0,0,0), rotation=unreal.Rotator()):
    a = MECH_CAPTURE_ACT.spawn_actor_from_class(cls, MECH_CAPTURE_BASE+unreal.Vector(*offset), rotation, transient=True)
    MECH_CAPTURE_ACTORS.append(a)
    a.set_actor_label('MechStyleRef_'+name)
    return a

def mech_cleanup():
    for a in reversed(MECH_CAPTURE_ACTORS):
        if a:
            MECH_CAPTURE_ACT.destroy_actor(a)
    MECH_CAPTURE_ED.set_level_viewport_camera_info(*MECH_CAPTURE_CAMERA_BEFORE)
    MECH_CAPTURE_ACT.set_selected_level_actors(MECH_CAPTURE_SELECTION)
    unreal.ViewportService.set_view_mode(MECH_CAPTURE_MODE_BEFORE)
    unreal.ViewportService.set_realtime(MECH_CAPTURE_VIEW_BEFORE.is_realtime)
    (OUT/'capture_report.json').write_text(json.dumps(MECH_CAPTURE_REPORT,indent=2,ensure_ascii=False),encoding='utf-8')

try:
    subjects = {}
    paths = {
        'SpiderMech': '/Game/Assets/Mech_Project/Characters/SpiderMech/Mesh/SpiderMech',
        'Mecha_01': '/Game/Assets/MechaController/Artistic/Meshes/Mechas/Mecha_01/Mecha_01',
        'Mecha_02': '/Game/Assets/MechaController/Artistic/Meshes/Mechas/Mecha_02/mecha_02',
        'FireWeapon_01': '/Game/Assets/MechaController/Artistic/Meshes/Weapons/Weapon_01/FireWeapon_01',
        'MissileWeapon_01': '/Game/Assets/MechaController/Artistic/Meshes/Weapons/Weapon_02/MissileWeapon_01',
        'Machinegun_lvl1': '/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Meshes_Skeletal/Weapons/Weapons_Machinegun_lvl1',
        'Cockpit_01': '/Game/Assets/MechaController/Artistic/Meshes/Cockpits/Cockpit_01',
        'Missile_01': '/Game/Assets/MechaController/Artistic/Meshes/Projectiles/Missile_01/lowPoly_missile_01',
    }
    for name,p in paths.items():
        mesh = unreal.load_asset(p)
        sk = isinstance(mesh,unreal.SkeletalMesh)
        a = mech_spawn(unreal.SkeletalMeshActor if sk else unreal.StaticMeshActor,name)
        c = a.get_component_by_class(unreal.SkeletalMeshComponent if sk else unreal.StaticMeshComponent)
        if sk: c.set_skeletal_mesh_asset(mesh)
        else: c.set_static_mesh(mesh)
        subjects[name]=a
    cls = unreal.load_class(None,'/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Blueprints/Mech_Lightest_Blueprint.Mech_Lightest_Blueprint_C')
    subjects['Mech_Lightest'] = mech_spawn(cls,'Mech_Lightest')
    for name,a in subjects.items():
        comps=[]
        for c in a.get_components_by_class(unreal.MeshComponent):
            m = c.get_editor_property('skeletal_mesh_asset') if isinstance(c,unreal.SkeletalMeshComponent) else c.get_editor_property('static_mesh') if isinstance(c,unreal.StaticMeshComponent) else None
            comps.append({'name':c.get_name(),'mesh':m.get_path_name() if m else None,'materials':[x.get_path_name() if x else None for x in c.get_materials()]})
        MECH_CAPTURE_REPORT['components'][name]=comps
        a.set_is_temporarily_hidden_in_editor(True)
    floor = mech_spawn(unreal.StaticMeshActor,'Floor',(0,0,-8))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.static_mesh_component.set_material(0,unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
    floor.set_actor_scale3d(unreal.Vector(1000,1000,.1))
    for i,(rot,power) in enumerate([((-38,-45,0),3.0),((-30,135,0),1.5)]):
        light=mech_spawn(unreal.DirectionalLight,'Light'+str(i),rotation=unreal.Rotator(*rot))
        c=light.get_component_by_class(unreal.DirectionalLightComponent)
        c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_intensity(power)
    sky=mech_spawn(unreal.SkyLight,'Sky').get_component_by_class(unreal.SkyLightComponent)
    sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.set_intensity(1.0)
    camera = mech_spawn(unreal.CameraActor,'Camera')
    cc = camera.get_component_by_class(unreal.CameraComponent)
    cc.set_field_of_view(35)
    cc.set_editor_property('aspect_ratio',1.5)
    pp = unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
        override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
        override_auto_exposure_bias=True,auto_exposure_bias=0.,override_motion_blur_amount=True,motion_blur_amount=0.,
        override_bloom_intensity=True,bloom_intensity=.1).items(): pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp)
    cc.set_editor_property('post_process_blend_weight',1.)
    MECH_CAPTURE_ACT.set_selected_level_actors([])
    unreal.ViewportService.set_view_mode('lit')
    unreal.ViewportService.set_realtime(True)
    # Meshes use +Y forward in this pack; retain both opposing views as evidence.
    views={'Hero': (1.5,2.0,.9),'Front':(0,2.6,0),'Side':(2.6,0,0),'Back':(0,-2.6,0)}
    jobs=[]
    for name in ['SpiderMech','Mecha_01','Mecha_02','Mech_Lightest']:
        for view in views: jobs.append((name,view))
    for name in ['FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Cockpit_01','Missile_01']:
        for view in ['Hero','Side']: jobs.append((name,view))
    MECH_CAPTURE_STATE={'i':0,'task':None,'configured':False,'warm':0.,'start':time.monotonic()}

    def mech_capture_tick(delta):
        try:
            st=MECH_CAPTURE_STATE
            if time.monotonic()-st['start']>300: raise RuntimeError('Capture time limit reached')
            if st['task']:
                if not st['task'].is_task_done():return
                name,view=jobs[st['i']]
                filename=name+'_'+view+'.png'
                if not (OUT/filename).is_file(): raise RuntimeError('Capture missing: '+filename)
                MECH_CAPTURE_REPORT['captures'].append(filename)
                st.update(i=st['i']+1,task=None,configured=False)
            if st['i']>=len(jobs):
                unreal.unregister_slate_post_tick_callback(MECH_CAPTURE_HANDLE)
                mech_cleanup()
                return
            name,view=jobs[st['i']]
            if not st['configured']:
                for key,a in subjects.items():a.set_is_temporarily_hidden_in_editor(key!=name)
                origin,extent=subjects[name].get_actor_bounds(False)
                span=max(extent.x,extent.y,extent.z)*2
                pos=origin+unreal.Vector(*views[view])*span
                rot=unreal.MathLibrary.find_look_at_rotation(pos,origin)
                camera.set_actor_location(pos,False,False)
                camera.set_actor_rotation(rot,False)
                cc.set_projection_mode(unreal.CameraProjectionMode.PERSPECTIVE if view=='Hero' else unreal.CameraProjectionMode.ORTHOGRAPHIC)
                cc.set_editor_property('ortho_width',span*1.7)
                MECH_CAPTURE_ED.set_level_viewport_camera_info(pos,rot)
                st.update(configured=True,warm=4. if st['i']==0 else .8)
                return
            st['warm']-=delta
            if st['warm']>0:return
            st['task']=unreal.AutomationLibrary.take_high_res_screenshot(1440,960,str(OUT/(name+'_'+view+'.png')),camera,delay=.3)
        except Exception:
            MECH_CAPTURE_REPORT['errors'].append(traceback.format_exc())
            unreal.unregister_slate_post_tick_callback(MECH_CAPTURE_HANDLE)
            mech_cleanup()
    MECH_CAPTURE_HANDLE=unreal.register_slate_post_tick_callback(mech_capture_tick)
    print('Source capture dispatched: '+str(len(jobs))+' images')
except Exception:
    MECH_CAPTURE_REPORT['errors'].append(traceback.format_exc())
    mech_cleanup()
    raise

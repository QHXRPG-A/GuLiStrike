"""Real UE frame-stepped reference playback in the unchanged Demo_Map.

This proves authored visual scale/timing, NOT authoritative/networked Q execution.
No environment or FX package is saved. Three explicitly changed Note tables may be reimported.
"""
import json,os,runpy,time,traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
if (ROOT/'Data/Excel/GuLiStrikeVfx.xlsx').exists():
    raise RuntimeError('This historical three-variant playback predates VfxId. Use Scripts/Vfx/validate_vfx_static.py for the current catalog.')
OUT=ROOT/'TestResults/Scale020/FXReview'
OUT.mkdir(parents=True,exist_ok=True)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=editor.get_editor_world()
state={'pid':os.getpid(),'started':time.time(),'actors':[],'clips':[]}

def write(name,data): (OUT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def asset(value): return value if isinstance(value,unreal.Object) else unreal.load_asset(str(value))
def spawn(cls,location,rotation=unreal.Rotator()):
    a=actors.spawn_actor_from_class(cls,location,rotation,transient=True)
    a.set_actor_location_and_rotation(location,rotation,False,True)
    state['actors'].append(a)
    return a
def finish(error=None):
    if state.get('handle'): unreal.unregister_slate_post_tick_callback(state['handle'])
    for a in reversed(state['actors']): actors.destroy_actor(a)
    report={k:v for k,v in state.items() if k not in ('actors','handle')}
    report.update(success=error is None,error=error,finished=time.time(),
        scope='Frame-stepped production asset playback, not a runtime or multiplayer acceptance test')
    write('capture.json',report)
    unreal.SystemLibrary.quit_editor()

try:
    assert world.get_path_name().split('.')[0]=='/Game/StylizedPineEnvironment/Maps/Demo_Map'
    assert not editor.get_game_world()
    demo=runpy.run_path(str(ROOT/'Scripts/Scale020/demo_review.py'))
    assert demo['audit']()['success']
    # Update reviewed annotations only; all numerical cells were previously imported and checked.
    ns={'__name__':'scale020_note_import'}
    source=(ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    exec(compile(source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0],
                 'import_data_to_engine.py','exec'),ns)
    ns['PROGRESS']=str(OUT/'notes-import.log')
    manifest=json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf-8'))
    imported=[]
    for name in ('DT_GuLiStrikeCommander_Soldiers','DT_GuLiStrikeShip_Camera','DT_GuLiStrikeSecondaryUnitSkills_Skills'):
        r=ns['import_table'](name,manifest['tables'][name]);assert r.get('imported'),r;imported.append(r)
    write('notes-import-readback.json',{'success':True,'tables':imported})
    # No material diagnostic/compile calls: inspection must not rewrite or recompile assets.
    mining='/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green'
    inspection={}
    for emitter in unreal.NiagaraService.list_emitters(mining):
        name=emitter.emitter_name
        listed=unreal.NiagaraEmitterService.list_modules(mining,name,'')
        modules=[unreal.NiagaraEmitterService.get_module_info(mining,name,m.module_name) for m in listed]
        modules=[m for m in modules if m]
        inspection[name]=[{'name':m.module_name,'stage':m.module_type,'enabled':m.is_enabled,
            'inputs':[{'name':p.input_name,'type':p.input_type,'value':p.current_value,
                       'default':p.default_value,'linked':p.is_linked,'source':p.linked_source} for p in m.inputs]}
            for m in modules]
    write('mining-module-inputs.json',inspection)

    style=unreal.load_asset('/Game/GuLiStrike/FX/GroundWarning/DA_GroundWarning_Red')
    field=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_MissileExplosion')
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeSpellFields_Fields')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    config=next(r for r in rows if r['Name']=='WM01_MissileExplosion')
    radius=float(config['RadiusCentimeters'])
    reference=field.get_editor_property('visual_reference_radius')
    assert radius==160. and reference==800.
    origin=demo['ground'](14500,-3800)+unreal.Vector(0,0,2)
    state.update(radius_cm=radius,art_reference_cm=reference,component_scale=radius/reference,
                 warning_period=style.wave_period,projection_depth=style.projection_depth,
                 origin=list(origin.to_tuple()),fps=30)
    decal=spawn(unreal.DecalActor,origin,unreal.Rotator(pitch=-90)).decal
    decal.set_editor_property('decal_size',unreal.Vector(style.projection_depth,radius/.96,radius/.96))
    decal.set_editor_property('fade_screen_size',0)
    decal.set_decal_material(asset(style.material))
    material=decal.create_dynamic_material_instance()
    assert material
    for k,v in [('Age',0.),('WavePeriod',style.wave_period),('RingWidth',style.ring_width),('Opacity',style.opacity)]:
        material.set_scalar_parameter_value(k,v)
    material.set_vector_parameter_value('Tint',unreal.LinearColor(1,.025,.015,1))
    decal.set_visibility(False)
    effect=spawn(unreal.NiagaraActor,origin)
    comp=effect.niagara_component
    comp.set_auto_activate(False);comp.set_force_solo(True);comp.set_random_seed_offset(1337)
    comp.set_cast_shadow(False);comp.set_can_render_while_seeking(True);comp.set_component_tick_enabled(False)
    effect.set_actor_scale3d(unreal.Vector(radius/reference,radius/reference,radius/reference))
    capture=spawn(unreal.SceneCapture2D,origin)
    cap=capture.capture_component2d
    pos=origin+unreal.Vector(-780,-900,1000)
    look=origin+unreal.Vector(0,0,90)
    capture.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,look),False,True)
    for k,v in [('capture_every_frame',False),('capture_on_movement',False),
                ('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR),('fov_angle',45.)]:cap.set_editor_property(k,v)
    pp=cap.get_editor_property('post_process_settings')
    for k,v in [('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('auto_exposure_bias',0.),
                ('auto_exposure_apply_physical_camera_exposure',False),('motion_blur_amount',0.)]:
        pp.set_editor_property('override_'+k,True);pp.set_editor_property(k,v)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1.)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8,
                                                     unreal.LinearColor(0,0,0,1),False,False)
    cap.set_editor_property('texture_target',rt)
    variants=list(field.activation_variants)
    assert len(variants)==3
    playback={'index':0,'frame':-25,'pending':None}
    def begin():
        variant=variants[playback['index']]
        system=asset(variant.system)
        assert system and str(variant.scale_parameter_name)=='None'
        comp.set_asset(system,True);comp.set_visibility(False);comp.deactivate()
        comp.set_component_tick_enabled(False)
        folder=OUT/system.get_name();folder.mkdir(exist_ok=True)
        playback.update(folder=folder,frame=-25,pending=None)
        state['clips'].append({'system':system.get_path_name(),'folder':str(folder),'frames':0,
                               'warning_start_s':.2,'impact_s':2.,'end_s':5.5,'variant_scale':variant.scale})
        assert variant.scale==1.
    begin()
    def tick(dt):
        try:
            if time.time()-state['started']>600: raise RuntimeError('FX capture timeout')
            f=playback['frame']
            if playback['pending'] is not None:
                unreal.RenderingLibrary.export_render_target(world,rt,str(playback['folder']),
                    'frame_%03d.png'%playback['pending'])
                state['clips'][-1]['frames']+=1
            if f>=165:
                comp.deactivate();comp.set_visibility(False);decal.set_visibility(False)
                playback['index']+=1
                if playback['index']>=len(variants):
                    # Transient actors are removed by finish before environment audit in the next run.
                    finish();return
                begin();return
            if f==60:
                comp.set_visibility(True);comp.reinitialize_system();comp.activate(True)
                comp.set_paused(False);comp.set_component_tick_enabled(False)
            elif 60<f<150: comp.advance_simulation(1,1/30)
            elif f==150: comp.deactivate();comp.set_visibility(False)
            decal.set_visibility(6<=f<60)
            material.set_scalar_parameter_value('Age',max(0.,(f-6)/30))
            cap.capture_scene()
            playback['pending']=f if f>=0 else None
            playback['frame']=f+1
        except Exception: finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
except Exception: finish(traceback.format_exc())

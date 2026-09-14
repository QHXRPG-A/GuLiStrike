"""Scoped, opt-in UE5.7 Commander combat-effects validation.

Execute in the editor through ue_exec.py. Set VFX_QA_MODE before exec:
calibration: transient final Crowd models and bright mount markers, never saves a map.
assets: compile/read back only CommanderWeapons systems and references.
runtime: read-only snapshot of all active PIE worlds, not a substitute for a network run.
Every stage records its real errors and restores/deletes its temporary objects.
"""
import json
import traceback
import csv
import statistics
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/commander-combat-effects'
DEST = '/Game/GuLiStrike/FX/CommanderWeapons'
OUT.mkdir(parents=True, exist_ok=True)
MODE = globals().get('VFX_QA_MODE', 'assets')
REPORT = {'mode': MODE, 'errors': [], 'checks': {}}

def props(obj, names):
    return {n: str(obj.get_editor_property(n)) for n in names}

def capture_setup(world, actors, center, unit=None):
    camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, center, transient=True)
    capture = camera.capture_component2d
    capture.set_editor_property('capture_every_frame', False)
    capture.set_editor_property('capture_on_movement', False)
    capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    capture.set_editor_property('fov_angle', 35)
    pp = capture.get_editor_property('post_process_settings')
    pp.set_editor_property('override_auto_exposure_bias', True)
    pp.set_editor_property('auto_exposure_bias', 0.0)
    capture.set_editor_property('post_process_settings', pp)
    if unit:
        capture.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        capture.show_only_actor_components(unit)
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1280, 1024, unreal.TextureRenderTargetFormat.RTF_RGBA8,
                                                        unreal.LinearColor(.015,.02,.03,1), False, False)
    capture.set_editor_property('texture_target', rt)
    return camera, capture, rt

def calibration():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    data = json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_WeaponMounts.json').read_text(encoding='utf-8'))
    material = unreal.Material()
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(0,6,1,1))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    REPORT['mounts'] = data
    for unit_type, name, path in [(1,'FourFRobot','/Game/Commander/Units/SM_CommanderFourFRobot_Crowd'),
                                   (2,'WM01','/Game/Commander/Units/SM_WM01_Crowd')]:
        temporary=[]
        try:
            unit = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,5000), transient=True)
            temporary.append(unit)
            unit.static_mesh_component.set_static_mesh(unreal.load_asset(path))
            center, extent = unit.get_actor_bounds(False)
            camera, capture, rt = capture_setup(world, actors, center, unit)
            temporary.append(camera)
            for mount in data:
                if mount['UnitTypeId'] != unit_type or mount['PointRole'].lower() != 'muzzle': continue
                xyz=mount['Offset']
                marker = actors.spawn_actor_from_class(unreal.StaticMeshActor,
                    unreal.Vector(xyz['X'],xyz['Y'],xyz['Z'])+unit.get_actor_location(), transient=True)
                temporary.append(marker)
                marker.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'))
                marker.static_mesh_component.set_material(0,material)
                marker.set_actor_scale3d(unreal.Vector(.35,.35,.35) if unit_type==1 else unreal.Vector(.7,.7,.7))
                capture.show_only_actor_components(marker)
            distance=max(extent.to_tuple())*3.3
            for label, offset in [('side',(0,-1,.1)),('front',(1,0,.15)),('above',(1,-1,.7))]:
                loc = center+unreal.Vector(*[v*distance for v in offset])
                camera.set_actor_location_and_rotation(loc,unreal.MathLibrary.find_look_at_rotation(loc,center),False,True)
                light = actors.spawn_actor_from_class(unreal.PointLight,loc,transient=True)
                temporary.append(light)
                light.point_light_component.set_intensity(200000000)
                light.point_light_component.set_attenuation_radius(distance*4)
                capture.capture_scene()
                unreal.RenderingLibrary.export_render_target(world,rt,str(OUT),name+'_mount_'+label+'.png')
                actors.destroy_actor(light); temporary.remove(light)
        finally:
            for actor in reversed(temporary): actors.destroy_actor(actor)

def assets():
    REPORT['systems']={}
    for name in ['NS_CommanderGunfireBatch','NS_WM01_MissileFlight','NS_WM01_Explosion_7','NS_WM01_Explosion_8','NS_WM01_Explosion_9']:
        path=DEST+'/'+name
        result=unreal.NiagaraService.compile_with_results(path)
        entry={'compile':props(result,['success','errors','warnings']), 'emitters':[]}
        if not result.get_editor_property('success') or result.get_editor_property('errors'):
            REPORT['errors'].append('Niagara compile failed: '+path)
        for emitter in unreal.NiagaraService.list_emitters(path):
            en=str(emitter.get_editor_property('emitter_name'))
            modules=list(unreal.NiagaraEmitterService.list_modules(path,en))
            details=[]
            enabled_lights=0
            for i,_ in enumerate(unreal.NiagaraEmitterService.list_renderers(path,en)):
                detail=unreal.NiagaraEmitterService.get_renderer_details(path,en,i)
                details.append(str(detail))
                if not detail.get_editor_property('is_enabled'): continue
                if str(detail.get_editor_property('renderer_type'))=='Light':
                    enabled_lights+=1
                    if name!='NS_CommanderGunfireBatch' or en not in ('Tracer','Muzzle'):
                        REPORT['errors'].append('Unbounded dynamic light renderer is enabled: '+path+'/'+en)
                material_path=str(detail.get_editor_property('material_path'))
                mesh_path=str(detail.get_editor_property('mesh_path'))
                material=unreal.load_asset(material_path) if material_path else None
                if not material and mesh_path:
                    mesh=unreal.load_asset(mesh_path); material=mesh.get_material(0) if mesh else None
                if material:
                    base=material
                    while base and not isinstance(base,unreal.Material): base=base.get_editor_property('parent')
                    if not base:
                        REPORT['errors'].append('Missing parent material: '+material.get_path_name()); continue
                    diagnostic=unreal.MaterialNodeService.get_material_diagnostics(base.get_path_name())
                    if not diagnostic.get_editor_property('success') or not diagnostic.get_editor_property('is_compiled_ok'):
                        REPORT['errors'].append('Material compilation/diagnostics failed: '+base.get_path_name())
                    used=set(unreal.MaterialEditingLibrary.get_used_textures(base))
                    if base!=material:
                        for parameter in unreal.MaterialEditingLibrary.get_texture_parameter_names(base):
                            default=unreal.MaterialEditingLibrary.get_material_default_texture_parameter_value(base,parameter)
                            actual=unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material,parameter)
                            if actual:
                                used.discard(default); used.add(actual)
                    textures=[t.get_path_name() for t in used]
                    REPORT.setdefault('materials',{})[material.get_path_name()]={'textures':textures,'diagnostics':str(diagnostic)}
                    if any('DefaultTexture' in t or 'WorldGrid' in t or 'DefaultMaterial' in t for t in textures):
                        REPORT['errors'].append('Placeholder reference: '+material.get_path_name())
                    if not material.get_path_name().startswith(DEST+'/') or any(not t.startswith(DEST+'/') for t in textures):
                        REPORT['errors'].append('Effect still uses a non-project material/texture: '+material.get_path_name())
                elif str(detail.get_editor_property('renderer_type')) in ['Sprite','Mesh','Ribbon']:
                    REPORT['errors'].append('Missing renderer material: '+path+'/'+en)
            entry['emitters'].append({'name':en,'details':str(emitter),
                'modules':str(modules),
                'renderers':details})
            if name=='NS_CommanderGunfireBatch':
                expected='PersistentMuzzleShape' if en=='Muzzle' else 'ExactTracerSegment'
                if not any(str(m.get_editor_property('module_name')).startswith(expected) for m in modules):
                    REPORT['errors'].append('Missing exact gunfire geometry module: '+en+'/'+expected)
                if enabled_lights!=1:
                    REPORT['errors'].append('Gunfire emitter must have exactly one enabled capped light renderer: '+en)
        REPORT['systems'][name]=entry
    gunfire_lights=[renderer for renderer in unreal.ObjectIterator(unreal.NiagaraLightRendererProperties)
                    if renderer.get_outermost().get_name()==DEST+'/NS_CommanderGunfireBatch']
    REPORT['gunfire_light_renderers']=[]
    for renderer in gunfire_lights:
        REPORT['gunfire_light_renderers'].append({
            'path':renderer.get_path_name(),
            'use_inverse_squared_falloff':bool(renderer.get_editor_property('use_inverse_squared_falloff')),
            'alpha_scales_brightness':bool(renderer.get_editor_property('alpha_scales_brightness')),
            'affects_translucency':bool(renderer.get_editor_property('affects_translucency')),
            'allow_mega_lights':bool(renderer.get_editor_property('allow_mega_lights')),
            'radius_scale':float(renderer.get_editor_property('radius_scale')),
            'default_exponent':float(renderer.get_editor_property('default_exponent')),
            'specular_scale':float(renderer.get_editor_property('specular_scale')),
            'diffuse_scale':float(renderer.get_editor_property('diffuse_scale'))})
    REPORT['checks']['safe_gunfire_light_renderers']=len(gunfire_lights)==2 and all(
        not renderer.get_editor_property('use_inverse_squared_falloff')
        and renderer.get_editor_property('alpha_scales_brightness')
        and not renderer.get_editor_property('affects_translucency')
        and not renderer.get_editor_property('allow_mega_lights')
        and float(renderer.get_editor_property('radius_scale'))<=1.0
        and float(renderer.get_editor_property('specular_scale'))<=.25
        and float(renderer.get_editor_property('diffuse_scale'))<=1.0
        for renderer in gunfire_lights)
    catalog=unreal.load_asset(DEST+'/DA_CommanderCombatEffects')
    REPORT['catalog']=str(catalog)
    if not catalog: REPORT['errors'].append('Missing effect catalog')
    else:
        lifetime=float(catalog.get_editor_property('tracer_lifetime'))
        width=float(catalog.get_editor_property('tracer_width'))
        hold=float(catalog.get_editor_property('muzzle_activity_hold_seconds'))
        refresh=float(catalog.get_editor_property('muzzle_refresh_rate'))
        muzzle_lifetime=float(catalog.get_editor_property('muzzle_particle_lifetime'))
        muzzle_width=float(catalog.get_editor_property('muzzle_width'))
        muzzle_length=float(catalog.get_editor_property('muzzle_length'))
        strobe_rate=float(catalog.get_editor_property('muzzle_strobe_rate'))
        strobe_duty=float(catalog.get_editor_property('muzzle_strobe_duty_cycle'))
        max_muzzle_lights=int(catalog.get_editor_property('maximum_muzzle_lights_per_frame'))
        max_tracer_lights=int(catalog.get_editor_property('maximum_tracer_lights_per_frame'))
        muzzle_light_radius=float(catalog.get_editor_property('muzzle_light_radius'))
        muzzle_light_brightness=float(catalog.get_editor_property('muzzle_light_brightness'))
        tracer_light_radius=float(catalog.get_editor_property('tracer_light_radius'))
        tracer_light_brightness=float(catalog.get_editor_property('tracer_light_brightness'))
        REPORT['gunfire_settings']={'tracer_lifetime':lifetime,'tracer_width':width,
            'muzzle_activity_hold_seconds':hold,'muzzle_refresh_rate':refresh,
            'muzzle_particle_lifetime':muzzle_lifetime,'muzzle_width':muzzle_width,
            'muzzle_length':muzzle_length,'muzzle_strobe_rate':strobe_rate,'muzzle_strobe_duty_cycle':strobe_duty,
            'maximum_muzzle_lights_per_frame':max_muzzle_lights,'maximum_tracer_lights_per_frame':max_tracer_lights,
            'muzzle_light_radius':muzzle_light_radius,'muzzle_light_brightness':muzzle_light_brightness,
            'tracer_light_radius':tracer_light_radius,'tracer_light_brightness':tracer_light_brightness,
            'tint':str(catalog.get_editor_property('gunfire_tint'))}
        REPORT['checks']['short_tracer_lifetime']=.05 <= lifetime <= .09
        REPORT['checks']['readable_tracer_width']=width >= 20
        REPORT['checks']['persistent_muzzle_hold']=1.5 <= hold <= 2.0
        REPORT['checks']['muzzle_refresh_bridges_frames']=refresh >= 20 and muzzle_lifetime >= 1.5/refresh
        REPORT['checks']['readable_muzzle_dimensions']=muzzle_width >= 350 and muzzle_length >= 800
        REPORT['checks']['muzzle_strobe_is_visible']=6 <= strobe_rate <= 16 and .25 <= strobe_duty <= .65
        REPORT['checks']['bounded_dynamic_lights']=0 < max_muzzle_lights <= 16 and 0 < max_tracer_lights <= 8 \
            and max_muzzle_lights+max_tracer_lights <= 24
        REPORT['checks']['muzzle_ground_light']=muzzle_light_radius >= 2200 and muzzle_light_brightness > 0
        REPORT['checks']['tracer_ground_light']=tracer_light_radius >= 1500 and tracer_light_brightness > 0
    mount_table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_WeaponMounts')
    if not mount_table:
        REPORT['errors'].append('Missing WeaponMounts DataTable')
    else:
        imported_mounts=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(mount_table))
        source_mounts=json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_WeaponMounts.json').read_text(encoding='utf-8'))
        REPORT['weapon_mounts']=imported_mounts
        def normalized_mount(row):
            offset=row.get('Offset') or {}
            if isinstance(offset,str):
                offset=dict(part.split('=',1) for part in offset.strip('()').split(',') if '=' in part)
            return {
                'Name':row.get('Name',''),
                'UnitTypeId':int(row.get('UnitTypeId',0)),
                'SlotId':row.get('SlotId',''),
                'PointRole':row.get('PointRole',''),
                'PointIndex':int(row.get('PointIndex',0)),
                'SocketName':row.get('SocketName',''),
                'Offset':tuple(float(offset.get(axis,0)) for axis in ('X','Y','Z')),
                'bCalibrated':bool(row.get('bCalibrated',False)),
            }
        REPORT['checks']['weapon_mount_table_matches_excel']=sorted(
            (normalized_mount(row) for row in imported_mounts),key=lambda row:row['Name']) == sorted(
            (normalized_mount(row) for row in source_mounts),key=lambda row:row['Name'])

    field=unreal.load_asset(DEST+'/DA_WM01_MissileExplosion')
    REPORT['field_definition']=str(field)
    if not field or str(field.get_editor_property('config_id'))!='WM01_MissileExplosion':
        REPORT['errors'].append('WM01 field definition is not bound to SpellFields/WM01_MissileExplosion')
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeSpellFields_Fields')
    if not table:
        REPORT['errors'].append('Missing SpellFields DataTable')
    else:
        rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        REPORT['spell_fields']=rows
        row=next((r for r in rows if r.get('Name')=='WM01_MissileExplosion'),None)
        REPORT['checks']['spell_field_table_values']=bool(row and float(row.get('Damage',0))==30
            and float(row.get('RadiusCentimeters',0))==800 and row.get('Timing')=='Instant')
    REPORT['errors'] += [key for key,value in REPORT['checks'].items() if not value]
    smoke=unreal.load_asset(DEST+'/M_WM01_BlastSmoke')
    subuv=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionParticleSubUV) if n.get_outer()==smoke]
    smoke_renderers=[r for r in unreal.ObjectIterator(unreal.NiagaraSpriteRendererProperties)
                     if r.get_path_name().startswith(DEST+'/') and r.get_editor_property('material')==smoke]
    REPORT['smoke_flipbooks']=[{'renderer':r.get_path_name(),
        'sub_image_size':list(r.get_editor_property('sub_image_size').to_tuple())} for r in smoke_renderers]
    if len(subuv)!=1 or not smoke_renderers or any(r.get_editor_property('sub_image_size')!=unreal.Vector2D(8,8) for r in smoke_renderers):
        REPORT['errors'].append('Blast smoke must sample an 8x8 ParticleSubUV flipbook, never the whole sprite sheet')

def preview():
    """Fixed-step three-phase renders with real frames between simulation, capture and export."""
    REPORT=globals()['REPORT']
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(): raise RuntimeError('Stop PIE before isolated preview')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    REPORT['captures']=[]
    REPORT['visual_review']='Pending human/model inspection of exported frames; execution success is not visual approval.'
    REPORT['pending']=True
    state={'names':['NS_WM01_Explosion_7','NS_WM01_Explosion_8','NS_WM01_Explosion_9','NS_WM01_MissileFlight'],
           'temporary':[], 'frames':0, 'phase':0, 'step':'create', 'age':0}
    def cleanup():
        for actor in reversed(state['temporary']): actors.destroy_actor(actor)
        state['temporary'].clear()
    def tick(delta):
        state['frames']+=1
        if state['frames']%3: return
        try:
            if state['step']=='create':
                if not state['names']:
                    unreal.unregister_slate_post_tick_callback(_vfx_preview_callback)
                    REPORT['pending']=False; REPORT['success']=not REPORT['errors']
                    (OUT/'validation_preview.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')
                    return
                name=state['names'].pop(0); state['name']=name; state['phase']=0; state['age']=0
                center=unreal.Vector(0,0,5000)
                effect=actors.spawn_actor_from_class(unreal.NiagaraActor,center,transient=True); state['temporary'].append(effect)
                component=effect.get_component_by_class(unreal.NiagaraComponent)
                component.set_asset(unreal.load_asset(DEST+'/'+name)); component.activate(True)
                component.set_variable_vec3('User.Velocity',unreal.Vector(6000,0,0))
                component.set_paused(True); state['component']=component
                camera,capture,rt=capture_setup(world,actors,center,effect); state['temporary'].append(camera)
                state.update(capture=capture,rt=rt)
                distance=3000 if 'Explosion' in name else 900
                loc=center+unreal.Vector(distance,-distance,distance*.65)
                camera.set_actor_location_and_rotation(loc,unreal.MathLibrary.find_look_at_rotation(loc,center),False,True)
                state['step']='simulate'
            elif state['step']=='simulate':
                now=[2/60,12/60,48/60,174/60][state['phase']]
                component=state['component']; component.set_paused(False)
                if 'MissileFlight' in state['name'] and state['phase']==2: component.deactivate()
                component.advance_simulation_by_time(now-state['age']+.00001,1/60)
                component.set_paused(True); state['age']=now; state['step']='capture'
            elif state['step']=='capture':
                state['capture'].capture_scene(); state['step']='export'
            else:
                label=['start','peak','fade','clear'][state['phase']]; file=state['name']+'_'+label+'.png'
                unreal.RenderingLibrary.export_render_target(world,state['rt'],str(OUT),file)
                REPORT['captures'].append({'asset':state['name'],'age':state['age'],'file':file})
                state['phase']+=1
                if state['phase']==4: cleanup(); state['step']='create'
                else: state['step']='simulate'
        except Exception as exc:
            cleanup(); unreal.unregister_slate_post_tick_callback(_vfx_preview_callback)
            REPORT['errors'].append(str(exc)); REPORT['pending']=False; REPORT['success']=False
            (OUT/'validation_preview.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')
    globals()['_vfx_preview_callback']=unreal.register_slate_post_tick_callback(tick)

def runtime_window():
    """Require exact tracers plus a continuously refreshed muzzle during sustained combat."""
    REPORT=globals()['REPORT']
    REPORT['pending']=True; REPORT['samples']=[]
    label=globals().get('VFX_QA_LABEL','runtime_window')
    if not label.replace('_','').replace('-','').isalnum(): raise ValueError('Alphanumeric evidence label required')
    state={'elapsed':0,'until_sample':0}
    def tick(delta):
        state['elapsed']+=delta; state['until_sample']-=delta
        if state['until_sample']>0: return
        state['until_sample']=.11
        try:
            worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
            if not worlds: raise RuntimeError('PIE stopped during observation')
            unreal.SystemLibrary.execute_console_command(worlds[0],'gs.CombatEffects.QA.Sample')
            native=json.loads((OUT/'native_runtime_sample.json').read_text(encoding='utf8'))
            sample=[]
            for row in native:
                world=next(w for w in worlds if w.get_path_name()==row['world'])
                by_asset={}
                for c in row.pop('niagara'):
                    name=c['asset'].split('.')[-1]
                    entry=by_asset.setdefault(name,{'allocated':0,'active':0,'particles':0})
                    entry['allocated']+=1; entry['active']+=int(c['active'])
                    entry['particles']+=sum(e['particles'] for e in c['emitters'])
                row['niagara']=by_asset
                for name in ['GuLiCombatEffectRuntimeSubsystem','GuLiCombatEffectPresentationSubsystem']:
                    system=next((s for s in unreal.ObjectIterator(getattr(unreal,name)) if s.get_outer()==world),None)
                    if system:
                        names=['projectiles_launched','fields_created','pulses','damage_commits','shots_published','last_step_milliseconds'] if 'Runtime' in name else ['received_shots','written_shots','dropped_shots','bursts_played','component_count','last_update_milliseconds']
                        row[name]={k:system.get_counters().get_editor_property(k) for k in names}
                sample.append(row)
            REPORT['samples'].append(sample)
            if state['elapsed']<5: return
            roles=sorted(r['net_mode'] for r in sample)
            REPORT['checks']['network_roles']=roles in [[2,3],[1,3,3]]
            REPORT['checks']['500_unit_rosters']=all(r['roster_count']==500 for r in sample)
            expected_visuals=unreal.SystemLibrary.get_console_variable_int_value('gs.CombatEffects.Visuals')!=0
            for index,row in enumerate(sample):
                history=[s[index] for s in REPORT['samples']]
                if row['net_mode']==1:
                    REPORT['checks']['dedicated_has_no_vfx']=all(not r['niagara'] for r in history)
                else:
                    gun_samples=[r['niagara'].get('NS_CommanderGunfireBatch',{}).get('particles',0) for r in history]
                    gun_particles=max(gun_samples)
                    REPORT['checks']['gun_particles_'+str(index)]=(gun_particles>0) if expected_visuals else (gun_particles==0)
                    first=history[0].get('GuLiCombatEffectPresentationSubsystem',{})
                    last=row.get('GuLiCombatEffectPresentationSubsystem',{})
                    REPORT['checks']['live_shots_'+str(index)]=last.get('received_shots',0)>first.get('received_shots',0)
                    received=[r.get('GuLiCombatEffectPresentationSubsystem',{}).get('received_shots',0) for r in history]
                    live_index=next((i for i,v in enumerate(received) if v>received[0]),None)
                    steady=gun_samples[min(live_index+2,len(gun_samples)):] if live_index is not None else []
                    # Once firing is established, the muzzle refresh must bridge all
                    # sampled frames; the hitscan tracer remains a separate short row.
                    REPORT['checks']['gunfire_persistent_'+str(index)]=(len(steady)>=5 and all(v>0 for v in steady)) \
                        if expected_visuals else all(v==0 for v in gun_samples)
                    if row['net_mode']==3: REPORT['checks']['client_no_authority_'+str(index)]='GuLiCombatEffectRuntimeSubsystem' not in row
            # Compare simultaneously living fields/projectiles by stable effect ID,
            # never infer consistency from aggregate explosion counts alone.
            visual_systems=[s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem)
                            if s.get_outer() in worlds]
            maps=[{tuple(x.effect_id.get_editor_property(n) for n in ['a','b','c','d']):x
                   for x in s.get_effect_states()} for s in visual_systems]
            common=set.intersection(*(set(m) for m in maps)) if len(maps)>=2 else set()
            REPORT['common_active_effects']=len(common)
            REPORT['checks']['shared_effect_identity']=bool(common)
            REPORT['checks']['shared_seed_and_variant']=all(
                len({(m[k].random_seed,m[k].variant_index,str(m[k].kind)) for m in maps})==1 for k in common)
            REPORT['errors'] += [k for k,v in REPORT['checks'].items() if not v]
            REPORT['pending']=False; REPORT['success']=not REPORT['errors']
            unreal.unregister_slate_post_tick_callback(_vfx_window_callback)
            (OUT/('validation_'+label+'.json')).write_text(json.dumps(REPORT,indent=2),encoding='utf8')
        except Exception as exc:
            REPORT['pending']=False; REPORT['success']=False; REPORT['errors'].append(str(exc))
            unreal.unregister_slate_post_tick_callback(_vfx_window_callback)
            (OUT/('validation_'+label+'.json')).write_text(json.dumps(REPORT,indent=2),encoding='utf8')
    globals()['_vfx_window_callback']=unreal.register_slate_post_tick_callback(tick)

def runtime():
    REPORT['worlds']=[]
    worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
    if worlds: unreal.SystemLibrary.execute_console_command(worlds[0],'gs.CombatEffects.QA.Sample')
    native=json.loads((OUT/'native_runtime_sample.json').read_text(encoding='utf8')) if worlds else []
    for world in unreal.ObjectIterator(unreal.World):
        if 'UEDPIE_' not in world.get_path_name(): continue
        entry=next((r for r in native if r['world']==world.get_path_name()),{'world':world.get_path_name()})
        for name in ['GuLiCombatEffectRuntimeSubsystem','GuLiCombatEffectPresentationSubsystem']:
            system=next((s for s in unreal.ObjectIterator(getattr(unreal,name)) if s.get_outer()==world),None)
            if system:
                fields=['projectiles_launched','fields_created','pulses','damage_commits','shots_published','candidate_checks','last_step_milliseconds'] if name=='GuLiCombatEffectRuntimeSubsystem' else ['received_shots','written_shots','dropped_shots','received_states','rejected_states','bursts_played','component_count','last_update_milliseconds']
                counters=system.get_counters()
                entry[name]={n:counters.get_editor_property(n) for n in fields}
            else: entry[name]=None
        game_state=unreal.GameplayStatics.get_game_state(world)
        entry['replication_components']=[c.get_class().get_name() for c in game_state.get_components_by_class(unreal.ActorComponent)] if game_state else []
        REPORT['worlds'].append(entry)
    if not REPORT['worlds']: REPORT['errors'].append('PIE is not running')

def performance():
    """Same 500-unit fixed-camera worlds; two 600-frame process-wide CSV captures."""
    report=REPORT
    label=globals().get('VFX_QA_LABEL','listen')
    if label not in ['listen','dedicated']: raise ValueError('Performance label must be listen or dedicated')
    worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
    if not worlds: raise RuntimeError('Start the acceptance PIE fixture first')
    world=worlds[0]
    settings={'r.GPUCsvStatsEnabled':1,'r.VSync':0,'t.MaxFPS':0,'r.ScreenPercentage':100,'r.DynamicRes.OperationMode':0}
    original={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in settings}
    original['gs.CombatEffects.Visuals']=unreal.SystemLibrary.get_console_variable_int_value('gs.CombatEffects.Visuals')
    report.update(pending=True,original_cvars=original,capture_cvars=settings,frames_per_capture=600,
                  scope='All PIE worlds in one Editor process, 960x540 per rendered client; not packaged-game performance',captures=[])
    def command(text): unreal.SystemLibrary.execute_console_command(world,text)
    for key,value in settings.items(): command(key+' '+str(value))
    for category in ['Particles','RHI']: command('CsvCategory '+category+' enable')
    state={'phase':0,'elapsed':0,'started':False,'filename':None,'wait':0}
    command('gs.CombatEffects.Visuals 0')
    def finish(error=None):
        if error: report['errors'].append(error)
        for key,value in original.items(): command(key+' '+str(value))
        unreal.unregister_slate_post_tick_callback(_vfx_perf_callback)
        report['pending']=False; report['success']=not report['errors']
        (OUT/('performance_'+label+'.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
    def tick(delta):
        state['elapsed']+=delta
        try:
            if not state['started']:
                if state['elapsed']<5: return
                suffix='off' if state['phase']==0 else 'on'
                file=label+'_'+suffix+'.csv'; state['filename']=file
                # CsvProfiler resolves a relative filename beneath Saved/Profiling/CSV.
                command('CsvProfile STARTFILE=../../../outputs/commander-combat-effects/'+file)
                command('CsvProfile FRAMES=600')
                state['started']=True; state['elapsed']=0
                return
            if state['elapsed']<12: return
            path=OUT/state['filename']
            try:
                complete=path.exists() and 'HasHeaderRowAtEnd' in path.read_text(encoding='utf8')[-8192:]
            except PermissionError:
                complete=False # Windows keeps the CSV exclusively locked until finalization.
            if not complete:
                if state['elapsed']>55: raise RuntimeError('CSV did not complete: '+str(path))
                return
            # Capture completes before opening the file. Collect counts outside the CSV window.
            command('gs.CombatEffects.QA.Sample')
            native=json.loads((OUT/'native_runtime_sample.json').read_text(encoding='utf8'))
            compact=[]
            for row in native:
                components=row.pop('niagara')
                row['components_registered']=len(components)
                row['components_active']=sum(int(c['active']) for c in components)
                row['particles']=sum(e['particles'] for c in components for e in c['emitters'])
                compact.append(row)
            report['captures'].append({'visuals':state['phase'],'csv':str(path),'worlds':compact})
            if state['phase']==1: finish(); return
            state.update(phase=1,elapsed=0,started=False)
            command('gs.CombatEffects.Visuals 1')
        except Exception as exc: finish(str(exc))
    globals()['_vfx_perf_callback']=unreal.register_slate_post_tick_callback(tick)

def performance_summary():
    report=REPORT
    report['metrics_ms']={}
    metrics=['FrameTime','GameThreadTime','RenderThreadTime','GPUTime','Exclusive/GameThread/Effects',
             'Exclusive/RenderThread/Niagara','Exclusive/AllWorkers/Effects','GPU/NiagaraGPUSimulation','GPU/Translucency']
    for mode in ['listen','dedicated']:
        for suffix in ['off','on']:
            path=OUT/(mode+'_'+suffix+'.csv')
            if not path.exists(): continue
            with path.open(encoding='utf8') as stream: rows=list(csv.DictReader(stream))[:600]
            data={}
            for key in metrics:
                values=[]
                for row in rows:
                    try: values.append(float(row[key]))
                    except (KeyError,ValueError,TypeError): pass
                if values:
                    values.sort(); data[key]={'p50':statistics.median(values),'p95':values[int((len(values)-1)*.95)],'samples':len(values)}
            report['metrics_ms'][mode+'_'+suffix]=data

def late_join():
    """Start in dedicated-late mode; observe existing effects on the joining peer."""
    report=REPORT
    worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
    systems=[s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer() in worlds and s.get_effect_states()]
    if len(worlds)!=2 or len(systems)!=1: raise RuntimeError('Requires the dedicated-late fixture before its second client joins')
    existing=systems[0].get_outer().get_path_name()
    join_time=unreal.GameplayStatics.get_game_state(systems[0].get_outer()).get_server_world_time_seconds()
    report.update(pending=True,join_server_time=join_time,observed_prejoin_projectiles=[],old_finished_fields_seen=[])
    state={'elapsed':0,'after_ready':0}
    def tick(delta):
        state['elapsed']+=delta
        try:
            peers=[s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem)
                   if 'UEDPIE_' in s.get_outer().get_path_name() and s.get_outer().get_path_name()!=existing]
            if peers and peers[0].get_effect_states():
                state['after_ready']+=delta
                for effect in peers[0].get_effect_states():
                    if effect.start_time>=join_time: continue
                    key=list(effect.effect_id.get_editor_property(n) for n in ['a','b','c','d'])
                    if effect.kind==unreal.GuLiCombatEffectKind.PROJECTILE:
                        if key not in report['observed_prejoin_projectiles']: report['observed_prejoin_projectiles'].append(key)
                    elif effect.end_time<=join_time:
                        if key not in report['old_finished_fields_seen']: report['old_finished_fields_seen'].append(key)
            if state['after_ready']<3 and state['elapsed']<20: return
            report['checks']['prejoin_live_projectiles_rebuilt']=bool(report['observed_prejoin_projectiles'])
            report['checks']['no_prejoin_finished_explosion_state']=not report['old_finished_fields_seen']
            report['errors'] += [k for k,v in report['checks'].items() if not v]
            report['pending']=False; report['success']=not report['errors']
            unreal.unregister_slate_post_tick_callback(_vfx_late_callback)
            (OUT/'validation_late_join.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        except Exception as exc:
            report['pending']=False; report['success']=False; report['errors'].append(str(exc))
            unreal.unregister_slate_post_tick_callback(_vfx_late_callback)
            (OUT/'validation_late_join.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    globals()['_vfx_late_callback']=unreal.register_slate_post_tick_callback(tick)
    unreal.SystemLibrary.execute_console_command(worlds[0],'gs.CombatEffects.QA.LateJoin')

def setup():
    """Start a disposable 500-unit opposing deployment; restore CDOs with the stop stage."""
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(): raise RuntimeError('End current PIE before setup')
    performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
    globals().setdefault('_vfx_qa_original_throttle',performance.get_editor_property('bThrottleCPUWhenNotForeground'))
    performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
    authority=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiBattleAuthoritySubsystem'))
    globals()['_vfx_qa_original_spawns']={n:list(authority.get_editor_property(n).to_tuple()) for n in ['RedSpawnCenter','BlueSpawnCenter']}
    # Preserve the existing 500-unit generator and its strict separation/nav validation.
    authority.set_editor_property('BlueSpawnCenter',unreal.Vector(-10000,97500,0))
    globals()['_vfx_qa_initialized_worlds']=set()
    def initialize_worlds(delta):
        try:
            worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
            for world in worlds:
                if world.get_path_name() in _vfx_qa_initialized_worlds: continue
                if unreal.GameplayStatics.get_game_mode(world): unreal.SystemLibrary.execute_console_command(world,'gs.GM.Set soldier.max_health 1000000')
                pc=unreal.GameplayStatics.get_player_controller(world,0)
                pawn=pc.get_controlled_pawn() if pc else None
                if not pawn: continue
                pawn.set_actor_tick_enabled(False)
                pawn.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),True)
                pawn.set_actor_location(unreal.Vector(-10000,112500,-7100),False,True)
                arm=pawn.get_component_by_class(unreal.SpringArmComponent)
                if arm:
                    arm.set_editor_property('do_collision_test',False)
                    arm.set_editor_property('target_arm_length',35000.)
                    for name in ['use_pawn_control_rotation','inherit_pitch','inherit_yaw','inherit_roll']:
                        arm.set_editor_property(name,False)
                    arm.set_relative_rotation(unreal.Rotator(pitch=-65,yaw=90,roll=0),False,True)
                _vfx_qa_initialized_worlds.add(world.get_path_name())
        except Exception as exc:
            unreal.unregister_slate_post_tick_callback(_vfx_qa_init_callback)
            print('Combat VFX fixture setup failed: '+str(exc))
    globals()['_vfx_qa_init_callback']=unreal.register_slate_post_tick_callback(initialize_worlds)
    mode=globals().get('VFX_QA_NET_MODE','listen')
    if mode not in ['listen','dedicated','dedicated-late']: raise ValueError('listen, dedicated or dedicated-late required')
    unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'gs.CombatEffects.QA.Start '+mode)
    REPORT['requested_network_mode']=mode

def stop():
    if '_vfx_qa_init_callback' in globals(): unreal.unregister_slate_post_tick_callback(globals().pop('_vfx_qa_init_callback'))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    authority=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiBattleAuthoritySubsystem'))
    for name,xyz in globals().pop('_vfx_qa_original_spawns',{}).items(): authority.set_editor_property(name,unreal.Vector(*xyz))
    if '_vfx_qa_original_throttle' in globals():
        performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        performance.set_editor_property('bThrottleCPUWhenNotForeground',globals().pop('_vfx_qa_original_throttle'))
    REPORT['restored_spawn_defaults']=True

try:
    {'calibration':calibration,'assets':assets,'runtime':runtime,'runtime_window':runtime_window,'performance':performance,
     'performance_summary':performance_summary,'late_join':late_join,'preview':preview,'setup':setup,'stop':stop}[MODE]()
except Exception as exc:
    REPORT['errors'].append(str(exc)); REPORT['traceback']=traceback.format_exc()
finally:
    REPORT['dirty_content']=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    REPORT['success']=None if REPORT.get('pending') else not REPORT['errors']
    (OUT/('validation_'+MODE+'.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'mode':MODE,'success':REPORT['success'],'errors':REPORT['errors'],'report':str(OUT/('validation_'+MODE+'.json'))}))

"""Record distance LOD review frames in an isolated UE editor world.

Call start(frame_root, names) from the editor. Game-owned assets render directly to
three equal-framing cameras. Simulation attributes and clocks are recorded separately.
"""
import json
import math
import sys
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/GroundMech/Rogue'))
from inspect_burst_geometry import shell_extent

OUT=ROOT/'ArtSource/FX/DistanceLOD_20261127'
TIERS=('Full','Reduced','Minimal')
DISTANCES=(3500.,11000.,19000.)
WIDTH,HEIGHT=360,248
V=unreal.Vector
V2=unreal.Vector2D
V4=unreal.Vector4
C=unreal.LinearColor
A=unreal.NiagaraDataInterfaceArrayFunctionLibrary
NS=unreal.NiagaraService
# width in world centimetres, simulation duration, look-at height
STAGING={
 'NS_Sparks':(1450,1.3,45), 'NS_Sparks_Smaller':(1050,1.2,45), 'NS_Trails':(1150,2.4,40),
 'NS_Tornado_under':(1100,3.2,175), 'P_UpgradeGlow_Converted':(1400,2.45,170),
 'NS_Hit_1':(320,.95,15),'NS_GroundMech_Muzzle_A3':(260,.6,4),
 'NS_ConstructionComplete':(1800,2.6,180),'NS_ConstructionTopLoop':(1250,2.4,160),
 'NS_GroundMech_RocketJet':(490,2.1,0), 'NS_GroundMech_Blink':(650,1.2,25),
 'NS_WingmanBombardment_01':(3400,2.7,340),'NS_WM01_Explosion_2A':(1800,2.25,170),
 'NS_GroundExplosiveImpact':(2100,2.4,210), 'NS_NoSurvivorsImpact':(3500,3,340),
 'NS_GroundBurningBurst':(1500,1.15,55),'NS_GroundBurningStatus':(520,2.45,70),
 'NS_NoSurvivorsCastFlash':(550,1.1,0),'NS_NoSurvivorsCastLightning':(570,1.4,15),
 'NS_GroundElectromagneticArc':(760,1.05,20),'NS_TransitFlash':(9500,.65,30),
 'NS_WarMachineHoverPool':(1180,2.55,10),'NS_RogueUpgrade_Lite':(650,2,80),
 'NS_WingmanLaserPool_SmallBatches':(1080,1.8,5),'NS_CommanderGunfireBatch':(1100,1.7,15),
 'NS_GroundDestruction_03':(2700,3.15,300),'NS_WingmanDestruction_05':(2750,3.15,260),
 'N_Stylized_Explosion_1A_Classic_Explosion':(2400,2.25,100),
 'NS_Explosion_04':(3400,2.6,260),'NS_Explosion_07':(4400,2.95,270),
 'NS_XuntianBlackSmoke':(1100,3.1,30),'NS_XuntianSilverGlow':(750,1.8,20),
}
GROUND_TRAILS={'NS_GroundBulletTrail_Orange','NS_GroundBurningBullet','NS_EnhancedGold','NS_EnhancedSilver','NS_EnhancedViolet','NS_GroundExplosiveTrail'}
MOBILE={'NS_Trails','NS_XuntianSilverGlow','NS_XuntianBlackSmoke','NS_WingmanFlightTrail','NS_WM01_MissileFlight','NS_GroundExplosiveFlight_Toon','NS_TransitTrail','NS_NoSurvivorsFlight'}
CYCLED={'NS_GroundMech_RocketJet','NS_GroundBurningStatus','NS_ConstructionTopLoop'}
# In the editor world Niagara restarts GPU instances when DI arrays change.
# Record these systems in PIE so their real slot history survives each upload.
PLAY_POOLS={'NS_WarMachineHoverPool','NS_RogueUpgrade_Lite','NS_WingmanLaserPool_SmallBatches','NS_CommanderGunfireBatch'}
CHANNEL='/Game/GuLiStrike/FX/CommanderWeapons/NDC_CommanderGunfire'

def feed(c,name,origin,t,tier,static=False):
    """Supply the same explicit public inputs as the corresponding presentation caller."""
    if name in GROUND_TRAILS:
        c.set_variable_vec2('User.TrailSize',V2(64,720))
        c.set_variable_vec3('User.TrailFacing',V(0,-.86,.51))
        c.set_variable_vec4('User.TrailStyle',V4(14,15,.12,.3))
        c.set_variable_vec4('User.TrailShape',V4(720,64,0,0))
        head=min(715,max(0,t*810))
        c.set_variable_vec4('User.TrailData',V4(720,head,max(0,t-.89),810))
    elif name=='NS_MassBulletTrail_GPU':
        if static:
            c.set_variable_int('User.TrailSlotCount',3)
            A.set_niagara_array_position(c,'User.TrailOrigins',[origin+V(-360,0,h) for h in (-64,0,64)])
            A.set_niagara_array_vector(c,'User.TrailDirections',[V(1,0,0)]*3)
            A.set_niagara_array_vector(c,'User.TrailFacings',[V(0,-.86,.51)]*3)
            A.set_niagara_array_float(c,'User.TrailSpeeds',[760.]*3)
            A.set_niagara_array_vector4(c,'User.TrailTimes',[V4(v,v+.9,-1.e30,0) for v in (0,.15,.3)])
            A.set_niagara_array_color(c,'User.TrailColors',[C(1,.32,.10,1)]*3)
            A.set_niagara_array_int32(c,'User.TrailGenerations',[1]*3)
            c.set_variable_vec4('User.TrailStyle',V4(11,12,.12,.24));c.set_variable_float('User.TrailMaximumLength',720)
        c.set_variable_float('User.TrailNow',t)
    elif name=='NS_WarMachineHoverPool':
        x=min(t,1.55)*530-385
        nozzles=[origin+V(x,0,40),origin+V(x+70,120,40)]
        c.set_variable_float('User.HoverTime',t)
        if static:
            c.set_variable_float('User.HoverMinTrailLength',500);c.set_variable_float('User.HoverMaxTrailLength',1500);c.set_variable_float('User.HoverTrailGrowTime',1)
        c.set_variable_position('User.HoverCamera',origin+V(0,-.86,.5103)*DISTANCES[tier])
        A.set_niagara_array_position(c,'User.LaserPositions',[p+V(0,0,-28) for p in nozzles])
        A.set_niagara_array_vector(c,'User.LaserDirections',[V(0,0,-1)]*2)
        A.set_niagara_array_vector2d(c,'User.LaserSizes',[V2(114,75)]*2+[V2(0,0)])
        A.set_niagara_array_color(c,'User.LaserColors',[C(.04,.28,1,.9)]*2+[C(0,0,0,0)])
        A.set_niagara_array_position(c,'User.MuzzlePositions',nozzles)
        A.set_niagara_array_vector2d(c,'User.MuzzleSizes',[V2(114,530 if t<1.55 else 0)]*2+[V2(0,0)])
        A.set_niagara_array_color(c,'User.MuzzleColors',[C(1,1,1,(1,3,9)[tier])]*2+[C(0,0,0,0)])
    elif name=='NS_RogueUpgrade_Lite':
        A.set_niagara_array_position(c,'User.UpgradePositions',[origin])
        A.set_niagara_array_color(c,'User.UpgradeColors',[C(2.5,1.2,.18,1),C(0,0,0,0)])
        A.set_niagara_array_vector(c,'User.UpgradeParameters',[V(t,1,(18,6,0)[tier]),V(0,0,0)])
    elif name=='NS_WingmanLaserPool_SmallBatches':
        if static: c.set_variable_int('User.LaserSlotCount',1)
        x=-290+(t%1.05)*560
        A.set_niagara_array_position(c,'User.LaserPositions',[origin+V(x,0,0)])
        A.set_niagara_array_vector(c,'User.LaserDirections',[V(1,0,0)])
        A.set_niagara_array_vector2d(c,'User.LaserSizes',[V2(18,245)])
        A.set_niagara_array_color(c,'User.LaserColors',[C(.9,3.2,5.5,1)])
        A.set_niagara_array_position(c,'User.MuzzlePositions',[origin+V(-340,0,0)])
        A.set_niagara_array_vector2d(c,'User.MuzzleSizes',[V2(34,26)])
        A.set_niagara_array_color(c,'User.MuzzleColors',[C(1.1,4,6,1 if tier<2 and (t%1.05)<.2 else 0)])
    elif name=='NS_CommanderGunfireBatch':
        if static:return
        # NDC writes use the world frame clock; the isolated editor's natural tick dispatches rows.
        period=(1,2,4)[tier]
        i=round(t*60)
        rows=[]
        if i%15==0:rows.append((0,origin,V(1,0,0),700.,11.,.10,0.))
        if i%(3*period)==0:rows.append((1,origin+V(-355,0,0),V(1,0,0),78.,24.,period*.09,3. if tier==0 else 0.))
        if rows:
            writer=unreal.NiagaraDataChannelLibrary.write_to_niagara_data_channel(c,unreal.load_asset(CHANNEL),unreal.NiagaraDataChannelSearchParameters(),len(rows),False,True,True,'Distance LOD review')
            if writer:
                for j,(mode,pos,direction,length,width,life,light) in enumerate(rows):
                    writer.write_position('Position',j,pos);writer.write_vector('Direction',j,direction)
                    writer.write_int('Mode',j,mode);writer.write_float('Length',j,length);writer.write_float('Width',j,width)
                    writer.write_linear_color('Tint',j,C(2,.53,.12,1));writer.write_float('Lifetime',j,life)
                    writer.write_float('VisualIntensity',j,2);writer.write_float('LightBrightness',j,light);writer.write_float('LightRadius',j,80.)
    elif name.startswith('NS_Construction'):
        if static:
            points=[V(-260,-180,0),V(260,-180,0),V(260,180,0),V(-260,180,0)]
            # Construction geometry is local to the component. Use a neutral footprint for review.
            A.set_niagara_array_vector(c,'User.RoofPoints',points)
            A.set_niagara_array_vector(c,'User.RoofTangents',[V(1,0,0),V(0,1,0),V(-1,0,0),V(0,-1,0)])
            A.set_niagara_array_float(c,'User.SegmentLengths',[520.,360.,520.,360.])
            c.set_variable_int('User.RoofPointCount',4);c.set_variable_float('User.BuildingHeight',340)
            c.set_variable_float('User.ColumnHeight',210);c.set_variable_float('User.RoofStep',58)
            c.set_variable_bool('User.SparksActive',True)
            if name=='NS_ConstructionComplete':
                mesh=unreal.load_asset('/Engine/BasicShapes/Cube')
                c.set_variable_static_mesh('User.ContourWallMesh',mesh);c.set_variable_static_mesh('User.FootprintMesh',mesh)
        c.set_variable_float('User.ConstructionHeight',70+min(t,2)*130)
    elif name=='NS_GroundElectromagneticArc':
        c.set_variable_position('User.ArcStart',origin+V(-220,0,20));c.set_variable_position('User.ArcEnd',origin+V(220,20,95))
    elif name=='NS_GroundMech_Blink':
        c.set_variable_vec3('User.Forward',V(1,0,0));c.set_variable_vec3('User.BlinkOffset',V(-min(330,1600*t),0,0))
        c.set_variable_float('User.Width',14);c.set_variable_float('User.Emission',5);c.set_variable_float('User.Lifetime',.75)
    elif name=='NS_GroundBurningBurst' and static:
        c.set_variable_float('User.Radius',500);c.set_variable_float('User.StartRadius',100)
    elif name in MOBILE:
        u=min(t,1.36); c.set_world_location(origin+V(-310+u*445,0,20+24*math.sin(u*2)),False,False)
        if static:
            c.set_variable_vec3('User.Forward',V(1,0,0));c.set_variable_vec3('User.Right',V(0,1,0))
            c.set_variable_vec3('User.Velocity',V(445,0,0));c.set_variable_vec3('User.FlightVelocity',V(445,0,0))
            c.set_variable_float('User.NozzleSpacing',70);c.set_variable_float('User.Opacity',1)
            c.set_variable_float('User.Throttle',1);c.set_variable_float('User.FlightLifetime',1.4)
            c.set_variable_float('User.FlightActive',1)
        if t>1.4:
            c.set_variable_float('User.FlightActive',0);c.deactivate()
    if name in CYCLED and t>1.35:c.deactivate()

def cases():
    audit=json.loads((ROOT/'Progress/Evidence/20261127-默认对局特效LOD审计.json').read_text(encoding='utf8'))
    authored=json.loads((OUT/'authored-systems.json').read_text(encoding='utf8'))
    rows={row['Id']:row for row in json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf-8-sig'))}
    selected={}
    for r in audit['niagara_assets']:
        group=r['scope'];name=r['name'];path=r['path']
        if group in ('未见距离减量','备用单弹','模型蓝图依赖'):
            p=authored.get(name,{}).get('path',path);selected[name]=[p]*3
        elif group=='已制作、当前模块未接入' and r['resident']:
            row=rows[r['catalog'][0][0]]
            selected[name]=[row[k].split('.')[0] for k in ('ResourcePath','ReducedResourcePath','MinimalResourcePath')]
    assert len(selected)==44,len(selected)
    return selected

def frame_width(name):
    if name in GROUND_TRAILS or name=='NS_MassBulletTrail_GPU':return (980,1.65,10)
    return STAGING.get(name,(1030,2.35,40))

def start(folder,selection=None,game_world=None):
    global REVIEW
    files=cases()
    selection=list(files if selection is None else selection)
    play_cases=[name for name in selection if not game_world and name in PLAY_POOLS]
    if play_cases:
        selection=[name for name in selection if name not in PLAY_POOLS]
        if not selection:
            return start_play_review(folder,play_cases)
    world=game_world or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    unreal.SystemLibrary.execute_console_command(world,'fx.Niagara.ForceWaitForCompilationOnActivate 1')
    reference=V(0,0,80000)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(reference,unreal.Rotator(yaw=90))
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    previous=json.loads((folder/'progress.json').read_text(encoding='utf8')) if (folder/'progress.json').is_file() else {}
    record={'finished':previous.get('finished',{}),'failed':previous.get('failed',{}),'pending':selection[:]}
    for name in selection:
        record['finished'].pop(name,None);record['failed'].pop(name,None)
    def stamp():
        tmp=folder/'progress.tmp';tmp.write_text(json.dumps(record,ensure_ascii=False,indent=1),encoding='utf8');tmp.replace(folder/'progress.json')
    def actor(cls,pos):
        if not game_world:
            return editor.spawn_actor_from_class(cls,pos,unreal.Rotator(),transient=True)
        transform=unreal.Transform(location=pos)
        statics=unreal.get_default_object(unreal.GameplayStatics)
        value=statics.call_method('BeginDeferredActorSpawnFromClass',(world,cls,transform,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN))
        return statics.call_method('FinishSpawningActor',(value,transform))
    def destroy(value):
        if game_world:value.destroy_actor()
        else:editor.destroy_actor(value)
    # Fixed exposure and neutral stage. The same two lights illuminate all three comparison columns.
    sun=actor(unreal.DirectionalLight,V(0,0,1000));sun.set_actor_rotation(unreal.Rotator(pitch=-55,yaw=-30),False);sun.light_component.set_intensity(3.5)
    sky=actor(unreal.SkyLight,V(0,0,1200));sky.light_component.set_intensity(.9)
    P={}
    def clear():
        for a in reversed(P.get('actors',[])):destroy(a)
        P.clear()
    def begin(name):
        width,duration,aim_z=frame_width(name);P.update(name=name,actors=[],effects=[],caps=[],frame=-3,duration=duration,stats=[{}, {}, {}],active_slots=[{}, {}, {}],states=[[],[],[]],width=width,aim_z=aim_z)
        out=folder/name;out.mkdir(exist_ok=True)
        for path in dict.fromkeys(files[name]):
            assert unreal.load_asset(path),path
            if game_world:continue
            result=NS.compile_with_results(path)
            assert result.success and not result.errors,(path,str(result))
        for j,(path,dist) in enumerate(zip(files[name],DISTANCES)):
            origin=reference+V(0,dist,0)
            comp_actor=actor(unreal.NiagaraActor,origin);P['actors'].append(comp_actor)
            sys=unreal.load_asset(path)
            c=comp_actor.niagara_component
            c.set_editor_property('auto_activate',False)
            assert not c.get_editor_property('auto_activate')
            c.set_editor_property('allow_scalability',False);c.set_force_solo(True);c.set_preview_lod_distance(True,dist,100000);c.set_asset(sys,True)
            c.set_can_render_while_seeking(True);c.set_random_seed_offset(4242);c.set_rendering_enabled(True)
            c.set_cast_shadow(False);c.set_component_tick_enabled(False)
            if name in GROUND_TRAILS:
                c.set_world_location(origin+V(-355,0,0),False,False)
                c.set_system_fixed_bounds(unreal.Box(min=V(-64,-64,-64),max=V(784,64,64)))
            if name in ('NS_MassBulletTrail_GPU','NS_CommanderGunfireBatch','NS_RogueUpgrade_Lite','NS_WarMachineHoverPool','NS_WingmanLaserPool_SmallBatches'):
                comp_actor.set_actor_location(V(0,0,0),False,False)
                b=unreal.Box(min=origin-V(2500,2500,2500),max=origin+V(2500,2500,2500));c.set_system_fixed_bounds(b)
                for emitter in NS.list_emitters(path):
                    c.set_emitter_fixed_bounds(str(emitter.emitter_name),b)
            feed(c,name,origin,0,j,True)
            target=origin+V(0,0,aim_z);pos=target+V(0,-.86,.5103)*dist
            ca=actor(unreal.SceneCapture2D,pos);P['actors'].append(ca)
            ca.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos,target),False)
            cap=ca.capture_component2d
            cap.set_editor_property('capture_every_frame',False);cap.set_editor_property('capture_on_movement',False)
            cap.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            cap.set_editor_property('fov_angle',math.degrees(2*math.atan(width/(2*dist))))
            pp=cap.get_editor_property('post_process_settings')
            for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('override_auto_exposure_bias',True),('auto_exposure_bias',0.),('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False),('override_motion_blur_amount',True),('motion_blur_amount',0.)]:pp.set_editor_property(k,v)
            cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1.)
            rt=unreal.RenderingLibrary.create_render_target2d(world,WIDTH,HEIGHT,unreal.TextureRenderTargetFormat.RTF_RGBA8,C(.08,.10,.13,1),False,False)
            cap.set_editor_property('texture_target',rt);cap.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
            cap.show_only_component(c)
            deck=actor(unreal.StaticMeshActor,origin-V(0,0,90));P['actors'].append(deck)
            mesh=deck.static_mesh_component;mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
            mesh.set_world_scale3d(V(width*.07,width*.07,.16));mesh.set_cast_shadow(False)
            base_mat=unreal.load_asset('/Game/Commander/Units/Tactical/Preview/Sweeper/M_ReviewFloor')
            if base_mat:mesh.set_material(0,base_mat)
            cap.show_only_actor_components(deck)
            P['effects'].append((c,origin));P['caps'].append((cap,rt))
    def sample(c,tier):
        if P['frame'] in (0,2,12):
            P['states'][tier].append(json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_distance_lod_preview_state(c)))
        shell_sample=None
        if P['name']=='NS_GroundBurningBurst':
            shell_sample={'frame':P['frame'],'count':0,'capture_valid':False,'component_active':c.is_active()}
            P.setdefault('shell_samples',[[],[],[]])[tier].append(shell_sample)
            if not c.is_active():return
        cache=unreal.NiagaraSimCacheFunctionLibrary.create_niagara_sim_cache(world)
        pars=unreal.NiagaraSimCacheCreateParameters();pars.set_editor_property('attribute_capture_mode',unreal.NiagaraSimCacheAttributeCaptureMode.ALL)
        if unreal.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(cache,pars,c,False):
            if shell_sample is not None:shell_sample['capture_valid']=True
            for en in cache.get_emitter_names():
                count=len(cache.read_position_attribute('Position',en))
                if count:P['stats'][tier][str(en)]=max(P['stats'][tier].get(str(en),0),count)
                if shell_sample is not None and count and str(en)=='BurningShell':
                    scales=cache.read_vector_attribute('Scale',en)
                    colors=cache.read_color_attribute('Color',en)
                    shell_sample.update(count=count)
                    if scales and colors:
                        shell_sample.update(radii_cm=[round(x*y,3) for x,y in zip(scales[0].to_tuple(),shell_extent(c.get_asset().get_path_name()))],
                                            rgba=list(colors[0].to_tuple()),
                                            age_seconds=cache.read_float_attribute('Age',en)[0],
                                            lifetime_seconds=cache.read_float_attribute('Lifetime',en)[0])
                if count and P['name'] in PLAY_POOLS:
                    colors=cache.read_color_attribute('Color',en)
                    positions=cache.read_position_attribute('Position',en)
                    origin=P['effects'][tier][1]
                    if colors and len(colors)==len(positions):
                        if P['name']=='NS_CommanderGunfireBatch':
                            sizes=cache.read_vector2_attribute('SpriteSize',en)
                            active=sum(1 for value,size,pos in zip(colors,sizes,positions) if size.x>0 and size.y>0 and max(value.r,value.g,value.b)>.001 and abs(pos.y-origin.y)<4000)
                        else:
                            active=sum(1 for value,pos in zip(colors,positions) if value.a>.001 and abs(pos.y-origin.y)<4000)
                        if active:P['active_slots'][tier][str(en)]=max(active,P['active_slots'][tier].get(str(en),0))
    # Compilation and GPU readback can pump Slate again while this callback is on the stack.
    in_step=False
    def step(dt):
        nonlocal in_step
        if in_step:return
        in_step=True
        try:
            if not P:
                if not record['pending']:
                    stamp();unreal.unregister_slate_post_tick_callback(handle)
                    destroy(sun);destroy(sky)
                    global REVIEW
                    REVIEW=None
                    def quit_next_tick(_):
                        unreal.unregister_slate_post_tick_callback(quit_handle)
                        if play_cases:start_play_review(folder,play_cases)
                        else:unreal.SystemLibrary.quit_editor()
                    quit_handle=unreal.register_slate_post_tick_callback(quit_next_tick)
                    return
                begin(record['pending'].pop(0));stamp()
            f=P['frame'];name=P['name'];count=40
            if f>=0:
                for j,(cap,rt) in enumerate(P['caps']):
                    if f:
                        unreal.RenderingLibrary.export_render_target(world,rt,str(folder/name),f'{j}-{f-1:03d}.png')
                        if not (folder/name/f'{j}-{f-1:03d}.png').is_file():raise RuntimeError('Render target export failed')
                if f==count:
                    if not all(P['stats']):raise RuntimeError('A tier has no simulated particles at the sample times: '+str(P['stats']))
                    record['finished'][name]={'paths':files[name],'frames':count,'duration':P['duration'],'distances_cm':list(DISTANCES),
                        'recording_world':'PIE' if game_world else 'Editor','framing_width_cm':P['width'],'emitter_peak_particles':P['stats'],'local_visible_particles':P['active_slots'],'instance_samples':P['states']}
                    if 'shell_samples' in P:record['finished'][name]['shell_samples']=P['shell_samples']
                    clear();stamp();return
            for j,((c,origin),(cap,rt)) in enumerate(zip(P['effects'],P['caps'])):
                if f==0:
                    c.set_preview_lod_distance(True,DISTANCES[j],100000);c.activate(True);c.set_component_tick_enabled(False);c.set_preview_lod_distance(True,DISTANCES[j],100000)
                if f>=0:
                    sub=1 if name=='NS_WarMachineHoverPool' else 3
                    simdt=P['duration']/((count-1)*sub)
                    for k in range(sub):
                        t=(max(f-1,0)*sub+k)*simdt
                        if f==0 and k:continue
                        feed(c,name,origin,t,j)
                        c.set_preview_lod_distance(True,DISTANCES[j],100000)
                        c.advance_simulation(1,simdt if f>0 else 1/120)
                    if f in (0,2,6,12,17,22) or (name=='NS_GroundBurningBurst' and f in (9,19)):sample(c,j)
                cap.capture_scene()
            P['frame']=f+1
        except Exception:
            key=P.get('name',selection[0]);record['failed'][key]=traceback.format_exc();clear();stamp()
        finally:
            in_step=False
    stamp();handle=unreal.register_slate_post_tick_callback(step)
    REVIEW=(step,handle,sun,sky)


def start_play_review(folder, selection):
    """Keep history across per-frame array uploads using runtime Niagara instances."""
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if level.is_in_play_in_editor():raise RuntimeError('Start the recording in an independent idle editor')
    files=cases()
    for path in dict.fromkeys(p for name in selection for p in files[name]):
        result=NS.compile_with_results(path)
        if not result.success or result.errors:raise RuntimeError(str(result))
    def on_play(_):
        w=editor.get_game_world()
        if not w or not unreal.GameplayStatics.get_player_controller(w,0):return
        unreal.unregister_slate_post_tick_callback(handle)
        start(folder,selection,w)
    handle=unreal.register_slate_post_tick_callback(on_play)
    level.editor_request_begin_play()

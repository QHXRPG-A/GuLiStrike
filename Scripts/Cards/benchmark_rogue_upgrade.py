"""Authorized original/lite VFX comparison in one unchanged PIE viewport.

Measures 100/500/1000 simultaneous upgrades on a transient fixture of real Mass soldiers.
Uses viewport FStatUnitData raw timings; Python setup/upload is recorded separately.
No saved map or asset changes, no screenshot inspection.
"""
import json
import math
import statistics
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/RogueCards/Upgrade/performance.json'
QA = unreal.GuLiRogueCardQALibrary
ARRAY = unreal.NiagaraDataInterfaceArrayFunctionLibrary
ORIGINAL = unreal.load_asset('/Game/Assets/VFX/NiagaraUpgradeGlow/Particles/P_UpgradeGlow04_Converted')
LITE = unreal.load_asset('/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite')
BENCH = {'phase':'ready', 'start':time.monotonic(), 'since':time.monotonic(), 'cases':[], 'case_index':0,
         'components':[], 'success':False, 'visual_review':'user_pending',
         'scope':'same PIE scene/camera and 1000 real Mass War Machines; varies upgraded subset; standalone rendering comparison, not network throughput',
         'timing_source':'viewport FStatUnitData raw game/render/GPU/frame milliseconds',
         'schedule':[(n,kind,rep) for n in (100,500,1000) for rep in range(3) for kind in ('baseline','original','lite')]}
BENCH_HANDLE = None

def phase(value):
    BENCH['phase']=value; BENCH['since']=time.monotonic()

def dispose():
    for c in BENCH['components']:
        if unreal.SystemLibrary.is_valid(c):
            c.destroy_component(c)
    BENCH['components']=[]

def finish(error=None):
    global BENCH_HANDLE
    if BENCH_HANDLE is not None:
        unreal.unregister_slate_post_tick_callback(BENCH_HANDLE); BENCH_HANDLE=None
    dispose()
    BENCH['success']=error is None
    if error: BENCH['error']=error
    BENCH.pop('components',None); BENCH.pop('positions',None)
    crowd=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundCrowdManager'))
    crowd.set_editor_property('MaxAgents',BENCH['crowd_capacity_original'])
    BENCH['crowd_capacity_restored']=crowd.get_editor_property('MaxAgents')
    for case in BENCH['cases']:
        samples=case['samples']
        case['summary']={key:{'median':statistics.median([s[key] for s in samples]),
                             'p95':sorted(s[key] for s in samples)[min(len(samples)-1,int(.95*len(samples)))]}
                         for key in ('game_ms','render_ms','gpu_ms','frame_ms') if samples}
    OUT.write_text(json.dumps(BENCH,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()

def begin_case(world,pc):
    n,kind,rep=BENCH['schedule'][BENCH['case_index']]
    case={'count':n,'kind':kind,'repeat':rep,'components':0 if kind=='baseline' else n if kind=='original' else 1,
          'nominal_visible_particles':0 if kind=='baseline' else n*(31 if kind=='original' else 20),
          'gpu_particle_capacity':20480 if kind=='lite' else 0,'samples':[], 'array_upload_ms':[]}
    BENCH['cases'].append(case)
    start=time.perf_counter()
    if kind=='original':
        for p in BENCH['positions'][:n]:
            c=unreal.NiagaraFunctionLibrary.spawn_system_at_location(world,ORIGINAL,p,auto_destroy=False,auto_activate=False,
                pooling_method=unreal.NCPoolMethod.NONE,pre_cull_check=False)
            assert c
            c.set_cast_shadow(False); c.set_visibility(True); BENCH['components'].append(c)
        for c in BENCH['components']: c.activate(True)
    elif kind=='lite':
        c=unreal.NiagaraFunctionLibrary.spawn_system_at_location(world,LITE,unreal.Vector(),auto_destroy=False,auto_activate=False,
            pooling_method=unreal.NCPoolMethod.NONE,pre_cull_check=False)
        assert c
        c.set_cast_shadow(False); BENCH['components']=[c]
        colors=[unreal.LinearColor(20,6.930114,.933554,1) if i<n else unreal.LinearColor(0,0,0,0) for i in range(1024)]
        ARRAY.set_niagara_array_position(c,'User.UpgradePositions',BENCH['positions'])
        ARRAY.set_niagara_array_color(c,'User.UpgradeColors',colors)
        params=[unreal.Vector(0,1,18) if i<n else unreal.Vector() for i in range(1024)]
        ARRAY.set_niagara_array_vector(c,'User.UpgradeParameters',params)
        center=unreal.Vector(*BENCH['grid_center'])
        bounds=unreal.Box(min=unreal.Vector(*(min(getattr(p,k) for p in BENCH['positions'])-500 for k in ('x','y','z'))),
                          max=unreal.Vector(*(max(getattr(p,k) for p in BENCH['positions'])+500 for k in ('x','y','z'))))
        bounds.set_editor_property('is_valid',True)
        c.set_system_fixed_bounds(bounds)
        c.set_visibility(True)
        c.activate(True)
    case['setup_ms']=(time.perf_counter()-start)*1000
    case['world_start']=unreal.GameplayStatics.get_time_seconds(world)
    phase('sample')

def tick_bench(dt):
    try:
        now=time.monotonic()
        if now-BENCH['start']>180: raise RuntimeError('benchmark timeout in '+BENCH['phase'])
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world: return
        pc=unreal.GameplayStatics.get_player_controller(world,0)
        if not pc: return
        elapsed=now-BENCH['since']
        if BENCH['phase']=='ready':
            if not json.loads(QA.snapshot(pc)).get('ready'): return
            if not json.loads(QA.frame_timings(pc))['unit_stat']:
                unreal.SystemLibrary.execute_console_command(world,'stat unit',pc)
            camera=pc.player_camera_manager
            origin=camera.get_camera_location(); forward=camera.get_camera_rotation().get_forward_vector()
            assert forward.z < -.1
            team=json.loads(QA.snapshot(pc))['team']
            roster=json.loads(unreal.GuLiTeleportQALibrary.snapshot(pc))
            army=[u for u in roster['units'] if u['team']==team and u['type']==2 and u['health']>0]
            center=unreal.Vector(*(sum(u[k] for u in army)/len(army) for k in ('x','y','z')))
            fixture=json.loads(QA.build_fixture(pc,1000,center,1500))
            assert fixture.get('created')==1000, f'Need 1000 actual units: {fixture.get("created")}'
            BENCH['fixture']=fixture
            positions=[unreal.Vector(u['x'],u['y'],u['z']) for u in fixture['units']]
            BENCH['positions']=positions+[positions[0]]*24
            center=unreal.Vector(*((min(getattr(p,k) for p in positions)+max(getattr(p,k) for p in positions))*.5 for k in ('x','y','z')))
            BENCH['grid_spacing_cm']=1500
            BENCH['grid_center']=[center.x,center.y,center.z]
            # A fixed overhead fixture camera includes all 1000 real units without changing the production camera policy.
            pawn=pc.get_controlled_pawn(); pawn.set_actor_tick_enabled(False)
            pawn.set_actor_location(center,False,True)
            arm=pawn.get_commander_spring_arm()
            arm.set_editor_property('target_arm_length',75000.)
            arm.set_relative_rotation(unreal.Rotator(pitch=-90,yaw=0,roll=0),False,True)
            pawn.get_commander_camera().set_field_of_view(70)
            phase('framing')
        elif BENCH['phase']=='framing' and elapsed>2.5:
            positions=BENCH['positions'][:1000]
            width,height=pc.get_viewport_size()
            screen=[unreal.GameplayStatics.project_world_to_screen(pc,p) for p in positions]
            assert all(p is not None and 0<=p.x<=width and 0<=p.y<=height for p in screen), 'Fixture outside viewport'
            BENCH['viewport']=list(pc.get_viewport_size())
            camera=pc.player_camera_manager; origin=camera.get_camera_location(); forward=camera.get_camera_rotation().get_forward_vector()
            BENCH['camera']={'location':[origin.x,origin.y,origin.z], 'forward':[forward.x,forward.y,forward.z]}
            BENCH['projected_inside_viewport']={str(n):sum(p is not None and 0<=p.x<=width and 0<=p.y<=height for p in screen[:n]) for n in (100,500,1000)}
            BENCH['settings']={k:unreal.SystemLibrary.get_console_variable_string_value(k) for k in
                ('r.VSync','t.MaxFPS','sg.EffectsQuality','r.ScreenPercentage','r.DynamicRes.OperationMode')}
            phase('warmup')
        elif BENCH['phase']=='warmup' and elapsed>3:
            begin_case(world,pc)
        elif BENCH['phase']=='sample':
            case=BENCH['cases'][-1]; age=unreal.GameplayStatics.get_time_seconds(world)-case['world_start']
            if case['kind']=='lite':
                upload=time.perf_counter()
                ARRAY.set_niagara_array_vector(BENCH['components'][0],'User.UpgradeParameters',
                    [unreal.Vector(age,1,18) if i<case['count'] else unreal.Vector() for i in range(1024)])
                case['array_upload_ms'].append((time.perf_counter()-upload)*1000)
            if .12 <= age <= .65:
                sample=json.loads(QA.frame_timings(pc)); sample['age']=age; case['samples'].append(sample)
            if age>.8:
                dispose(); BENCH['case_index']+=1
                if BENCH['case_index']==len(BENCH['schedule']):
                    assert all(len(c['samples'])>=3 for c in BENCH['cases']), 'too few timing samples'
                    assert any(s['gpu_ms']>0 for c in BENCH['cases'] for s in c['samples']), 'GPU timings unavailable'
                    assert len(set(s['game_ms'] for c in BENCH['cases'] for s in c['samples']))>10, 'Timing source was not updating'
                    finish()
                else: phase('cooldown')
        elif BENCH['phase']=='cooldown' and elapsed>1.25:
            begin_case(world,pc)
    except Exception:
        finish(traceback.format_exc())

assert ORIGINAL and LITE
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
crowd=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundCrowdManager'))
BENCH['crowd_capacity_original']=crowd.get_editor_property('MaxAgents')
crowd.set_editor_property('MaxAgents',4096)
BENCH['crowd_capacity_fixture']=4096
assert unreal.GuLiComponentSkillQALibrary.start_pie(0,1)
BENCH_HANDLE=unreal.register_slate_post_tick_callback(tick_bench)
print(json.dumps({'started':True,'report':str(OUT)}))

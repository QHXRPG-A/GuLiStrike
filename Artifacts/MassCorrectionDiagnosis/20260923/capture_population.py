import json
import math
import time
from pathlib import Path
import unreal

_pop_root=Path('D:/UE5.7/test1/Artifacts/MassCorrectionDiagnosis/20260923')
_pop_world=unreal.find_object(None,'/Game/Maps/UEDPIE_2_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
_pop_pc=unreal.GameplayStatics.get_player_controller(_pop_world,0)
_pop_actor=unreal.GameplayStatics.get_actor_of_class(_pop_world,unreal.GuLiCommanderPresentationActor)
_pop_snapshot=Path('D:/UE5.7/test1/outputs/mass-correction-diagnosis-20260923/population-start.json')
unreal.SystemLibrary.execute_console_command(_pop_world,'gs.Commander.QA.InputSnapshot "'+_pop_snapshot.as_posix()+'"',_pop_pc)
_pop_roster=json.loads(_pop_snapshot.read_text(encoding='utf-8-sig'))['soldiers']
_pop_bypos={tuple(round(s['world_position'][v],1) for v in 'xyz'):s['id'] for s in _pop_roster if s['alive']}
_pop_components=[c for c in _pop_actor.get_components_by_class(unreal.InstancedStaticMeshComponent) if '_Team_' in c.get_name()]
_pop_slots=[]
_pop_prev={}
for c in _pop_components:
    for i in range(c.get_instance_count()):
        tr=c.get_instance_transform(i,world_space=True)
        if tr.scale3d.x <= 0: continue
        pos=(tr.translation.x,tr.translation.y,tr.translation.z)
        sid=_pop_bypos.get(tuple(round(v,1) for v in pos))
        if sid:
            _pop_slots.append((c,i,sid))
            _pop_prev[sid]=pos
_pop_stats={sid:{'samples':0,'moving':0,'above_1p5x':0,'at_3x':0,'max_speed':0.0} for _,_,sid in _pop_slots}
_pop_frame_rows=[]
_pop_start=time.perf_counter()
_pop_prev_time=unreal.GameplayStatics.get_time_seconds(_pop_world)
_pop_meta={'wall_start':time.time(),'tracked':len(_pop_slots),'duration':16,'mode':'read_only_ism_sampling','input_injected':False}

def _pop_tick(delta):
    global _pop_prev_time
    begin=time.perf_counter()
    try:
        now=unreal.GameplayStatics.get_time_seconds(_pop_world)
        dt=now-_pop_prev_time
        if dt<=0: return
        count_fast=0
        count_cap=0
        top=[]
        for c,i,sid in _pop_slots:
            tr=c.get_instance_transform(i,world_space=True)
            if tr.scale3d.x<=0: continue
            pos=(tr.translation.x,tr.translation.y,tr.translation.z)
            speed=math.dist(pos,_pop_prev[sid])/dt
            _pop_prev[sid]=pos
            stat=_pop_stats[sid]
            stat['samples']+=1
            stat['moving']+=int(speed>50)
            stat['above_1p5x']+=int(speed>1080)
            stat['at_3x']+=int(speed>=2155)
            stat['max_speed']=max(stat['max_speed'],speed)
            if speed>1080:
                count_fast+=1
                count_cap+=int(speed>=2155)
                top.append([sid,round(speed,2)])
        _pop_prev_time=now
        _pop_frame_rows.append({'elapsed':begin-_pop_start,'world_time':now,'dt':dt,'fast':count_fast,'at_cap':count_cap,
            'fastest':sorted(top,key=lambda v:v[1],reverse=True)[:12],'capture_ms':(time.perf_counter()-begin)*1000})
        if begin-_pop_start >=16:
            unreal.unregister_slate_post_tick_callback(_pop_handle)
            candidates=sorted(_pop_stats,key=lambda sid:(_pop_stats[sid]['at_3x'],_pop_stats[sid]['above_1p5x']),reverse=True)
            chosen=candidates[0]
            _pop_meta['trace_soldier']=chosen
            _pop_meta['trace_worlds']=[{'client':1,'seconds':30},{'client':2,'seconds':33}]
            for index,duration in ((1,30),(2,33)):
                w=unreal.find_object(None,'/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index))
                pc=unreal.GameplayStatics.get_player_controller(w,0)
                unreal.SystemLibrary.execute_console_command(w,'gs.Commander.PredictionTrace.Start baseline {} {}'.format(chosen,duration),pc)
            _pop_meta['wall_end']=time.time()
            (_pop_root/'population-capture.json').write_text(json.dumps({'meta':_pop_meta,'units':_pop_stats,'frames':_pop_frame_rows},indent=2),encoding='utf-8')
            unreal.log('GULI_CORRECTION_POPULATION_END tracked={} trace_soldier={}'.format(len(_pop_slots),chosen))
    except Exception as exc:
        unreal.unregister_slate_post_tick_callback(_pop_handle)
        (_pop_root/'population-error.txt').write_text(str(exc),encoding='utf-8')

_pop_handle=unreal.register_slate_post_tick_callback(_pop_tick)
unreal.log('GULI_CORRECTION_POPULATION_BEGIN')
unreal.MCPythonHelper.submit_result(json.dumps(_pop_meta))

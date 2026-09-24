import json
import time
from pathlib import Path
import unreal

_layer_root=Path('D:/UE5.7/test1/Artifacts/MassCorrectionDiagnosis/20260923')
_layer_worlds=[unreal.find_object(None,'/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(i)) for i in range(3)]
assert all(_layer_worlds)
_layer_snapshot=Path('D:/UE5.7/test1/outputs/mass-correction-diagnosis-20260923/layer-start.json')
unreal.SystemLibrary.execute_console_command(_layer_worlds[2],'gs.Commander.QA.InputSnapshot "'+_layer_snapshot.as_posix()+'"',unreal.GameplayStatics.get_player_controller(_layer_worlds[2],0))
_layer_roster=json.loads(_layer_snapshot.read_text(encoding='utf-8-sig'))['soldiers']
_layer_candidates=[s for s in _layer_roster if s['alive'] and s['id']%3==2]
assert _layer_candidates
_layer_id=_layer_candidates[0]['id']
_layer_meta={'soldier':_layer_id,'start_wall':time.time(),'world_start':[unreal.GameplayStatics.get_time_seconds(w) for w in _layer_worlds],
    'duration':35,'prior_log_net':'Log','input_injected':False}
unreal.log('GULI_POSE_LAYER_BEGIN soldier={}'.format(_layer_id))
unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'gs.GM.Commander.Nav.Soldier '+str(_layer_id))
unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'Log LogNet VeryVerbose')
unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'CsvProfile STARTFILE=../../../Artifacts/MassCorrectionDiagnosis/20260923/layer-engine.csv')
for i,duration in ((1,30),(2,33)):
    unreal.SystemLibrary.execute_console_command(_layer_worlds[i],'gs.Commander.PredictionTrace.Start baseline {} {}'.format(_layer_id,duration),unreal.GameplayStatics.get_player_controller(_layer_worlds[i],0))
_layer_start=time.perf_counter()
_layer_frames=[]

def _layer_tick(_delta):
    try:
        elapsed=time.perf_counter()-_layer_start
        _layer_frames.append([elapsed,unreal.GameplayStatics.get_time_seconds(_layer_worlds[0]),unreal.GameplayStatics.get_world_delta_seconds(_layer_worlds[0])])
        if elapsed>=35:
            unreal.unregister_slate_post_tick_callback(_layer_handle)
            unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'Log LogNet Log')
            unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'CsvProfile STOP')
            _layer_meta['end_wall']=time.time()
            _layer_meta['restored_log_net']='Log'
            (_layer_root/'layer-capture.json').write_text(json.dumps({'meta':_layer_meta,'frames':_layer_frames},indent=2),encoding='utf-8')
            unreal.log('GULI_POSE_LAYER_END restored_LogNet_Log')
    except Exception as exc:
        unreal.unregister_slate_post_tick_callback(_layer_handle)
        unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'Log LogNet Log')
        unreal.SystemLibrary.execute_console_command(_layer_worlds[0],'CsvProfile STOP')
        (_layer_root/'layer-error.txt').write_text(str(exc),encoding='utf-8')

_layer_handle=unreal.register_slate_post_tick_callback(_layer_tick)
(_layer_root/'layer-start.json').write_text(json.dumps(_layer_meta,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(_layer_meta))

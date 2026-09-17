"""Run the project's existing wingman PIE acceptance entry and record art readback.
No automation registration, test cases, map save, or gameplay configuration writes.
"""
import unreal,json,time,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StylePass_20260917'
assert '-CelRuntimeReviewWorker' in unreal.SystemLibrary.get_command_line()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state={'phase':0,'start':time.monotonic(),'last':0,'samples':[],'assets_seen':{},'success':False}
def command(c):unreal.SystemLibrary.execute_console_command(world,c)
def write():
    (OUT/'runtime_review.json').write_text(json.dumps({k:v for k,v in state.items() if k!='handle'},ensure_ascii=False,indent=2),encoding='utf8')
def finish(error=None):
    if error:state['error']=error
    write();unreal.unregister_slate_post_tick_callback(state['handle'])
    if unreal.WidgetService.is_pie_running():unreal.WidgetService.stop_pie()
    unreal.SystemLibrary.quit_editor()
def sample():
    rows=[]
    worlds=unreal.EditorLevelLibrary.get_pie_worlds(True)
    for w in worlds:
        dedicated=unreal.SystemLibrary.is_dedicated_server(w)
        row={'world':w.get_path_name(),'dedicated':dedicated,'time':unreal.GameplayStatics.get_time_seconds(w)}
        f=next((x for x in unreal.ObjectIterator(unreal.GuLiUnitFeedbackSubsystem) if x.get_outer()==w),None)
        row['feedback_exists']=f is not None
        if f:row.update(ground_active=f.get_active_explosion_count(),loaded=f.get_loaded_explosion_count())
        c=next((x for x in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if x.get_outer()==w),None)
        row['presentation_exists']=c is not None
        if c:
            count=c.get_counters();row.update(bursts=count.bursts_played,components=count.component_count,states=count.received_states)
        row['fx']=[]
        for n in unreal.ObjectIterator(unreal.NiagaraComponent):
            if n.get_world()!=w or not n.get_asset():continue
            p=n.get_asset().get_path_name()
            if '/CombatExplosions/' not in p:continue
            item={'asset':p,'active':n.is_active(),'scale':list(n.get_world_scale().to_tuple()),'area':list(n.get_variable_float('User.Area_Scale'))}
            row['fx'].append(item)
            state['assets_seen'][p]=state['assets_seen'].get(p,0)+int(item['active'])
        rows.append(row)
    state['samples'].append(rows);write()
def tick(dt):
    try:
        now=time.monotonic()-state['start']
        if now>240:finish('PIE review timeout');return
        if state['phase']==0 and now>3:
            command('gs.WingmanAttack.QA.Start');state['phase']=1;write();return
        ready=[w for w in unreal.EditorLevelLibrary.get_pie_worlds(True) if '/UEDPIE_' in w.get_path_name() and not unreal.SystemLibrary.is_dedicated_server(w) and unreal.GameplayStatics.get_player_controller(w,0)] if state['phase']==1 else []
        if state['phase']==1 and len(ready)==2 and min(unreal.GameplayStatics.get_time_seconds(w) for w in ready)>6:
            command('gs.WingmanAttack.QA.Target ground');state.update(phase=2,ground_started=now);write()
        if state['phase']>=2 and now-state['last']>1:
            state['last']=now;sample()
            elapsed=now-state['ground_started']
            if elapsed>3 and not state.get('manual_ground'):
                for f in unreal.ObjectIterator(unreal.GuLiUnitFeedbackSubsystem):
                    w=f.get_outer();pc=unreal.GameplayStatics.get_player_controller(w,0)
                    if pc:
                        p=pc.get_view_target().get_actor_location()
                        for i in range(3):f.play_destruction(p+unreal.Vector(2000+i*1000,0,-1000),f.get_reference_unit_size())
                state['manual_ground']=True
            if elapsed>8 and not state.get('camera_set'):
                command('gs.WingmanAttack.QA.View 1 oblique');command('gs.WingmanAttack.QA.View 2 oblique');state['camera_set']=True
            if elapsed>23 and not state.get('captured'):
                command('gs.WingmanAttack.QA.Capture cel_reference_bomb 2');state['captured']=True
            if elapsed>65:
                command('gs.WingmanAttack.QA.Sample')
                p=ROOT/'TestResults/WingmanAttack/pie-sample.json'
                if p.exists():state['native_attack_sample']=json.loads(p.read_text(encoding='utf-8-sig'))
                state['success']=True;finish()
    except Exception:finish(traceback.format_exc())
state['handle']=unreal.register_slate_post_tick_callback(tick)
write()

"""Explicit dedicated-server / late-client smoke check in the authored Mass map. No asset saves."""
import unreal,json,time,traceback
from pathlib import Path

def start_hover_network_check():
    out=Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260929/network-late-join.json')
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    state={'start':time.monotonic(),'held':False,'joined':False,'samples':[],'done':False,'sample_at':0}
    def worlds():
        result={}
        for rep in unreal.ObjectIterator(unreal.GuLiSoldierStateReplicator):
            w=rep.get_world()
            if w and 'UEDPIE_' in w.get_path_name():result[w]=rep
        return result
    def finish(error=None):
        unreal.unregister_slate_post_tick_callback(state['handle']);state['done']=True
        out.write_text(json.dumps({'success':not error,'error':error,'scope':'Dedicated server plus initial client and later second client; normal stop orders; no loss/latency injection; no death/teleport in this check','samples':state['samples']},indent=2),encoding='utf8')
    def tick(dt):
        try:
            now=time.monotonic();found=worlds()
            if now-state['start']>120:raise RuntimeError('Late-join acceptance timed out')
            for w,rep in found.items():
                if unreal.SystemLibrary.is_dedicated_server(w) and not state['held'] and len(rep.get_all_soldier_states())==500:
                    unreal.SystemLibrary.execute_console_command(w,'gs.Commander.HoverQA.Hold');state['held']=True;state['held_at']=now
            if state['held'] and not state['joined'] and now-state['held_at']>8:
                unreal.SystemLibrary.execute_console_command(editor,'gs.CombatEffects.QA.LateJoin');state['joined']=True;state['joined_at']=now
            if not state['joined'] or now-state['joined_at']<10 or len(found)!=3 or now<state['sample_at']:return
            rows=[]
            for w,rep in found.items():
                row={'world':w.get_path_name(),'dedicated':unreal.SystemLibrary.is_dedicated_server(w),'roster':len(rep.get_all_soldier_states()),'bodies':0,'poses':[]}
                if row['roster']!=500:return
                for actor in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderPresentationActor):
                    for ism in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
                        if not ism.static_mesh or '_Rigid' not in ism.static_mesh.get_name():continue
                        row['bodies']+=ism.get_instance_count()
                        if ism.get_name()=='UnitInstances_Type_2_Team_2':
                            floats=list(ism.get_editor_property('per_instance_sm_custom_data'))
                            for i in range(ism.get_instance_count()):
                                root=ism.get_instance_transform(i,True)
                                row['poses'].append({'root':list(root.translation.to_tuple()),'hover_raw':floats[i*29+12:i*29+15]})
                            if ism.get_instance_count():
                                center=ism.get_instance_transform(0,True).translation
                                pc=unreal.GameplayStatics.get_player_controller(w,0)
                                if pc:
                                    pawn=pc.get_controlled_pawn();pawn.set_actor_tick_enabled(False)
                                    arm=pawn.get_component_by_class(unreal.SpringArmComponent);arm.set_component_tick_enabled(False)
                                    camera=pawn.get_component_by_class(unreal.CameraComponent);camera.set_absolute(True,True,True)
                                    loc=center+unreal.Vector(0,-9000,6000)
                                    camera.set_world_location_and_rotation(loc,unreal.MathLibrary.find_look_at_rotation(loc,center),False,True)
                row['hover_components']=sum(c.get_world()==w and bool(c.get_asset()) and c.get_asset().get_name()=='NS_WarMachineHoverPool' for c in unreal.ObjectIterator(unreal.NiagaraComponent))
                rows.append(row)
            state['samples'].append({'seconds_after_join':now-state['joined_at'],'worlds':rows});state['sample_at']=now+.5
            if len(state['samples'])>=8:finish()
        except:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    unreal.SystemLibrary.execute_console_command(editor,'gs.Commander.HoverPreview 0')
    unreal.SystemLibrary.execute_console_command(editor,'gs.CombatEffects.QA.Start dedicated-late')
    return state

HOVER_NETWORK=start_hover_network_check()
unreal.MCPythonHelper.submit_result(json.dumps({'started':True}))

"""Evidence runner for the existing standalone HUD QA commands; no new native tests."""
import sys, json, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'Scripts'))
from commander_editor_python import call_editor
OUT = Path(__file__).resolve().parent
PREFIX = "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\npc=unreal.GameplayStatics.get_player_controller(w,0)\n"

def run(code):
    r = call_editor(PREFIX+code, timeout=20)
    if not r.get('success') or (isinstance(r.get('result'),dict) and r['result'].get('success') is False):
        raise RuntimeError(r)
    return r['result']

def command(text):
    return run(f'unreal.SystemLibrary.execute_console_command(w,{text!r},pc)\nunreal.MCPythonHelper.submit_result(json.dumps({{"submitted":True}}))')

def probe(label):
    result=run('''rows=[]
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderHealthBarRenderer):
 rows.append({'tick':a.is_actor_tick_enabled(),'slots':sum(c.get_instance_count() for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent))})
maprows=[]
for m in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.GuLiCommanderMiniMapWidget,False):
 g=m.get_cached_geometry();size=unreal.SlateLibrary.get_local_size(g)
 pix,vp=unreal.SlateLibrary.local_to_viewport(w,g,size*.5)
 maprows.append({'path':m.get_path_name(),'size':[size.x,size.y],'center_pixels':[pix.x,pix.y],'scale':unreal.WidgetLayoutLibrary.get_viewport_scale(w),'visible':m.is_visible()})
p=unreal.GameplayStatics.get_player_pawn(w,0);pos=p.get_actor_location();rot=pc.player_camera_manager.get_camera_rotation()
unreal.MCPythonHelper.submit_result(json.dumps({'world':w.get_path_name(),'team':str(pc.player_state.get_team()),'bars':rows,'maps':maprows,'camera_pivot':[pos.x,pos.y,pos.z],'camera_yaw':rot.yaw}))''')
    (OUT/f'ui-{label}.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(label,json.dumps(result),flush=True)
    return result

def hud(action,label):
    path=f'D:/UE5.7/test1/outputs/commander-ring-hud-20260830/cpu-20260916/{label}.json'
    command(f'gs.Commander.QA.HUD {action} {path}')
    return Path(path)

if __name__=='__main__':
    probe(sys.argv[1])

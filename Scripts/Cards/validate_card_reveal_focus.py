"""Exercise the authorized window-focus change in this demo's own PIE session."""
import json,time,traceback,unreal
from pathlib import Path
ROOT='/Game/GuLiStrike/Cards/RevealDemo'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')
assert not unreal.WidgetService.is_pie_running()
assert 'LVL_CardRevealDemo' in unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
old_mouse_control=settings.get_editor_property('GameGetsMouseControl')
settings.set_editor_property('GameGetsMouseControl',True)
settings.set_editor_property('NewWindowWidth',960)
settings.set_editor_property('NewWindowHeight',536)
DATA={'checks':[],'snapshots':{},'errors':[],'method':'Actual PIE window focus, then focus the owned flash material editor; no OS keyboard simulation.'}
S={'stage':0,'deadline':time.monotonic()+30,'wait':0}
MATERIAL=unreal.load_asset(ROOT+'/Materials/M_CardConfirmFlash')

def check(name,condition):
    DATA['checks'].append({'name':name,'passed':bool(condition)})
    assert condition,name

def snap(label):
    c=D.get_editor_property('Card1')
    h=next(x for x in c.get_components_by_class(unreal.SceneComponent) if x.get_name()=='HoverPivot')
    rotation=h.get_editor_property('relative_rotation')
    data={'focused':unreal.GuLiCardRevealViewportLibrary.is_card_reveal_viewport_focused(P),
          'mouse_valid':D.get_editor_property('MouseValid'),'hovered':D.get_editor_property('HoveredIndex'),
          'phase':D.get_editor_property('Phase'),'yaw':rotation.yaw,'roll':rotation.roll}
    DATA['snapshots'][label]=data
    return data

def finish():
    unreal.unregister_slate_post_tick_callback(HANDLE)
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(MATERIAL)
    settings.set_editor_property('GameGetsMouseControl',old_mouse_control)
    unreal.WidgetService.stop_pie()
    DATA['success']=not DATA['errors'] and all(c['passed'] for c in DATA['checks'])
    (OUT/'focus-validation.json').write_text(json.dumps(DATA,ensure_ascii=False,indent=2),encoding='utf-8')

def tick(delta):
    global W,P,D
    try:
        assert time.monotonic()<S['deadline'],'Focus check timeout'
        S['wait']-=delta
        if S['wait']>0:return
        if S['stage']==0:
            W=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not W:return
            P=unreal.GameplayStatics.get_player_controller(W,0)
            if not P:return
            D=P.get_editor_property('Director')
            if not D or D.get_editor_property('Phase')!=1:return
            check('null controller is safely unfocused',not unreal.GuLiCardRevealViewportLibrary.is_card_reveal_viewport_focused(None))
            check('PIE game window is focused',unreal.GuLiCardRevealViewportLibrary.is_card_reveal_viewport_focused(P))
            width,height=P.get_viewport_size()
            u=.5+.8*D.get_editor_property('CardScale')*16.0875/D.get_editor_property('ViewWidth')
            v=.5+.8*D.get_editor_property('CardScale')*24.3085/D.get_editor_property('ViewHeight')
            P.set_mouse_location(round(width*u),round(height*v))
            S.update(stage=1,wait=.6)
        elif S['stage']==1:
            a=snap('active_hover')
            check('active window accepts hover',a['focused'] and a['mouse_valid'] and a['hovered']==1 and a['yaw']<-8 and a['roll']>8)
            check('open owned material editor',unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([MATERIAL]))
            S.update(stage=2,wait=.8)
        elif S['stage']==2:
            a=snap('game_window_deactivated')
            check('other UE window disables pointer',not a['focused'] and not a['mouse_valid'] and a['hovered']==-1)
            check('focus loss smoothly recenters',abs(a['yaw'])<.08 and abs(a['roll'])<.08)
            sub=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if not s.get_name().startswith('Default__'))
            sub.inject_input_vector_for_action(unreal.load_asset(ROOT+'/Input/IA_CardRevealClick'),unreal.Vector(1,0,0),[],[])
            S.update(stage=3,wait=.15)
        else:
            check('background press cannot select',D.get_editor_property('Phase')==1 and D.get_editor_property('SelectedIndex')==-1)
            finish()
    except Exception:
        DATA['errors'].append(traceback.format_exc())
        finish()

HANDLE=unreal.register_slate_post_tick_callback(tick)
assert unreal.WidgetService.start_pie()
print(json.dumps({'focus_validation_started':True}))

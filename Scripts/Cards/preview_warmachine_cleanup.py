"""Capture the cleaned artwork at center and opposite hover limits in live PIE.

Does not synthesize OS input or rerun unrelated flow tests. Run only in the
dedicated review-map PIE; stop that owned preview after inspecting its output.
"""
from pathlib import Path
import json
import time
import traceback
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v4_Clean/Previews')
OUT.mkdir(exist_ok=True)
assert unreal.WidgetService.is_pie_running()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE={'start':time.monotonic(),'wait':2.,'index':0,'ready':False}
REPORT={'success':False,'poses':[],'captures':[]}


def pose(x,y):
    rows=[]
    for i in range(3):
        card=D.get_editor_property('Card'+str(i))
        location=card.get_actor_location()
        card.call_method('ApplyFrame',(location.x,D.get_editor_property('CardScale'),1.,
            D.get_editor_property('FarDistance'),0.,x,y,D.get_editor_property('MaximumTilt'),
            D.get_editor_property('HoverInterpSpeed'),1.,0.,D.get_editor_property('FlashIntensity'),True))
        pivot=next(c for c in card.get_components_by_class(unreal.SceneComponent) if c.get_name()=='HoverPivot')
        rows.append({'index':i,'location':list(card.get_actor_location().to_tuple()),'rotation':str(pivot.get_editor_property('relative_rotation'))})
    REPORT['poses'].append({'input':[x,y],'cards':rows})


def shot(name):
    path=OUT/(name+'.png')
    unreal.SystemLibrary.execute_console_command(P.get_world(),'Shot showui filename="'+str(path)+'" -nosuffix',P)
    REPORT['captures'].append(str(path))


def export_cards():
    source=Path('D:/UE5.7/test1/Scripts/Cards/export_warmachine_card_previews.py').read_text(encoding='utf-8')
    source=source.replace('Cel_Closeups_v3/Previews','Cel_Closeups_v4_Clean/Previews')
    exec(compile(source,'export_warmachine_card_previews.py','exec'),{})


def finish():
    unreal.unregister_slate_post_tick_callback(HANDLE)
    (OUT/'cleanup-preview.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')


JOBS=[lambda:pose(0,0),lambda:shot('three-cards-clean'),export_cards,
      lambda:pose(-1,-1),lambda:shot('hover-top-left'),
      lambda:pose(1,1),lambda:shot('hover-bottom-right'),lambda:pose(0,0)]


def tick(delta):
    global P,D
    try:
        if time.monotonic()-STATE['start']>90:raise RuntimeError('Cleanup preview timed out')
        STATE['wait']-=delta
        if STATE['wait']>0:return
        if not STATE['ready']:
            P=next(p for p in unreal.ObjectIterator(unreal.PlayerController)
                if not p.get_name().startswith('Default__') and p.get_viewport_size()[0]>0)
            assert 'LVL_WarMachineTarotReview' in P.get_world().get_path_name()
            D=P.get_editor_property('Director')
            D.set_actor_tick_enabled(False)
            D.call_method('SetPhase',(6,))
            D.call_method('StartPresentation')
            D.call_method('TickPresentation',(1.5,))
            assert D.get_editor_property('Phase')==1
            REPORT['viewport']=list(P.get_viewport_size())
            REPORT['camera']={'position':list(D.get_component_by_class(unreal.CameraComponent).get_world_location().to_tuple()),
                              'fov':D.get_component_by_class(unreal.CameraComponent).field_of_view}
            REPORT['text']=[]
            for i in range(3):
                card=D.get_editor_property('Card'+str(i))
                wc=next(c for c in card.get_components_by_class(unreal.WidgetComponent) if c.get_name()=='EditableText')
                widget=wc.get_user_widget_object()
                REPORT['text'].append([str(widget.get_editor_property('CardTitle').get_text()),str(widget.get_editor_property('CardDescription').get_text())])
            STATE.update(ready=True,wait=1.)
            return
        if STATE['index']>=len(JOBS):
            REPORT['success']=all(Path(p).is_file() for p in REPORT['captures'])
            finish()
            return
        JOBS[STATE['index']]()
        STATE['index']+=1
        STATE['wait']=1.
    except Exception:
        REPORT['error']=traceback.format_exc()
        finish()


HANDLE=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'preview_started':True,'output':str(OUT)}))

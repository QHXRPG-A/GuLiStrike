"""Reuse the approved card-flow preview and add scoped artwork/FText evidence.

Run only after starting this task's review-map PIE. It does not launch/close UE,
save assets, modify the text table, or synthesize OS input.
"""
from pathlib import Path
import json
import unreal

CARD_REVEAL_LIVE=True
assert unreal.WidgetService.is_pie_running()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert 'LVL_WarMachineTarotReview' in world.get_path_name()
preview_out=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v3/Previews')


def verify_text():
    expected=[('增加射速','提升重防号的射击频率。'),('增加导弹伤害','提升重防号的导弹伤害。'),('极速机动','提升重防号的移动速度。')]
    REPORT['editable_text']=[]
    for i,values in enumerate(expected):
        c=prop('Card'+str(i));wc=component(c,'EditableText');widget=wc.get_user_widget_object()
        title=str(widget.get_editor_property('CardTitle').get_text())
        description=str(widget.get_editor_property('CardDescription').get_text())
        check('FText table row '+str(i),(title,description)==values,[title,description])
        check('text component is one sided '+str(i),not wc.get_two_sided())
        check('full card text UV mesh '+str(i),component(c,'CardText').static_mesh.get_name()=='S_Card_Base')
        check('legacy decoration hidden '+str(i),all(not component(c,n).is_visible() for n in ['CardFrame','CardPictograms','CardVignette']))
        REPORT['editable_text'].append({'title':title,'description':description,'widget':widget.get_path_name()})


def pose(x,y,capture=False):
    for i in range(3):
        c=prop('Card'+str(i));before=c.get_actor_location()
        c.call_method('ApplyFrame',(before.x,prop('CardScale'),1.,prop('FarDistance'),0.,x,y,
            prop('MaximumTilt'),prop('HoverInterpSpeed'),1.,0.,prop('FlashIntensity'),True))
        rot=component(c,'HoverPivot').get_editor_property('relative_rotation')
        check('fixed center '+str((i,x,y)),(before-c.get_actor_location()).length()<.001)
        check('tilt direction '+str((i,x,y)),abs(rot.yaw+x*12)<.01 and abs(rot.roll-y*12)<.01,str(rot))
    if capture:shot('tilt_'+str(x)+'_'+str(y))


def long_text():
    w=component(prop('Card1'),'EditableText').get_user_widget_object()
    w.call_method('SetContent',(unreal.Text('INCREASE MISSILE DAMAGE'),unreal.Text('Increase the missile damage of War Machine. Longer localized descriptions wrap and shrink to fit.')))
    STATE['wait']=1.


def restore_text():
    prop('Card1').call_method('SetEditableText',(prop('CardTextDataTable'),unreal.Name('WarMachine_MissileDamage')))
    STATE['wait']=1.


source=Path('D:/UE5.7/test1/Scripts/Cards/preview_card_reveal_worker.py').read_text(encoding='utf-8')
source=source.replace("OUT=Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')","OUT=preview_out")
source=source.replace('if LIVE:\n    JOBS.append(replay_button)','if False:\n    JOBS.append(replay_button)')
marker='HANDLE=unreal.register_slate_post_tick_callback(tick)'
assert marker in source
addon='''
REPORT['input_boundary']='Direct calls to the existing Blueprint flow and ApplyFrame; OS mouse/focus and culture catalogs are not automated. English is only a temporary layout probe.'
JOBS.extend([lambda:(call('StartPresentation'),step(1.5)),verify_text])
for x,y in [(0,0),(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(1,-1),(-1,1),(1,1)]:
    JOBS.append(lambda x=x,y=y:pose(x,y,x!=0 and y!=0))
JOBS.extend([lambda:pose(0,0),lambda:shot('final-three-cards'),long_text,lambda:shot('English-layout-validated'),restore_text,verify_text])
'''
source=source.replace(marker,addon+'\n'+marker)
exec(compile(source,'preview_card_reveal_worker.py','exec'),globals())

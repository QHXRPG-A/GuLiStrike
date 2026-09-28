"""Reuse the requested flow checks for the enlarged review cards; no OS input."""
import json
import math
from pathlib import Path
import unreal

CARD_REVEAL_LIVE=True
assert unreal.WidgetService.is_pie_running()
out=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v5_Redraw/Production/Previews')


def dimensions():
    baseline=prop('CardScale')/math.sqrt(2.)
    check('review area config',prop('CardAreaMultiplier')==2.)
    check('review thickness config',prop('CardThicknessMultiplier')==2.)
    check('whole card tilt unchanged',prop('MaximumTilt')==12.)
    REPORT['dimensions']=[]
    for i in range(3):
        c=prop('Card'+str(i))
        face=component(c,'CardFace');back=component(c,'CardBack')
        a=unreal.MathLibrary.transform_location(face.get_world_transform(),face.static_mesh.get_bounds().origin)
        b=unreal.MathLibrary.transform_location(back.get_world_transform(),back.static_mesh.get_bounds().origin)
        ratio=(a-b).length()/(.9271096766*baseline)
        scale=c.get_actor_scale3d()
        check('actual area doubled '+str(i),abs(scale.x*scale.z/(baseline*baseline)-2)<.0001)
        check('actual front back thickness doubled '+str(i),abs(ratio-2)<.001,ratio)
        check('solid edge visible '+str(i),component(c,'CardSolidEdge').is_visible())
        wc=component(c,'EditableText')
        text=wc.get_user_widget_object()
        check('editable text retained '+str(i),bool(str(text.get_editor_property('CardTitle').get_text())))
        REPORT['dimensions'].append({'card':i,'scale':list(scale.to_tuple()),'face_separation_world':(a-b).length(),'thickness_ratio':ratio})


def pose(x,y):
    for i in range(3):
        c=prop('Card'+str(i));before=c.get_actor_location()
        c.call_method('ApplyFrame',(before.x,prop('CardScale'),1.,prop('FarDistance'),0.,x,y,12.,prop('HoverInterpSpeed'),1.,0.,prop('FlashIntensity'),True))
        rot=component(c,'HoverPivot').get_editor_property('relative_rotation')
        check('fixed center '+str((i,x,y)),(before-c.get_actor_location()).length()<.001)
        check('unchanged tilt direction '+str((i,x,y)),abs(rot.yaw+x*12)<.01 and abs(rot.roll-y*12)<.01,str(rot))


source=Path('D:/UE5.7/test1/Scripts/Cards/preview_card_reveal_worker.py').read_text(encoding='utf-8')
source=source.replace("OUT=Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')","OUT=out")
source=source.replace('if LIVE:\n    JOBS.append(replay_button)','if False:\n    JOBS.append(replay_button)')
marker='HANDLE=unreal.register_slate_post_tick_callback(tick)'
addon='''
REPORT['input_boundary']='Existing Blueprint flow, dimensions and ApplyFrame checks; real OS mouse, window focus and player art acceptance are not automated.'
JOBS.insert(4,dimensions)
JOBS.append(replay_button)
for x,y in [(0,0),(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(1,-1),(-1,1),(1,1)]:
    JOBS.append(lambda x=x,y=y:pose(x,y))
JOBS.extend([lambda:pose(0,0),lambda:shot('flow-final-three-cards')])
'''
source=source.replace(marker,addon+'\n'+marker)
exec(compile(source,'preview_card_reveal_worker.py','exec'),globals())

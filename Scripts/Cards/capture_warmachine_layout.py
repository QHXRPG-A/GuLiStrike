"""Read the active review viewport's enlarged layout and capture its real render."""
import json
from pathlib import Path
import unreal

p=next(p for p in unreal.ObjectIterator(unreal.PlayerController) if not p.get_name().startswith('Default__') and p.get_viewport_size()[0]>0)
assert 'LVL_WarMachineTarotReview' in p.get_world().get_path_name()
d=p.get_editor_property('Director')
d.set_actor_tick_enabled(False)
d.call_method('SetPhase',(6,))
d.call_method('StartPresentation')
d.call_method('TickPresentation',(1.5,))
w,h=p.get_viewport_size()
aspect='16x10' if abs(w/h-1.6)<.01 else ('21x9' if w/h>2.2 else '16x9')
out=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v5_Redraw/Production/Previews')
r={'viewport':[w,h],'aspect':aspect,'cards':[]}
for i in range(3):
    c=d.get_editor_property('Card'+str(i))
    scale=c.get_actor_scale3d().x
    center=.5+c.get_actor_location().x/d.get_editor_property('ViewWidth')
    half=scale*16.0875/d.get_editor_property('ViewWidth')
    r['cards'].append({'center_u':center,'hit_left':center-half,'hit_right':center+half,
        'visible_width_fraction':scale*30/d.get_editor_property('ViewWidth'),
        'visible_height_fraction':scale*45/d.get_editor_property('ViewHeight')})
r['no_overlap']=all(r['cards'][i]['hit_right']<r['cards'][i+1]['hit_left'] for i in range(2))
r['in_bounds']=all(0<a['hit_left']<a['hit_right']<1 for a in r['cards'])
assert r['no_overlap'] and r['in_bounds']
(out/('layout-'+aspect+'.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
# Wait for newly spawned world-space widgets to draw before screenshotting.
pending={'ticks':0}
def take(delta):
    pending['ticks']+=1
    if pending['ticks']<45:return
    path=out/('layout-'+aspect+'.png')
    unreal.SystemLibrary.execute_console_command(p.get_world(),'Shot showui filename="'+str(path)+'" -nosuffix',p)
    unreal.unregister_slate_post_tick_callback(handle)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle=unreal.register_slate_post_tick_callback(take)
print(json.dumps(r))

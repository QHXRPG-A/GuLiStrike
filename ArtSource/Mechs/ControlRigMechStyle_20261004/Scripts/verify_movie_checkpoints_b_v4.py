"""Decode the delivered MP4s, inspect their media dimensions, and save actual checkpoints."""
import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4'); V=R/'AnimationPreviews'
results=[]
for clip,expected in [('Deploy',76),('Idle',131),('Walk',76)]:
    s=bpy.data.scenes.new('MovieReadback_'+clip); bpy.context.window.scene=s
    s.render.resolution_x=1280; s.render.resolution_y=1280; s.render.resolution_percentage=100
    s.render.fps=15; s.view_settings.view_transform='Standard'; s.view_settings.look='None'
    s.render.image_settings.file_format='PNG'; seq=s.sequence_editor_create()
    strip=seq.strips.new_movie('ActualDeliveredMP4',str(V/f'ControlRigMech_B_v4_{clip}.mp4'),1,1)
    assert strip.frame_duration==expected,(clip,strip.frame_duration,expected)
    element=strip.elements[0]; assert (element.orig_width,element.orig_height)==(1280,1280)
    for label,frame in [('Start',1),('Middle',expected//2+1),('End',expected)]:
        s.frame_set(frame); s.render.filepath=str(V/f'Decoded_{clip}_{label}.png'); bpy.ops.render.render(write_still=True)
    results.append({'clip':clip,'delivered_mp4_decoded':True,'frame_count':strip.frame_duration,'width':element.orig_width,'height':element.orig_height,'decoded_checkpoint_images':3})
    bpy.data.scenes.remove(s)
(V/'movie_readback_report.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print('B_MOVIES_READBACK_OK',json.dumps(results),flush=True)

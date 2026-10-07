import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v2'; P=R/'Production_B_v1/AnimationPreviews'
s=bpy.data.scenes.new('EncodeDistinctFrames'); bpy.context.window.scene=s
s.render.resolution_x=1280; s.render.resolution_y=720; s.render.resolution_percentage=100; s.render.fps=15
s.frame_start=1; s.frame_end=2; s.view_settings.view_transform='Standard'; s.view_settings.look='None'
seq=s.sequence_editor_create()
seq.strips.new_image('Distinct_Start',str(P/'ControlRigMech_Deploy_Start.png'),1,1)
seq.strips.new_image('Distinct_End',str(P/'ControlRigMech_Deploy_End.png'),1,2)
s.render.image_settings.media_type='VIDEO'; s.render.image_settings.file_format='FFMPEG'; s.render.ffmpeg.format='MPEG4'; s.render.ffmpeg.codec='H264'
s.render.filepath=str(O/'DistinctFrameProbe.mp4'); bpy.ops.render.render(animation=True)
v=bpy.data.scenes.new('ReadDistinctFrames'); bpy.context.window.scene=v
v.render.resolution_x=1280; v.render.resolution_y=720; v.render.resolution_percentage=100
v.view_settings.view_transform='Standard'; v.view_settings.look='None'
movie=v.sequence_editor_create().strips.new_movie('Readback',str(O/'DistinctFrameProbe.mp4'),1,1)
for frame in (1,2):
    v.frame_set(frame); v.render.filepath=str(O/f'DistinctProbe_{frame}.png'); bpy.ops.render.render(write_still=True)
print('DISTINCT_VIDEO_PROBE_OK',movie.frame_duration,flush=True)

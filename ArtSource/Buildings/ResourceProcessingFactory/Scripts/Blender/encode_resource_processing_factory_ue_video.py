"""Encode the actual UE screenshots with Blender's bundled FFmpeg, no edits."""
from pathlib import Path
import bpy

out=Path('D:/UE5.7/test1/outputs/resource-processing-factory-20260909')
frames=sorted((out/'UE_frames').glob('frame_*.png'))
if len(frames)!=90:raise RuntimeError('Expected 90 UE render frames')
scene=bpy.context.scene
editor=scene.sequence_editor_create()
strip=editor.strips.new_image('UE rendered door cycle',str(frames[0]),channel=1,frame_start=1)
for frame in frames[1:]:strip.elements.append(frame.name)
scene.frame_start=1;scene.frame_end=90;scene.render.fps=10
scene.render.resolution_x=1280;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.render.use_sequencer=True
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.view_settings.exposure=0;scene.view_settings.gamma=1
scene.render.image_settings.media_type='VIDEO';scene.render.image_settings.file_format='FFMPEG'
scene.render.ffmpeg.format='MPEG4';scene.render.ffmpeg.codec='H264';scene.render.ffmpeg.constant_rate_factor='HIGH'
scene.render.filepath=str(out/'RPF_UE_Door_Preview.mp4')
bpy.ops.render.render(animation=True)
print('RPF_UE_VIDEO_COMPLETE',flush=True)

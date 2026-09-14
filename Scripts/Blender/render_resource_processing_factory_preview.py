"""Render the saved RPF preview in an isolated Blender process, without resaving it."""
from pathlib import Path
import bpy

ROOT=Path('D:/UE5.7/test1')
scene=bpy.data.scenes['GS_RPF_Authoring'];bpy.context.window.scene=scene
rig=bpy.data.objects['RPF_Rig'];rig.animation_data.action=None
bone=rig.pose.bones['door_hinge']
for frame,angle in [(1,0),(11,0),(41,-1.5707963267948966),(51,-1.5707963267948966),(81,0),(91,0)]:
    bone.rotation_euler=(0,angle,0);bone.keyframe_insert(data_path='rotation_euler',frame=frame)
rig.animation_data.action.name='Door_Video_Temporary'
scene.frame_start=1;scene.frame_end=90;scene.frame_step=1;scene.render.fps=10
scene.camera=bpy.data.objects['RPF_CAM_Hero']
scene.render.resolution_x=960;scene.render.resolution_y=660;scene.render.resolution_percentage=100
scene.render.engine='CYCLES';scene.cycles.samples=8;scene.cycles.use_denoising=True
scene.render.use_persistent_data=True
scene.render.image_settings.media_type='VIDEO';scene.render.image_settings.file_format='FFMPEG'
scene.render.ffmpeg.format='MPEG4';scene.render.ffmpeg.codec='H264';scene.render.ffmpeg.constant_rate_factor='MEDIUM'
scene.render.filepath=str(ROOT/'outputs/resource-processing-factory-20260909/RPF_Door_Preview.mp4')
bpy.ops.render.render(animation=True)
print('RPF_PREVIEW_VIDEO_COMPLETE',flush=True)

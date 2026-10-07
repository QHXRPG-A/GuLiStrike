"""Render complete actual source actions; encode frames with native Blender FFmpeg."""
import bpy,json,math,hashlib
import numpy as np
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v3'
blend_hash=hashlib.sha256((O/'ControlRigMech_B_v3_Production.blend').read_bytes()).hexdigest()
geometry_hash=hashlib.sha256((O/'realtime_readback_audit.json').read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v3_Production.blend'))
s=bpy.data.scenes['STYLE_REALTIME_B_v3']; bpy.context.window.scene=s
rig=bpy.data.objects['Armature']; rig.data.pose_position='POSE'
s.render.resolution_x=1280; s.render.resolution_y=1280; s.render.resolution_percentage=100
s.render.fps=30; s.camera.data.ortho_scale=18.5; s.render.use_freestyle=False
s.render.image_settings.file_format='PNG'; s.render.image_settings.color_mode='RGB'
V=O/'AnimationPreviews'; V.mkdir(exist_ok=True)
foot_names=['foot_fr_01_l','foot_fr_01_r','foot_bk_01_l','foot_bk_01_r']
body=bpy.data.objects['ControlRigMech_LOD0_Body']
foot_indices={n:[v.index for v in body.data.vertices if any(g.group==body.vertex_groups[n].index and g.weight>.5 for g in v.groups)] for n in foot_names}
results=[]
for clip in ('Deploy','Idle','Walk'):
    data=json.loads((O/'AnimationSource'/f'Mech_{clip}_FullPose.json').read_text())
    action=bpy.data.actions[f'ControlRigMech_Mech_{clip}_Source30fps']
    rig.animation_data.action=action; rig.animation_data.action_slot=action.slots[0]
    duration=data['duration_s']; fps=15; count=int(round(duration*fps))+1
    folder=V/f'Mech_{clip}_Frames'; folder.mkdir(exist_ok=True)
    samples=[]
    for k in range(count):
        t=min(duration,k/fps); frame=1+t*30
        s.frame_set(int(frame),subframe=frame-int(frame)); bpy.context.view_layer.update()
        s.render.filepath=str(folder/f'{k+1:04d}.png')
        bpy.ops.render.render(write_still=True,scene=s.name)
        if k in (0,count//2,count-1):
            evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get())
            coords=np.array([(evaluated.matrix_world@v.co)[:] for v in evaluated.data.vertices])
            samples.append({'time_s':t,'frame_30fps':frame,'body_bounds_m':[coords.min(axis=0).tolist(),coords.max(axis=0).tolist()],
                            'foot_component_min_z_m':{n:float(coords[ids,2].min()) if ids else None for n,ids in foot_indices.items()},
                            'base_pose_position_m':list(rig.pose.bones['base'].matrix.translation),
                            'cannon_02_basis_scale':list(rig.pose.bones['cannon_02'].scale)})
            endpoint=V/f'ControlRigMech_{clip}_{"Start" if k==0 else "End" if k==count-1 else "Middle"}.png'
            import shutil; shutil.copy2(folder/f'{k+1:04d}.png',endpoint)
        if k%15==0: print('MOTION_FRAME',clip,k+1,count,flush=True)
    video=bpy.data.scenes.new(f'Encode_{clip}'); video.render.resolution_x=1280; video.render.resolution_y=1280
    video.render.resolution_percentage=100; video.render.fps=fps; video.frame_start=1; video.frame_end=count
    video.view_settings.view_transform='Standard'; video.view_settings.look='None'
    seq=video.sequence_editor_create()
    for k in range(1,count+1): seq.strips.new_image(f'ActualFrame_{k}',str(folder/f'{k:04d}.png'),1,k)
    video.render.image_settings.media_type='VIDEO'; video.render.image_settings.file_format='FFMPEG'
    video.render.ffmpeg.format='MPEG4'; video.render.ffmpeg.codec='H264'; video.render.ffmpeg.constant_rate_factor='HIGH'
    video.render.filepath=str(V/f'ControlRigMech_B_v3_{clip}.mp4')
    bpy.context.window.scene=video; bpy.ops.render.render(animation=True)
    bpy.context.window.scene=s
    results.append({'clip':clip,'actual_source_duration_s':duration,'source_fps':30,'preview_fps':fps,
                    'source_blend_at_render_sha256':blend_hash,'saved_body_readback_report_sha256':geometry_hash,
                    'preview_frame_count':count,'includes_exact_start_and_end':True,'line_method':'actual skinned structural-line shader + editable outline shell; Freestyle disabled',
                    'samples':samples,'file':str(V/f'ControlRigMech_B_v3_{clip}.mp4')})
    bpy.data.scenes.remove(video)
rig.animation_data.action=None; s.frame_set(1)
(V/'animation_preview_report.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print('B_FULL_MOTION_PREVIEWS_OK',flush=True)

"""Read original animation FBX transforms; no production motion is invented."""
import bpy
import json
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
manifest=json.loads((ROOT/'Source/Animations/animation_source_manifest.json').read_text(encoding='utf-8'))
scene=bpy.context.scene
records=[]
for row in manifest['animations']:
    before=set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT/row['file']),use_anim=True,automatic_bone_orientation=False)
    imported=set(bpy.data.objects)-before
    arms=[o for o in imported if o.type=='ARMATURE']
    assert len(arms)==1
    arm=arms[0]
    action=arm.animation_data.action
    assert action is not None
    start,end=map(float,action.frame_range)
    fps=scene.render.fps/scene.render.fps_base
    count=round(row['duration_s']*30)+1
    frames=[]
    for i in range(count):
        frame=min(end,start+i*fps/30)
        scene.frame_set(int(frame),subframe=frame-int(frame))
        bpy.context.view_layer.update()
        frames.append({'armature_world':[list(r) for r in arm.matrix_world],
                       'bones':{b.name:[list(r) for r in arm.matrix_world@b.matrix] for b in arm.pose.bones}})
    name=row['path'].split('.')[-1]
    records.append({'name':name,'source':row['path'],'source_fbx':row['file'],'source_sha256':row['sha256'],
                    'duration_s':row['duration_s'],'target_fps':30,'source_import_fps':fps,
                    'source_frame_range':[start,end],'sampling_origin_frame':start,
                    'sampling_preserves_original_seconds':True,
                    'source_bones':len(arm.data.bones),'frames':frames})
    print(json.dumps({'animation':name,'bones':len(arm.data.bones),'frames':count,'range':[start,end],
                      'duration_s':row['duration_s']}),flush=True)
    for obj in imported: bpy.data.objects.remove(obj,do_unlink=True)
out=ROOT/'Source/Animations/source_world_poses.json'
out.write_text(json.dumps({'animations':records,'sampling':'original FBX world poses at 30fps'},separators=(',',':')),encoding='utf-8')
print(json.dumps({'animations':len(records),'poses_saved':str(out),'bytes':out.stat().st_size}),flush=True)

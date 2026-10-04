"""Actual EEVEE seven-action videos, with a native Blender contact reference floor."""
import bpy
import json
import sys
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
TARGET=OUT/'AnimationPreviews'
TARGET.mkdir(parents=True,exist_ok=True)
assert not (OUT/'review_manifest.json').exists(), 'Published B version is frozen'
scene=bpy.context.scene
rig=bpy.data.objects['Armature']
body=bpy.data.objects['RSGMech_LOD0_Body']
records=json.loads((OUT/'animation_binding_report.json').read_text(encoding='utf-8'))['animations']
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
if args:
    records=[r for r in records if any(name in r['action'] for name in args)]
setup=json.loads((ROOT/'References_A_v3/reference_setup.json').read_text(encoding='utf-8'))
pose=setup['cameras']['Hero']
scene.camera.location=pose['location_m']
scene.camera.rotation_euler=pose['rotation_radians']
scene.camera.data.type='ORTHO'
scene.camera.data.ortho_scale=pose['ortho_scale_m']*1.20
scene.render.engine='BLENDER_EEVEE'
scene.render.use_freestyle=False
scene.render.resolution_x=scene.render.resolution_y=1080
scene.render.resolution_percentage=100
scene.render.fps=30
scene.render.fps_base=1
scene.render.image_settings.media_type='VIDEO'
scene.render.ffmpeg.format='MPEG4'
scene.render.ffmpeg.codec='H264'
scene.render.ffmpeg.constant_rate_factor='HIGH'
scene.render.ffmpeg.ffmpeg_preset='GOOD'
scene.render.ffmpeg.audio_codec='NONE'
scene.render.use_file_extension=True
floor_z=min(v.co.z for v in body.data.vertices)
mesh=bpy.data.meshes.new('ReviewFloor_Only_NoExport')
extent=18
mesh.from_pydata([(-extent,-extent,floor_z),(extent,-extent,floor_z),(extent,extent,floor_z),(-extent,extent,floor_z)],[],[(0,1,2,3)])
floor=bpy.data.objects.new('ReviewFloor_Only_NoExport',mesh)
scene.collection.objects.link(floor)
mat=bpy.data.materials.new('ReviewFloor_Only_NoExport')
mat.use_nodes=True
nt=mat.node_tree
nt.nodes.clear()
out=nt.nodes.new('ShaderNodeOutputMaterial')
em=nt.nodes.new('ShaderNodeEmission')
em.inputs['Color'].default_value=(.73,.70,.65,1)
nt.links.new(em.outputs[0],out.inputs[0])
floor.data.materials.append(mat)
# Sparse grid cylinders establish a stable contact plane; they are preview-only.
gridmat=bpy.data.materials.new('ReviewGrid_Only_NoExport')
gridmat.use_nodes=True
nodes=gridmat.node_tree.nodes
nodes.clear()
gout=nodes.new('ShaderNodeOutputMaterial')
gem=nodes.new('ShaderNodeEmission')
gem.inputs['Color'].default_value=(.57,.55,.52,1)
gridmat.node_tree.links.new(gem.outputs[0],gout.inputs[0])
for i in range(-7,8):
    for a,b in (((i*2,-14,floor_z+.001),(i*2,14,floor_z+.001)),((-14,i*2,floor_z+.001),(14,i*2,floor_z+.001))):
        curve=bpy.data.curves.new('ReviewGrid','CURVE')
        curve.dimensions='3D'; curve.bevel_depth=.006; curve.bevel_resolution=0
        spline=curve.splines.new('POLY'); spline.points.add(1)
        spline.points[0].co=(*a,1); spline.points[1].co=(*b,1)
        ob=bpy.data.objects.new('ReviewGrid_Only_NoExport',curve)
        scene.collection.objects.link(ob); curve.materials.append(gridmat)

old_report_path=OUT/'animation_preview_report.json'
old_report=json.loads(old_report_path.read_text(encoding='utf-8')) if args and old_report_path.exists() else None
report=old_report or {'production_file':str(OUT/'RSGMech_Production_B_v1.blend'),
        'renderer':'Blender EEVEE','resolution':[1080,1080],'fps':30,
        'original_motion_unchanged':True,'floor_z_m':floor_z,
        'preview_floor_exported':False,'videos':[]}
for record in records:
    action=bpy.data.actions[record['action']]
    rig.animation_data.action=action
    if action.slots: rig.animation_data.action_slot=action.slots[0]
    scene.camera.animation_data_clear()
    scene.camera.location=pose['location_m']
    scene.camera.data.ortho_scale=pose['ortho_scale_m']*(1.45 if 'Death' in action.name else 1.20)
    tracking='vertical body center' if 'Landing' in action.name else 'fixed reference direction, wider action framing'
    if 'Landing' in action.name:
        rest_z=(min(v.co.z for v in body.data.vertices)+max(v.co.z for v in body.data.vertices))/2
        for frame in range(1,record['frames']+1):
            scene.frame_set(frame)
            ev=body.evaluated_get(bpy.context.evaluated_depsgraph_get())
            data=ev.to_mesh()
            zs=[(ev.matrix_world @ v.co).z for v in data.vertices]
            offset=(min(zs)+max(zs))/2-rest_z
            scene.camera.location=Vector(pose['location_m'])+Vector((0,0,offset))
            scene.camera.keyframe_insert(data_path='location',frame=frame)
            ev.to_mesh_clear()
    scene.frame_start=1; scene.frame_end=record['frames']
    scene.frame_set(1)
    scene.render.image_settings.media_type='VIDEO'
    scene.render.filepath=str(TARGET/(record['action']+'.mp4'))
    print(json.dumps({'phase':'render_animation','action':record['action'],'frames':record['frames']}),flush=True)
    bpy.ops.render.render(animation=True)
    stills=[]
    scene.render.image_settings.media_type='IMAGE'
    scene.render.image_settings.file_format='PNG'
    for frame in sorted({1,(record['frames']+1)//2,record['frames']}):
        scene.frame_set(frame)
        path=TARGET/(record['action']+f'_Frame{frame:03d}.png')
        scene.render.filepath=str(path)
        bpy.ops.render.render(write_still=True)
        stills.append(path.name)
    report['videos']=[v for v in report['videos'] if v['action']!=record['action']]
    report['videos'].append({'action':record['action'],'file':record['action']+'.mp4',
                             'frames':record['frames'],'motion_duration_s':record['duration_s'],
                             'encoded_duration_s':record['frames']/30,'stills':stills,
                             'camera':tracking})
    (OUT/'animation_preview_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
rig.animation_data.action=None
for bone in rig.pose.bones: bone.matrix_basis.identity()
scene.frame_set(1)
print(json.dumps({'videos':len(report['videos']),'finished':True}),flush=True)

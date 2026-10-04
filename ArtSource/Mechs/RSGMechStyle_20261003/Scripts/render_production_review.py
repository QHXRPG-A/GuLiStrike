import bpy
import json
import sys
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
stage=args[0] if args else 'WorkPreviews'
target=OUT/stage
target.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
rig=bpy.data.objects['Armature']
if rig.animation_data: rig.animation_data.action=None
for bone in rig.pose.bones: bone.matrix_basis.identity()
scene.frame_set(1)
setup=json.loads((ROOT/'References_A_v3/reference_setup.json').read_text(encoding='utf-8'))
scene.render.engine='BLENDER_EEVEE'
scene.render.use_freestyle=False
scene.render.resolution_x=scene.render.resolution_y=2048
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
for view in ('Hero','Front','Left','Back'):
    pose=setup['cameras'][view]
    scene.camera.location=pose['location_m']
    scene.camera.rotation_euler=pose['rotation_radians']
    scene.camera.data.type='ORTHO'
    scene.camera.data.ortho_scale=pose['ortho_scale_m']
    scene.render.filepath=str(target/f'RSG_B_v1_{view}.png')
    bpy.ops.render.render(write_still=True)
scene.camera.rotation_euler=setup['cameras']['Hero']['rotation_radians']
scene.camera.location=Vector((0,-2.62,3.55))+scene.camera.rotation_euler.to_matrix() @ Vector((0,0,25))
scene.camera.data.ortho_scale=3.1
scene.render.filepath=str(target/'RSG_B_v1_HeadDetail.png')
bpy.ops.render.render(write_still=True)
print(json.dumps({'actual_production_mesh':True,'reference':'A-v3','views':4,'resolution':[2048,2048],
                  'internal_lines':'baked_structural_mask','freestyle':False}),flush=True)

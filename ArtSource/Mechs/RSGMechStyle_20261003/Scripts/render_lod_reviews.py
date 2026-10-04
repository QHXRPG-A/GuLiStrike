"""Native same-camera body/contour LOD images, close inspection and screen sizes."""
import bpy
import json
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
TARGET=OUT/'LODPreviews'
TARGET.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
rig=bpy.data.objects['Armature']
rig.animation_data.action=None
for p in rig.pose.bones: p.matrix_basis.identity()
scene.frame_set(1)
setup=json.loads((ROOT/'References_A_v3/reference_setup.json').read_text(encoding='utf-8'))
cam=setup['cameras']['Hero']
scene.camera.location=cam['location_m']
scene.camera.rotation_euler=cam['rotation_radians']
scene.camera.data.type='ORTHO'
scene.render.engine='BLENDER_EEVEE'
scene.render.use_freestyle=False
scene.render.resolution_x=scene.render.resolution_y=1024
scene.render.resolution_percentage=100
scene.render.image_settings.media_type='IMAGE'
scene.render.image_settings.file_format='PNG'
report={'preview_type':'Blender screen-coverage proxies; actual commander-camera UE previews deferred to B approval',
        'views':[]}
for lod,screen in enumerate((1,.4,.16,.06)):
    for n in range(4):
        for obj in bpy.data.collections[f'03_LOD{n}_Review'].objects:
            obj.hide_render=n!=lod
            obj.hide_set(n!=lod)
    for variant,coverage in (('Inspection',1),('Screen',screen)):
        scene.camera.data.ortho_scale=cam['ortho_scale_m']/coverage
        path=TARGET/f'RSG_B_v1_LOD{lod}_{variant}.png'
        scene.render.filepath=str(path)
        bpy.ops.render.render(write_still=True)
        report['views'].append({'LOD':lod,'variant':variant,'screen_size':screen,
                                'projection_scale_relative_to_LOD0':coverage,'file':path.name})
(OUT/'lod_preview_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({'images':len(report['views'])}),flush=True)

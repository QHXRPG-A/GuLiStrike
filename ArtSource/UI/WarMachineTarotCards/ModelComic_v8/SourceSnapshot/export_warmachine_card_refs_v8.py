"""Export the revised v6 model for a separate UE card-reference copy; no rendering."""
import bpy
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path('D:/UE5.7/test1')
SOURCE = ROOT / 'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6'
OUT = ROOT / 'ArtSource/UI/WarMachineTarotCards/ModelComic_v8/ModelPreview'
OUT.mkdir(parents=True, exist_ok=True)
source = SOURCE / 'WarMachine_LevelNodes_Review.blend'
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
body = bpy.data.objects['WarMachine_After_Body']
contour = bpy.data.objects['WarMachine_After_Contour']
bpy.ops.object.select_all(action='DESELECT')
for obj in (body, contour):
    obj.hide_set(False)
    obj.hide_viewport = False
    obj.select_set(True)
bpy.context.view_layer.objects.active = body
body.data.materials[0].name = 'CardReference_Body'
contour.data.materials[0].name = 'CardReference_Contour'
fbx = OUT / 'SM_WarMachine_LevelNodes_Reference.fbx'
bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, global_scale=1,
    apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y',
    axis_up='Z', mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False,
    path_mode='STRIP', use_custom_props=True, object_types={'MESH'})
for name in ('T_WarMachine_BaseColor.png', 'T_WarMachine_LineMask.png'):
    shutil.copyfile(SOURCE / 'ReviewData/After' / name, OUT / name)
assert source_hash == hashlib.sha256(source.read_bytes()).hexdigest()
report = {'success': True, 'source': str(source), 'source_sha256': source_hash,
    'exported_objects': [body.name, contour.name], 'source_unchanged': True,
    'triangles': sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in (body, contour)),
    'files': [{'file': p.name, 'bytes': p.stat().st_size,
               'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
              for p in (fbx, OUT/'T_WarMachine_BaseColor.png', OUT/'T_WarMachine_LineMask.png')]}
(OUT/'export-manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('CARD_REF_V8_EXPORT_OK', json.dumps(report), flush=True)

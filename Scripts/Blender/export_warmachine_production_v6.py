"""Export the approved v6 geometry with the existing WarMachine rigid skeleton.

Run in background Blender; never renders or changes the editable/review sources.
"""
import bpy
import hashlib
import json
import shutil
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1')
SOURCE = ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6'
OUT = SOURCE/'UEProduction'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT/'Scripts/Blender'))
import package_tactical_handbuilt as package

def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()

review = SOURCE/'WarMachine_LevelNodes_Review.blend'
editable = SOURCE/'WarMachine_LevelNodes_Editable.blend'
old_rig = ROOT/'ArtSource/StylePass_20260917/Models/WarMachine_Cel.blend'
protected = {str(p): digest(p) for p in (review, editable, old_rig)}
report = json.loads((SOURCE/'WarMachine_handbuilt_report.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(review))
body = bpy.data.objects['WarMachine_After_Body']
contour = bpy.data.objects['WarMachine_After_Contour']
for obj in list(bpy.data.objects):
    if obj not in (body, contour): bpy.data.objects.remove(obj, do_unlink=True)
with bpy.data.libraries.load(str(old_rig), link=False) as (src, dst):
    dst.objects = [name for name in src.objects if name == 'Rig_WarMachine']
assert len(dst.objects) == 1 and dst.objects[0], 'Original rigid skeleton missing'
arm = dst.objects[0]
bpy.context.scene.collection.objects.link(arm)
assert [b.name for b in arm.data.bones] == ['root', 'Body']
bone_contract = {b.name: {'parent': b.parent.name if b.parent else None,
                          'matrix_local': [list(row) for row in b.matrix_local]} for b in arm.data.bones}
for pb in arm.pose.bones:
    assert pb.matrix_basis == __import__('mathutils').Matrix.Identity(4), 'Unexpected old pose'
for obj, name, mat_name in ((body, 'SM_WarMachine_Cel_Body', 'M_WarMachine_Cel'),
                            (contour, 'SM_WarMachine_Cel_Contour', 'M_Tactical_Contour')):
    obj.name = name
    obj.hide_set(False)
    obj.hide_viewport = False
    obj.hide_render = False
    assert obj.matrix_world == __import__('mathutils').Matrix.Identity(4)
    obj.vertex_groups.clear()
    group = obj.vertex_groups.new(name='Body')
    group.add(list(range(len(obj.data.vertices))), 1.0, 'REPLACE')
    assert len(obj.data.uv_layers) == 1
    package.metadata(obj, report)
    obj.data.materials[0].name = mat_name
    mod = obj.modifiers.new('Rigid mechanical bones', 'ARMATURE')
    mod.object = arm
    obj.parent = arm
    assert all(len(v.groups) == 1 and abs(v.groups[0].weight-1) < 1e-6 for v in obj.data.vertices)

for name in ('T_WarMachine_BaseColor.png', 'T_WarMachine_LineMask.png'):
    shutil.copyfile(SOURCE/'ReviewData/After'/name, OUT/name)

def bounds(objects):
    vertices = [o.matrix_world @ v.co for o in objects for v in o.data.vertices]
    return [[min(v[i] for v in vertices) for i in range(3)], [max(v[i] for v in vertices) for i in range(3)]]

expected_bounds = bounds((body, contour))
report.update(source_review=str(review), source_sha256=protected[str(review)],
              model_revision='WarMachine_LevelNodes_v6', bounds_with_contour_m=expected_bounds,
              body_triangles=sum(len(p.vertices)-2 for p in body.data.polygons),
              contour_triangles=sum(len(p.vertices)-2 for p in contour.data.polygons),
              uv_layers=[uv.name for uv in body.data.uv_layers], rigid_weights_valid=True,
              bone_contract=bone_contract, armature_matrix=[list(row) for row in arm.matrix_world],
              approval_message='重防号模型导出至UE成为正式资产', assistant_image_inspection='not_performed_per_user_instruction')

bpy.context.scene.name = 'WarMachine_Production_v6'
bpy.context.scene['approval'] = report['approval_message']
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
contour.select_set(True)
bpy.context.view_layer.objects.active = body
options = dict(use_selection=True, global_scale=1, apply_unit_scale=True,
               apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y', axis_up='Z',
               mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False,
               path_mode='STRIP', use_custom_props=True)
bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_WarMachine_Cel.fbx'), object_types={'MESH'}, **options)
arm.select_set(True)
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_WarMachine_Cel.fbx'), object_types={'MESH', 'ARMATURE'},
                         use_armature_deform_only=True, **options)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'WarMachine_Production_Rigged.blend'))

# Read exported geometry back without rendering or inspecting an image.
checks = []
for prefix in ('SM', 'SK'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(OUT/(prefix+'_WarMachine_Cel.fbx')), use_custom_normals=True)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    arms = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    actual = bounds(meshes)
    err = max(abs(actual[a][b]-expected_bounds[a][b]) for a in range(2) for b in range(3))
    assert len(meshes) == 2 and err < 1e-4, (prefix, len(meshes), err)
    assert all(len(o.data.uv_layers) == 3 for o in meshes)
    assert bool(arms) == (prefix == 'SK')
    if arms:
        assert [b.name for b in arms[0].data.bones] == ['root', 'Body']
        assert all(len(v.groups) == 1 and abs(v.groups[0].weight-1) < 1e-6 for o in meshes for v in o.data.vertices)
    checks.append({'file': prefix+'_WarMachine_Cel.fbx', 'mesh_objects': len(meshes),
                   'bounds_m': actual, 'max_bound_error_m': err, 'uv_channels': 3,
                   'bones': [b.name for b in arms[0].data.bones] if arms else []})

assert all(digest(Path(p)) == h for p, h in protected.items())
report.update(success=True, fbx_readback=checks, protected_sources_unchanged=True,
              files=[{'file': p.name, 'bytes': p.stat().st_size, 'sha256': digest(p)}
                     for p in (OUT/'SM_WarMachine_Cel.fbx', OUT/'SK_WarMachine_Cel.fbx',
                               OUT/'WarMachine_Production_Rigged.blend', OUT/'T_WarMachine_BaseColor.png', OUT/'T_WarMachine_LineMask.png')])
(OUT/'export-manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('WARMACHINE_V6_PRODUCTION_EXPORT_OK', json.dumps({'success': True, 'fbx_readback': checks}), flush=True)

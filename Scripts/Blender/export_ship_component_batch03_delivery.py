"""Export B-approved Batch03 static assets using the existing Ship readback."""
import ast
import importlib.util
import json
import shutil
import time
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

PROJECT = Path('D:/UE5.7/test1')
spec = importlib.util.spec_from_file_location('batch03_surface', PROJECT / 'Scripts/Blender/style_ship_component_batch03_materials.py')
batch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(batch)
s = batch.s
assert batch.VERSION == 'v3'
DELIVERY = s.ROOT / 'Delivery/v3'
APPROVAL = json.loads((s.ROOT / 'approval_B_20260917.json').read_text(encoding='utf-8'))
assert APPROVAL['gate'] == 'B' and APPROVAL['decision'] == 'approved'
assert s.filehash(s.ROOT / APPROVAL['review_manifest']) == APPROVAL['review_manifest_sha256']
for folder in ('Blender', 'FBX', 'Textures', 'Previews', 'Parameters', 'Validation'):
    (DELIVERY / folder).mkdir(parents=True, exist_ok=True)


def load_existing_functions(path, names):
    tree = ast.parse(path.read_text(encoding='utf-8'))
    nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    assert {node.name for node in nodes} == set(names)
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), 'exec'), globals())


# Reuse existing functions verbatim, without executing first-batch top-level
# directory writes or the building validator's unrelated asset loop.
reader_path = PROJECT / 'ArtSource/Buildings/IndustrialDefenseSet/scripts/validate_exports.py'
load_existing_functions(reader_path, ('import_file', 'world_points'))
exporter_path = PROJECT / 'Scripts/Blender/export_ship_component_material_delivery.py'
load_existing_functions(exporter_path, ('uv_order_for_export', 'vertex_owners', 'portable_image_paths',
    'save_delivery_blend', 'max_closest', 'validate', 'overview', 'verify_portable_blends'))


def export_material(key):
    mat = bpy.data.materials.new('M_SC_' + key + '_Portable')
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Metallic'].default_value = .2
    bsdf.inputs['Roughness'].default_value = .64
    image = bpy.data.images.load(str(DELIVERY / 'Textures' / f'T_SC_{key}_BaseColor_2K.png'), check_existing=False)
    image.colorspace_settings.name = 'sRGB'
    uv = nodes.new('ShaderNodeUVMap')
    uv.uv_map = s.UV_NAME
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = image
    links.new(uv.outputs[0], tex.inputs[0])
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    mat['GuLiStrike_Toon_Parameters'] = f'../Parameters/{key}_material_parameters.json'
    mat['GuLiStrike_LineMask'] = f'../Textures/T_SC_{key}_LineMask_2K.png'
    mat['GuLiStrike_ORM'] = f'../Textures/T_SC_{key}_ORM_2K.png'
    return mat


def export_one(key):
    approved = APPROVAL['parts'][key]
    source = s.ROOT / approved['approved_blend']
    assert s.filehash(source) == approved['approved_blend_sha256']
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    scene.frame_set(0)
    body = next(o for o in scene.objects if o.type == 'MESH' and o.get('source_file'))
    assert not body.modifiers and not any(o.type == 'ARMATURE' for o in scene.objects)
    assert s.ink_bake.geometry_hash(body.data) == approved['original_geometry_sha256']
    scene['review_B'] = 'approved: 三件均通过 B，导出 FBX'
    scene['approval_record'] = '//../Parameters/approval_B_20260917.json'
    scene['approval_A'] = 'approved; //../Parameters/approval_A_20260917.json'
    scene['delivery_version'] = 'Batch03 v3'
    for role in ('BaseColor', 'ORM', 'LineMask'):
        stem = f'T_SC_{key}_{role}_2K'
        image = next(i for i in bpy.data.images if i.name.startswith(stem))
        assert image.packed_file
        image.use_fake_user = True
    portable_image_paths()
    blend_path = DELIVERY / 'Blender' / f'SC_{key}_Styled.blend'
    save_delivery_blend(blend_path)

    # Only the disposable export copy changes UV channel order/material graph.
    body.data = body.data.copy()
    order = uv_order_for_export(body.data)
    body.data.materials.clear()
    body.data.materials.append(export_material(key))
    for polygon in body.data.polygons:
        polygon.material_index = 0
    source_part = s.SNAP['parts'][key]
    sockets = source_part['sockets']
    assert sockets == [] and source_part['bones'] == []
    body['GuLiStrike_Sockets_JSON'] = json.dumps(sockets, separators=(',', ':'))
    body['GuLiStrike_CompatibleSockets_JSON'] = json.dumps(source_part['compatible_sockets'], separators=(',', ':'))
    body['GuLiStrike_PaintUV_Index'] = 0
    body['GuLiStrike_LineUV_Index'] = 1
    body['GuLiStrike_OriginalGeometry_SHA256'] = approved['original_geometry_sha256']
    body['GuLiStrike_SourceApproval'] = 'Batch03 B approved v3; original static geometry retained'
    body['GuLiStrike_SourceMesh'] = source_part['original_mesh']
    body['GuLiStrike_MaterialParameters'] = f'../Parameters/{key}_material_parameters.json'
    interface = {'key': key, 'mechanical_type': 'static', 'sockets': sockets, 'bones': [],
        'compatible_sockets': source_part['compatible_sockets'],
        'part_relative_transform': source_part['part_relative_transform'],
        'source_blueprint': source_part['blueprint'], 'source_mesh': source_part['original_mesh'],
        'coordinates': 'Original UE centimeters; Blender meters uses (UE X, -UE Y, UE Z)/100',
        'FBX_custom_property': 'GuLiStrike_Sockets_JSON', 'Blender_markers': 'none; source mesh has no sockets',
        'compatible_socket_note': 'Blueprint compatibility names are ship attachment interfaces, not mesh sockets'}
    if key == 'Drone_LaunchBay':
        interface.update(launch_direction_blender=[0, 1, 0], launch_direction_UE=[0, -1, 0],
            mark_surface='thin long wall outward -X face; texture only')
    s.dump(DELIVERY / 'Parameters' / f'{key}_Sockets.json', interface)
    points = [body.matrix_world @ v.co for v in body.data.vertices]
    body.data.calc_loop_triangles()
    expected = {'points': [list(p) for p in points], 'triangles': len(body.data.loop_triangles),
        'uv_names': order, 'uv_values': {u.name: [list(d.uv) for d in u.data] for u in body.data.uv_layers},
        'origin': list(body.matrix_world.translation), 'sockets': sockets, 'bones': {}, 'rigid_owners': [],
        'body_geometry_sha256': s.ink_bake.geometry_hash(body.data),
        'approved_blend_sha256': approved['approved_blend_sha256'],
        'final_blend': str(blend_path.relative_to(DELIVERY)), 'final_blend_sha256': s.filehash(blend_path)}
    assert expected['body_geometry_sha256'] == approved['original_geometry_sha256']
    s.select(body)
    fbx = DELIVERY / 'FBX' / f'SM_SC_{key}_Styled.fbx'
    bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, object_types={'MESH'},
        use_mesh_modifiers=True, add_leaf_bones=False, bake_anim=False, axis_forward='-Y', axis_up='Z',
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', mesh_smooth_type='OFF',
        use_tspace=True, path_mode='RELATIVE', use_custom_props=True, embed_textures=False)
    expected.update(fbx_file=str(fbx.relative_to(DELIVERY)), fbx_sha256=s.filehash(fbx))
    s.dump(DELIVERY / 'Validation' / f'{key}_export_expected.json', expected)
    print('BATCH03_APPROVED_FBX_EXPORTED', key, flush=True)
    return expected


def main():
    for name in ('approval_A_20260917.json', 'approval_B_20260917.json'):
        shutil.copy2(s.ROOT / name, DELIVERY / 'Parameters' / name)
    for key in batch.KEYS:
        for item in APPROVAL['parts'][key]['textures']:
            source = s.ROOT / item['path']
            assert s.filehash(source) == item['sha256']
            shutil.copy2(source, DELIVERY / 'Textures' / source.name)
        shutil.copy2(s.OUT / f'{key}_material_parameters.json', DELIVERY / 'Parameters' / f'{key}_material_parameters.json')
    for source in s.PRE.glob('*.png'):
        shutil.copy2(source, DELIVERY / 'Previews' / source.name)
    expected = {key: export_one(key) for key in batch.KEYS}
    reports = [validate(key, row) for key, row in expected.items()]
    s.dump(DELIVERY / 'Validation/FBX_Readback_Summary.json', {'passed': all(r['passed'] for r in reports), 'checks': reports,
        'reader': str(reader_path.relative_to(PROJECT)), 'readback_implementation': str(exporter_path.relative_to(PROJECT)),
        'readback_implementation_sha256': s.filehash(exporter_path), 'scope': 'Three approved static support components'})
    overview()
    verify_portable_blends()
    for key in batch.KEYS:
        assert s.filehash(s.ROOT / APPROVAL['parts'][key]['approved_blend']) == APPROVAL['parts'][key]['approved_blend_sha256']
    print('SHIP_BATCH03_APPROVED_DELIVERY_EXPORTED', flush=True)


if __name__ == '__main__':
    main()

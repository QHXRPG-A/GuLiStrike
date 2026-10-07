"""Palette revision on the frozen actual-source A-v1 scene; no geometry edits."""
import bpy
import copy
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
OUT = ROOT / 'References_A_v2'
OUT.mkdir(parents=True, exist_ok=True)
if (OUT / 'reference_manifest.json').exists():
    raise RuntimeError('Published A-v2 is frozen; revise into a sibling version.')
previous = json.loads((ROOT / 'References_A_v1/reference_setup.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'References_A_v1/ControlRigMech_A_v1_ReferenceStudy.blend'))
scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == 'MESH']
PALETTE = {
    'DeepTealGray': '#2C3735',
    'RustRed': '#8E3A2A',
    'MutedTeal': '#557B78',
    'SandBeige': '#D5C09C',
}
REMAP = {
    'WarmWhite': ('MutedTeal', PALETTE['MutedTeal']),
    'Coral': ('RustRed', PALETTE['RustRed']),
    'Chassis': ('DeepTealGray', PALETTE['DeepTealGray']),
    'Steel': ('SandBeige', PALETTE['SandBeige']),
    'Amber': ('FunctionalLensSand', PALETTE['SandBeige']),
    'Ink': ('InkDerived', '#1B2422'),
}


def linear(hexcode):
    srgb = [int(hexcode[i:i+2], 16)/255 for i in (1, 3, 5)]
    return tuple(v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in srgb)


def geometry_hash(obj):
    h = hashlib.sha256()
    for v in obj.data.vertices:
        h.update(struct.pack('<3f', *v.co))
    for p in obj.data.polygons:
        h.update(struct.pack('<I', len(p.vertices)))
        h.update(struct.pack('<' + 'I'*len(p.vertices), *p.vertices))
    for v in obj.data.vertices:
        for g in v.groups:
            h.update(struct.pack('<IIf', v.index, g.group, g.weight))
    return h.hexdigest()


def structure_snapshot():
    result = {}
    for obj in scene.objects:
        if obj.type not in {'MESH', 'ARMATURE'}:
            continue
        result[obj.name] = {
            'type': obj.type,
            'world_matrix': [list(row) for row in obj.matrix_world],
            'parent': obj.parent.name if obj.parent else None,
            'modifiers': [(m.name, m.type) for m in obj.modifiers],
        }
        if obj.type == 'ARMATURE':
            result[obj.name]['bones'] = [
                {'name': b.name, 'parent': b.parent.name if b.parent else None,
                 'matrix_local': [list(row) for row in b.matrix_local]}
                for b in obj.data.bones
            ]
            result[obj.name]['pose_matrices'] = {
                b.name: [list(row) for row in b.matrix_basis] for b in obj.pose.bones
            }
    return result


before = {o.name: geometry_hash(o) for o in meshes}
assert before == previous['geometry_skinweight_hash_after'], 'Loaded geometry differs from frozen A-v1'
structure_before = structure_snapshot()
indices_before = {o.name: [p.material_index for p in o.data.polygons] for o in meshes}
normal_before = {o.name: [tuple(n.vector) for n in o.data.corner_normals] for o in meshes}
material_report = {}
for old_name, (new_name, hexcode) in REMAP.items():
    mat = bpy.data.materials.get('A_Reference_' + old_name)
    assert mat is not None, f'Missing source study material: {old_name}'
    color = linear(hexcode)
    ramps = [n for n in mat.node_tree.nodes if n.type == 'VALTORGB']
    assert len(ramps) == 1 and len(ramps[0].color_ramp.elements) == 3
    mat.name = 'A_v2_Reference_' + new_name
    mat.diffuse_color = (*color, 1)
    for e, threshold, factor in zip(ramps[0].color_ramp.elements,
                                    previous['tone_thresholds'], previous['tone_linear_factors']):
        assert abs(e.position-threshold) < 1e-6
        e.color = (*[c*factor for c in color], 1)
    material_report[new_name] = {'base_srgb': hexcode, 'replaces_A_v1_region': old_name,
                                 'tone_linear_factors': previous['tone_linear_factors']}

for lineset in scene.view_layers[0].freestyle_settings.linesets:
    lineset.linestyle.color = linear('#1B2422')

after = {o.name: geometry_hash(o) for o in meshes}
assert before == after
assert structure_before == structure_snapshot(), 'Object transforms, bones or pose changed'
assert indices_before == {o.name: [p.material_index for p in o.data.polygons] for o in meshes}
assert normal_before == {o.name: [tuple(n.vector) for n in o.data.corner_normals] for o in meshes}

scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = scene.render.resolution_y = 2048
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.render.use_freestyle = True
scene.render.use_stamp = False
camera = scene.camera
for view, config in previous['cameras'].items():
    camera.data.type = config['type']
    camera.data.ortho_scale = config['ortho_scale_m']
    camera.location = config['location_m']
    camera.rotation_euler = config['rotation_radians']
    scene.render.filepath = str(OUT / f'ControlRigMech_A_v2_{view}.png')
    bpy.ops.render.render(write_still=True)

# Save the editable reference study with the hero camera; never overwrite A-v1.
hero = previous['cameras']['Hero']
camera.location = hero['location_m']
camera.rotation_euler = hero['rotation_radians']
scene.render.filepath = str(OUT / 'ControlRigMech_A_v2_Hero.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ControlRigMech_A_v2_ReferenceStudy.blend'))

setup = copy.deepcopy(previous)
setup['version'] = 'A-v2'
setup['revises_version'] = 'A-v1'
setup['revision_basis'] = {'user_message': '改成这种配色', 'date': '2026-10-04',
                          'attachment': 'Inputs/UserPalette_20261004.jpg'}
setup['palette_srgb'] = PALETTE
setup['ink_srgb'] = '#1B2422'
setup['ink_is_derived_from_deep_teal_gray'] = True
setup['functional_lens_srgb'] = PALETTE['SandBeige']
setup['material_revision'] = material_report
setup['geometry_skinweight_hash_before'] = before
setup['geometry_skinweight_hash_after'] = after
setup['same_material_regions_as_A_v1'] = True
setup['object_transforms_bones_reference_pose_unchanged'] = True
setup['source_corner_normals_unchanged'] = True
setup['same_cameras_as_A_v1_and_gray_baseline'] = True
setup['geometry_changed'] = False
setup['decimation_applied'] = False
setup['saved_camera'] = 'Hero'
for assignment in setup['component_color_assignments']:
    assignment['colors_by_polygon_count'] = {
        REMAP.get(name, (name, None))[0]: count
        for name, count in assignment['colors_by_polygon_count'].items()
    }
setup['reference_triangles_by_color'] = {
    REMAP.get(name, (name, None))[0]: count
    for name, count in previous['reference_triangles_by_color'].items()
}
setup['reference_material_slots'] = {
    o.name: [slot.material.name if slot.material else None for slot in o.material_slots]
    for o in meshes
}
setup['armature_pose_snapshot_sha256'] = hashlib.sha256(
    json.dumps(structure_before, sort_keys=True, separators=(',', ':')).encode('utf-8')
).hexdigest()
(OUT / 'reference_setup.json').write_text(json.dumps(setup, ensure_ascii=False, indent=2), encoding='utf-8')
print('CONTROLRIG_REFERENCE_A_V2_RENDER_OK', flush=True)

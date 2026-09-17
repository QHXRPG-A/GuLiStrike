"""Add editable camera-independent ink to the current Ship Blender study."""
import bpy
import bmesh
import hashlib
import json
import math
import shutil
import struct
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipStylizedStudy_20260916')
OWNER = 'ShipStylizedLineart_20260916'
study = bpy.data.scenes['Ship_Stylized_Study']
ship = bpy.data.objects['SS_Dreadnought_Stylized']
baseline = ROOT / 'GuLiStrike_Ship_AnimeStudy_NoLines.blend'
if not baseline.exists():
    assert not bpy.data.is_dirty, 'Save the current approved working state before taking the baseline copy.'
    shutil.copy2(bpy.data.filepath, baseline)


def geometry_hash(mesh):
    digest = hashlib.sha256()
    for v in mesh.vertices:
        digest.update(struct.pack('<3f', *v.co))
    for p in mesh.polygons:
        digest.update(struct.pack('<I', len(p.vertices)))
        digest.update(struct.pack('<' + 'I' * len(p.vertices), *p.vertices))
    return digest.hexdigest()


before_hash = geometry_hash(ship.data)
existing = bpy.data.collections.get('SS_03_Lineart')
if existing:
    assert existing.get('owner') == OWNER
    for obj in list(bpy.data.objects):
        if obj.get('owner') == OWNER:
            bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(existing)
collection = bpy.data.collections.new('SS_03_Lineart')
collection['owner'] = OWNER
study.collection.children.link(collection)


def ink_material(name, rgb, cull=False):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.use_backface_culling = cull
    mat.use_backface_culling_shadow = True
    mat.use_transparent_shadow = True
    nodes = mat.node_tree.nodes; nodes.clear()
    emission = nodes.new('ShaderNodeEmission')
    emission.inputs['Color'].default_value = rgb
    emission.inputs['Strength'].default_value = 1
    out = nodes.new('ShaderNodeOutputMaterial')
    light_path = nodes.new('ShaderNodeLightPath')
    transparent = nodes.new('ShaderNodeBsdfTransparent')
    mix = nodes.new('ShaderNodeMixShader')
    mat.node_tree.links.new(light_path.outputs['Is Shadow Ray'], mix.inputs[0])
    mat.node_tree.links.new(emission.outputs[0], mix.inputs[1])
    mat.node_tree.links.new(transparent.outputs[0], mix.inputs[2])
    mat.node_tree.links.new(mix.outputs[0], out.inputs['Surface'])
    mat.diffuse_color = rgb
    return mat


def linear_hex(value):
    channels = [int(value[i:i+2], 16) / 255 for i in (0,2,4)]
    return tuple(c/12.92 if c <= 0.04045 else ((c+.055)/1.055)**2.4 for c in channels) + (1,)


outer_ink = ink_material('SS_Ink_Outer_Navy', linear_hex('112636'), True)
panel_ink = ink_material('SS_Ink_Panel_Navy', linear_hex('213D50'))

bm = bmesh.new(); bm.from_mesh(ship.data); bm.normal_update()
bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
visited = set(); parts = []
for vert in bm.verts:
    if vert in visited:
        continue
    stack = [vert]; visited.add(vert); verts = []
    while stack:
        current = stack.pop(); verts.append(current)
        for edge in current.link_edges:
            other = edge.other_vert(current)
            if other not in visited:
                visited.add(other); stack.append(other)
    faces = {f for v in verts for f in v.link_faces}
    area = sum(f.calc_area() for f in faces)
    parts.append((verts, faces, area))

crease_segments = []; boundary_segments = []; selected_vertices = set(); edge_keys = set()
for verts, faces, area in parts:
    if area < 350:
        continue
    selected_vertices.update(v.index for v in verts)
    for edge in {e for v in verts for e in v.link_edges}:
        length = edge.calc_length()
        if length < 2.8 or not edge.link_faces:
            continue
        if all(f.material_index == 3 for f in edge.link_faces):
            continue
        if area < 900 and all(f.material_index == 2 for f in edge.link_faces):
            continue
        is_boundary = edge.is_boundary
        is_crease = edge.is_manifold and edge.calc_face_angle() > math.radians(28)
        if not (is_boundary or is_crease):
            continue
        if max(f.calc_area() for f in edge.link_faces) < 8:
            continue
        key = tuple(sorted(tuple(round(c, 2) for c in v.co) for v in edge.verts))
        if key in edge_keys:
            continue
        edge_keys.add(key)
        n = sum((f.normal for f in edge.link_faces), Vector())
        if n.length > 1e-8:
            n.normalize()
        segment = [v.co + n * 0.055 for v in edge.verts]
        (boundary_segments if is_boundary else crease_segments).append(segment)


def make_ink_curves(name, segments, radius):
    data = bpy.data.curves.new(name, 'CURVE')
    data.dimensions = '3D'; data.resolution_u = 1
    data.bevel_depth = radius; data.bevel_resolution = 1
    data.use_fill_caps = True
    data.materials.append(panel_ink)
    for start, end in segments:
        spline = data.splines.new('POLY'); spline.points.add(1)
        spline.points[0].co = (*start, 1); spline.points[1].co = (*end, 1)
    obj = bpy.data.objects.new(name, data); collection.objects.link(obj)
    obj.matrix_world = ship.matrix_world.copy()
    obj['owner'] = OWNER; obj['line_radius_m'] = radius
    obj.visible_shadow = False
    return obj


crease_obj = make_ink_curves('SS_Ink_Armor_Creases', crease_segments, 0.135)
boundary_obj = make_ink_curves('SS_Ink_Panel_Boundaries', boundary_segments, 0.19)

# Reverse a separate shell and offset along its inward normals. Back-face culling
# exposes the widened silhouette while the original colored hull hides its interior.
shell_mesh = ship.data.copy(); shell_mesh.name = 'SS_Ink_Silhouette_Mesh'
shell_bm = bmesh.new(); shell_bm.from_mesh(shell_mesh); shell_bm.verts.ensure_lookup_table()
bmesh.ops.delete(shell_bm, geom=[v for v in shell_bm.verts if v.index not in selected_vertices], context='VERTS')
bmesh.ops.reverse_faces(shell_bm, faces=list(shell_bm.faces))
for face in shell_bm.faces:
    face.material_index = 0
shell_bm.to_mesh(shell_mesh); shell_bm.free(); bm.free()
shell_mesh.materials.clear(); shell_mesh.materials.append(outer_ink)
shell = bpy.data.objects.new('SS_Ink_Outer_Contour', shell_mesh); collection.objects.link(shell)
shell.matrix_world = ship.matrix_world.copy(); shell['owner'] = OWNER
shell.visible_shadow = False
offset = shell.modifiers.new('Ink_Width_0.42m', 'DISPLACE')
offset.direction = 'NORMAL'; offset.strength = -0.42; offset.mid_level = 0
shell['width_m'] = 0.42

# Keep the existing original/stylized comparison useful after adding the ink.
compare_scene = bpy.data.scenes.get('Ship_Compare_Original_Stylized')
compare_ship = bpy.data.objects.get('SS_Compare_Stylized')
if compare_scene and compare_ship:
    assert compare_ship.parent is None and ship.parent is None
    # matrix_basis reads the saved local transform even before an inactive scene's
    # dependency graph has evaluated matrix_world.
    transform = compare_ship.matrix_basis @ ship.matrix_basis.inverted()
    for obj in list(collection.objects):
        copy = obj.copy(); copy.name = obj.name + '_Compare'
        compare_scene.collection.objects.link(copy)
        copy.matrix_world = transform @ obj.matrix_world

after_hash = geometry_hash(ship.data)
assert before_hash == after_hash, 'The ink layer must not edit the colored hull mesh.'
fill = bpy.data.objects.get('SS_Fill_Sun')
if fill and fill.type == 'LIGHT':
    fill.data.use_shadow = False
report = {
    'owner': OWNER, 'source_geometry_unchanged': before_hash == after_hash,
    'source_geometry_sha256': after_hash, 'ship_triangles': sum(len(p.vertices)-2 for p in ship.data.polygons),
    'crease_segments': len(crease_segments), 'boundary_segments': len(boundary_segments),
    'crease_radius_m': 0.135, 'boundary_radius_m': 0.19, 'silhouette_offset_m': 0.42,
    'collection': collection.name, 'objects': [o.name for o in collection.objects],
    'baseline_blend': str(baseline),
    'ink_shadows': 'transparent to shadow rays', 'studio_fill_shadows': False,
    'implementation': 'Camera-independent inverted shell plus selected geometric crease/boundary curves; no triangulation wireframe.',
}
(ROOT/'lineart_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'GuLiStrike_Ship_AnimeStudy.blend'))
result = report

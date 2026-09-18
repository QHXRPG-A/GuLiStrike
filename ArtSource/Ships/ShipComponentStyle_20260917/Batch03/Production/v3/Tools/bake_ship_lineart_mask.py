"""Bake the existing Ship ink curves to a non-color mask, keeping the outline shell.

Run on the geometric-line backup in background Blender. Rasterize the exact
selected structural edges in world units, with antialiasing and UV padding.
The delivered study retains its EEVEE toon materials.
"""
import bpy
import hashlib
import json
import math
import struct
from pathlib import Path
from mathutils import Vector

import numpy as np

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipStylizedStudy_20260916')
UV_NAME = 'SS_LineartUV'
IMAGE_NAME = 'SS_Ship_Internal_LineMask_4K'
MASK_PATH = ROOT / 'Textures' / 'T_Ship_Internal_LineMask_4K.png'
UV_PATH = ROOT / 'lineart_uv.npz'
SIZE = 4096
CURVE_NAMES = ('SS_Ink_Armor_Creases', 'SS_Ink_Panel_Boundaries')


def geometry_hash(mesh):
    digest = hashlib.sha256()
    for v in mesh.vertices:
        digest.update(struct.pack('<3f', *v.co))
    for p in mesh.polygons:
        digest.update(struct.pack('<I', len(p.vertices)))
        digest.update(struct.pack('<' + 'I' * len(p.vertices), *p.vertices))
    return digest.hexdigest()


def select_only(scene, objects, active):
    for obj in scene.objects:
        obj.select_set(False)
    for obj in objects:
        obj.select_set(True)
    scene.view_layers[0].objects.active = active


def install_mask(ship, image):
    """Mix the unlit ink color after toon lighting, matching the original curves."""
    rgb = [int('213D50'[i:i+2], 16) / 255 for i in (0, 2, 4)]
    ink = tuple(c / 12.92 if c <= 0.04045 else ((c + .055) / 1.055) ** 2.4 for c in rgb) + (1,)
    for mat in ship.data.materials:
        # The cyan engine material has no panel ink and stays emissive.
        if mat.name == 'SS_Engine_Cyan':
            continue
        nodes, links = mat.node_tree.nodes, mat.node_tree.links
        assert not nodes.get('SS_Baked_Line_Mix'), 'Mask already installed.'
        emission = next(n for n in nodes if n.type == 'EMISSION')
        original = emission.inputs['Color'].links[0].from_socket
        uv = nodes.new('ShaderNodeUVMap')
        uv.name = 'SS_Lineart_UV'; uv.label = 'Dedicated non-overlapping line UV'
        uv.uv_map = UV_NAME; uv.location = (-280, -500)
        texture = nodes.new('ShaderNodeTexImage')
        texture.name = 'SS_Internal_Line_Mask'; texture.label = 'White = navy ink; black = paint'
        texture.image = image; texture.interpolation = 'Linear'; texture.extension = 'EXTEND'
        texture.location = (-50, -500)
        strength = nodes.new('ShaderNodeValue')
        strength.name = 'SS_Lineart_Strength'; strength.label = 'Internal line opacity (0 to 1)'
        strength.outputs[0].default_value = 1.0; strength.location = (-30, -220)
        factor = nodes.new('ShaderNodeMath'); factor.operation = 'MULTIPLY'; factor.use_clamp = True
        factor.name = 'SS_Lineart_Factor'; factor.location = (230, -220)
        mix = nodes.new('ShaderNodeMixRGB'); mix.blend_type = 'MIX'
        mix.name = 'SS_Baked_Line_Mix'; mix.label = 'Paint + baked panel ink'
        mix.location = (420, 0); mix.inputs[2].default_value = ink
        links.new(uv.outputs['UV'], texture.inputs['Vector'])
        links.new(texture.outputs['Color'], factor.inputs[0])
        links.new(strength.outputs[0], factor.inputs[1])
        links.new(factor.outputs[0], mix.inputs[0])
        links.new(original, mix.inputs[1])
        links.new(mix.outputs[0], emission.inputs['Color'])
    for name in CURVE_NAMES:
        for suffix in ('', '_Compare'):
            obj = bpy.data.objects.get(name + suffix)
            if obj:
                assert obj.get('owner') == 'ShipStylizedLineart_20260916'
                bpy.data.objects.remove(obj, do_unlink=True)
    ship['internal_lineart'] = 'Packed 4096x4096 non-color mask; no internal curve geometry'
    ship['lineart_mask_uv'] = UV_NAME
    ship['lineart_mask'] = '//Textures/' + MASK_PATH.name


def selected_edge_faces(ship):
    from mathutils.kdtree import KDTree
    mesh = ship.data
    kd = KDTree(len(mesh.vertices))
    for v in mesh.vertices:
        kd.insert(v.co, v.index)
    kd.balance()
    edges = {}
    for name in CURVE_NAMES:
        obj = bpy.data.objects[name]
        for spline in obj.data.splines:
            ends = []
            for point in spline.points:
                co, index, distance = kd.find(point.co.xyz)
                assert distance < .057
                ends.append(tuple(round(c, 4) for c in co))
            edges[tuple(sorted(ends))] = obj.data.bevel_depth
    matched = set(); face_edges = {}
    for polygon in mesh.polygons:
        coords = [tuple(round(c, 4) for c in mesh.vertices[v].co) for v in polygon.vertices]
        matches = []
        for a, b in zip(coords, coords[1:] + coords[:1]):
            key = tuple(sorted((a, b)))
            if key in edges:
                matched.add(key)
                matches.append((np.array(a, dtype=np.float32), np.array(b, dtype=np.float32), edges[key]))
        if matches:
            face_edges[polygon.index] = matches
    assert len(matched) == len(edges), (len(matched), len(edges))
    return face_edges, len(edges)


def raster_mask(mesh, uv, face_edges):
    """Bake constant world-space line widths onto adjacent armor faces.

    Every sample is inside its real face and uses distance to the original
    selected edge, avoiding projection misses at hard corners or smoothed cages.
    """
    mesh.calc_loop_triangles()
    hits = np.zeros((SIZE, SIZE), dtype=np.float32)
    coverage = np.zeros((SIZE, SIZE), dtype=np.float32)
    triangles = [t for t in mesh.loop_triangles if t.polygon_index in face_edges]
    offsets = (.125, .375, .625, .875)
    for index, triangle in enumerate(triangles):
        coords = np.array([uv.data[i].uv[:] for i in triangle.loops], dtype=np.float32) * SIZE
        vertices = np.array([mesh.vertices[v].co[:] for v in triangle.vertices], dtype=np.float32)
        uv_a, uv_b = coords[1] - coords[0], coords[2] - coords[0]
        determinant = uv_a[0] * uv_b[1] - uv_a[1] * uv_b[0]
        if abs(determinant) < 1e-7:
            continue
        low = np.maximum(np.floor(coords.min(axis=0)).astype(int), 0)
        high = np.minimum(np.ceil(coords.max(axis=0)).astype(int), SIZE)
        x0, y0 = low; x1, y1 = high
        if x0 == x1 or y0 == y1:
            continue
        grid_x = np.arange(x0, x1, dtype=np.float32)[None, :]
        grid_y = np.arange(y0, y1, dtype=np.float32)[:, None]
        local_hits = np.zeros((y1-y0, x1-x0), dtype=np.float32)
        local_coverage = np.zeros_like(local_hits)
        edges = face_edges[triangle.polygon_index]
        for ox in offsets:
            for oy in offsets:
                dx, dy = grid_x + ox - coords[0, 0], grid_y + oy - coords[0, 1]
                b = (dx * uv_b[1] - dy * uv_b[0]) / determinant
                c = (uv_a[0] * dy - uv_a[1] * dx) / determinant
                inside = (b >= 0) & (c >= 0) & (b+c <= 1)
                if not inside.any():
                    continue
                points = vertices[0] + b[inside, None] * (vertices[1] - vertices[0]) + c[inside, None] * (vertices[2] - vertices[0])
                ink = np.zeros(len(points), dtype=bool)
                for start, end, radius in edges:
                    vector = end - start
                    t = np.clip(np.sum((points-start) * vector, axis=1) / np.dot(vector, vector), 0, 1)
                    delta = points - (start + t[:, None] * vector)
                    ink |= np.sum(delta * delta, axis=1) <= radius * radius
                local_hits[inside] += ink
                local_coverage += inside
        hits[y0:y1, x0:x1] += local_hits
        coverage[y0:y1, x0:x1] += local_coverage
        if index % 500 == 0:
            print(f'SHIP_MASK_RASTER {index}/{len(triangles)}', flush=True)
    filled = coverage > 0
    mask = np.divide(hits, coverage, out=np.zeros_like(hits), where=filled)
    coverage_fraction = float(filled.mean())
    # Extend nearest boundary colors only outside UV faces, including black, so
    # neighboring UV islands cannot inherit white ink indiscriminately.
    for _ in range(8):
        weights = np.zeros_like(mask)
        values = np.zeros_like(mask)
        for axis, shift in ((0, 1), (0, -1), (1, 1), (1, -1)):
            neighbor = np.roll(filled, shift, axis=axis)
            weights += neighbor
            values += np.roll(mask, shift, axis=axis) * neighbor
        new = (~filled) & (weights > 0)
        mask[new] = values[new] / weights[new]
        filled |= new
    return mask, coverage_fraction


def bake():
    ship = bpy.data.objects['SS_Dreadnought_Stylized']
    study = bpy.data.scenes['Ship_Stylized_Study']
    before = geometry_hash(ship.data)
    outline_before = geometry_hash(bpy.data.objects['SS_Ink_Outer_Contour'].data)
    original_scene = bpy.context.window.scene
    old_active_uv = ship.data.uv_layers.active_index
    old_render_uv = next(u.name for u in ship.data.uv_layers if u.active_render)
    assert UV_NAME not in ship.data.uv_layers
    face_edges, edge_count = selected_edge_faces(ship)
    bpy.context.window.scene = study
    select_only(study, [ship], ship)
    uv = ship.data.uv_layers.new(name=UV_NAME)
    ship.data.uv_layers.active = uv
    # Only surfaces bearing ink need atlas space. All other faces sample a
    # reserved black texel; the original three UV channels remain untouched.
    for v in ship.data.vertices:
        v.select = False
    for e in ship.data.edges:
        e.select = False
    for p in ship.data.polygons:
        p.select = p.index in face_edges
    bpy.context.tool_settings.mesh_select_mode = (False, False, True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), margin_method='FRACTION',
                             island_margin=0.006, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    uv = ship.data.uv_layers[UV_NAME]
    for p in ship.data.polygons:
        for loop in p.loop_indices:
            if p.index in face_edges:
                uv.data[loop].uv = uv.data[loop].uv * .96 + Vector((.02, .02))
            else:
                uv.data[loop].uv = (.005, .005)
    coords = np.empty(len(uv.data) * 2, dtype=np.float32)
    uv.data.foreach_get('uv', coords)
    np.savez_compressed(UV_PATH, uv=coords)
    assert geometry_hash(ship.data) == before
    print(f'SHIP_MASK_UV_READY {len(face_edges)} faces / {edge_count} edges', flush=True)
    mask, coverage_fraction = raster_mask(ship.data, uv, face_edges)
    stats = {'minimum': float(mask.min()), 'maximum': float(mask.max()),
             'white_pixel_fraction': float((mask > .5).mean()),
             'nonzero_pixel_fraction': float((mask > .01).mean()),
             'atlas_face_coverage': coverage_fraction}
    assert stats['maximum'] > .9 and 0.0001 < stats['white_pixel_fraction'] < .5, stats
    image = bpy.data.images.new(IMAGE_NAME, width=SIZE, height=SIZE, alpha=False, float_buffer=True)
    image.colorspace_settings.name = 'Non-Color'
    pixels = np.ones((SIZE, SIZE, 4), dtype=np.float32)
    pixels[:, :, :3] = mask[:, :, None]
    image.pixels.foreach_set(pixels.ravel()); image.update()
    scene = bpy.data.scenes.new('SS_TEMP_LineMask_Save')
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'BW'
    scene.render.image_settings.color_depth = '8'
    scene.view_settings.view_transform = 'Raw'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0; scene.view_settings.gamma = 1
    MASK_PATH.parent.mkdir(parents=True, exist_ok=True)
    image.save_render(str(MASK_PATH), scene=scene)
    bpy.data.scenes.remove(scene); bpy.data.images.remove(image)
    image = bpy.data.images.load(str(MASK_PATH), check_existing=False)
    image.name = IMAGE_NAME; image.colorspace_settings.name = 'Non-Color'
    image.pack(); image.filepath = '//Textures/' + MASK_PATH.name
    install_mask(ship, image)
    ship.data.uv_layers.active_index = old_active_uv
    ship.data.uv_layers[old_render_uv].active_render = True
    assert geometry_hash(ship.data) == before
    assert geometry_hash(bpy.data.objects['SS_Ink_Outer_Contour'].data) == outline_before
    study.view_layers[0].update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    counts = []
    for name in (ship.name, 'SS_Ink_Outer_Contour'):
        obj = bpy.data.objects[name]
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh(); mesh.calc_loop_triangles()
        counts.append({'name': name, 'vertices': len(mesh.vertices),
                       'polygons': len(mesh.polygons), 'triangles': len(mesh.loop_triangles)})
        evaluated.to_mesh_clear()
    report = {'success': True, 'method': 'Selected structural edge distance rasterized into UV space',
              'resolution': [SIZE, SIZE], 'mask': str(MASK_PATH), 'uv_layer': UV_NAME,
              'mask_convention': 'white=ink, black=paint', 'color_space': 'Non-Color',
              'mask_stats': stats, 'antialias_samples_per_texel': 16, 'padding_pixels': 8,
              'selected_edges': edge_count, 'atlas_faces': len(face_edges),
              'line_radii_m': [.135, .19], 'unlined_faces': 'reserved black texel',
              'body_geometry_sha256': before, 'body_geometry_unchanged': True,
              'outline_geometry_sha256': outline_before, 'outline_geometry_unchanged': True,
              'objects': counts, 'total_triangles': sum(x['triangles'] for x in counts),
              'previous_triangles': 37362, 'removed_triangles': 20960,
              'source_backup': str(ROOT / 'GuLiStrike_Ship_AnimeStudy_GeometricLines.blend')}
    (ROOT / 'lineart_bake_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    bpy.context.window.scene = original_scene
    select_only(study, [ship], ship)
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / 'GuLiStrike_Ship_AnimeStudy_BakedCandidate.blend'))
    print('SHIP_MASK_BAKE_SUCCESS ' + json.dumps(report), flush=True)


if __name__ == '__main__':
    bake()



"""Bake the approved v5 to explicit UE mesh descriptions and dense vertex atlases.

Run with Blender --background --python. Never saves or edits the reviewed blend.
No rendering, screenshots or visual inspection. Positions and normals are data.
"""
import bpy
import json
import math
import hashlib
import struct
import zlib
from pathlib import Path
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'quadruped-mech-side-panels/blender/quadruped-mech-vertex-animation-10k-v5.blend'
SHA = '9b2fd48e472117d6c2dfed25e437305137b1b1b2a1c9c2f5ca8c4e9cb3ca454c'
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
WIDTH = 2048
PRESENTATION_SIZE_MULTIPLIER = 2.5  # User's 2026-11-13 in-game size correction.
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SHA
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'Textures').mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE), load_ui=False)
SCENES = {s.name[:2]: s for s in bpy.data.scenes}


def key(obj):
    body = obj.parent if obj.get('is_lineart') else obj
    return (body.get('asset_source_name', body.name), bool(obj.get('is_lineart')))


def objects(scene):
    return {key(o): o for o in scene.objects if o.type == 'MESH'
            and 'Studio ground' not in key(o)[0] and 'Laser' not in key(o)[0]}


def array(collection, field, size):
    result = np.empty(len(collection) * size, np.float32)
    collection.foreach_get(field, result)
    return result.reshape((-1, size))


def read(obj, dg):
    ev = obj.evaluated_get(dg)
    mesh = ev.to_mesh(preserve_all_data_layers=True, depsgraph=dg)
    mesh.calc_loop_triangles()
    mat = np.asarray(ev.matrix_world, dtype=np.float64)
    positions = array(mesh.vertices, 'co', 3) @ mat[:3, :3].T + mat[:3, 3]
    normals = array(mesh.corner_normals, 'vector', 3) @ np.linalg.inv(mat[:3, :3])
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    tris = np.array([t.vertices[:] for t in mesh.loop_triangles], dtype=np.int32)
    loops = np.array([t.loops[:] for t in mesh.loop_triangles], dtype=np.int32)
    slots = [mesh.polygons[t.polygon_index].material_index for t in mesh.loop_triangles]
    result = dict(p=positions, n=normals[loops], t=tris, slots=slots,
                  materials=list(mesh.materials), lamp=obj.get('team_light_part') == 'Lens',
                  protected=bool(obj.get('is_team_light')), outline=bool(obj.get('is_lineart')))
    ev.to_mesh_clear()
    return result


static = SCENES['00']
bpy.context.window.scene = static
static.frame_set(1)
source_objects = objects(static)
ordered = sorted(source_objects)
rest = {k: read(source_objects[k], bpy.context.evaluated_depsgraph_get()) for k in ordered}
assert sum(len(m['t']) for m in rest.values()) == 9854
assert sum(1 for m in rest.values() if m['lamp']) == 4
dense = {k: [rest[k]['p']] for k in ordered}
dense_normals = {k: [rest[k]['n']] for k in ordered}
for prefix, count in [('01', 29), ('02', 73)]:
    scene = SCENES[prefix]
    bpy.context.window.scene = scene
    obs = objects(scene)
    assert set(obs) == set(ordered), (prefix, set(obs).symmetric_difference(ordered))
    for frame in range(1, count + 1):
        scene.frame_set(frame)
        dg = bpy.context.evaluated_depsgraph_get()
        for k in ordered:
            m = read(obs[k], dg)
            assert np.array_equal(m['t'], rest[k]['t']), (prefix, frame, k)
            dense[k].append(m['p'])
            dense_normals[k].append(m['n'])
    print('SAMPLED', prefix, count, flush=True)
dense = {k: np.asarray(v, np.float32) for k, v in dense.items()}
dense_normals = {k: np.asarray(v, np.float32) for k, v in dense_normals.items()}
FRAMES = 103
assert all(len(v) == FRAMES for v in dense.values())
# UE +X is Blender -Y; UE +Y is Blender +X. This rotation preserves numeric
# handedness. Reverse triangle corners below for UE's clockwise front faces.
AXES = np.array([[0, -1, 0], [1, 0, 0], [0, 0, 1]], np.float64)
all_rest = np.concatenate([m['p'] for k, m in rest.items() if not k[1]]) @ AXES.T * 100
origin = np.array([(all_rest[:, 0].min() + all_rest[:, 0].max()) / 2, 0, all_rest[:, 2].min()])
rest_length = float(np.ptp(all_rest[:, 0]))


def to_ue(p):
    return (p @ AXES.T * 100 - origin).astype(np.float32)


def simplify(m, ratio):
    if m['protected'] or ratio == 1 or len(m['t']) <= 3:
        return m['p'], m['t'], m['slots']
    mesh = bpy.data.meshes.new('VAT_LowerLOD_Work')
    mesh.from_pydata(m['p'].tolist(), [], m['t'].tolist())
    for material in m['materials']:
        mesh.materials.append(material)
    for poly, slot in zip(mesh.polygons, m['slots']):
        poly.material_index = slot
    obj = bpy.data.objects.new('VAT_LowerLOD_Work', mesh)
    bpy.context.scene.collection.objects.link(obj)
    modifier = obj.modifiers.new('LOD reduction', 'DECIMATE')
    modifier.ratio = ratio
    modifier.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    reduced = ev.to_mesh()
    reduced.calc_loop_triangles()
    result = (array(reduced.vertices, 'co', 3).copy(),
              np.array([t.vertices[:] for t in reduced.loop_triangles], np.int32),
              [reduced.polygons[t.polygon_index].material_index for t in reduced.loop_triangles])
    ev.to_mesh_clear()
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(mesh)
    return result


def animate_reduction(k, points):
    m = rest[k]
    tree = BVHTree.FromPolygons(m['p'].tolist(), m['t'].tolist(), all_triangles=True)
    indices, coefficients = [], []
    for p in points:
        _, _, index, _ = tree.find_nearest(Vector(p))
        tri = m['t'][index]
        a, b, c = m['p'][tri]
        normal = np.cross(b - a, c - a)
        normal /= max(np.linalg.norm(normal), 1e-12)
        basis = np.stack([b - a, c - a, normal], axis=1)
        coeff = np.linalg.lstsq(basis, p - a, rcond=None)[0]
        indices.append(tri)
        coefficients.append(coeff)
    p = dense[k][:, np.array(indices)]
    a, b, c = p[:, :, 0], p[:, :, 1], p[:, :, 2]
    n = np.cross(b - a, c - a)
    n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-12)
    coef = np.asarray(coefficients)
    return a + (b - a) * coef[None, :, :1] + (c - a) * coef[None, :, 1:2] + n * coef[None, :, 2:3]


def png(path, rgba):
    height, width, _ = rgba.shape
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    raw = b''.join(b'\0' + row.tobytes() for row in rgba)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def atlas(path, values, position):
    rows = math.ceil(values.shape[1] / WIDTH)
    pixels = np.zeros((FRAMES, rows * WIDTH, 4), np.float32)
    pixels[:, :values.shape[1], :3] = values
    pixels[:, :, 3] = 1
    pixels = pixels.reshape((FRAMES * rows, WIDTH, 4))
    if position:
        im = bpy.data.images.new(path.stem, width=WIDTH, height=len(pixels), alpha=True, float_buffer=True)
        im.colorspace_settings.name = 'Non-Color'
        im.pixels.foreach_set(pixels.ravel())
        scene = bpy.context.scene
        scene.render.image_settings.file_format = 'OPEN_EXR'
        scene.render.image_settings.color_depth = '16'
        scene.render.image_settings.exr_codec = 'ZIP'
        im.save_render(str(path), scene=scene)
        bpy.data.images.remove(im)
        # Numeric half-float readback; this is texture-data validation, not an image review.
        loaded = bpy.data.images.load(str(path), check_existing=False)
        loaded.colorspace_settings.name = 'Non-Color'
        data = np.asarray(loaded.pixels[:]).reshape(pixels.shape)
        error = float(np.max(np.abs(data - pixels)))
        assert error <= .26, error
        bpy.data.images.remove(loaded)
    else:
        png(path, np.round(np.clip(pixels * .5 + .5, 0, 1) * 255).astype(np.uint8))
        error = 1 / 255
    return dict(rows=rows, height=len(pixels), width=WIDTH, error=error)


def palette(material):
    # Read the authored fixed RGB node ahead of the three-tone multiply.
    rgb = next((n.outputs[0].default_value[:3] for n in material.node_tree.nodes if n.type == 'RGB'), None) if material.use_nodes else None
    if rgb is None:
        rgb = material.diffuse_color[:3]
    # MeshDescription stores LINEAR colors. UE StaticMeshBuilder applies
    # ToFColor(true), so quantize the desired sRGB bytes then decode them here.
    # The material decodes the resulting FColor once, preserving the source palette.
    srgb = [round((12.92*x if x <= .0031308 else 1.055*x**(1/2.4)-.055)*255)/255 for x in rgb]
    return [x/12.92 if x <= .04045 else ((x+.055)/1.055)**2.4 for x in srgb]


metadata = dict(source=str(SOURCE), source_sha256=SHA, total_frames=FRAMES, fps=24,
                rest_length_cm=rest_length, base_presentation_scale=360 / rest_length,
                presentation_size_multiplier=PRESENTATION_SIZE_MULTIPLIER,
                presentation_scale=360 / rest_length * PRESENTATION_SIZE_MULTIPLIER, origin_cm=origin.tolist(),
                walk_cycle_distance_cm=120 / .62, source_walk_duration=28 / 24,
                clips=[dict(name='Idle', first=0, count=1, duration=1),
                       dict(name='Walk', first=1, count=29, duration=28 / 24),
                       dict(name='Mining', first=30, count=73, duration=3)],
                screen_sizes=[1., .056, .028], lods=[], visual_review='User only; no renders or screenshots')
# Fit the rigid front plate: the muzzle follows exactly the same baked body pose.
body_key = next(k for k in ordered if not k[1] and k[0].startswith('Front_Armor') and len(rest[k]['p']) >= 8)
a = np.column_stack([rest[body_key]['p'], np.ones(len(rest[body_key]['p']))])
muzzle = []
for frame in range(FRAMES):
    transform = np.linalg.lstsq(a, dense[body_key][frame], rcond=None)[0]
    assert np.max(np.abs(a @ transform - dense[body_key][frame])) < .00002
    muzzle.append(to_ue(np.array([0, -2.055, 1.712, 1]) @ transform).tolist())
metadata['muzzle_samples_cm'] = muzzle
metadata['reference_muzzle_cm'] = muzzle[0]

for lod, budget in enumerate([9854, 5000, 3000]):
    ratio = 1 if lod == 0 else [.0, .46, .25][lod]
    while True:
        reduced = {k: simplify(rest[k], ratio) for k in ordered}
        count = sum(len(x[1]) for x in reduced.values())
        if count <= budget:
            break
        ratio *= .93
    print('LOD', lod, 'triangles', count, 'ratio', ratio, flush=True)
    all_p, all_n, triangles, colors, uvs, slots, material_names = [], [], [], [], [[], [], []], [], []
    vertex_offset = normal_offset = 0
    for k in ordered:
        m = rest[k]
        points, tris, source_slots = reduced[k]
        if len(tris) == 0:
            continue
        poses = dense[k] if lod == 0 or m['protected'] else animate_reduction(k, points)
        posed = to_ue(poses)
        if lod == 0 or m['protected']:
            normals = dense_normals[k] @ AXES.T
        else:
            p = poses[:, tris]
            n = np.cross(p[:, :, 1] - p[:, :, 0], p[:, :, 2] - p[:, :, 0]) @ AXES.T
            n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-12)
            normals = np.repeat(n[:, :, None, :], 3, axis=2)
        assert len(tris) == normals.shape[1]
        all_p.append(posed)
        all_n.append(normals.reshape((FRAMES, -1, 3)))
        # Crease lines follow physical edges, never triangulation diagonals.
        adjacent = {}
        face_normals = normals[0, :, 0]
        for face, tri in enumerate(tris):
            for i in range(3):
                adjacent.setdefault(tuple(sorted((int(tri[i]), int(tri[(i + 1) % 3])))), []).append(face)
        marked = {e for e, f in adjacent.items() if len(f) == 2 and
                  (source_slots[f[0]] != source_slots[f[1]] or float(np.dot(face_normals[f[0]], face_normals[f[1]])) < math.cos(math.radians(46)))}
        for face, tri in enumerate(tris):
            slot_name = 'Outline' if m['outline'] else 'Lamp' if m['lamp'] else 'Body'
            if slot_name not in material_names:
                material_names.append(slot_name)
            slot = material_names.index(slot_name)
            triangles.append([*(tri + vertex_offset).tolist(), slot])
            rgb = [0, 0, 0] if m['outline'] else [1, 1, 1] if m['lamp'] else palette(m['materials'][source_slots[face]])
            p = posed[0, tri]
            distances = np.full((3, 3), 999, np.float32)
            if not m['outline'] and not m['protected']:
                n = np.cross(p[1] - p[0], p[2] - p[0]); n /= max(np.linalg.norm(n), 1e-12)
                for e in range(3):
                    if tuple(sorted((int(tri[e]), int(tri[(e + 1) % 3])))) in marked:
                        perp = np.cross(n, p[(e + 1) % 3] - p[e]); perp /= max(np.linalg.norm(perp), 1e-12)
                        distances[:, e] = np.abs((p - p[e]) @ perp)
            for corner, v in enumerate(tri):
                # Integer IDs stay farther apart than UE's UV weld tolerance.
                uvs[0].append([int(v) + vertex_offset, lod])
                uvs[1].append(distances[corner, :2].tolist())
                uvs[2].append([float(distances[corner, 2]), normal_offset + face * 3 + corner])
                colors.append([*rgb, 7 / 255 if m['lamp'] else 0])
        vertex_offset += len(points)
        normal_offset += len(tris) * 3
    positions = np.concatenate(all_p, axis=1)
    normals = np.concatenate(all_n, axis=1)
    assert np.isfinite(positions).all() and np.isfinite(normals).all()
    assert np.max(np.abs(positions[1] - positions[29])) < .001
    assert np.max(np.abs(positions[30] - positions[102])) < .001
    # UE computes its geometric normal with cross(P2-P0, P1-P0). Keep each
    # corner's attributes together; UV2.y still addresses the unchanged atlas.
    corner_order = np.arange(len(triangles) * 3).reshape((-1, 3))[:, [0, 2, 1]].reshape(-1)
    mesh = dict(lod=lod, positions=positions[0].tolist(),
                triangles=[[t[0], t[2], t[1], t[3]] for t in triangles],
                colors=[colors[i] for i in corner_order], normals=normals[0, corner_order].tolist(),
                uvs=[[channel[i] for i in corner_order] for channel in uvs],
                material_names=material_names, winding='ue_clockwise')
    p = positions[0, np.array(mesh['triangles'])[:, :3]]
    geometric = np.cross(p[:, 2] - p[:, 0], p[:, 1] - p[:, 0])
    assert ((geometric * np.array(mesh['normals']).reshape((-1, 3, 3)).mean(axis=1)).sum(axis=1) > 0).all()
    (OUT / f'lod{lod}.json').write_text(json.dumps(mesh, separators=(',', ':')), encoding='utf8')
    info = dict(lod=lod, triangles=len(triangles), vertices=positions.shape[1], normal_count=normals.shape[1],
                position=atlas(OUT / f'Textures/Position_LOD{lod}.exr', positions, True),
                normal=atlas(OUT / f'Textures/Normal_LOD{lod}.png', normals, False),
                bounds_min=positions.min(axis=(0, 1)).tolist(), bounds_max=positions.max(axis=(0, 1)).tolist(),
                lamp_triangles=sum(t[3] == material_names.index('Lamp') for t in triangles),
                materials=material_names)
    metadata['lods'].append(info)
metadata['vertex_rgb_encoding'] = 'linear_mesh_description'
metadata['vat_id_encoding'] = 'integer_full_precision_uv'
metadata['winding'] = 'ue_clockwise'
metadata['success'] = True
assert all(x['lamp_triangles'] == metadata['lods'][0]['lamp_triangles'] for x in metadata['lods'])
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SHA
(OUT / 'metadata.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding='utf8')
print('BAKE_COMPLETE', json.dumps(metadata['lods']), flush=True)

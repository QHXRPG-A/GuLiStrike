"""Create a 10k-triangle production candidate while preserving both vertex clips.
Only a new ArtSource blend is saved. The reviewed source is read-only; no render.
"""
import bpy
import bmesh
import json
import math
import hashlib
from pathlib import Path
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'quadruped-mech-side-panels/blender/quadruped-mech-vertex-animation.blend'
SOURCE_HASH = 'c4d2649b5d6edce48d6475a6988ac8ac1677f6676994288019c5de97be583850'
OUT = ROOT / 'ArtSource/QuadrupedMiner_20261113/Production_10k_v1'
OUT.mkdir(parents=True, exist_ok=True)
DEST = OUT / 'quadruped-mech-production-10k.blend'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SOURCE_HASH
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
STATIC = '00_静态 · 当前资源'
CLIPS = (('01_行进 · 顶点动画', 29), ('02_采矿 · 顶点动画', 73))
ANKLES = {
    'Leg_Front_L': Vector((-1.55, -2.06, .25)), 'Leg_Front_R': Vector((1.55, -2.06, .25)),
    'Leg_Rear_L': Vector((-1.98, 1.96, .25)), 'Leg_Rear_R': Vector((1.98, 1.96, .25)),
}

def name_of(obj):
    if obj.get('is_lineart', False) and obj.parent:
        return obj.parent.get('asset_source_name', obj.parent.name)
    return obj.get('asset_source_name', obj.name)

def is_beam(obj):
    return name_of(obj).startswith('M · Mining Laser')

def bodies(scene):
    return [obj for obj in scene.objects if obj.type == 'MESH' and not obj.get('is_lineart', False)
        and name_of(obj) != 'Studio ground' and not is_beam(obj)]

def arr(collection, prop, size):
    data = np.empty(len(collection) * size, dtype=np.float32)
    collection.foreach_get(prop, data)
    return data.reshape((-1, size))

def read_mesh(obj, scene):
    bpy.context.window.scene = scene
    scene.frame_set(1)
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    mesh = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    mesh.calc_loop_triangles()
    coords = arr(mesh.vertices, 'co', 3)
    normals = arr(mesh.corner_normals, 'vector', 3)
    transform = np.asarray(obj.matrix_world, dtype=np.float32)
    coords = coords @ transform[:3, :3].T + transform[:3, 3]
    rotation = np.array(transform[:3, :3], dtype=np.float32, copy=True)
    rotation /= np.maximum(np.linalg.norm(rotation, axis=0, keepdims=True), 1e-12)
    normals = normals @ rotation.T
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    data = dict(coords=coords, normals=normals,
        polygons=[tuple(poly.vertices) for poly in mesh.polygons],
        loops=[list(tri.loops) for tri in mesh.loop_triangles],
        triangles=[tuple(tri.vertices) for tri in mesh.loop_triangles],
        materials=list(mesh.materials), material_indices=[poly.material_index for poly in mesh.polygons])
    bpy.data.meshes.remove(mesh)
    return data

def dense_keys(obj, scene, count):
    keys = obj.data.shape_keys
    assert keys and not keys.use_relative and len(keys.key_blocks) == count, (obj.name, scene.name)
    saved_time = keys.eval_time
    poses = []
    for index, key in enumerate(keys.key_blocks):
        if obj.modifiers:
            # Source outlines are keyed before Solidify; sample the evaluated
            # outer/inner pair so indices match the reference animation surface.
            keys.eval_time = index * 10.0
            bpy.context.view_layer.update()
            dg = bpy.context.evaluated_depsgraph_get()
            ev = obj.evaluated_get(dg)
            mesh = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=False, depsgraph=dg)
            points = arr(mesh.vertices, 'co', 3)
            transform = np.asarray(ev.matrix_world, dtype=np.float32)
            bpy.data.meshes.remove(mesh)
        else:
            points = arr(key.data, 'co', 3)
            transform = np.asarray(obj.matrix_world, dtype=np.float32)
        poses.append(points @ transform[:3, :3].T + transform[:3, 3])
    keys.eval_time = saved_time
    scene.frame_set(1)
    bpy.context.view_layer.update()
    return np.asarray(poses, dtype=np.float32)

def components(mesh):
    adjacent = [[] for _ in mesh['coords']]
    edges = set()
    for polygon in mesh['polygons']:
        for a, b in zip(polygon, polygon[1:] + polygon[:1]):
            adjacent[a].append(b)
            adjacent[b].append(a)
    visited = set()
    result = []
    for seed in range(len(adjacent)):
        if seed in visited:
            continue
        stack = [seed]
        visited.add(seed)
        ids = []
        while stack:
            a = stack.pop()
            ids.append(a)
            for b in adjacent[a]:
                if b not in visited:
                    visited.add(b)
                    stack.append(b)
        ids = set(ids)
        polygons = [poly for poly in mesh['polygons'] if poly[0] in ids]
        result.append((sorted(ids), polygons))
    return result

def subset(mesh, ids, polygons):
    remap = {old: new for new, old in enumerate(ids)}
    data = bpy.data.meshes.new('TEMP_ProductionPart')
    data.from_pydata(mesh['coords'][ids].tolist(), [], [tuple(remap[i] for i in poly) for poly in polygons])
    data.update()
    for material in mesh['materials']:
        data.materials.append(material)
    lookup = {poly: index for poly, index in zip(mesh['polygons'], mesh['material_indices'])}
    for polygon in data.polygons:
        old = polygons[polygon.index]
        polygon.material_index = lookup[old]
        polygon.use_smooth = True
    return data

def decimate_island(mesh, ids, polygons, target, protect):
    source_triangles = sum(len(poly) - 2 for poly in polygons)
    reduced = source_triangles > target
    data = subset(mesh, ids, polygons)
    obj = None
    if reduced:
        obj = bpy.data.objects.new('TEMP_ProductionIsland', data)
        bpy.context.scene.collection.objects.link(obj)
        mod = obj.modifiers.new('Budgeted regular components', 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.use_collapse_triangulate = True
        mod.ratio = max(.0001, min(1.0, target / source_triangles))
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.modifier_apply(modifier=mod.name)
        data = obj.data
    # Clean only coincident/zero-area artifacts inside one source component.
    # Never weld separate mechanical islands together.
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-7)
    bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
    bmesh.ops.delete(bm, geom=[face for face in bm.faces if face.calc_area() < 1e-12], context='FACES')
    bmesh.ops.delete(bm, geom=[edge for edge in bm.edges if not edge.link_faces], context='EDGES')
    bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context='VERTS')
    assert bm.faces and bm.verts, 'Degenerate-only source island needs explicit review'
    bm.normal_update()
    bm.to_mesh(data)
    bm.free()
    data.update()
    data.calc_loop_triangles()
    count = len(data.loop_triangles)
    assert count > 0 and len(data.vertices) > 0, 'A mechanical island was removed'
    if obj:
        bpy.data.objects.remove(obj, do_unlink=True)
    return data, count, reduced

def category(name):
    if '液压杆' in name:
        return 'hydraulic'
    if '关节枪铁' in name:
        return 'joint'
    if '橡胶与线缆' in name:
        return 'cable'
    return 'armor'

def optimize_body(obj, mesh, factor):
    prefix = name_of(obj).split(' · ')[0]
    ankle = ANKLES.get(prefix)
    pieces = []
    target_count = 0
    for ids, polygons in components(mesh):
        count = sum(len(poly) - 2 for poly in polygons)
        points = mesh['coords'][ids]
        center = Vector(points.mean(axis=0).tolist())
        contact = bool(ankle and '黑色主体框架' in name_of(obj) and center.z < .25)
        local_factor = max(factor, .18) if contact else factor
        target = count if count <= 40 or contact and count <= 240 else max(12, round(count * local_factor))
        target = min(count, target)
        data, actual, reduced = decimate_island(mesh, ids, polygons, target, contact)
        pieces.append(data)
        target_count += actual
    # Join through bmesh without welding distinct mechanical components.
    bm = bmesh.new()
    material_list = []
    for piece in pieces:
        for material in piece.materials:
            if material not in material_list:
                material_list.append(material)
    for piece in pieces:
        layer = bm.verts.layers.int.new('source_material') if 'source_material' not in bm.verts.layers.int else bm.verts.layers.int['source_material']
        part = bmesh.new()
        part.from_mesh(piece)
        part.verts.ensure_lookup_table()
        part.faces.ensure_lookup_table()
        mapping = {}
        for vertex in part.verts:
            mapping[vertex] = bm.verts.new(vertex.co)
        for face in part.faces:
            try:
                created = bm.faces.new([mapping[vertex] for vertex in face.verts])
                created.material_index = material_list.index(piece.materials[face.material_index])
                created.smooth = face.smooth
            except ValueError:
                raise AssertionError(('Duplicate geometry after decimation', name_of(obj)))
        part.free()
        bpy.data.meshes.remove(piece)
    bm.to_mesh(result := bpy.data.meshes.new(name_of(obj) + '_ProductionMesh'))
    bm.free()
    # bmesh stores face material indices, not the object's material slots.
    # Restore the source palette explicitly on the joined production mesh.
    for material in material_list:
        result.materials.append(material)
    result.update()
    result.calc_loop_triangles()
    assert len(result.loop_triangles) == target_count, (name_of(obj), len(result.loop_triangles), target_count)
    assert len(components(dict(coords=arr(result.vertices, 'co', 3), polygons=[tuple(p.vertices) for p in result.polygons]))) == len(components(mesh))
    return result

def projection(target, reference, tree, face_indices=None):
    targets = np.asarray(target, dtype=np.float64)
    triangles = np.asarray(reference['triangles'], dtype=np.int32)
    coords = np.asarray(reference['coords'], dtype=np.float64)
    if '_projection_vertex_lookup' not in reference:
        from mathutils.kdtree import KDTree
        kd = KDTree(len(coords))
        for index, point in enumerate(coords):
            kd.insert(point.tolist(), index)
        kd.balance()
        valid_faces = set(int(index) for index in (range(len(triangles)) if face_indices is None else face_indices))
        incident = [[] for _ in coords]
        for face in valid_faces:
            for vertex in triangles[face]:
                incident[vertex].append(face)
        reference['_projection_vertex_lookup'] = (kd, incident)
    kd, incident = reference['_projection_vertex_lookup']
    indices = np.empty(len(targets), dtype=np.int32)
    weights = np.empty((len(targets), 3), dtype=np.float32)
    for i, point in enumerate(targets):
        nearest, _, face, distance = tree.find_nearest(Vector(point.tolist()))
        assert face is not None, i
        _, closest_vertex, vertex_distance = kd.find(point.tolist())
        if vertex_distance + 1e-6 < distance and incident[closest_vertex]:
            # A collapse endpoint can belong to a degenerate source face.
            # Sample its exact source vertex, not a distant neighboring surface.
            assert vertex_distance < .5, {'vertex': i, 'target': point.tolist(), 'vertex_distance_m': vertex_distance}
            source_face = incident[closest_vertex][0]
            a, b, c = triangles[source_face]
            indices[i] = source_face
            weights[i] = (float(closest_vertex == a), float(closest_vertex == b), float(closest_vertex == c))
            continue
        assert distance < .5, {'vertex': i, 'target': point.tolist(), 'nearest': list(nearest) if nearest else None, 'distance_m': distance}
        nearest = np.asarray(nearest, dtype=np.float64)
        source_face = int(face if face_indices is None else face_indices[face])
        a, b, c = triangles[source_face]
        v0, v1 = coords[b] - coords[a], coords[c] - coords[a]
        d00, d01, d11 = np.dot(v0, v0), np.dot(v0, v1), np.dot(v1, v1)
        d20, d21 = np.dot(nearest - coords[a], v0), np.dot(nearest - coords[a], v1)
        denominator = d00 * d11 - d01 * d01
        assert denominator > 1e-24, (source_face, denominator)
        w1 = (d11 * d20 - d01 * d21) / denominator
        w2 = (d00 * d21 - d01 * d20) / denominator
        assert min(1 - w1 - w2, w1, w2) > -.001 and max(1 - w1 - w2, w1, w2) < 1.001, {
            'vertex': i, 'target': point.tolist(), 'nearest': nearest.tolist(), 'tree_face': face,
            'source_face': source_face, 'triangle': [int(a), int(b), int(c)], 'triangle_coords': coords[[a, b, c]].tolist(),
            'dots': [float(d00), float(d01), float(d11), float(d20), float(d21)], 'weights': [float(1 - w1 - w2), float(w1), float(w2)]}
        indices[i] = source_face
        weights[i] = (1 - w1 - w2, w1, w2)
    assert weights.min() > -.001 and weights.max() < 1.001, (weights.min(), weights.max())
    return indices, weights

def tree_for(mesh, outline=False):
    triangles = np.asarray(mesh['triangles'], dtype=np.int32)
    candidate_indices = np.arange(len(triangles), dtype=np.int32)
    if outline:
        # Preserve full-mesh triangle IDs when filtering the displaced half.
        displaced = triangles.min(axis=1) >= len(mesh['coords']) // 2
        triangles = triangles[displaced]
        candidate_indices = candidate_indices[displaced]
    points = np.asarray(mesh['coords'], dtype=np.float64)[triangles]
    first = points[:, 1] - points[:, 0]
    second = points[:, 2] - points[:, 0]
    areas = np.linalg.norm(np.cross(first, second), axis=1)
    valid = areas > 1e-14
    assert valid.any(), 'No usable source surface'
    face_indices = candidate_indices[valid]
    # Only zero-area source faces are excluded from this lookup.
    return BVHTree.FromPolygons(mesh['coords'].tolist(), triangles[valid].tolist(), all_triangles=True), face_indices

def transfer(projected, dense, triangles):
    indices, weights = projected
    vertex_ids = np.asarray(triangles, dtype=np.int32)[indices]
    return (dense[:, vertex_ids, :] * weights[None, :, :, None]).sum(axis=2)

def rest_error(dense, coords):
    return float(np.linalg.norm(dense[0] - coords, axis=1).max())

def smooth_normals(mesh, reference, tree, face_indices=None):
    coords = arr(mesh.vertices, 'co', 3)
    indices, weights = projection(coords, reference, tree, face_indices)
    result = []
    for loop in mesh.loops:
        vertex = loop.vertex_index
        face = indices[vertex]
        values = reference['normals'][reference['loops'][face]]
        normal = (values * weights[vertex, :, None]).sum(axis=0)
        normal /= max(float(np.linalg.norm(normal)), 1e-12)
        result.append(tuple(float(v) for v in normal))
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    mesh.normals_split_custom_set(result)
    mesh.update()

def outline_mesh(body, original):
    work = body.copy()
    obj = bpy.data.objects.new('TEMP_SingleOutline', work)
    bpy.context.scene.collection.objects.link(obj)
    source = next(mod for mod in original.modifiers if mod.type == 'SOLIDIFY')
    mod = obj.modifiers.new('Single outward contour', 'SOLIDIFY')
    for prop in ('thickness', 'offset', 'use_flip_normals', 'use_rim', 'use_quality_normals'):
        setattr(mod, prop, getattr(source, prop))
    # Reduced components have less uniform normals; bound corner extrusion.
    mod.use_even_offset = False
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)
    count = len(body.vertices)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()
    bmesh.ops.delete(bm, geom=[face for face in bm.faces if all(v.index < count for v in face.verts)], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    for face in bm.faces:
        face.material_index = 0
    bm.to_mesh(work)
    bm.free()
    work.materials.clear()
    assert len(original.data.materials) > 0, original.name
    for material in original.data.materials:
        work.materials.append(material)
    for polygon in work.polygons:
        polygon.material_index = 0
    work.update()
    work.calc_loop_triangles()
    assert len(work.loop_triangles) == len(body.loop_triangles), (len(work.loop_triangles), len(body.loop_triangles))
    assert len(work.vertices) == len(body.vertices)
    bpy.data.objects.remove(obj, do_unlink=True)
    return work

def all_fcurves(action):
    if hasattr(action, 'fcurves'):
        yield from action.fcurves
        return
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                yield from bag.fcurves

def key_object(obj, poses, clip, count):
    obj.shape_key_add(name=clip + '_001_Basis', from_mix=False)
    for index in range(1, count):
        obj.shape_key_add(name=f'{clip}_{index + 1:03d}', from_mix=False)
    keys = obj.data.shape_keys
    keys.use_relative = False
    for frame in range(count):
        keys.key_blocks[frame].data.foreach_set('co', np.asarray(poses[frame], dtype=np.float32).ravel())
    keys.eval_time = 0.0
    keys.keyframe_insert(data_path='eval_time', frame=1)
    keys.eval_time = float((count - 1) * 10)
    keys.keyframe_insert(data_path='eval_time', frame=count)
    for curve in all_fcurves(keys.animation_data.action):
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'

# Capture only the source surfaces needed for reprojection, before cloning scenes.
original_scenes = [bpy.data.scenes[STATIC]] + [bpy.data.scenes[name] for name, _ in CLIPS]
original_objects = {obj for scene in original_scenes for obj in scene.objects}
reference = {}
dense = {}
for scene in original_scenes:
    reference[scene.name] = {}
    dense[scene.name] = {}
    for obj in scene.objects:
        if obj.type != 'MESH' or name_of(obj) == 'Studio ground' or is_beam(obj):
            continue
        mesh = read_mesh(obj, scene)
        reference[scene.name][obj.name] = mesh
        if scene.name != STATIC:
            dense[scene.name][obj.name] = dense_keys(obj, scene, 29 if scene.name.startswith('01_') else 73)
            assert dense[scene.name][obj.name].shape[1] == len(mesh['coords']), (scene.name, obj.name, len(obj.modifiers), dense[scene.name][obj.name].shape, len(mesh['coords']))
            assert rest_error(dense[scene.name][obj.name], mesh['coords']) < .0001

# FULL_COPY adds .001 suffixes; never use copied display names as animation IDs.
source_index = {scene.name: {(name_of(obj), bool(obj.get('is_lineart', False))): obj.name
    for obj in scene.objects if obj.type == 'MESH'} for scene in original_scenes}

copies = []
for index, original in enumerate(original_scenes):
    bpy.context.window.scene = original
    bpy.ops.scene.new(type='FULL_COPY')
    copies.append(bpy.context.scene)
static_source = original_scenes[0]
static_bodies = bodies(static_source)
assert len(static_bodies) == 43
source_body_count = sum(len(reference[STATIC][obj.name]['triangles']) for obj in static_bodies)
report = {'source': str(SOURCE), 'source_sha256': SOURCE_HASH, 'output': str(DEST),
    'target_triangles': 10524, 'source_triangles': 373394, 'source_body_triangles': source_body_count,
    'source_outline_triangles': 373394 - source_body_count, 'visual_review': 'pending user B; no assistant image inspection',
    'method': 'component-wise collapse with protected foot islands; one displaced inverted-hull face per body face; source-surface animation reprojection'}
weights = {'hydraulic': .42, 'joint': .58, 'cable': .45, 'armor': 1.0}
weighted = sum(len(reference[STATIC][obj.name]['triangles']) * weights[category(name_of(obj))] for obj in static_bodies)
factor = 3400 / weighted
body_meshes = {}
outline_meshes = {}
body_report = []
for obj in static_bodies:
    mesh = reference[STATIC][obj.name]
    optimized = optimize_body(obj, mesh, factor * weights[category(name_of(obj))])
    normal_tree, normal_faces = tree_for(mesh)
    smooth_normals(optimized, mesh, normal_tree, normal_faces)
    original_outline = next((item for item in static_source.objects if item.get('is_lineart', False) and item.parent == obj), None)
    outline = outline_mesh(optimized, original_outline) if original_outline else None
    body_meshes[name_of(obj)] = optimized
    outline_meshes[name_of(obj)] = outline
    row = {'name': name_of(obj), 'source_body_triangles': len(mesh['triangles']),
        'production_body_triangles': len(optimized.loop_triangles),
        'production_outline_triangles': len(outline.loop_triangles) if outline else 0,
        'source_components': len(components(mesh))}
    body_report.append(row)
    print('OPTIMIZED', json.dumps(row, ensure_ascii=False), flush=True)
body_count = sum(len(mesh.loop_triangles) for mesh in body_meshes.values())
outline_count = sum(len(mesh.loop_triangles) for mesh in outline_meshes.values() if mesh)
assert body_count + outline_count <= 10524, (body_count, outline_count)

def link_like(new, old, scene):
    for collection in old.users_collection:
        if collection != scene.collection and new.name not in collection.objects:
            collection.objects.link(new)
    if not new.users_collection:
        scene.collection.objects.link(new)

for index, (source_scene, scene) in enumerate(zip(original_scenes, copies)):
    bpy.context.window.scene = scene
    old_objects = [obj for obj in scene.objects if obj.type == 'MESH' and name_of(obj) != 'Studio ground' and not is_beam(obj)]
    old_outlines = {name_of(obj): obj for obj in old_objects if obj.get('is_lineart', False)}
    new_objects = {}
    for old in old_objects:
        if old.get('is_lineart', False):
            continue
        source_name = name_of(old)
        parent = bpy.data.objects.new(source_name, body_meshes[source_name].copy())
        link_like(parent, old, scene)
        parent.matrix_world = Matrix.Identity(4)
        for key, value in old.items():
            parent[key] = value
        parent['asset_source_name'] = source_name
        parent['production_version'] = 'Production_10k_v1'
        new_objects[(source_name, False)] = parent
        if outline_meshes[source_name]:
            original_outline = old_outlines[source_name]
            outline = bpy.data.objects.new('OUTLINE · ' + source_name, outline_meshes[source_name].copy())
            link_like(outline, original_outline, scene)
            outline.parent = parent
            outline.matrix_parent_inverse = Matrix.Identity(4)
            outline.matrix_basis = Matrix.Identity(4)
            for key, value in original_outline.items():
                outline[key] = value
            outline['is_lineart'] = True
            outline['asset_source_name'] = source_name
            outline['production_version'] = 'Production_10k_v1'
            outline.hide_select = True
            for prop in ('visible_shadow', 'visible_diffuse', 'visible_glossy', 'visible_transmission', 'visible_volume_scatter'):
                if hasattr(outline, prop):
                    setattr(outline, prop, False)
            new_objects[(source_name, True)] = outline
    if index:
        maximum = 0.0
        for old in old_objects:
            is_outline = bool(old.get('is_lineart', False))
            source_name = name_of(old)
            new = new_objects[(source_name, is_outline)]
            body = new_objects[(source_name, False)]
            body_id = source_index[source_scene.name][(source_name, False)]
            ref = reference[source_scene.name][body_id]
            target_tree, source_faces = tree_for(ref)
            # Both surfaces use their corresponding reduced body vertex.
            projected = projection(arr(body.data.vertices, 'co', 3), ref, target_tree, source_faces)
            source_id = source_index[source_scene.name][(source_name, is_outline)]
            dense_poses = dense[source_scene.name][source_id]
            coords = arr(new.data.vertices, 'co', 3)
            if is_outline:
                assert len(body.data.vertices) == len(new.data.vertices)
                assert dense_poses.shape[1] == 2 * len(ref['coords']), ('Outline/body source mismatch', source_id)
                triangles = np.asarray(ref['triangles'], dtype=np.int32) + len(ref['coords'])
            else:
                assert dense_poses.shape[1] == len(ref['coords']), ('Body source mismatch', source_id)
                triangles = ref['triangles']
            poses = transfer(projected, dense_poses, triangles)
            # Transfer source deformation while preserving the production rest exactly.
            poses += coords[None, :, :] - poses[:1]
            maximum = max(maximum, rest_error(poses, coords))
            key_object(new, poses, 'Walk' if index == 1 else 'Mining', len(poses))
        assert maximum < .0001, (scene.name, 'First-frame projection mismatch', maximum)
        scene['production_rest_projection_error_m'] = maximum
        scene.frame_set(1)
        scene['production_version'] = 'Production_10k_v1'
    for old in old_objects:
        bpy.data.objects.remove(old, do_unlink=True)
    print('ANIMATION_SCENE', scene.name, scene.get('production_rest_projection_error_m', 0.0), flush=True)

for scene in original_scenes:
    bpy.data.scenes.remove(scene)
for obj in original_objects:
    if obj.name in bpy.data.objects:
        bpy.data.objects.remove(obj, do_unlink=True)
for scene in copies:
    scene.name = STATIC if scene == copies[0] else next(name for name, _ in CLIPS if name.startswith('01_' if scene == copies[1] else '02_'))
    for obj in scene.objects:
        if obj.get('is_lineart', False):
            obj.name = 'OUTLINE · ' + name_of(obj)
        elif 'asset_source_name' in obj:
            obj.name = obj['asset_source_name']
        elif obj.name.endswith('.001'):
            obj.name = obj.name[:-4]

# Validate topology, palette and all source animation closures without rendering.
check_scene = copies[0]
bpy.context.window.scene = check_scene
check_scene.frame_set(1)
dg = bpy.context.evaluated_depsgraph_get()
def foot_islands(coords, polygons, prefix):
    result = []
    if prefix not in ANKLES:
        return result
    ankle = ANKLES[prefix]
    for ids, _ in components(dict(coords=coords, polygons=polygons)):
        center = Vector(coords[ids].mean(axis=0).tolist())
        if center.z <= .38 or (center - ankle).length < .34:
            result.append(ids)
    return result

actual = {'body': 0, 'outline': 0}
degenerates = []
foot_targets = {}
foot_island_count = 0
production_palette = set()
for obj in check_scene.objects:
    if obj.type != 'MESH' or name_of(obj) == 'Studio ground' or is_beam(obj):
        continue
    evaluated = obj.evaluated_get(dg)
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    role = 'outline' if obj.get('is_lineart', False) else 'body'
    actual[role] += len(mesh.loop_triangles)
    transform = np.asarray(obj.matrix_world, dtype=np.float32)
    world = arr(mesh.vertices, 'co', 3) @ transform[:3, :3].T + transform[:3, 3]
    for triangle in mesh.loop_triangles:
        a, b, c = world[list(triangle.vertices)]
        if np.linalg.norm(np.cross(b - a, c - a)) < 1e-12:
            degenerates.append({'object': obj.name, 'vertices': list(triangle.vertices)})
    if role == 'body':
        source_name = name_of(obj)
        prefix = source_name.split(' · ')[0]
        target = foot_islands(world, [tuple(p.vertices) for p in mesh.polygons], prefix)
        source = foot_islands(reference[STATIC][source_name]['coords'], reference[STATIC][source_name]['polygons'], prefix)
        assert len(target) == len(source), (source_name, 'Foot island count changed', len(source), len(target))
        foot_targets[source_name] = np.asarray(sorted(i for ids in target for i in ids), dtype=np.int32)
        foot_island_count += len(target)
        production_palette.update(material.name for material in mesh.materials if material)
        for material in mesh.materials:
            if not material or not material.name.startswith('机甲 ·'):
                continue
            ramp = material.node_tree.nodes.get('三档明暗 · 硬边')
            assert ramp and ramp.color_ramp.interpolation == 'CONSTANT', material.name
            assert len(ramp.color_ramp.elements) == 3, material.name
    else:
        assert len(mesh.materials) == 1 and mesh.materials[0].name == '线稿 · 深蓝灰外轮廓', obj.name
    assert not obj.modifiers, (obj.name, 'Unexpected live geometry modifier')
    evaluated.to_mesh_clear()
assert actual['body'] == body_count and actual['outline'] == outline_count
assert actual['body'] + actual['outline'] <= 10524
assert not degenerates, ('Zero-area production triangles', degenerates[:20])
source_palette = {material.name for name in body_meshes for material in reference[STATIC][name]['materials'] if material}
assert production_palette == source_palette, (source_palette, production_palette)
assert set(name.split(' · ')[0] for name, ids in foot_targets.items() if len(ids)) == set(ANKLES)
report['production_body_triangles'] = body_count
report['production_outline_triangles'] = outline_count
report['production_triangles'] = body_count + outline_count
report['body_objects'] = body_report
report['degenerate_triangles'] = degenerates
report['protected_foot_islands'] = foot_island_count
report['protected_foot_vertices_per_scene'] = sum(len(ids) for ids in foot_targets.values())
report['palette_preserved'] = sorted(production_palette)
report['three_tone_ramps'] = 'CONSTANT, three stops on every mech material'
report['side_panel_angles_degrees'] = {obj.get('asset_source_name', obj.name): obj.get('内倾角度_度') for obj in bodies(check_scene) if obj.get('内倾角度_度') is not None}
report['scenes'] = {}

def evaluated_world(obj, dg):
    evaluated = obj.evaluated_get(dg)
    mesh = evaluated.to_mesh()
    matrix = np.asarray(evaluated.matrix_world, dtype=np.float32)
    result = arr(mesh.vertices, 'co', 3) @ matrix[:3, :3].T + matrix[:3, 3]
    evaluated.to_mesh_clear()
    return result

for scene, (_, count) in zip(copies[1:], CLIPS):
    # Each scene has its own dependency graph; explicitly activate it before
    # evaluating frames, otherwise objects can remain in their basis pose.
    bpy.context.window.scene = scene
    objects = [obj for obj in scene.objects if obj.type == 'MESH' and name_of(obj) != 'Studio ground' and not is_beam(obj)]
    assert len(objects) == 85
    assert all(obj.data.shape_keys and not obj.data.shape_keys.use_relative and len(obj.data.shape_keys.key_blocks) == count for obj in objects)
    scene.frame_start = 1
    scene.frame_end = count
    scene.render.fps = 24
    closure = 0.0
    foot_motion = 0.0
    evaluated_key_error = 0.0
    first = None
    chassis_samples = []
    for frame in range(1, count + 1):
        scene.frame_set(frame)
        dg = bpy.context.evaluated_depsgraph_get()
        current = {obj.name: evaluated_world(obj, dg) for obj in objects}
        if first is None:
            first = current
        for obj in objects:
            points = current[obj.name]
            if frame == count:
                closure = max(closure, float(np.linalg.norm(points - first[obj.name], axis=1).max()))
            if not obj.get('is_lineart', False):
                ids = foot_targets[name_of(obj)]
                if len(ids) and scene.name.startswith('02_'):
                    foot_motion = max(foot_motion, float(np.linalg.norm(points[ids] - first[obj.name][ids], axis=1).max()))
                if name_of(obj).startswith('Body_Chassis · 机甲 · 黑色主体框架'):
                    chassis_samples.append(points.mean(axis=0))
            transform = np.asarray(obj.matrix_world, dtype=np.float32)
            expected = arr(obj.data.shape_keys.key_blocks[frame - 1].data, 'co', 3) @ transform[:3, :3].T + transform[:3, 3]
            evaluated_key_error = max(evaluated_key_error, float(np.linalg.norm(points - expected, axis=1).max()))
    assert closure < .0001, (scene.name, 'Loop closure', closure)
    assert evaluated_key_error < .0001, (scene.name, 'Evaluated key mismatch', evaluated_key_error)
    report_row = {'frame_count': count, 'frames_evaluated': count, 'animated_mesh_objects': len(objects),
        'loop_max_vertex_error_m': closure, 'rest_projection_error_m': scene['production_rest_projection_error_m'],
        'evaluated_key_max_error_m': evaluated_key_error}
    if scene.name.startswith('02_'):
        assert foot_motion < .00001, ('Mining foot moved', foot_motion)
        chassis_samples = np.asarray(chassis_samples)
        down = float(chassis_samples[0, 2] - chassis_samples[:, 2].min())
        back = float(chassis_samples[:, 1].max() - chassis_samples[0, 1])
        assert down > .20 and back > .20, ('Mining recoil missing', down, back)
        report_row.update(mining_foot_max_motion_m=foot_motion, protected_foot_vertices=int(sum(len(ids) for ids in foot_targets.values())),
            protected_foot_islands=foot_island_count, chassis_down_travel_m=down, chassis_back_travel_m=back)
    report['scenes'][scene.name] = report_row

notes = bpy.data.texts.get('生产近景审核说明') or bpy.data.texts.new('生产近景审核说明')
notes.clear()
notes.write(f'''四足采矿机甲 · 生产近景 10k v1

整机 {body_count + outline_count} 三角面：本体 {body_count}，单层外轮廓 {outline_count}。
原始文件未覆盖。只优化几何，保留原材质、三档明暗、侧板倾角、四足分件及行进／采矿绝对形态键动画。
外轮廓不再保留双面 Solidify 的重复原面；每个机械连通件保留，足部小岛优先保留。

在 Scene 下拉框切换 01_行进 · 顶点动画、02_采矿 · 顶点动画。
按空格播放。采矿激光仍只用于 Blender 审核，不计入机甲面数，也不导出为机甲 VAT 网格。
未进行助手渲染、截图或视觉目检；请审核轮廓、足部衔接、关节和线稿。
本版本尚待 Blender B 审核，未替换正式矿车。
''')
bpy.context.window.scene = copies[0]
copies[0].frame_set(1)
bpy.ops.outliner.orphans_purge(do_local_ids=True, do_linked_ids=False, do_recursive=True)
bpy.ops.wm.save_as_mainfile(filepath=str(DEST), check_existing=False, compress=True)
report['saved_blend_sha256'] = hashlib.sha256(DEST.read_bytes()).hexdigest()
report['source_sha256_after'] = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
assert report['source_sha256_after'] == SOURCE_HASH
report['success'] = True
(OUT / 'production_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps({'success': True, 'blend': str(DEST), 'body': body_count, 'outline': outline_count,
    'total': body_count + outline_count, 'degenerates': len(degenerates), 'scenes': report['scenes']}, ensure_ascii=False, indent=2))

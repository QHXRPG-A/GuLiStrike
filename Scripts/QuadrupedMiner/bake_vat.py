"""Bake the reviewed quadruped's absolute shape keys into position/normal VAT.

Run with Blender --background --python this_file. No rendering or image review.
The source blend is read-only. Only this versioned ArtSource directory is written.
"""
import bpy
import bmesh
import hashlib
import json
import math
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "ArtSource" / "QuadrupedMiner_20261113"
SOURCE = ROOT / "quadruped-mech-side-panels" / "blender" / "quadruped-mech-vertex-animation.blend"
EXPECTED_SHA256 = "c4d2649b5d6edce48d6475a6988ac8ac1677f6676994288019c5de97be583850"
for folder in ("FBX", "Textures", "Reports"):
    (ART / folder).mkdir(parents=True, exist_ok=True)
REPORT = ART / "Reports" / "vat_metadata.json"
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == EXPECTED_SHA256, "Animation source changed; review before rebaking"
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
STATIC = "00_静态 · 当前资源"
WALK = "01_行进 · 顶点动画"
MINING = "02_采矿 · 顶点动画"
CLIP_SPECS = [
    dict(name="Walk", scene=WALK, frames=28, seconds=28 / 24, candidates=[9, 13, 17, 23, 29]),
    dict(name="Mining", scene=MINING, frames=72, seconds=3.0, candidates=[19, 25, 33, 41, 49, 57, 65, 73]),
]
LEG_NAMES = ("Leg_Front_L", "Leg_Front_R", "Leg_Rear_L", "Leg_Rear_R")
LINE_COLOR = (26 / 255, 24 / 255, 47 / 255)


def log(message):
    print("QUADRUPED_VAT " + str(message), flush=True)


def vectors(collection, prop, size):
    result = np.empty(len(collection) * size, dtype=np.float32)
    collection.foreach_get(prop, result)
    return result.reshape((-1, size))


def srgb_to_linear(value):
    value = np.asarray(value, dtype=np.float32)
    return np.where(value <= 0.04045, value / 12.92, ((value + 0.055) / 1.055) ** 2.4)


def matrix_array(matrix):
    return np.asarray(matrix, dtype=np.float32)


def world_points(obj, coords):
    matrix = matrix_array(obj.matrix_world)
    return coords @ matrix[:3, :3].T + matrix[:3, 3]


def evaluated_mesh(obj, scene):
    if bpy.context.window.scene != scene:
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
    assert len(mesh.loop_triangles) > 0, obj.name
    coords = world_points(evaluated, vectors(mesh.vertices, "co", 3))
    normals = vectors(mesh.corner_normals, "vector", 3)
    normals = world_directions(evaluated, normals)
    polygons = [tuple(p.vertices) for p in mesh.polygons]
    triangles = [tuple(t.vertices) for t in mesh.loop_triangles]
    loops = np.asarray([t.loops[:] for t in mesh.loop_triangles], dtype=np.int32)
    materials = [m.name if m else "" for m in mesh.materials]
    material_indices = [p.material_index for p in mesh.polygons]
    bpy.data.meshes.remove(mesh)
    return dict(coords=coords, normals=normals, polygons=polygons, triangles=triangles,
                loops=loops, materials=materials, material_indices=material_indices)


def world_directions(obj, values):
    rotation = matrix_array(obj.matrix_world.to_3x3())
    inverse_scale = np.linalg.norm(rotation, axis=0)
    rotation = rotation / np.maximum(inverse_scale, 1e-12)
    result = values @ rotation.T
    return result / np.maximum(np.linalg.norm(result, axis=1, keepdims=True), 1e-12)


def static_palette(obj):
    if obj.get("is_lineart", False):
        return dict(name="线稿 · 深蓝灰外轮廓", srgb=LINE_COLOR, emission=1.0, outline=True)
    material = obj.data.materials[0]
    node = material.node_tree.nodes.get("组件纯色基色")
    assert node, (obj.name, material.name)
    linear = np.asarray(node.outputs[0].default_value[:3], dtype=np.float32)
    srgb = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.maximum(linear, 0) ** (1 / 2.4) - 0.055)
    emission = next((n.inputs["Strength"].default_value for n in material.node_tree.nodes if n.bl_idname == "ShaderNodeEmission"), 1.0)
    return dict(name=material.name, srgb=srgb.tolist(), linear=linear.tolist(), emission=emission, outline=False)


def source_name(obj):
    if obj.get("is_lineart", False) and obj.parent:
        return obj.parent.get("asset_source_name", obj.parent.name)
    return obj.get("asset_source_name", obj.name)


def is_demo_beam(obj):
    # The review-only laser must never become part of the runtime static mesh.
    return source_name(obj).startswith("M · Mining Laser")


def source_keys(obj, scene_name, frame_count):
    keys = obj.data.shape_keys
    assert keys and not keys.use_relative
    assert len(keys.key_blocks) == frame_count + 1
    result = []
    matrix = matrix_array(obj.matrix_world)
    for frame in range(frame_count + 1):
        local = vectors(keys.key_blocks[frame].data, "co", 3)
        result.append(local @ matrix[:3, :3].T + matrix[:3, 3])
    return np.asarray(result, dtype=np.float32)


def transfer_data(target_coords, reference_coords, triangles, source_dense, return_mapping=False):
    # Build the BVH from the high-resolution reference, then project target vertices.
    # Degenerate faces are excluded only from this lookup, never from the exported LOD0.
    target_coords = np.asarray(target_coords, dtype=np.float64)
    reference_coords = np.asarray(reference_coords, dtype=np.float64)
    source_dense = np.asarray(source_dense, dtype=np.float64)
    assert len(reference_coords) == source_dense.shape[1]
    assert max(max(triangle) for triangle in triangles) < len(reference_coords)
    assert np.isfinite(target_coords).all() and np.isfinite(reference_coords).all() and np.isfinite(source_dense).all()
    assert np.abs(target_coords).max() < 100 and np.abs(reference_coords).max() < 100
    assert np.abs(source_dense).max() < 100, (np.min(source_dense, axis=(0, 1)), np.max(source_dense, axis=(0, 1)))
    triangles = [triangle for triangle in triangles
        if np.linalg.norm(np.cross(reference_coords[triangle[1]] - reference_coords[triangle[0]],
            reference_coords[triangle[2]] - reference_coords[triangle[0]])) > 1e-6]
    tree = BVHTree.FromPolygons(reference_coords.tolist(), triangles, all_triangles=True)
    weights, indices = [], []
    for point in target_coords:
        location, normal, face, distance = tree.find_nearest(Vector(point))
        assert face is not None and np.isfinite(np.asarray(location)).all()
        triangle = triangles[face]
        a, b, c = [reference_coords[index] for index in triangle]
        v0, v1, v2 = b - a, c - a, np.asarray(location, dtype=np.float64) - a
        d00, d01, d11 = np.dot(v0, v0), np.dot(v0, v1), np.dot(v1, v1)
        d20, d21 = np.dot(v2, v0), np.dot(v2, v1)
        denominator = d00 * d11 - d01 * d01
        assert denominator > 1e-12, (triangle, a, b, c, denominator)
        w1 = (d11 * d20 - d01 * d21) / denominator
        w2 = (d00 * d21 - d01 * d20) / denominator
        w0 = 1.0 - w1 - w2
        bary = np.asarray((w0, w1, w2), dtype=np.float64)
        assert np.isfinite(bary).all() and abs(bary.sum() - 1) < 1e-7
        assert bary.min() > -0.15 and bary.max() < 1.15, (point, triangle, bary.tolist())
        assert np.linalg.norm(a * w0 + b * w1 + c * w2 - np.asarray(location)) < 0.00001
        indices.append(triangle)
        weights.append(bary)
    indices = np.asarray(indices, dtype=np.int32)
    weights = np.asarray(weights, dtype=np.float64)
    dense = (source_dense[:, indices, :] * weights[None, :, :, None]).sum(axis=2)
    # Preserve target rest geometry; transfer only reference deformation.
    dense += target_coords[None, :, :] - dense[:1]
    if return_mapping:
        return dense, indices, weights
    return dense


def edge_marks(mesh):
    polygons = mesh["polygons"]
    incidence = {}
    for polygon_index, polygon in enumerate(polygons):
        for a, b in zip(polygon, polygon[1:] + polygon[:1]):
            incidence.setdefault(tuple(sorted((a, b))), []).append(polygon_index)
    marked = set()
    for (a, b), faces in incidence.items():
        if len(faces) != 2:
            marked.add((a, b))
            continue
        pa, pb = faces
        materials = mesh["materials"]
        if materials[mesh["material_indices"][pa]] != materials[mesh["material_indices"][pb]]:
            marked.add((a, b))
            continue
        va, vb, vc = [mesh["coords"][index] for index in polygons[pa][:3]]
        na = np.cross(vb - va, vc - va)
        va, vb, vc = [mesh["coords"][index] for index in polygons[pb][:3]]
        nb = np.cross(vb - va, vc - va)
        denominator = max(float(np.linalg.norm(na) * np.linalg.norm(nb)), 1e-16)
        if abs(float(np.dot(na, nb)) / denominator) < math.cos(math.radians(46)):
            marked.add((a, b))
    return marked


def triangle_edge_distances(coords, triangle, marked):
    a, b, c = coords[list(triangle)]
    normal = np.cross(b - a, c - a)
    normal /= max(float(np.linalg.norm(normal)), 1e-12)
    distances = np.full((3, 3), 999.0, dtype=np.float32)
    for edge in range(3):
        start, end = triangle[edge], triangle[(edge + 1) % 3]
        if tuple(sorted((start, end))) not in marked:
            continue
        direction = coords[end] - coords[start]
        perpendicular = np.cross(normal, direction)
        perpendicular /= max(float(np.linalg.norm(perpendicular)), 1e-12)
        for corner in range(3):
            distances[corner, edge] = abs(float(np.dot(coords[triangle[corner]] - coords[start], perpendicular))) * 100
    return distances


def make_mesh_copy(source, destination_scene, source_static_scene):
    bpy.context.window.scene = source_static_scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(source.evaluated_get(depsgraph))
    mesh.transform(source.matrix_world)
    # LOD0 must preserve the source indexing exactly. Decimation belongs only
    # to the two distance tiers; do not weld or re-index the source bodies.
    mesh.update()
    obj = bpy.data.objects.new(source.get("asset_source_name", source.name), mesh)
    destination_scene.collection.objects.link(obj)
    obj["asset_source_name"] = source.get("asset_source_name", source.name)
    obj["is_lineart"] = False
    return obj


def build_lod_objects(lod, ratio, temp_scene, static_scene, originals):
    del originals[:]
    for source in static_scene.objects:
        if source.type != "MESH" or source.get("is_lineart", False) or source.get("asset_source_name", source.name) == "Studio ground" or is_demo_beam(source):
            continue
        body = make_mesh_copy(source, temp_scene, static_scene)
        if lod:
            bpy.context.window.scene = temp_scene
            bpy.ops.object.select_all(action="DESELECT")
            body.select_set(True)
            bpy.context.view_layer.objects.active = body
            modifier = body.modifiers.new("Distance LOD only", "DECIMATE")
            modifier.decimate_type = "COLLAPSE"
            modifier.ratio = ratio
            modifier.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        body.data.materials.clear()
        for material in source.data.materials:
            body.data.materials.append(material)
        outline = None
        original_outline = next((o for o in static_scene.objects if o.get("is_lineart") and o.parent == source), None)
        structural_contour = not any(label in source_name(source) for label in ("橡胶与线缆", "关节枪铁", "液压杆"))
        if original_outline and (lod < 2 or structural_contour):
            outline = body.copy()
            outline.data = body.data.copy()
            outline.name = "OUTLINE · " + body.name
            outline["is_lineart"] = True
            temp_scene.collection.objects.link(outline)
            for modifier in original_outline.modifiers:
                if modifier.type == "SOLIDIFY":
                    new = outline.modifiers.new("Source contour thickness", "SOLIDIFY")
                    for key in ("thickness", "offset", "use_flip_normals", "use_rim", "use_even_offset", "use_quality_normals"):
                        setattr(new, key, getattr(modifier, key))
                    if lod:
                        # Decimation can create near-degenerate corners. Even-thickness
                        # extrapolation divides by their tiny angle and throws contour
                        # vertices thousands of metres away. Plain vertex-normal
                        # extrusion keeps the same thickness with bounded offsets.
                        new.use_even_offset = False
                        new.thickness_clamp = 0.5
        originals.append((source, original_outline, body, outline))
    count = 0
    bpy.context.window.scene = temp_scene
    bpy.context.view_layer.update()
    for source, original_outline, body, outline in originals:
        for obj in (body, outline):
            if obj:
                mesh = evaluated_mesh(obj, temp_scene)
                count += len(mesh["triangles"])
    return count


def choose_lod_ratio(lod, temp_scene, static_scene):
    if not lod:
        return 1.0
    target = (0, 6000, 3200)[lod]
    lower, upper = 0.001, 1.0
    best = None
    for iteration in range(9):
        ratio = (lower + upper) / 2
        objects = []
        count = build_lod_objects(lod, ratio, temp_scene, static_scene, objects)
        log(f"LOD{lod} ratio {ratio:.6f} triangles {count}")
        if count <= target:
            best = ratio
            lower = ratio
        else:
            upper = ratio
        for _, _, body, outline in objects:
            for obj in (body, outline):
                if obj:
                    mesh = obj.data
                    bpy.data.objects.remove(obj, do_unlink=True)
                    if mesh.users == 0:
                        bpy.data.meshes.remove(mesh)
    assert best is not None, f"Cannot reach LOD{lod} target {target}"
    return best


def key_animation(objects, frame_count):
    for _, _, body, outline in objects:
        # A clip owns a fresh absolute-key sequence. Never append Mining keys to
        # the preceding Walk sequence or evaluate an old absolute key frame.
        rest = vectors(body.data.vertices, "co", 3).copy()
        body.shape_key_clear()
        body.data.vertices.foreach_set("co", rest.ravel())
        body.shape_key_add(name="Rest", from_mix=False)
        for frame in range(1, frame_count + 1):
            body.shape_key_add(name=f"Pose_{frame:03d}", from_mix=False)
        keys = body.data.shape_keys
        keys.use_relative = False
        keys.eval_time = 0
        for key in keys.key_blocks:
            key.interpolation = "KEY_LINEAR"
        if outline:
            outline.data = body.data.copy()
            outline.data.shape_keys.eval_time = 0
            outline["is_lineart"] = True


def set_proxy_frame(objects, frame):
    for _, _, body, outline in objects:
        body.data.shape_keys.eval_time = frame * 10.0
        if outline:
            outline.data.shape_keys.eval_time = frame * 10.0
    # Refresh once per frame, not once per component in evaluated_mesh().
    bpy.context.view_layer.update()


def png(path, rgba):
    height, width, _ = rgba.shape
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + row.tobytes() for row in rgba)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def exr(path, rgba, scene):
    height, width, _ = rgba.shape
    image = bpy.data.images.new(path.stem, width=width, height=height, alpha=True, float_buffer=True)
    image.colorspace_settings.name = "Non-Color"
    image.alpha_mode = "STRAIGHT"
    image.pixels.foreach_set(rgba.ravel())
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_depth = "16"
    scene.render.image_settings.exr_codec = "ZIP"
    image.save_render(str(path), scene=scene)
    bpy.data.images.remove(image)
    read = bpy.data.images.load(str(path), check_existing=False)
    read.colorspace_settings.name = "Non-Color"
    actual = np.asarray(read.pixels[:], dtype=np.float32).reshape(rgba.shape)
    bpy.data.images.remove(read)
    assert np.max(np.abs(actual - rgba)) < 0.03
    return actual


def interpolate(dense, sample_frames):
    times = (sample_frames - 1).astype(np.float32)
    lower = np.floor(times).astype(np.int32)
    upper = np.ceil(times).astype(np.int32)
    alpha = (times - lower)[None, :, None, None]
    return dense[:, lower] * (1 - alpha) + dense[:, upper] * alpha


def sample_error(dense, sample_frames):
    error = np.zeros(dense.shape[1], dtype=np.float32)
    for start, end in zip(sample_frames[:-1], sample_frames[1:]):
        frames = np.arange(start, end + 1)
        alpha = ((frames - start) / (end - start)).astype(np.float32)[:, None, None]
        approximate = dense[start - 1][None, :, :] * (1 - alpha) + dense[end - 1][None, :, :] * alpha
        error = np.maximum(error, np.linalg.norm(approximate - dense[frames - 1], axis=2).max(axis=0))
    return float(error.max() * 100), error


def frame_errors(dense, sample_frames):
    errors = np.zeros(dense.shape[0], dtype=np.float32)
    for start, end in zip(sample_frames[:-1], sample_frames[1:]):
        frames = np.arange(start, end + 1)
        alpha = ((frames - start) / (end - start)).astype(np.float32)[:, None, None]
        approximate = dense[start - 1][None, :, :] * (1 - alpha) + dense[end - 1][None, :, :] * alpha
        errors[frames - 1] = np.linalg.norm(approximate - dense[frames - 1], axis=2).max(axis=1)
    return errors * 100


def choose_samples(name, candidates, dense):
    # Insert the source frame with the largest interpolation error. A uniform
    # reduction can miss a fast knee/ankle transition even when most frames agree.
    samples = np.asarray([1, dense.shape[0]], dtype=np.int32)
    reports = []
    for count in range(2, candidates[-1] + 1):
        errors = frame_errors(dense, samples)
        maximum = float(errors.max())
        reports.append(dict(samples=len(samples), source_frames=samples.tolist(),
                            maximum_error_cm=maximum, worst_source_frame=int(errors.argmax() + 1)))
        log(f"{name} adaptive samples {len(samples)} maximum {maximum:.6f} cm")
        if maximum <= 0.30:
            return samples, reports
        next_frame = int(errors.argmax() + 1)
        assert next_frame not in samples, (name, maximum, next_frame, samples.tolist())
        samples = np.sort(np.append(samples, next_frame)).astype(np.int32)
    raise AssertionError((name, "Sampling budget not met with every source frame", reports[-1]))


def foot_component(record):
    """Select the rigid foot island, not the ankle transition above it."""
    name = source_name(record["source"])
    if name.split(" · ")[0] not in LEG_NAMES or "黑色主体框架" not in name:
        return np.zeros(record["position_count"], dtype=bool)
    mesh = record["mesh"]
    parents = np.arange(len(mesh["coords"]), dtype=np.int32)
    def root(index):
        while parents[index] != index:
            parents[index] = parents[parents[index]]
            index = parents[index]
        return index
    for triangle in mesh["triangles"]:
        a, b, c = [int(index) for index in triangle]
        parents[root(b)] = root(a)
        parents[root(c)] = root(a)
    labels = np.asarray([root(index) for index in range(len(parents))])
    candidates = []
    for label in np.unique(labels):
        indices = np.flatnonzero(labels == label)
        if len(indices) >= 3:
            candidates.append((float(mesh["coords"][indices, 2].mean()), indices))
    assert candidates, name
    height, indices = min(candidates, key=lambda item: item[0])
    assert height < 0.25, (name, height, "Lowest rigid island is not a foot")
    result = np.zeros(len(parents), dtype=bool)
    result[indices] = True
    return result


log("Loading source geometry and dense deformation")
static_scene = bpy.data.scenes[STATIC]
sources = [obj for obj in static_scene.objects if obj.type == "MESH" and not obj.get("is_lineart", False) and source_name(obj) != "Studio ground" and not is_demo_beam(obj)]
assert len(sources) == 43, [source_name(obj) for obj in sources]
source_meshes = {}
source_dense = {}
for scene_name, frame_count in ((WALK, 28), (MINING, 72)):
    scene = bpy.data.scenes[scene_name]
    bpy.context.window.scene = scene
    scene.frame_set(1)
    source_meshes[scene_name] = {}
    for obj in scene.objects:
        if obj.type != "MESH" or source_name(obj) == "Studio ground" or obj.get("is_lineart", False) or is_demo_beam(obj):
            continue
        name = source_name(obj)
        mesh = evaluated_mesh(obj, scene)
        source_meshes[scene_name][name] = mesh
        source_dense.setdefault(name, {})[scene_name] = source_keys(obj, scene_name, frame_count)
        assert len(mesh["coords"]) == source_dense[name][scene_name].shape[1], (name, len(mesh["coords"]), source_dense[name][scene_name].shape)
        assert max(max(triangle) for triangle in mesh["triangles"]) < len(mesh["coords"]), name
        assert np.max(np.abs(mesh["coords"] - source_dense[name][scene_name][0])) < 0.0001, (name, "Evaluated first frame differs from source key zero")
log(f"Source bodies {len(source_dense)}")

palette = {source_name(obj): static_palette(obj) for obj in sources}
palette["OUTLINE"] = dict(name="线稿 · 深蓝灰外轮廓", srgb=LINE_COLOR, emission=1.0, outline=True)
materials = []
for value in palette.values():
    if not any(item["name"] == value["name"] for item in materials):
        materials.append(value)

report = dict(version="QuadrupedMiner_PositionVAT_v1", source_blend=str(SOURCE), source_sha256=EXPECTED_SHA256,
              source_validation=str(ROOT / "quadruped-mech-side-panels" / "blender" / "vertex-animation-validation.json"),
              coordinate_contract="Numeric cm in Blender (x,y,z); explicit FbxFactory convert_scene=True convert_scene_unit=False yields UE (x,-y,z)",
              runtime_bones=0, runtime_skeletal_components=0, shader="absolute position RGBA16F plus local normal RGBA8; no rigid-quaternion assumption",
              fps=24, screen_sizes=[1.0, 0.056, 0.028], materials=materials, palette=list(palette.keys()),
              mining_beam="Existing NS_MiningLaser_Green follows actual MiningTargetWorld; no fixed Blender beam in VAT",
              source_visual_acceptance="用户授权继续接入；未记录明确的 Blender 动画视觉通过回执", assistant_visual_inspection=False,
              technical_checks="offline only; UE import/readback not yet performed", lods=[], clips=[], sampling_budget_cm=0.30,
              sampling_policy="Greedy worst-frame insertion on LOD0; identical source samples and playback phase for all three LODs")

chosen_samples = {}
for lod in range(3):
    temp_scene = bpy.data.scenes.new(f"Quadruped_VAT_LOD{lod}")
    ratio = choose_lod_ratio(lod, temp_scene, static_scene)
    objects = []
    actual_triangles = build_lod_objects(lod, ratio, temp_scene, static_scene, objects)
    log(f"LOD{lod} selected ratio {ratio:.8f}, evaluated triangles {actual_triangles}")
    lod_records = []
    for source, original_outline, body, outline in objects:
        for obj in (body, outline):
            if not obj:
                continue
            mesh = evaluated_mesh(obj, temp_scene)
            name = source_name(source) + ("_Outline" if obj == outline else "_Body")
            lod_records.append(dict(name=name, object=obj, source=source, outline=obj == outline,
                mesh=mesh, vertex_count=len(mesh["coords"]), projection={}))
    dense = {}
    for scene_name, frame_count in ((WALK, 28), (MINING, 72)):
        pieces = []
        for record in lod_records:
            name = source_name(record["source"])
            reference = source_meshes[scene_name][name]
            if not lod and not record["outline"]:
                piece = source_dense[name][scene_name]
            else:
                # Transfer once from the deterministic source surface, without
                # reduced-mesh shape keys or animated Solidify evaluation.
                piece, indices, weights = transfer_data(record["mesh"]["coords"], reference["coords"],
                    reference["triangles"], source_dense[name][scene_name], return_mapping=True)
                if lod:
                    record["projection"][scene_name] = (indices, weights)
            pieces.append(piece)
        dense[scene_name] = np.concatenate(pieces, axis=1).astype(np.float32)
    del objects
    # Corner normals are indexed by the source polygon loop, not by triangle corner.
    # Keep Blender's triangulation and explicitly carry its triangle-to-loop mapping.
    position_offset = 0
    normal_offset = 0
    for record in lod_records:
        mesh = record["mesh"]
        assert mesh["loops"].shape == (len(mesh["triangles"]), 3)
        assert len(mesh["normals"]) == sum(len(polygon) for polygon in mesh["polygons"])
        assert mesh["loops"].max() < len(mesh["normals"])
        record["position_offset"] = position_offset
        position_offset += len(mesh["coords"])
        groups = {}
        loop_ids = np.empty(mesh["loops"].size, dtype=np.int32)
        triangle_loops = mesh["loops"].ravel()
        triangle_vertices = np.asarray(mesh["triangles"], dtype=np.int32).ravel()
        triangle_normals = mesh["normals"][triangle_loops]
        for corner, (vertex_index, normal) in enumerate(zip(triangle_vertices, triangle_normals)):
            key = (int(vertex_index), tuple(np.rint(normal * 512).astype(np.int32)))
            if key not in groups:
                groups[key] = normal_offset + len(groups)
            loop_ids[corner] = groups[key]
        representatives = np.empty(len(groups), dtype=np.int32)
        for corner, index in enumerate(loop_ids):
            representatives[index - normal_offset] = triangle_loops[corner]
        record["normal_offset"] = normal_offset
        record["normal_count"] = len(groups)
        record["representatives"] = representatives
        normal_offset += len(groups)
    for clip in CLIP_SPECS:
        scene_name = clip["scene"]
        frame_count = clip["frames"]
        if lod == 0:
            sample_frames, candidate_report = choose_samples(clip["name"], clip["candidates"], dense[scene_name])
            chosen_samples[clip["name"]] = (sample_frames, candidate_report)
        else:
            sample_frames, candidate_report = chosen_samples[clip["name"]]
            log(f"{clip['name']} LOD{lod} reuses LOD0 sample frames {sample_frames.tolist()}")
        clip["selected_frames"] = sample_frames
        clip["sampling_candidates"] = candidate_report
        digest = hashlib.sha256(b"source_loop_normal_v4_geometric_fallback")
        digest.update(dense[scene_name].tobytes())
        digest.update(sample_frames.tobytes())
        for record in lod_records:
            digest.update(record["mesh"]["normals"].tobytes())
            digest.update(record["mesh"]["loops"].tobytes())
            digest.update(record["representatives"].tobytes())
            if lod:
                indices, weights = record["projection"][scene_name]
                digest.update(indices.tobytes())
                digest.update(weights.tobytes())
        cache_path = ART / "Reports" / f"normal_cache_LOD{lod}_{clip['name']}.npz"
        normal_frames = None
        if cache_path.exists():
            with np.load(cache_path, allow_pickle=False) as cache:
                if str(cache["signature"]) == digest.hexdigest() and np.array_equal(cache["sample_frames"], sample_frames):
                    normal_frames = cache["normals"]
                    assert normal_frames.shape == (len(sample_frames), normal_offset, 3)
                    assert np.isfinite(normal_frames).all()
                    log(f"LOD{lod} {clip['name']} verified normal cache loaded")
        if normal_frames is None:
            normal_frames = np.empty((len(sample_frames), normal_offset, 3), dtype=np.float32)
            if lod:
                scene = bpy.data.scenes[scene_name]
                bpy.context.window.scene = scene
                originals = {source_name(obj): obj for obj in scene.objects if obj.type == "MESH" and not obj.get("is_lineart", False)}
                for sample_index, frame in enumerate(sample_frames):
                    scene.frame_set(int(frame))
                    evaluated_sources = {}
                    for record in lod_records:
                        name = source_name(record["source"])
                        if name not in evaluated_sources:
                            evaluated_sources[name] = evaluated_mesh(originals[name], scene)
                        mesh = evaluated_sources[name]
                        indices, weights = record["projection"][scene_name]
                        vertices = np.asarray(record["mesh"]["triangles"], dtype=np.int32).ravel()[record["representatives"]]
                        source_triangles = indices[vertices]
                        loops = mesh["loops"].ravel()[source_triangles]
                        corner_normals = mesh["normals"][loops].copy()
                        missing = np.linalg.norm(corner_normals, axis=2) <= 0.5
                        if np.any(missing):
                            points = mesh["coords"][source_triangles]
                            geometric = np.cross(points[:, 1] - points[:, 0], points[:, 2] - points[:, 0])
                            geometric /= np.maximum(np.linalg.norm(geometric, axis=1, keepdims=True), 1e-12)
                            corner_normals[missing] = np.broadcast_to(geometric[:, None, :], corner_normals.shape)[missing]
                        values = (corner_normals * weights[vertices][:, :, None]).sum(axis=1)
                        normal_frames[sample_index, record["normal_offset"]:record["normal_offset"] + record["normal_count"]] = values
            else:
                scene = bpy.data.scenes[scene_name]
                bpy.context.window.scene = scene
                for sample_index, frame in enumerate(sample_frames):
                    scene.frame_set(int(frame))
                    for record in lod_records:
                        source = record["source"]
                        original = next(o for o in scene.objects if o.type == "MESH" and source_name(o) == source_name(source) and bool(o.get("is_lineart", False)) == record["outline"])
                        mesh = evaluated_mesh(original, scene)
                        assert len(mesh["normals"]) == len(record["mesh"]["normals"])
                        normal_frames[sample_index, record["normal_offset"]:record["normal_offset"] + record["normal_count"]] = mesh["normals"][record["representatives"]]
            np.savez(cache_path, signature=digest.hexdigest(), sample_frames=sample_frames, normals=normal_frames)
            log(f"LOD{lod} {clip['name']} normal cache saved")
        clip["normal_frames"] = normal_frames
    # Assemble export geometry in source rest space; transform only once at export.
    bpy.context.window.scene = temp_scene
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for record in lod_records:
        obj = record["object"]
        # Solidify doubles the contour vertex count. Export the evaluated mesh,
        # never write body-sized arrays into an unevaluated contour datablock.
        evaluated = bpy.data.meshes.new_from_object(
            obj.evaluated_get(depsgraph), preserve_all_data_layers=False, depsgraph=depsgraph)
        evaluated.calc_loop_triangles()
        assert len(evaluated.vertices) == len(record["mesh"]["coords"]), (lod, obj.name)
        assert len(evaluated.loop_triangles) == len(record["mesh"]["triangles"]), (lod, obj.name)
        obj.modifiers.clear()
        obj.shape_key_clear()
        old_mesh = obj.data
        obj.data = evaluated
        if old_mesh.users == 0:
            bpy.data.meshes.remove(old_mesh)
        assert obj.matrix_world.is_identity, (obj.name, "Bake geometry must be world-space")
        mesh = record["mesh"]
        obj.data.calc_loop_triangles()
        for layer in list(obj.data.uv_layers):
            obj.data.uv_layers.remove(layer)
        for attribute in list(obj.data.color_attributes):
            obj.data.color_attributes.remove(attribute)
        obj.data.materials.clear()
        for value in materials:
            material = bpy.data.materials.get("Quadruped_" + value["name"])
            if not material:
                material = bpy.data.materials.new("Quadruped_" + value["name"])
                material.use_nodes = False
            obj.data.materials.append(material)
        color_attribute = obj.data.color_attributes.new(name="GuLi_PaletteSRGB", type="BYTE_COLOR", domain="CORNER")
        uv_layers = [obj.data.uv_layers.new(name=name) for name in ("GuLi_VATPosition", "GuLi_StructuralDistance_RG", "GuLi_StructuralDistance_B_VATNormal")]
        color = np.tile((0.18, 0.18, 0.18, 1.0), (len(obj.data.loops), 1)).astype(np.float32)
        uv0 = np.zeros((len(obj.data.loops), 2), dtype=np.float32)
        uv1 = np.full((len(obj.data.loops), 2), 999.0, dtype=np.float32)
        uv2 = np.zeros((len(obj.data.loops), 2), dtype=np.float32)
        uv2[:, 0] = 999
        marked = edge_marks(mesh)
        source_palette = palette[source_name(record["source"])]
        if record["outline"]:
            source_palette = palette["OUTLINE"]
        material_index = next(i for i, item in enumerate(materials) if item["name"] == source_palette["name"])
        for polygon in obj.data.polygons:
            polygon.material_index = material_index
        vertex_pos_id = np.arange(len(mesh["coords"]), dtype=np.float32) + record["position_offset"]
        loop_vertex = np.asarray([loop.vertex_index for loop in obj.data.loops], dtype=np.int32)
        normals = mesh["normals"]
        triangle_loops = mesh["loops"].ravel()
        assert len(obj.data.loops) == len(normals), (lod, obj.name, len(obj.data.loops), len(normals))
        assert mesh["loops"].shape == (len(mesh["triangles"]), 3)
        assert triangle_loops.max() < len(normals)
        assert np.array_equal(loop_vertex[triangle_loops], np.asarray(mesh["triangles"]).ravel()), obj.name
        groups = {}
        normal_ids = np.empty(len(normals), dtype=np.int32)
        for corner, (vertex_index, normal) in enumerate(zip(np.asarray(mesh["triangles"]).ravel(), normals[triangle_loops])):
            key = (int(vertex_index), tuple(np.rint(normal * 512).astype(np.int32)))
            if key not in groups:
                groups[key] = record["normal_offset"] + len(groups)
            normal_ids[triangle_loops[corner]] = groups[key]
        assert len(groups) == record["normal_count"], obj.name
        for triangle_index, triangle in enumerate(mesh["triangles"]):
            distances = triangle_edge_distances(mesh["coords"], triangle, marked)
            for corner, loop_index in enumerate(mesh["loops"][triangle_index]):
                uv0[loop_index, 0] = (vertex_pos_id[loop_vertex[loop_index]] + 0.5) / 4096
                uv1[loop_index] = distances[corner, :2]
                uv2[loop_index, 0] = distances[corner, 2]
                uv2[loop_index, 1] = (normal_ids[loop_index] + 0.5) / 4096
                color[loop_index] = (*source_palette["srgb"], 0.0)
        color_attribute.data.foreach_set("color_srgb", color.ravel())
        for layer, values in zip(uv_layers, (uv0, uv1, uv2)):
            layer.data.foreach_set("uv", values.ravel())
        record["export_rest"] = mesh["coords"].copy()
        record["position_count"] = len(mesh["coords"])
        # new_from_object returns local coordinates. Replace them with the exact
        # world-space rest positions used by the atlas, not the original local mesh.
        obj.data.vertices.foreach_set("co", mesh["coords"].astype(np.float32).ravel())
        obj.data.update()
    # Join into one mesh with one slot per true material. No geometry is joined in the source blend.
    bpy.ops.object.select_all(action="DESELECT")
    export_objects = [record["object"] for record in lod_records]
    for obj in export_objects:
        obj.hide_set(False)
        obj.hide_viewport = False
        obj.hide_render = False
        obj.select_set(True)
    bpy.context.view_layer.objects.active = export_objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.matrix_world = Matrix.Identity(4)
    # Verified with an explicit-factory triangle probe: numeric centimetres
    # retain Blender (X,Y,Z); UE convert_scene=True supplies the one Y flip.
    joined.data.transform(Matrix.Scale(100, 4))
    joined.data.update()
    assert joined.matrix_world.is_identity, 'FBX export must have an identity object transform'
    fbx = ART / "FBX" / f"SM_QuadrupedMiner_VAT_LOD{lod}.fbx"
    bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, object_types={"MESH"}, global_scale=1,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Y", axis_up="Z",
        use_mesh_modifiers=False, mesh_smooth_type="FACE", use_tspace=True, colors_type="SRGB", bake_anim=False, path_mode="STRIP")
    position_count = sum(record["position_count"] for record in lod_records)
    first_frame = 0
    for clip in CLIP_SPECS:
        clip["first_frame"] = first_frame
        clip["sample_count"] = len(clip["selected_frames"])
        first_frame += clip["sample_count"]
    frame_total = first_frame
    position_rows = math.ceil(position_count / 4096)
    normal_rows = math.ceil(normal_offset / 4096)
    position_atlas = np.ones((frame_total * position_rows, 4096, 4), dtype=np.float32)
    normal_atlas = np.empty((frame_total * normal_rows, 4096, 4), dtype=np.uint8)
    normal_atlas[:] = (128, 128, 128, 255)
    maximum_half_error = 0.0
    maximum_normal_error = 0.0
    zero_normal_count = 0
    for clip in CLIP_SPECS:
        positions = dense[clip["scene"]][clip["selected_frames"] - 1]
        assert np.isfinite(positions).all(), (lod, clip["name"], "Non-finite VAT position")
        assert np.abs(positions).max() < 10, (lod, clip["name"], "LOD geometry escaped the source bounds", float(np.abs(positions).max()))
        positions = positions[:, :, (0, 1, 2)] * np.asarray((100, -100, 100), dtype=np.float32)
        half = positions.astype(np.float16).astype(np.float32)
        maximum_half_error = max(maximum_half_error, float(np.abs(half - positions).max()))
        positions = half
        for frame in range(clip["sample_count"]):
            atlas_frame = clip["first_frame"] + frame
            linear = np.arange(position_count, dtype=np.int64)
            # Index pixels explicitly. Raveling a sliced RGB view returns a copy.
            position_atlas[atlas_frame * position_rows + linear // 4096, linear % 4096, :3] = positions[frame]
        normals = clip["normal_frames"]
        lengths = np.linalg.norm(normals, axis=2)
        valid_normals = lengths > 0.5
        zero_normal_count += int(np.count_nonzero(~valid_normals))
        # Degenerate source faces have no meaningful normal. Do not normalize zero.
        normals = np.where(valid_normals[:, :, None], normals / np.maximum(lengths[:, :, None], 1e-12), np.asarray((0, 0, 1), dtype=np.float32))
        normals = normals[:, :, (0, 1, 2)] * np.asarray((1, -1, 1), dtype=np.float32)
        packed = np.rint(np.clip(normals * 0.5 + 0.5, 0, 1) * 255).astype(np.uint8)
        decoded = packed.astype(np.float32) / 255 * 2 - 1
        decoded /= np.maximum(np.linalg.norm(decoded, axis=2, keepdims=True), 1e-8)
        error = np.degrees(np.arccos(np.clip((decoded * normals).sum(axis=2), -1, 1)))
        if np.any(valid_normals):
            maximum_normal_error = max(maximum_normal_error, float(error[valid_normals].max()))
        for frame in range(clip["sample_count"]):
            atlas_frame = clip["first_frame"] + frame
            linear = np.arange(normal_offset, dtype=np.int64)
            normal_atlas[atlas_frame * normal_rows + linear // 4096, linear % 4096, :3] = packed[frame, :, :3]
    assert maximum_half_error <= 0.13, maximum_half_error
    assert maximum_normal_error <= 0.7, maximum_normal_error
    position_file = ART / "Textures" / f"T_QuadrupedMiner_Position_LOD{lod}.exr"
    normal_file = ART / "Textures" / f"T_QuadrupedMiner_Normal_LOD{lod}.png"
    saved_position = exr(position_file, position_atlas, temp_scene)
    png(normal_file, normal_atlas)
    # Padding texels are white/alpha 1; compute bounds from populated rows only.
    minimum = np.asarray((1e9, 1e9, 1e9), dtype=np.float32)
    bounds_maximum = -minimum.copy()
    for clip in CLIP_SPECS:
        for frame in range(clip["sample_count"]):
            atlas_frame = clip["first_frame"] + frame
            block = saved_position[atlas_frame * position_rows:atlas_frame * position_rows + position_rows, :, :3].ravel()[:position_count * 3].reshape((-1, 3))
            minimum = np.minimum(minimum, block.min(axis=0))
            bounds_maximum = np.maximum(bounds_maximum, block.max(axis=0))
    clip_report = []
    for clip in CLIP_SPECS:
        selected = clip.pop("selected_frames")
        normal_frames = clip.pop("normal_frames")
        sampling_maximum, per_vertex = sample_error(dense[clip["scene"]], selected)
        foot_mask = np.zeros(position_count, dtype=bool)
        component_errors = []
        for record in lod_records:
            foot_mask[record["position_offset"]:record["position_offset"] + record["position_count"]] = foot_component(record)
            section = slice(record["position_offset"], record["position_offset"] + record["position_count"])
            error, _ = sample_error(dense[clip["scene"]][:, section], selected)
            component_errors.append(dict(name=record["name"], outline=record["outline"], temporal_error_cm=error,
                normal_samples=int(len(record["representatives"]) * clip["sample_count"]),
                zero_normal_samples=int(np.count_nonzero(np.linalg.norm(normal_frames[:, record["normal_offset"]:record["normal_offset"] + record["normal_count"]], axis=2) <= 0.5))))
        log(f"LOD{lod} {clip['name']} worst components " + str(sorted(component_errors, key=lambda item: item["temporal_error_cm"], reverse=True)[:3]))
        foot_movement = 0.0
        if clip["name"] == "Mining" and foot_mask.any():
            sample_positions = dense[clip["scene"]][:, foot_mask]
            foot_movement = float(np.linalg.norm(sample_positions - sample_positions[:1], axis=2).max() * 100)
        clip_report.append(dict(name=clip["name"], source_scene=clip["scene"], first_frame=clip["first_frame"],
            frames_per_clip=clip["sample_count"], duration_seconds=clip["seconds"], source_frames=selected.tolist(),
            stride_cm=72 if clip["name"] == "Walk" else 0, loop=True,
            maximum_temporal_error_cm=sampling_maximum, maximum_mining_foot_displacement_cm=foot_movement,
            foot_vertex_count=int(foot_mask.sum()), component_errors=component_errors,
            closure_error_cm=float(np.linalg.norm(dense[clip["scene"]][0] - dense[clip["scene"]][-1], axis=1).max() * 100),
            candidates=clip["sampling_candidates"]))
    if lod == 0:
        report["clips"] = clip_report
    lod_report = dict(lod=lod, ratio=ratio, evaluated_triangles=actual_triangles, fbx=str(fbx), clips=clip_report,
        position_vertices=position_count, normal_vertices=normal_offset, texture_width=4096,
        position_rows_per_frame=position_rows, normal_rows_per_frame=normal_rows, texture_frames=frame_total,
        position_size=[4096, frame_total * position_rows], normal_size=[4096, frame_total * normal_rows],
        position_gpu_bytes=frame_total * position_rows * 4096 * 8, normal_gpu_bytes=frame_total * normal_rows * 4096 * 4,
        position=str(position_file), normal=str(normal_file),
        maximum_position_half_error_cm=maximum_half_error, maximum_normal_quantization_error_degrees=maximum_normal_error,
        zero_normal_samples_replaced=zero_normal_count,
        animation_bounds_cm=dict(min=minimum.tolist(), max=bounds_maximum.tolist()),
        source_mesh_count=len(sources), outline_mesh_count=sum(record["outline"] for record in lod_records),
        saved_exr_readback_passed=True, source_geometry_unchanged=lod == 0,
        files_sha256={path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (fbx, position_file, normal_file)})
    report["lods"].append(lod_report)
    report["gpu_animation_bytes"] = sum(item["position_gpu_bytes"] + item["normal_gpu_bytes"] for item in report["lods"])
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")
    log(f"LOD{lod} ready " + json.dumps({key: value for key, value in lod_report.items() if key not in ("files_sha256",)}, ensure_ascii=False))
    bpy.data.scenes.remove(temp_scene)
report["offline_bake_completed"] = True
report["ue_imported"] = False
REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")
log("Complete " + str(REPORT))

import bpy
import bmesh
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parent.parent
BLEND_OUTPUT = ROOT / "blender" / "quadruped-mech-side-panels.blend"
GLB_OUTPUT = ROOT / "public" / "models" / "quadruped-mech-side-panels.glb"
REPORT_OUTPUT = ROOT / "blender" / "modification-report.json"


def connected_components(mesh):
    parent = list(range(len(mesh.vertices)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra
    for edge in mesh.edges:
        union(edge.vertices[0], edge.vertices[1])
    groups = {}
    for i in range(len(mesh.vertices)):
        groups.setdefault(find(i), []).append(i)
    result = [group for group in groups.values() if len(group) >= 4]
    result.sort(key=len, reverse=True)
    return result


def delete_vertices_except(obj, keep_indices):
    keep = set(keep_indices)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()
    delete = [vert for vert in bm.verts if vert.index not in keep]
    bmesh.ops.delete(bm, geom=delete, context="VERTS")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def delete_vertices(obj, delete_indices):
    delete_set = set(delete_indices)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()
    delete = [vert for vert in bm.verts if vert.index in delete_set]
    bmesh.ops.delete(bm, geom=delete, context="VERTS")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def rotate_vertices(obj, indices, pivot, rotation, correction=Vector((0, 0, 0))):
    index_set = set(indices)
    matrix_world = obj.matrix_world.copy()
    matrix_local = matrix_world.inverted()
    for vertex in obj.data.vertices:
        if vertex.index in index_set:
            original_world = matrix_world @ vertex.co + correction
            rotated_world = pivot + rotation @ (original_world - pivot)
            vertex.co = matrix_local @ rotated_world
    obj.data.update()


def world_bvh(obj):
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    polygons = [list(polygon.vertices) for polygon in obj.data.polygons]
    return BVHTree.FromPolygons(points, polygons, all_triangles=False, epsilon=0.0)


def min_surface_distance(source_obj, target_obj):
    source_points = [source_obj.matrix_world @ vertex.co for vertex in source_obj.data.vertices]
    target_bvh = world_bvh(target_obj)
    best = None
    for point in source_points:
        location, normal, face_index, distance = target_bvh.find_nearest(point)
        if location is not None and (best is None or distance < best[0]):
            best = (distance, location - point)
    return best


original_vertex_count = sum(len(obj.data.vertices) for obj in bpy.context.scene.objects if obj.type == "MESH")
original_mesh_count = sum(1 for obj in bpy.context.scene.objects if obj.type == "MESH")

front_obj = bpy.data.objects["Front_Armor · 机甲 · 蓝灰喷涂外甲"]
rear_obj = bpy.data.objects["Rear_Armor · 机甲 · 蓝灰喷涂外甲"]
front_components = connected_components(front_obj.data)
rear_components = connected_components(rear_obj.data)

panel_specs = [
    {"key": "Front_L", "object": front_obj, "indices": front_components[1], "side": -1, "angle": 32.5},
    {"key": "Front_R", "object": front_obj, "indices": front_components[2], "side": 1, "angle": 32.5},
    {"key": "Rear_L", "object": rear_obj, "indices": rear_components[1], "side": -1, "angle": 35.0},
    {"key": "Rear_R", "object": rear_obj, "indices": rear_components[2], "side": 1, "angle": 35.0},
]

# Capture all component indices before any source mesh is edited.
front_rubber = bpy.data.objects["Front_Armor · 机甲 · 橡胶与线缆"]
rear_rubber = bpy.data.objects["Rear_Armor · 机甲 · 橡胶与线缆"]
rear_black = bpy.data.objects["Rear_Armor · 机甲 · 黑色主体框架"]
rear_gunmetal = bpy.data.objects["Rear_Armor · 机甲 · 关节枪铁"]
front_rubber_components = connected_components(front_rubber.data)
rear_rubber_components = connected_components(rear_rubber.data)
rear_black_components = connected_components(rear_black.data)
rear_gunmetal_components = connected_components(rear_gunmetal.data)

attachments = {
    "Front_L": [(front_rubber, front_rubber_components[1], Vector((0, 0, 0)))],
    "Front_R": [(front_rubber, front_rubber_components[2], Vector((0, 0, 0)))],
    "Rear_L": [
        (rear_rubber, rear_rubber_components[0], Vector((0, 0, 0))),
        (rear_black, rear_black_components[0], Vector((0, 0, 0))),
        (rear_black, rear_black_components[1], Vector((0, 0, 0))),
        # The 26 mm correction seats the previously floating fastener on the panel.
        (rear_gunmetal, rear_gunmetal_components[0], Vector((0.0258, -0.0039, -0.0050))),
    ],
    "Rear_R": [
        (rear_rubber, rear_rubber_components[1], Vector((0, 0, 0))),
        (rear_black, rear_black_components[2], Vector((0, 0, 0))),
        (rear_black, rear_black_components[3], Vector((0, 0, 0))),
        (rear_gunmetal, rear_gunmetal_components[1], Vector((-0.0258, -0.0039, -0.0050))),
    ],
}

panel_records = []
created_panels = {}
for spec in panel_specs:
    source = spec["object"]
    indices = spec["indices"]
    points = [source.data.vertices[index].co.copy() for index in indices]
    z_min = min(point.z for point in points)
    z_max = max(point.z for point in points)
    bottom = [point for point in points if point.z <= z_min + (z_max - z_min) * 0.06]
    pivot = sum(bottom, Vector()) / len(bottom)
    signed_angle = -spec["side"] * spec["angle"]
    rotation = Matrix.Rotation(math.radians(signed_angle), 3, "Y")

    panel = source.copy()
    panel.data = source.data.copy()
    panel.name = f"Side_Panel_{spec['key']}"
    source.users_collection[0].objects.link(panel)
    panel.parent = source.parent
    delete_vertices_except(panel, indices)

    # Move the native origin to the lower hinge edge, then rotate around local Y.
    for vertex in panel.data.vertices:
        vertex.co -= pivot
    panel.data.update()
    panel.matrix_world = Matrix.Translation(pivot) @ rotation.to_4x4()
    panel["部件说明"] = "机身侧板，底边内倾贴合"
    panel["内倾角度_度"] = spec["angle"]
    panel["原始来源"] = source.name
    created_panels[spec["key"]] = panel

    for attachment_obj, attachment_indices, correction in attachments[spec["key"]]:
        rotate_vertices(attachment_obj, attachment_indices, pivot, rotation, correction)

    panel_records.append({
        "panel": panel.name,
        "source": source.name,
        "side": "L" if spec["side"] < 0 else "R",
        "inward_angle_degrees": spec["angle"],
        "signed_rotation_y_degrees": signed_angle,
        "pivot": list(pivot),
        "vertex_count": len(panel.data.vertices),
        "attachment_components_rotated": len(attachments[spec["key"]]),
    })

# Remove the four original vertical panel components from their material-batch meshes.
delete_vertices(front_obj, front_components[1] + front_components[2])
delete_vertices(rear_obj, rear_components[1] + rear_components[2])

bpy.context.view_layer.update()

# Validate that the two corrected fasteners now meet their rotated rear panels.
fastener_contacts = {}
for side, component_indices, panel_key in [
    ("L", rear_gunmetal_components[0], "Rear_L"),
    ("R", rear_gunmetal_components[1], "Rear_R"),
]:
    # Build a temporary component BVH from the grouped gunmetal mesh.
    points = [rear_gunmetal.matrix_world @ rear_gunmetal.data.vertices[index].co for index in component_indices]
    panel_bvh = world_bvh(created_panels[panel_key])
    best_distance = None
    for point in points:
        location, normal, face_index, distance = panel_bvh.find_nearest(point)
        if location is not None and (best_distance is None or distance < best_distance):
            best_distance = distance
    fastener_contacts[side] = best_distance

bpy.ops.object.select_all(action="DESELECT")
root = bpy.data.objects.get("MECH_ROOT")
export_objects = [root] + list(root.children_recursive) if root else []
for obj in export_objects:
    if obj.type in {"EMPTY", "MESH"} and not obj.hide_render:
        obj.select_set(True)
if root:
    bpy.context.view_layer.objects.active = root

GLB_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.export_scene.gltf(
    filepath=str(GLB_OUTPUT),
    export_format="GLB",
    use_selection=True,
    export_apply=True,
    export_yup=True,
    export_animations=False,
    export_cameras=False,
    export_lights=False,
    export_extras=True,
    export_image_format="AUTO",
)

bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_OUTPUT), check_existing=False)

final_vertex_count = sum(len(obj.data.vertices) for obj in bpy.context.scene.objects if obj.type == "MESH")
final_mesh_count = sum(1 for obj in bpy.context.scene.objects if obj.type == "MESH")
report = {
    "source_blend": str(ROOT / "blender" / "quadruped-mech-original.blend"),
    "output_blend": str(BLEND_OUTPUT),
    "output_glb": str(GLB_OUTPUT),
    "panels": panel_records,
    "floating_fastener_gap_before_m": 0.02611,
    "floating_fastener_gap_after_m": fastener_contacts,
    "original_mesh_count": original_mesh_count,
    "final_mesh_count": final_mesh_count,
    "original_vertex_count": original_vertex_count,
    "final_vertex_count": final_vertex_count,
    "vertex_count_preserved": original_vertex_count == final_vertex_count,
    "four_independent_panels_created": len(created_panels) == 4,
}
REPORT_OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(report, ensure_ascii=False, indent=2))

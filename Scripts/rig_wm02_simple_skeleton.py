"""Build a non-destructive simplified six-leg rigid rig for WM02 via UE Python.

Run through Scripts/ue_exec.py. The target Skeletal Mesh and Skeleton must have
already been duplicated and linked so the marketplace source assets stay intact.
"""

import json
import math
import traceback

import unreal


STATIC_MESH_PATH = (
    "/Game/Assets/WarMachines/CombatAvatarWM-02/"
    "WM02_Avatar.WM02_Avatar"
)
SKELETAL_MESH_PATH = "/Game/GuLiStrike/Robots/WM02/SKM_WM02_Hexapod_Simple"
SKELETON_PATH = "/Game/GuLiStrike/Robots/WM02/SK_WM02_Hexapod_Simple"
LEG_POLYGON_GROUP = 2


BODY_POSITION = (0.0, 515.5, -885.3)

# Negative X is treated as left. Negative Y is treated as forward for this asset.
CHAINS = {
    "LF": {
        "Hip": (-641.7, -847.8, -885.3),
        "Knee": (-2027.0, -2150.8, -3479.9),
        "Ankle": (-2031.4, -3112.0, -4068.1),
        "Foot": (-2031.4, -3488.0, -5592.9),
    },
    "RF": {
        "Hip": (641.7, -847.8, -885.3),
        "Knee": (2027.0, -2150.8, -3479.9),
        "Ankle": (2031.4, -3112.0, -4068.1),
        "Foot": (2031.4, -3488.0, -5592.9),
    },
    "LM": {
        "Hip": (-1055.0, -131.9, -885.3),
        "Knee": (-3812.1, -1480.5, -3286.4),
        "Ankle": (-3815.2, -2407.1, -4068.1),
        "Foot": (-3810.2, -2783.1, -5592.9),
    },
    "RM": {
        "Hip": (1055.0, -131.9, -885.3),
        "Knee": (3812.1, -1480.5, -3286.4),
        "Ankle": (3815.2, -2407.1, -4068.1),
        "Foot": (3810.2, -2783.1, -5592.9),
    },
    "LR": {
        "Hip": (-413.3, 979.6, -885.3),
        "Knee": (-3812.1, 2451.4, -3344.6),
        "Ankle": (-3815.2, 3438.1, -4068.1),
        "Foot": (-3810.2, 3814.1, -5592.9),
    },
    "RR": {
        "Hip": (413.3, 979.6, -885.3),
        "Knee": (3812.1, 2451.4, -3344.6),
        "Ankle": (3815.2, 3438.1, -4068.1),
        "Foot": (3810.2, 3814.1, -5592.9),
    },
}


def subtract(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vector_tuple(vector):
    return (float(vector.x), float(vector.y), float(vector.z))


def point_segment_distance_sq(point, start, end):
    direction = subtract(end, start)
    offset = subtract(point, start)
    length_sq = sum(value * value for value in direction)
    if length_sq <= 1.0e-8:
        return sum(value * value for value in offset)
    alpha = max(
        0.0,
        min(1.0, sum(offset[i] * direction[i] for i in range(3)) / length_sq),
    )
    closest = tuple(start[i] + direction[i] * alpha for i in range(3))
    delta = subtract(point, closest)
    return sum(value * value for value in delta)


def chain_distance_sq(point, nodes):
    positions = [nodes[name] for name in ("Hip", "Knee", "Ankle", "Foot")]
    return min(
        point_segment_distance_sq(point, positions[index], positions[index + 1])
        for index in range(len(positions) - 1)
    )


def add_bones(report):
    existing_info = unreal.SkeletonService.list_bones(SKELETAL_MESH_PATH)
    existing = {str(info.bone_name) for info in existing_info}
    root_info = next((info for info in existing_info if str(info.bone_name) == "Root"), None)
    if root_info is None:
        raise RuntimeError("Root bone not found")
    root_position = vector_tuple(root_info.global_transform.translation)

    definitions = [("Body", "Root", BODY_POSITION, root_position)]
    for code, nodes in CHAINS.items():
        hip_name = f"Leg_{code}_Hip"
        knee_name = f"Leg_{code}_Knee"
        ankle_name = f"Leg_{code}_Ankle"
        foot_name = f"Leg_{code}_Foot"
        definitions.extend(
            [
                (hip_name, "Body", nodes["Hip"], BODY_POSITION),
                (knee_name, hip_name, nodes["Knee"], nodes["Hip"]),
                (ankle_name, knee_name, nodes["Ankle"], nodes["Knee"]),
                (foot_name, ankle_name, nodes["Foot"], nodes["Ankle"]),
            ]
        )

    added = []
    for bone_name, parent_name, global_position, parent_global_position in definitions:
        if bone_name in existing:
            continue
        local = subtract(global_position, parent_global_position)
        transform = unreal.Transform(location=unreal.Vector(*local))
        if not unreal.SkeletonService.add_bone(
            SKELETAL_MESH_PATH, bone_name, parent_name, transform
        ):
            raise RuntimeError(f"Failed to add bone: {bone_name}")
        existing.add(bone_name)
        added.append(bone_name)

    if added:
        if not unreal.SkeletonService.commit_bone_changes(SKELETAL_MESH_PATH, True):
            raise RuntimeError("commit_bone_changes failed")
        if not unreal.SkeletonService.save_asset(SKELETAL_MESH_PATH):
            raise RuntimeError("Failed to save mesh after bone commit")
        if not unreal.SkeletonService.save_asset(SKELETON_PATH):
            raise RuntimeError("Failed to save skeleton after bone commit")

    report["bones_added"] = added
    report["bone_count_after_add"] = len(
        unreal.SkeletonService.list_bones(SKELETAL_MESH_PATH)
    )


def component_polygon_group(mesh_description, vertex_id):
    polygons = mesh_description.get_vertex_connected_polygons(vertex_id)
    if not polygons:
        return -1
    return int(
        mesh_description.get_polygon_polygon_group(polygons[0]).id_value
    )


def choose_leg_bone(center):
    code = min(CHAINS, key=lambda item: chain_distance_sq(center, CHAINS[item]))
    if center[2] < -4750.0:
        joint = "Foot"
    elif center[2] < -3850.0:
        joint = "Ankle"
    elif center[2] < -2450.0:
        joint = "Knee"
    else:
        joint = "Hip"
    return f"Leg_{code}_{joint}"


def apply_rigid_weights(report):
    static_mesh = unreal.EditorAssetLibrary.load_asset(STATIC_MESH_PATH)
    skeletal_mesh = unreal.EditorAssetLibrary.load_asset(SKELETAL_MESH_PATH)
    if not static_mesh or not skeletal_mesh:
        raise RuntimeError("Source or target mesh failed to load")
    assigned_skeleton = skeletal_mesh.get_editor_property("skeleton")
    if not assigned_skeleton or assigned_skeleton.get_path_name().split(".")[0] != SKELETON_PATH:
        raise RuntimeError("Target mesh is not using its private skeleton")

    mesh_description = static_mesh.get_static_mesh_description(0)
    modifier = unreal.SkinWeightModifier()
    if not modifier.set_skeletal_mesh(skeletal_mesh):
        raise RuntimeError("SkinWeightModifier could not load target mesh")
    vertex_count = mesh_description.get_vertex_count()
    if modifier.get_num_vertices() != vertex_count:
        raise RuntimeError(
            f"Vertex order mismatch: static={vertex_count}, skeletal={modifier.get_num_vertices()}"
        )

    positions = {}
    valid_vertices = []
    for index in range(vertex_count):
        vertex_id = unreal.VertexID(index)
        if mesh_description.is_vertex_valid(vertex_id):
            positions[index] = vector_tuple(mesh_description.get_vertex_position(vertex_id))
            valid_vertices.append(index)

    unseen = set(valid_vertices)
    assignments = {}
    component_counts = {}
    set_failures = []
    component_total = 0
    while unseen:
        seed = unseen.pop()
        stack = [seed]
        component = [seed]
        while stack:
            current = stack.pop()
            adjacent = mesh_description.get_vertex_adjacent_vertices(
                unreal.VertexID(current)
            )
            for neighbor in adjacent:
                neighbor_index = int(neighbor.id_value)
                if neighbor_index in unseen:
                    unseen.remove(neighbor_index)
                    stack.append(neighbor_index)
                    component.append(neighbor_index)

        component_total += 1
        center = tuple(
            sum(positions[index][axis] for index in component) / len(component)
            for axis in range(3)
        )
        group_id = component_polygon_group(
            mesh_description, unreal.VertexID(component[0])
        )
        bone_name = "Body" if group_id != LEG_POLYGON_GROUP else choose_leg_bone(center)
        component_counts[bone_name] = component_counts.get(bone_name, 0) + 1
        assignments[bone_name] = assignments.get(bone_name, 0) + len(component)
        weights = {unreal.Name(bone_name): 1.0}
        for index in component:
            if not modifier.set_vertex_weights(index, weights, True):
                if len(set_failures) < 20:
                    set_failures.append(index)

    if set_failures:
        raise RuntimeError(f"Weight writes failed, first vertices: {set_failures}")
    modifier.normalize_all_weights()
    modifier.enforce_max_influences(1)
    if not modifier.commit_weights_to_skeletal_mesh():
        raise RuntimeError("commit_weights_to_skeletal_mesh failed")
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        skeletal_mesh, only_if_is_dirty=False
    ):
        raise RuntimeError("Failed to save weighted skeletal mesh")

    report["component_count"] = component_total
    report["vertex_assignments"] = assignments
    report["component_assignments"] = component_counts
    report["weight_vertex_count"] = modifier.get_num_vertices()


def main():
    report = {"success": False, "errors": []}
    try:
        add_bones(report)
        apply_rigid_weights(report)
        report["success"] = True
    except Exception:
        report["errors"].append(traceback.format_exc())
    print(json.dumps(report, ensure_ascii=False, default=str))


if __name__ == "__main__":
    main()

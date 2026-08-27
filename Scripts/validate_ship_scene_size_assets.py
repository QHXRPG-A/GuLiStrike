"""Read-only validation for the ship and scene size-normalization assets."""

import json
import math
import re
import traceback

import unreal


REPORT_PATH = "D:/UE5.7/test1/Data/tmp_size_asset_validation_report.json"
DREAD_PATH = (
    "/Game/Assets/Arma/CombatAvatarFly-01/StaticMeshes/"
    "SM_Dreadnought_Hull.SM_Dreadnought_Hull"
)
DRONE_PATH = (
    "/Game/Assets/Arma/fly-02/sci_fi_surveillance_drone/StaticMeshes/"
    "sci_fi_surveillance_drone.sci_fi_surveillance_drone"
)
ROBOT_PATH = (
    "/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes/"
    "cannon_war_machine.cannon_war_machine"
)
SKELETON_PATH = (
    "/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes/"
    "cannon_war_machine_Skeleton1.cannon_war_machine_Skeleton1"
)
PARENT_BP = "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip"
CHILD_BP = "/Game/GuLiStrike/Ship/BP_CombatAvatarFly01"

EXPECTED_BOUNDS = {
    "dread": [19378.0390625, 45023.203125, 6984.66015625],
    "drone": [2352.538330078125, 2575.222900390625, 527.1665649414062],
    "robot": [1441.22119140625, 1441.22119140625, 1187.7590599060059],
}
EXPECTED_ROBOT_SLOTS = [
    "metal_1_1_sub",
    "metal_2_2_sub",
    "escudo_2_sub",
    "metal_2_3_sub",
    "oro_2_sub",
    "metal_4_sub",
    "oro_1_sub",
    "metal_2_1_sub",
    "cuerda_sub",
    "metal_3_1_sub",
    "escudo_sub_1",
    "metal_3_2_sub",
    "metal_1_2_sub",
    "metal_3_3_sub",
    "escudo_3_sub",
    "oro_3_sub",
]


def vector_values(vector):
    return [float(vector.x), float(vector.y), float(vector.z)]


def bounds_size(asset):
    bounds = asset.get_bounds()
    return [
        bounds.box_extent.x * 2.0,
        bounds.box_extent.y * 2.0,
        bounds.box_extent.z * 2.0,
    ]


def close_values(actual, expected, abs_tol=0.1, rel_tol=1.0e-5):
    return all(
        math.isclose(a, e, abs_tol=abs_tol, rel_tol=rel_tol)
        for a, e in zip(actual, expected)
    )


def cdo_components(package_path):
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(package_path)
    if not generated_class:
        raise RuntimeError(f"Generated class not found: {package_path}")
    cdo = unreal.get_default_object(generated_class)
    return cdo, {
        component.get_name(): component
        for component in cdo.get_components_by_class(unreal.SceneComponent)
    }


def export_table(asset_name):
    path = f"/Game/GuLiStrike/Data/{asset_name}.{asset_name}"
    table = unreal.load_object(None, path)
    if not table:
        raise RuntimeError(f"DataTable unavailable: {path}")
    rows = json.loads(
        unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    )
    return {row["Name"]: row for row in rows}


def parse_vector(value):
    if isinstance(value, dict):
        return [float(value[axis]) for axis in ("X", "Y", "Z")]
    numbers = [float(item) for item in re.findall(r"[-+0-9.eE]+", str(value))]
    return numbers[:3]


report = {"success": False, "checks": {}, "errors": []}
try:
    dread = unreal.load_object(None, DREAD_PATH)
    drone = unreal.load_object(None, DRONE_PATH)
    robot = unreal.load_object(None, ROBOT_PATH)
    if not dread or not drone or not robot:
        raise RuntimeError("One or more target meshes failed to load")

    mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    dread_scales = [
        vector_values(mesh_subsystem.get_lod_build_settings(dread, lod).build_scale3d)
        for lod in range(dread.get_num_lods())
    ]
    drone_scales = [
        vector_values(mesh_subsystem.get_lod_build_settings(drone, lod).build_scale3d)
        for lod in range(drone.get_num_lods())
    ]

    body_dread = dread.get_editor_property("body_setup")
    body_drone = drone.get_editor_property("body_setup")
    robot_materials = list(robot.get_editor_property("materials"))
    robot_slots = [
        str(material.get_editor_property("material_slot_name"))
        for material in robot_materials
    ]
    robot_material_assets = [
        material.get_editor_property("material_interface").get_path_name()
        if material.get_editor_property("material_interface")
        else None
        for material in robot_materials
    ]
    robot_skeleton = robot.get_editor_property("skeleton")
    robot_physics = robot.get_editor_property("physics_asset")

    mesh_checks = {
        "dread_bounds": close_values(bounds_size(dread), EXPECTED_BOUNDS["dread"]),
        "dread_build_scale": all(
            close_values(scale, [0.5, 0.5, 0.5], 1.0e-5)
            for scale in dread_scales
        ),
        "dread_collision": "CTF_USE_SIMPLE_AND_COMPLEX"
        in str(body_dread.get_editor_property("collision_trace_flag")),
        "drone_bounds": close_values(bounds_size(drone), EXPECTED_BOUNDS["drone"]),
        "drone_build_scale": all(
            close_values(scale, [2.0, 2.0, 2.0], 1.0e-5)
            for scale in drone_scales
        ),
        "drone_collision": "CTF_USE_DEFAULT"
        in str(body_drone.get_editor_property("collision_trace_flag")),
        "robot_bounds": close_values(bounds_size(robot), EXPECTED_BOUNDS["robot"]),
        "robot_skeleton": bool(robot_skeleton)
        and robot_skeleton.get_path_name() == SKELETON_PATH,
        "robot_physics_asset": robot_physics is None,
        "robot_material_slots": robot_slots == EXPECTED_ROBOT_SLOTS,
        "robot_material_assets": len(robot_material_assets) == 16
        and all(robot_material_assets),
    }
    report["meshes"] = {
        "dread_bounds": bounds_size(dread),
        "dread_lod_scales": dread_scales,
        "dread_collision": str(body_dread.get_editor_property("collision_trace_flag")),
        "drone_bounds": bounds_size(drone),
        "drone_lod_scales": drone_scales,
        "drone_collision": str(body_drone.get_editor_property("collision_trace_flag")),
        "robot_bounds": bounds_size(robot),
        "robot_skeleton": robot_skeleton.get_path_name() if robot_skeleton else None,
        "robot_physics_asset": robot_physics.get_path_name() if robot_physics else None,
        "robot_material_slots": robot_slots,
        "robot_material_assets": robot_material_assets,
    }
    report["checks"].update(mesh_checks)

    _parent_cdo, parent_components = cdo_components(PARENT_BP)
    _child_cdo, child_components = cdo_components(CHILD_BP)
    parent_collision = parent_components["CollisionCylinder"]
    child_collision = child_components["CollisionCylinder"]
    child_hull = child_components["Hull Mesh"]
    bp_checks = {
        "parent_scale": close_values(
            vector_values(parent_collision.get_editor_property("relative_scale3d")),
            [0.3, 0.3, 0.3],
            1.0e-4,
        ),
        "child_scale": close_values(
            vector_values(child_collision.get_editor_property("relative_scale3d")),
            [1.0, 1.0, 1.0],
            1.0e-4,
        ),
        "capsule_radius": math.isclose(
            child_collision.get_unscaled_capsule_radius(), 800.0, abs_tol=1.0e-4
        ),
        "capsule_half_height": math.isclose(
            child_collision.get_unscaled_capsule_half_height(), 2000.0, abs_tol=1.0e-4
        ),
        "hull_scale": close_values(
            vector_values(child_hull.get_editor_property("relative_scale3d")),
            [1.0, 1.0, 1.0],
            1.0e-4,
        ),
        "hull_location": close_values(
            vector_values(child_hull.get_editor_property("relative_location")),
            [-7081.0, 0.0, -464.5],
            1.0e-4,
        ),
        "hull_yaw": math.isclose(
            child_hull.get_editor_property("relative_rotation").yaw,
            -90.0,
            abs_tol=1.0e-4,
        ),
    }
    report["blueprints"] = {
        "parent_scale": vector_values(
            parent_collision.get_editor_property("relative_scale3d")
        ),
        "child_scale": vector_values(
            child_collision.get_editor_property("relative_scale3d")
        ),
        "capsule_radius": child_collision.get_unscaled_capsule_radius(),
        "capsule_half_height": child_collision.get_unscaled_capsule_half_height(),
        "hull_scale": vector_values(child_hull.get_editor_property("relative_scale3d")),
        "hull_location": vector_values(
            child_hull.get_editor_property("relative_location")
        ),
        "hull_yaw": child_hull.get_editor_property("relative_rotation").yaw,
    }
    report["checks"].update(bp_checks)

    tuning = export_table("DT_GuLiStrikeShip_Tuning")["Dreadnought"]
    camera = export_table("DT_GuLiStrikeShip_Camera")["Dreadnought"]
    expected_camera = {
        "CameraDefaultArmLength": 25000.0,
        "CameraZoomStep": 6666.667,
        "CameraZoomMin": 13333.333,
        "CameraZoomMax": 83333.333,
        "CameraCollisionProbeRadius": 3333.333,
        "CameraCollisionMinArm": 833.333,
    }
    data_checks = {
        "tuning_hull_offset": close_values(
            parse_vector(tuning["HullMeshOffset"]), [-7081.0, 0.0, -464.5], 0.01
        )
    }
    for key, expected in expected_camera.items():
        data_checks[f"camera_{key}"] = math.isclose(
            float(camera[key]), expected, rel_tol=5.0e-6, abs_tol=1.0e-3
        )
    report["data_tables"] = {
        "hull_offset": parse_vector(tuning["HullMeshOffset"]),
        "camera": {key: float(camera[key]) for key in expected_camera},
    }
    report["checks"].update(data_checks)

    failed = [name for name, passed in report["checks"].items() if not passed]
    report["failed_checks"] = failed
    report["success"] = not failed
except Exception:
    report["errors"].append(traceback.format_exc())

with open(REPORT_PATH, "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, ensure_ascii=False, indent=2)

print(json.dumps(report, ensure_ascii=False))

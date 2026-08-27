"""Validate the normalized player ship in an active LVL_Main PIE session."""

import json
import math
import traceback

import unreal


REPORT_PATH = "D:/UE5.7/test1/Data/tmp_size_pie_validation_report.json"
EXPECTED_CLASS = "/Game/GuLiStrike/Ship/BP_CombatAvatarFly01.BP_CombatAvatarFly01_C"
EXPECTED_HULL = (
    "/Game/Assets/Arma/CombatAvatarFly-01/StaticMeshes/"
    "SM_Dreadnought_Hull.SM_Dreadnought_Hull"
)


def values(vector):
    return [float(vector.x), float(vector.y), float(vector.z)]


def close(actual, expected, abs_tol=0.1, rel_tol=1.0e-5):
    return all(
        math.isclose(a, e, abs_tol=abs_tol, rel_tol=rel_tol)
        for a, e in zip(actual, expected)
    )


def vector_distance(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


report = {"success": False, "checks": {}, "errors": []}
try:
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_game_world()
    if not world:
        raise RuntimeError("PIE world is not active")

    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if not pawn:
        raise RuntimeError("Player 0 has no pawn")

    components = {
        component.get_name(): component
        for component in pawn.get_components_by_class(unreal.SceneComponent)
    }
    collision = components.get("CollisionCylinder")
    hull = components.get("Hull Mesh")
    spring_arm = components.get("Spring Arm")
    camera = components.get("Camera")
    if not collision or not hull or not spring_arm or not camera:
        raise RuntimeError(f"Runtime ship components incomplete: {sorted(components)}")

    local_min, local_max = hull.get_local_bounds()
    local_size = local_max - local_min
    local_center = (local_min + local_max) * 0.5
    hull_world_center = hull.get_world_transform().transform_location(local_center)
    root_world_location = collision.get_world_location()
    center_error = vector_distance(hull_world_center, root_world_location)
    hull_mesh = hull.get_editor_property("static_mesh")

    player_starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
    player_start = player_starts[0] if len(player_starts) == 1 else None
    ground_hit = unreal.SystemLibrary.line_trace_single(
        world,
        unreal.Vector(root_world_location.x, root_world_location.y, root_world_location.z + 100000.0),
        unreal.Vector(root_world_location.x, root_world_location.y, root_world_location.z - 100000.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
        True,
        [pawn],
        unreal.DrawDebugTrace.NONE,
        True,
    )
    ground_values = ground_hit.to_tuple() if ground_hit else None
    ground_location = ground_values[5] if ground_values else None
    ground_actor = ground_values[9] if ground_values else None
    ground_clearance = (
        root_world_location.z - local_size.z * 0.5 - ground_location.z
        if ground_location
        else None
    )

    camera_values = {
        "default_arm": float(pawn.get_editor_property("camera_default_arm_length")),
        "zoom_step": float(pawn.get_editor_property("camera_zoom_step")),
        "zoom_min": float(pawn.get_editor_property("camera_zoom_min")),
        "zoom_max": float(pawn.get_editor_property("camera_zoom_max")),
        "probe_radius": float(
            pawn.get_editor_property("camera_collision_probe_radius")
        ),
        "min_arm": float(pawn.get_editor_property("camera_collision_min_arm")),
        "actual_arm": float(spring_arm.get_editor_property("target_arm_length")),
    }
    expected_camera = {
        "default_arm": 25000.0,
        "zoom_step": 6666.667,
        "zoom_min": 13333.333,
        "zoom_max": 83333.333,
        "probe_radius": 3333.333,
        "min_arm": 833.333,
    }

    overlaps = [actor.get_name() for actor in pawn.get_overlapping_actors()]
    checks = {
        "player_class": pawn.get_class().get_path_name() == EXPECTED_CLASS,
        "actor_scale": close(values(pawn.get_actor_scale3d()), [1.0, 1.0, 1.0], 1.0e-4),
        "root_scale": close(
            values(collision.get_editor_property("relative_scale3d")),
            [1.0, 1.0, 1.0],
            1.0e-4,
        ),
        "capsule_radius": math.isclose(
            collision.get_unscaled_capsule_radius(), 800.0, abs_tol=1.0e-4
        ),
        "capsule_half_height": math.isclose(
            collision.get_unscaled_capsule_half_height(), 2000.0, abs_tol=1.0e-4
        ),
        "hull_asset": bool(hull_mesh) and hull_mesh.get_path_name() == EXPECTED_HULL,
        "hull_local_size": close(
            values(local_size), [19378.0390625, 45023.203125, 6984.66015625], 0.5
        ),
        "hull_centered": center_error <= 1.0,
        "spawned_at_player_start": bool(player_start)
        and vector_distance(root_world_location, player_start.get_actor_location()) <= 1.0,
        "ground_is_landscape": bool(ground_actor)
        and isinstance(ground_actor, unreal.Landscape),
        "ground_clearance": ground_clearance is not None and ground_clearance >= 199.0,
        "camera_component": camera is not None,
        "camera_arm_in_range": camera_values["min_arm"]
        <= camera_values["actual_arm"]
        <= camera_values["zoom_max"],
        "no_initial_overlaps": not overlaps,
    }
    for key, expected in expected_camera.items():
        checks[f"camera_{key}"] = math.isclose(
            camera_values[key], expected, rel_tol=5.0e-6, abs_tol=1.0e-3
        )

    report.update(
        {
            "world": world.get_path_name(),
            "pawn": pawn.get_name(),
            "pawn_class": pawn.get_class().get_path_name(),
            "pawn_location": values(pawn.get_actor_location()),
            "actor_scale": values(pawn.get_actor_scale3d()),
            "root_scale": values(collision.get_editor_property("relative_scale3d")),
            "capsule_radius": collision.get_unscaled_capsule_radius(),
            "capsule_half_height": collision.get_unscaled_capsule_half_height(),
            "hull_asset": hull_mesh.get_path_name() if hull_mesh else None,
            "hull_local_size": values(local_size),
            "hull_world_center": values(hull_world_center),
            "root_world_location": values(root_world_location),
            "hull_center_error": center_error,
            "player_start_location": values(player_start.get_actor_location())
            if player_start
            else None,
            "ground_actor": ground_actor.get_name() if ground_actor else None,
            "ground_location": values(ground_location) if ground_location else None,
            "ground_clearance": ground_clearance,
            "camera": camera_values,
            "overlaps": overlaps,
            "checks": checks,
            "failed_checks": [name for name, passed in checks.items() if not passed],
        }
    )
    report["success"] = not report["failed_checks"]
except Exception:
    report["errors"].append(traceback.format_exc())

with open(REPORT_PATH, "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, ensure_ascii=False, indent=2)

print(json.dumps(report, ensure_ascii=False))

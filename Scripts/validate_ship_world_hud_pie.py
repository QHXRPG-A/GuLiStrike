"""Record one read-only Ship world-HUD acceptance stage from active PIE.

Run through ``Scripts/ue_exec.py`` after setting these optional globals:

``GULI_WORLD_HUD_STAGE``
    default, omni_center, omni_clamped, bounded_center, bounded_clamped,
    exit, orbit, or multiplayer_default.
``GULI_WORLD_HUD_REQUESTED_RESOLUTION``
    Two-element list used to label the case and validate the PIE client size.
``GULI_WORLD_HUD_RESET``
    True only for the first case in a fresh acceptance run.

The script never changes gameplay or editor state. Debug reticle transitions,
camera rotations, possession, and PIE lifecycle are driven separately so that
the world receives normal frames between each action and observation.
"""

from __future__ import annotations

import json
import math
import os
import traceback

import unreal


OUTPUT = "D:/UE5.7/test1/outputs/review/ship-world-hud/pie-validation.json"
STAGE = str(globals().get("GULI_WORLD_HUD_STAGE", "default"))
REQUESTED_RESOLUTION = list(
    globals().get("GULI_WORLD_HUD_REQUESTED_RESOLUTION", [0, 0])
)
RESET = bool(globals().get("GULI_WORLD_HUD_RESET", False))
WORLD_NODE_NAMES = (
    "ShipWorldHUD_Flight",
    "ShipWorldHUD_Combat",
    "ShipWorldHUD_Reticle",
    "ShipWorldHUD_AimBounds",
)
FIXED_DRAW_SIZES = {
    "ShipWorldHUD_Flight": (280, 144),
    "ShipWorldHUD_Combat": (300, 164),
    "ShipWorldHUD_Reticle": (96, 96),
}
STATUS_WIDGET_CLASS_TOKEN = "/Game/Ship/UI/Widgets/World/WBP_ShipWorldStatus"
MATERIAL = (
    "/Game/Ship/UI/Materials/"
    "M_UI_ShipWorld_NoDepth.M_UI_ShipWorld_NoDepth"
)


def _vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def _vector2(value):
    return [float(value.x), float(value.y)]


def _rotator(value):
    return [float(value.pitch), float(value.yaw), float(value.roll)]


def _visible(component):
    return bool(component.is_visible()) and not bool(
        component.get_editor_property("hidden_in_game")
    )


def _close(actual, expected, tolerance=1.0):
    return abs(float(actual) - float(expected)) <= tolerance


def _clamp_center(desired, draw_size, viewport):
    half = (draw_size[0] * 0.5, draw_size[1] * 0.5)
    minimum = (half[0] + 24.0, half[1] + 24.0)
    maximum = (viewport[0] - minimum[0], viewport[1] - minimum[1])
    return (
        max(minimum[0], min(desired[0], maximum[0]))
        if minimum[0] <= maximum[0]
        else viewport[0] * 0.5,
        max(minimum[1], min(desired[1], maximum[1]))
        if minimum[1] <= maximum[1]
        else viewport[1] * 0.5,
    )


def _world_bounds_corners(mesh):
    local_min, local_max = mesh.get_local_bounds()
    transform = mesh.get_world_transform()
    transformed = []
    for x in (local_min.x, local_max.x):
        for y in (local_min.y, local_max.y):
            for z in (local_min.z, local_max.z):
                transformed.append(transform.transform_location(unreal.Vector(x, y, z)))
    minimum = unreal.Vector(
        min(point.x for point in transformed),
        min(point.y for point in transformed),
        min(point.z for point in transformed),
    )
    maximum = unreal.Vector(
        max(point.x for point in transformed),
        max(point.y for point in transformed),
        max(point.z for point in transformed),
    )
    return [
        unreal.Vector(x, y, z)
        for x in (minimum.x, maximum.x)
        for y in (minimum.y, maximum.y)
        for z in (minimum.z, maximum.z)
    ], (minimum + maximum) * 0.5


def _active_legacy_hud_count(world):
    count = 0
    for widget in unreal.ObjectIterator(unreal.UserWidget):
        try:
            if (
                widget.get_world() == world
                and "/Game/Ship/UI/Widgets/WBP_ShipHUD" in widget.get_class().get_path_name()
                and widget.is_in_viewport()
            ):
                count += 1
        except Exception:
            continue
    return count


def _ship_actors(world):
    return [
        actor
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
        if actor.get_class().get_path_name().endswith("BP_CombatAvatarFly01_C")
    ]


def _status_widgets(world, controller):
    result = []
    for widget in unreal.ObjectIterator(unreal.UserWidget):
        try:
            if (
                widget.get_world() == world
                and STATUS_WIDGET_CLASS_TOKEN in widget.get_class().get_path_name()
                and widget.get_owning_player() == controller
                and widget.is_in_viewport()
            ):
                result.append(widget)
        except Exception:
            continue
    return result


def _inspect_local_ship(world, ship):
    controller = ship.get_controller()
    components = {
        component.get_name(): component
        for component in ship.get_components_by_class(unreal.ActorComponent)
    }
    nodes = {name: components.get(name) for name in WORLD_NODE_NAMES}
    missing = [name for name, node in nodes.items() if node is None]
    status_widgets = _status_widgets(world, controller) if controller else []
    result = {
        "world": world.get_path_name(),
        "ship": ship.get_name(),
        "controller": controller.get_name() if controller else None,
        "missing_world_nodes": missing,
        "status_widget_count": len(status_widgets),
        "checks": {},
    }
    checks = result["checks"]
    if missing or not controller or len(status_widgets) != 1:
        checks["four_world_nodes_and_one_status_widget"] = False
        return result

    status = status_widgets[0]

    width, height = controller.get_viewport_size()
    viewport = (float(width), float(height))
    result["viewport"] = [int(width), int(height)]
    checks["requested_width"] = not REQUESTED_RESOLUTION[0] or int(width) == int(
        REQUESTED_RESOLUTION[0]
    )
    # PIE window chrome can add a few client pixels depending on DPI policy.
    checks["requested_height"] = not REQUESTED_RESOLUTION[1] or abs(
        int(height) - int(REQUESTED_RESOLUTION[1])
    ) <= 8
    checks["four_world_nodes_and_one_status_widget"] = len(nodes) == 4

    node_rows = {}
    for name, node in nodes.items():
        draw = node.get_editor_property("draw_size")
        widget = node.get_user_widget_object()
        material = node.get_material(0)
        row = {
            "visible": _visible(node),
            "draw_size": [int(draw.x), int(draw.y)],
            "scale": _vector(node.get_world_scale()),
            "widget_class": widget.get_class().get_path_name() if widget else None,
            "material": material.get_path_name() if material else None,
            "tick_mode": str(node.get_editor_property("tick_mode")),
            "collision": str(node.get_collision_enabled()),
            "world_space": str(node.get_widget_space()),
            "manual_redraw": bool(node.get_manually_redraw()),
            "window_focusable": bool(node.get_window_focusable()),
            "receive_hardware_input": bool(
                node.get_editor_property("receive_hardware_input")
            ),
            "widget_focusable": bool(widget.get_editor_property("is_focusable"))
            if widget
            else True,
        }
        node_rows[name] = row
        checks[name + ".world_space"] = "WORLD" in row["world_space"].upper()
        checks[name + ".no_collision"] = "NO_COLLISION" in row["collision"].upper()
        checks[name + ".manual_redraw"] = row["manual_redraw"]
        checks[name + ".tick_disabled"] = "DISABLED" in row["tick_mode"].upper()
        checks[name + ".no_input"] = not row["receive_hardware_input"]
        checks[name + ".not_focusable"] = not row["window_focusable"] and not row[
            "widget_focusable"
        ]
        checks[name + ".no_depth_material"] = row["material"] == MATERIAL
        if name in FIXED_DRAW_SIZES:
            checks[name + ".draw_size"] = tuple(row["draw_size"]) == FIXED_DRAW_SIZES[
                name
            ]
    result["nodes"] = node_rows

    viewport_scale = float(unreal.WidgetLayoutLibrary.get_viewport_scale(world))
    # ``APlayerController::GetLocalPlayer`` is not reflected by every UE Python
    # build.  Single-player PIE still has a full-width player region, while
    # reflected builds can additionally validate the split-screen fraction.
    local_player = (
        controller.get_local_player()
        if hasattr(controller, "get_local_player")
        else None
    )
    player_size = (
        local_player.get_editor_property("size")
        if local_player
        else unreal.Vector2D(1, 1)
    )
    status_position = (
        status.get_position_in_viewport()
        if hasattr(status, "get_position_in_viewport")
        else None
    )
    status_size = (
        status.get_desired_size_in_viewport()
        if hasattr(status, "get_desired_size_in_viewport")
        else status.get_desired_size()
    )
    status_alignment = status.get_alignment_in_viewport()
    expected_status_x = width * float(player_size.x) / max(viewport_scale, 0.001) * 0.5
    result["status_widget"] = {
        "class": status.get_class().get_path_name(),
        "visible": "HIT_TEST_INVISIBLE" in str(status.get_visibility()).upper(),
        "focusable": bool(status.get_editor_property("is_focusable")),
        "position": _vector2(status_position) if status_position else None,
        "desired_size": _vector2(status_size),
        "alignment": _vector2(status_alignment),
        "viewport_scale": viewport_scale,
        "z_order_contract": 20,
        "runtime_position_api_available": status_position is not None,
    }
    checks["status_visible"] = result["status_widget"]["visible"]
    checks["status_no_input_or_focus"] = (
        not result["status_widget"]["focusable"]
        and "HIT_TEST_INVISIBLE" in str(status.get_visibility()).upper()
    )
    checks["status_size_420x72"] = _close(status_size.x, 420.0, 0.1) and _close(
        status_size.y, 72.0, 0.1
    )
    checks["status_top_center_alignment"] = _close(
        status_alignment.x, 0.5, 0.001
    ) and _close(status_alignment.y, 0.0, 0.001)
    if status_position is not None:
        checks["status_top_center_24"] = _close(
            status_position.x, expected_status_x, 1.0
        ) and _close(status_position.y, 24.0, 0.1)
    else:
        result["unobserved_checks"] = [
            "status_top_center_24: UE 5.7 Python does not expose viewport-position getter"
        ]

    checks["flight_visible"] = node_rows["ShipWorldHUD_Flight"]["visible"]
    checks["combat_visible"] = node_rows["ShipWorldHUD_Combat"]["visible"]
    checks["legacy_screen_hud_absent"] = _active_legacy_hud_count(world) == 0

    hull = components.get("Hull Mesh")
    camera_manager = controller.player_camera_manager
    if hull and camera_manager and width > 0 and height > 0:
        corners, hull_world_center = _world_bounds_corners(hull)
        camera_location = camera_manager.get_camera_location()
        camera_rotation = camera_manager.get_camera_rotation()
        camera_forward = camera_rotation.get_forward_vector()
        depths = [
            (corner.x - camera_location.x) * camera_forward.x
            + (corner.y - camera_location.y) * camera_forward.y
            + (corner.z - camera_location.z) * camera_forward.z
            for corner in corners
        ]
        projection_points = [
            corner for corner, depth in zip(corners, depths) if depth >= 10.0
        ]
        for a, b in (
            (0, 1), (0, 2), (0, 4), (1, 3), (1, 5), (2, 3),
            (2, 6), (3, 7), (4, 5), (4, 6), (5, 7), (6, 7),
        ):
            if (depths[a] >= 10.0) == (depths[b] >= 10.0):
                continue
            depth_range = depths[b] - depths[a]
            if abs(depth_range) <= 1.0e-8:
                continue
            alpha = max(0.0, min(1.0, (10.0 - depths[a]) / depth_range))
            projection_points.append(corners[a] + (corners[b] - corners[a]) * alpha)
        if not projection_points:
            projection_points = [hull_world_center]
        projected = [
            point
            for point in (
                controller.project_world_location_to_screen(world_point)
                for world_point in projection_points
            )
            if point is not None
        ]
        result["projected_hull_corner_count"] = len(projected)
        if not projected:
            result.setdefault("unobserved_checks", []).append(
                "world panel placement: no hull corner projected in this offscreen PIE frame"
            )
            projected = [unreal.Vector2D(viewport[0] * 0.5, viewport[1] * 0.5)]
        hull_min = (
            min(point.x for point in projected),
            min(point.y for point in projected),
        )
        hull_max = (
            max(point.x for point in projected),
            max(point.y for point in projected),
        )
        hull_center = (
            (hull_min[0] + hull_max[0]) * 0.5,
            (hull_min[1] + hull_max[1]) * 0.5,
        )
        expected_centers = {
            "ShipWorldHUD_Flight": _clamp_center(
                (hull_min[0] - 28.0 - 140.0, hull_center[1]), (280.0, 144.0), viewport
            ),
            "ShipWorldHUD_Combat": _clamp_center(
                (hull_max[0] + 28.0 + 150.0, hull_center[1]), (300.0, 164.0), viewport
            ),
        }
        placements = {}
        for name, expected in expected_centers.items():
            actual = controller.project_world_location_to_screen(nodes[name].get_world_location())
            placements[name] = {
                "actual": _vector2(actual),
                "expected": [float(expected[0]), float(expected[1])],
            }
            checks[name + ".projected_placement"] = _close(actual.x, expected[0], 2.0) and _close(
                actual.y, expected[1], 2.0
            )
        result["projected_placements"] = placements

        delta = hull_world_center - camera_location
        plane_distance = (
            delta.x * camera_forward.x
            + delta.y * camera_forward.y
            + delta.z * camera_forward.z
        )
        horizontal_half = math.radians(camera_manager.get_fov_angle() * 0.5)
        vertical_half = math.atan(math.tan(horizontal_half) / (width / height))
        expected_scale = 2.0 * plane_distance * math.tan(vertical_half) / height
        result["constant_pixel_scale"] = {
            "plane_distance": plane_distance,
            "fov": camera_manager.get_fov_angle(),
            "expected": expected_scale,
            "actual": node_rows["ShipWorldHUD_Flight"]["scale"][0],
        }
        for name in (
            "ShipWorldHUD_Flight",
            "ShipWorldHUD_Combat",
        ):
            checks[name + ".constant_pixel_scale"] = math.isclose(
                node_rows[name]["scale"][0], expected_scale, rel_tol=0.002, abs_tol=0.02
            )

    aim = components.get("ShipAim")
    spring_arm = components.get("Spring Arm")
    mode = str(aim.get_reticle_mode()) if aim else "missing"
    position = aim.get_reticle_screen_position_normalized() if aim else unreal.Vector2D()
    result["aim"] = {
        "mode": mode,
        "position_normalized": _vector2(position),
        "camera_rotation": _rotator(camera_manager.get_camera_rotation())
        if camera_manager
        else None,
        "rotation_lag": bool(spring_arm.get_editor_property("enable_camera_rotation_lag"))
        if spring_arm
        else None,
        "mouse_cursor": bool(controller.get_editor_property("show_mouse_cursor")),
    }
    expected_mode = {
        "default": "NONE",
        "omni_center": "OMNI",
        "omni_clamped": "OMNI",
        "bounded_center": "BOUNDED",
        "bounded_clamped": "BOUNDED",
        "exit": "NONE",
        "orbit": "NONE",
        "multiplayer_default": "NONE",
    }.get(STAGE)
    if expected_mode:
        checks["expected_reticle_mode"] = expected_mode in mode.upper()
    aiming = expected_mode in ("OMNI", "BOUNDED")
    checks["reticle_visibility"] = node_rows["ShipWorldHUD_Reticle"]["visible"] == aiming
    checks["bounds_visibility"] = node_rows["ShipWorldHUD_AimBounds"]["visible"] == (
        expected_mode == "BOUNDED"
    )
    if aiming and camera_manager and spring_arm:
        ship_forward = ship.get_actor_forward_vector()
        camera_forward = camera_manager.get_camera_rotation().get_forward_vector()
        alignment = (
            ship_forward.x * camera_forward.x
            + ship_forward.y * camera_forward.y
            + ship_forward.z * camera_forward.z
        )
        result["aim"]["camera_ship_forward_dot"] = alignment
        checks["aim_camera_aligned"] = alignment >= 0.9999
        checks["aim_camera_roll_removed"] = abs(camera_manager.get_camera_rotation().roll) <= 0.02
        checks["aim_rotation_lag_disabled"] = not result["aim"]["rotation_lag"]
        checks["aim_system_cursor_hidden"] = not result["aim"]["mouse_cursor"]
    if STAGE in ("omni_center", "bounded_center"):
        checks["claim_resets_to_center"] = _close(position.x, 0.5, 1.0e-5) and _close(
            position.y, 0.5, 1.0e-5
        )
    if STAGE == "omni_clamped":
        expected = ((width - 72.0) / width, 72.0 / height)
        checks["omni_safe_viewport_clamp"] = _close(position.x, expected[0], 1.0e-5) and _close(
            position.y, expected[1], 1.0e-5
        )
    if STAGE == "bounded_clamped":
        expected = ((0.3 * width + 60.0) / width, (0.65 * height - 60.0) / height)
        checks["bounded_rectangle_clamp"] = _close(position.x, expected[0], 1.0e-5) and _close(
            position.y, expected[1], 1.0e-5
        )
    if expected_mode == "BOUNDED":
        bounds_size = nodes["ShipWorldHUD_AimBounds"].get_editor_property("draw_size")
        checks["bounded_dynamic_draw_size"] = int(bounds_size.x) == round(width * 0.4) and int(
            bounds_size.y
        ) == round(height * 0.3)
    if STAGE == "exit" and spring_arm:
        checks["exit_rotation_lag_restored"] = result["aim"]["rotation_lag"]
    result["failed_checks"] = [name for name, passed in checks.items() if not passed]
    result["success"] = not result["failed_checks"]
    return result


case = {
    "stage": STAGE,
    "requested_resolution": REQUESTED_RESOLUTION,
    "worlds": [],
    "errors": [],
}
try:
    worlds = sorted(
        [
            world
            for world in unreal.ObjectIterator(unreal.World)
            if "UEDPIE_" in world.get_path_name()
        ],
        key=lambda world: world.get_path_name(),
    )
    if not worlds:
        raise RuntimeError("PIE world is not active")
    for world in worlds:
        ships = _ship_actors(world)
        entry = {
            "world": world.get_path_name(),
            "ship_count": len(ships),
            "local_ships": [],
            "remote_ship_node_counts": [],
        }
        for ship in ships:
            runtime_nodes = [
                component
                for component in ship.get_components_by_class(unreal.ActorComponent)
                if component.get_name().startswith("ShipWorldHUD_")
            ]
            if ship.is_locally_controlled() and ship.get_controller() and ship.get_controller().is_local_controller():
                entry["local_ships"].append(_inspect_local_ship(world, ship))
            else:
                entry["remote_ship_node_counts"].append(
                    {"ship": ship.get_name(), "nodes": len(runtime_nodes)}
                )
        entry["remote_ships_have_no_nodes"] = all(
            item["nodes"] == 0 for item in entry["remote_ship_node_counts"]
        )
        case["worlds"].append(entry)
    local_results = [
        result
        for world_entry in case["worlds"]
        for result in world_entry["local_ships"]
    ]
    case["checks"] = {
        "at_least_one_local_ship": bool(local_results),
        "all_local_ship_checks_pass": bool(local_results)
        and all(result.get("success", False) for result in local_results),
        "all_remote_ships_have_no_nodes": all(
            world_entry["remote_ships_have_no_nodes"] for world_entry in case["worlds"]
        ),
    }
    case["success"] = all(case["checks"].values())
except Exception:
    case["errors"].append(traceback.format_exc())
    case["success"] = False

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
if RESET or not os.path.exists(OUTPUT):
    report = {"schema": "guli.ship-world-hud.pie-acceptance.v1", "cases": []}
else:
    with open(OUTPUT, encoding="utf-8") as stream:
        report = json.load(stream)
report["cases"].append(case)
report["success"] = bool(report["cases"]) and all(
    item.get("success", False) for item in report["cases"]
)
with open(OUTPUT, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

unreal.MCPythonHelper.submit_result(json.dumps(case, ensure_ascii=False))

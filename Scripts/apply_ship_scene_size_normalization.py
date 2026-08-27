"""Apply the approved ship/scene size normalization in the live UE editor."""

import json
import math
import traceback

import unreal


MAP_PATH = "/Game/Maps/LVL_Main"
BASELINE_PATH = (
    "C:/Users/a/AppData/Local/Temp/GuLiStrike_ship_size_baseline_20260827.json"
)
REPORT_PATH = "D:/UE5.7/test1/Data/tmp_size_normalization_report.json"

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
PARENT_BP_PATH = "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip.BP_GuLiStrikeShip"
CHILD_BP_PATH = "/Game/GuLiStrike/Ship/BP_CombatAvatarFly01.BP_CombatAvatarFly01"

EXPECTED_COUNTS = {"dread": 2, "drone": 1, "robot": 128}
EXPECTED_OLD_SCALES = {"dread": 0.5, "drone": 2.0, "robot": 30.0}


def values(vector):
    return [float(vector.x), float(vector.y), float(vector.z)]


def rotation_values(rotation):
    return [float(rotation.roll), float(rotation.pitch), float(rotation.yaw)]


def parse_vector(text):
    if isinstance(text, (list, tuple)):
        return [float(value) for value in text]
    return [float(value) for value in str(text).split()]


def close_vector(actual, expected, abs_tol=0.1, rel_tol=1.0e-5):
    return all(
        math.isclose(a, e, abs_tol=abs_tol, rel_tol=rel_tol)
        for a, e in zip(actual, expected)
    )


def asset_path(asset):
    return asset.get_path_name() if asset else None


def skeletal_asset(component):
    try:
        return component.get_editor_property("skeletal_mesh_asset")
    except Exception:
        try:
            return component.get_skinned_asset()
        except Exception:
            return component.get_editor_property("skeletal_mesh")


def classify_actor(actor):
    static_component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if static_component:
        path = asset_path(static_component.get_editor_property("static_mesh"))
        if path == DREAD_PATH:
            return "dread"
        if path == DRONE_PATH:
            return "drone"

    skeletal_component = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    if skeletal_component and asset_path(skeletal_asset(skeletal_component)) == ROBOT_PATH:
        return "robot"
    return None


def components_by_name(blueprint):
    package_path = blueprint.get_path_name().split(".", 1)[0]
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(package_path)
    if not generated_class:
        raise RuntimeError(f"Unable to load generated class for {package_path}")
    cdo = unreal.get_default_object(generated_class)
    return {
        component.get_name(): component
        for component in cdo.get_components_by_class(unreal.SceneComponent)
    }


def component_state(component):
    return {
        "scale": values(component.get_editor_property("relative_scale3d")),
        "location": values(component.get_editor_property("relative_location")),
        "rotation": rotation_values(component.get_editor_property("relative_rotation")),
    }


def bounds_size(actor):
    _origin, extent = actor.get_actor_bounds(False)
    return [extent.x * 2.0, extent.y * 2.0, extent.z * 2.0]


report = {"success": False, "map": MAP_PATH}
try:
    with open(BASELINE_PATH, encoding="utf-8") as baseline_file:
        baseline = json.load(baseline_file)

    loaded_world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if not loaded_world:
        raise RuntimeError(f"Unable to load map: {MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    groups = {name: [] for name in EXPECTED_COUNTS}
    for actor in actor_subsystem.get_all_level_actors():
        group = classify_actor(actor)
        if group:
            groups[group].append(actor)

    counts_before = {name: len(actors) for name, actors in groups.items()}
    if counts_before != EXPECTED_COUNTS:
        raise RuntimeError(
            f"Target actor count mismatch: {counts_before} != {EXPECTED_COUNTS}"
        )

    baseline_by_group = {
        group: {entry["name"]: entry for entry in baseline["groups"][group]}
        for group in EXPECTED_COUNTS
    }
    for group, actors in groups.items():
        if set(actor.get_name() for actor in actors) != set(baseline_by_group[group]):
            raise RuntimeError(f"Actor identity mismatch for {group}")
        expected_scale = EXPECTED_OLD_SCALES[group]
        for actor in actors:
            current_scale = values(actor.get_actor_scale3d())
            valid_old = close_vector(current_scale, [expected_scale] * 3, abs_tol=1.0e-4)
            valid_new = close_vector(current_scale, [1.0, 1.0, 1.0], abs_tol=1.0e-4)
            if not (valid_old or valid_new):
                raise RuntimeError(
                    f"Unexpected scale for {actor.get_name()}: {current_scale}"
                )

    transforms_before = {
        actor.get_name(): {
            "location": values(actor.get_actor_location()),
            "rotation": rotation_values(actor.get_actor_rotation()),
        }
        for actors in groups.values()
        for actor in actors
    }

    normalized = {name: 0 for name in EXPECTED_COUNTS}
    for group, actors in groups.items():
        for actor in actors:
            if not close_vector(values(actor.get_actor_scale3d()), [1.0] * 3, 1.0e-4):
                actor.modify()
                actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
                normalized[group] += 1

    child_blueprint = unreal.load_object(None, CHILD_BP_PATH)
    parent_blueprint = unreal.load_object(None, PARENT_BP_PATH)
    if not child_blueprint or not parent_blueprint:
        raise RuntimeError("Unable to load ship blueprints")

    child_blueprint.modify()
    child_components = components_by_name(child_blueprint)
    collision = child_components.get("CollisionCylinder")
    hull = child_components.get("Hull Mesh")
    if not collision or not hull:
        raise RuntimeError(
            f"Child components unavailable: {sorted(child_components.keys())}"
        )

    collision.modify()
    collision.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    hull.modify()
    hull.set_relative_location(unreal.Vector(-7081.0, 0.0, -464.5), False, False)
    hull.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    hull.set_relative_rotation(
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0), False, False
    )

    unreal.BlueprintEditorLibrary.compile_blueprint(child_blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(child_blueprint, False):
        raise RuntimeError("Failed to save BP_CombatAvatarFly01")

    if not unreal.EditorLevelLibrary.save_current_level():
        raise RuntimeError("Failed to save LVL_Main")

    child_components_after = components_by_name(child_blueprint)
    parent_components_after = components_by_name(parent_blueprint)
    child_collision = child_components_after["CollisionCylinder"]
    child_hull = child_components_after["Hull Mesh"]
    parent_collision = parent_components_after["CollisionCylinder"]

    bp_validation = {
        "parent_collision": component_state(parent_collision),
        "child_collision": component_state(child_collision),
        "child_hull": component_state(child_hull),
    }
    if not close_vector(bp_validation["parent_collision"]["scale"], [0.3] * 3, 1.0e-4):
        raise RuntimeError("Parent CollisionCylinder scale changed")
    if not close_vector(bp_validation["child_collision"]["scale"], [1.0] * 3, 1.0e-4):
        raise RuntimeError("Child CollisionCylinder scale override did not persist")
    if not close_vector(
        bp_validation["child_hull"]["location"], [-7081.0, 0.0, -464.5], 1.0e-3
    ):
        raise RuntimeError("Child Hull Mesh location override did not persist")
    if not close_vector(bp_validation["child_hull"]["scale"], [1.0] * 3, 1.0e-4):
        raise RuntimeError("Child Hull Mesh scale changed")

    validation = {name: [] for name in EXPECTED_COUNTS}
    failures = []
    for group, actors in groups.items():
        for actor in actors:
            name = actor.get_name()
            base = baseline_by_group[group][name]
            current_location = values(actor.get_actor_location())
            current_rotation = rotation_values(actor.get_actor_rotation())
            current_scale = values(actor.get_actor_scale3d())
            current_bounds = bounds_size(actor)
            baseline_location = parse_vector(base["location"])
            baseline_rotation = parse_vector(base["rotation"])
            baseline_bounds = parse_vector(base["world_bounds_size"])

            checks = {
                "location": close_vector(current_location, baseline_location, 0.1),
                "rotation": close_vector(current_rotation, baseline_rotation, 0.01),
                "scale": close_vector(current_scale, [1.0] * 3, 1.0e-4),
                "world_bounds": close_vector(current_bounds, baseline_bounds, 1.0, 1.0e-4),
            }
            if not all(checks.values()):
                failures.append(
                    {
                        "group": group,
                        "name": name,
                        "checks": checks,
                        "bounds": current_bounds,
                        "baseline_bounds": baseline_bounds,
                    }
                )
            validation[group].append(
                {
                    "name": name,
                    "scale": current_scale,
                    "location": current_location,
                    "rotation": current_rotation,
                    "bounds": current_bounds,
                    "checks": checks,
                }
            )

    report.update(
        {
            "success": not failures,
            "counts": counts_before,
            "normalized": normalized,
            "blueprint": bp_validation,
            "failures": failures,
            "actors": validation,
        }
    )
    if failures:
        raise RuntimeError(f"Post-normalization validation failed: {len(failures)} actors")
except Exception:
    report["success"] = False
    report["error"] = traceback.format_exc()

with open(REPORT_PATH, "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, ensure_ascii=False, indent=2)

print(json.dumps({key: value for key, value in report.items() if key != "actors"}, ensure_ascii=False))

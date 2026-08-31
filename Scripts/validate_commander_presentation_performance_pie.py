"""Validate Commander presentation performance settings in the live PIE World."""

import json
import traceback

import unreal


OUTPUT = "D:/UE5.7/test1/Progress/CommanderPresentationPerformancePIE.json"
CROWD_MESH = (
    "/Game/Commander/Units/SM_CommanderFourFRobot_Crowd."
    "SM_CommanderFourFRobot_Crowd"
)


def component_snapshot(component):
    start_cull, end_cull = component.get_cull_distances()
    mesh = component.get_editor_property("static_mesh")
    return {
        "name": component.get_name(),
        "instances": component.get_instance_count(),
        "mesh": mesh.get_path_name() if mesh else None,
        "start_cull_cm": int(start_cull),
        "end_cull_cm": int(end_cull),
        "cast_shadow": bool(component.get_editor_property("cast_shadow")),
        "affect_distance_field_lighting": bool(
            component.get_editor_property("affect_distance_field_lighting")
        ),
        "affect_dynamic_indirect_lighting": bool(
            component.get_editor_property("affect_dynamic_indirect_lighting")
        ),
        "visible_in_ray_tracing": bool(
            component.get_editor_property("visible_in_ray_tracing")
        ),
    }


def find_presentation_components(world):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if "GuLiCommanderPresentationActor" not in actor.get_class().get_path_name():
            continue
        components = {
            component.get_name(): component
            for component in actor.get_components_by_class(
                unreal.InstancedStaticMeshComponent
            )
        }
        return actor, components.get("UnitInstances"), components.get("RingInstances")
    return None, None, None


def execute(world, command):
    unreal.SystemLibrary.execute_console_command(world, command)


def add_check(result, name, condition, detail):
    passed = bool(condition)
    result["checks"][name] = passed
    if not passed:
        result["errors"].append(f"{name}: {detail}")


result = {
    "world": None,
    "actor": None,
    "snapshots": {},
    "checks": {},
    "errors": [],
}

try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_game_world()
    result["world"] = world.get_path_name() if world else None
    if world is None:
        raise RuntimeError("PIE World is not ready")

    actor, unit, ring = find_presentation_components(world)
    result["actor"] = actor.get_path_name() if actor else None
    if actor is None or unit is None or ring is None:
        raise RuntimeError("PresentationActor Unit/Ring ISM pair was not found")

    baseline = {
        "unit": component_snapshot(unit),
        "ring": component_snapshot(ring),
    }
    result["snapshots"]["baseline"] = baseline

    execute(world, "gs.Commander.Presentation.Set unit_cull_distance_cm 250000")
    execute(world, "gs.Commander.Presentation.Set ring_cull_distance_cm 654321")
    execute(world, "gs.Commander.Presentation.Set unit.cast_shadow true")
    execute(
        world,
        "gs.Commander.Presentation.Set unit.affect_distance_field_lighting true",
    )
    execute(
        world,
        "gs.Commander.Presentation.Set unit.affect_dynamic_indirect_lighting true",
    )
    execute(world, "gs.Commander.Presentation.Set unit.visible_in_ray_tracing true")
    overridden = {
        "unit": component_snapshot(unit),
        "ring": component_snapshot(ring),
    }
    result["snapshots"]["overridden"] = overridden

    # These commands must be rejected rather than clamped or loosely parsed.
    execute(world, "gs.Commander.Presentation.Set unit_cull_distance_cm nan")
    execute(world, "gs.Commander.Presentation.Set unit_cull_distance_cm 1 extra")
    rejected = {
        "unit": component_snapshot(unit),
        "ring": component_snapshot(ring),
    }
    result["snapshots"]["after_rejected_commands"] = rejected

    execute(world, "gs.Commander.Presentation.Reset all")
    reset = {
        "unit": component_snapshot(unit),
        "ring": component_snapshot(ring),
    }
    result["snapshots"]["reset"] = reset

    add_check(
        result,
        "baseline_unit_hard_cull",
        baseline["unit"]["start_cull_cm"] == 100000
        and baseline["unit"]["end_cull_cm"] == 100000,
        baseline["unit"],
    )
    add_check(
        result,
        "baseline_ring_unculled",
        baseline["ring"]["start_cull_cm"] == 0
        and baseline["ring"]["end_cull_cm"] == 0,
        baseline["ring"],
    )
    add_check(
        result,
        "baseline_crowd_mesh",
        baseline["unit"]["mesh"] == CROWD_MESH,
        baseline["unit"]["mesh"],
    )
    add_check(
        result,
        "baseline_instance_counts",
        baseline["unit"]["instances"] == 500
        and baseline["ring"]["instances"] == 500,
        {"unit": baseline["unit"]["instances"], "ring": baseline["ring"]["instances"]},
    )
    heavy_keys = (
        "cast_shadow",
        "affect_distance_field_lighting",
        "affect_dynamic_indirect_lighting",
        "visible_in_ray_tracing",
    )
    add_check(
        result,
        "baseline_unit_heavy_features_disabled",
        not any(baseline["unit"][key] for key in heavy_keys),
        baseline["unit"],
    )
    add_check(
        result,
        "ring_heavy_features_always_disabled",
        not any(baseline["ring"][key] for key in heavy_keys)
        and not any(overridden["ring"][key] for key in heavy_keys),
        {"baseline": baseline["ring"], "overridden": overridden["ring"]},
    )
    add_check(
        result,
        "local_cull_overrides_apply",
        overridden["unit"]["start_cull_cm"] == 250000
        and overridden["unit"]["end_cull_cm"] == 250000
        and overridden["ring"]["start_cull_cm"] == 654321
        and overridden["ring"]["end_cull_cm"] == 654321,
        overridden,
    )
    add_check(
        result,
        "local_unit_boolean_overrides_apply",
        all(overridden["unit"][key] for key in heavy_keys),
        overridden["unit"],
    )
    add_check(
        result,
        "rejected_commands_do_not_mutate",
        rejected == overridden,
        {"overridden": overridden, "after_rejected": rejected},
    )
    add_check(
        result,
        "reset_restores_baseline",
        reset == baseline,
        {"baseline": baseline, "reset": reset},
    )
    add_check(
        result,
        "instance_counts_remain_stable",
        all(
            snapshot[component_name]["instances"] == 500
            for snapshot in (baseline, overridden, rejected, reset)
            for component_name in ("unit", "ring")
        ),
        result["snapshots"],
    )
except Exception as error:  # noqa: BLE001
    result["errors"].append(repr(error))
    result["traceback"] = traceback.format_exc()

result["passed"] = not result["errors"] and all(result["checks"].values())
with open(OUTPUT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2, default=str)

print(
    "Commander presentation PIE validation passed"
    if result["passed"]
    else "Commander presentation PIE validation failed"
)

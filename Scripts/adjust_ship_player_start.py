"""Raise LVL_Main's PlayerStart so the normalized Dreadnought clears terrain."""

import json
import traceback

import unreal


REPORT_PATH = "D:/UE5.7/test1/Data/tmp_player_start_adjustment_report.json"
CLEARANCE = 200.0
HULL_HALF_HEIGHT = 6984.66015625 * 0.5

report = {"success": False, "clearance": CLEARANCE}
try:
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    starts = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if isinstance(actor, unreal.PlayerStart)
    ]
    if len(starts) != 1:
        raise RuntimeError(f"Expected one PlayerStart, found {len(starts)}")

    player_start = starts[0]
    before = player_start.get_actor_location()
    rotation_before = player_start.get_actor_rotation()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    hit = unreal.SystemLibrary.line_trace_single(
        world,
        unreal.Vector(before.x, before.y, 100000.0),
        unreal.Vector(before.x, before.y, -100000.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
        True,
        [],
        unreal.DrawDebugTrace.NONE,
        True,
    )
    if not hit:
        raise RuntimeError("No terrain hit below PlayerStart")
    hit_values = hit.to_tuple()
    impact = hit_values[5]
    hit_actor = hit_values[9]
    if not hit_actor or not isinstance(hit_actor, unreal.Landscape):
        raise RuntimeError(f"PlayerStart trace did not hit Landscape: {hit_actor}")

    target_z = impact.z + HULL_HALF_HEIGHT + CLEARANCE
    player_start.modify()
    player_start.set_actor_location(
        unreal.Vector(before.x, before.y, target_z), False, False
    )
    after = player_start.get_actor_location()
    rotation_after = player_start.get_actor_rotation()

    report.update(
        {
            "success": abs(after.z - target_z) <= 0.01,
            "player_start": player_start.get_name(),
            "before": [before.x, before.y, before.z],
            "after": [after.x, after.y, after.z],
            "rotation_before": [
                rotation_before.roll,
                rotation_before.pitch,
                rotation_before.yaw,
            ],
            "rotation_after": [
                rotation_after.roll,
                rotation_after.pitch,
                rotation_after.yaw,
            ],
            "terrain_z": impact.z,
            "hull_half_height": HULL_HALF_HEIGHT,
            "target_bottom_z": after.z - HULL_HALF_HEIGHT,
            "actual_clearance": after.z - HULL_HALF_HEIGHT - impact.z,
        }
    )
except Exception:
    report["error"] = traceback.format_exc()

with open(REPORT_PATH, "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, ensure_ascii=False, indent=2)

print(json.dumps(report, ensure_ascii=False))

import json

import unreal


world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if world is None:
    raise RuntimeError("PIE world is not ready")

actors = unreal.GameplayStatics.get_all_actors_of_class(
    world, unreal.GuLiWingmanPresentationActor
)
if not actors:
    raise RuntimeError("Wingman presentation actor is missing")

component = actors[0].get_owner_instances()
items = []
for index in range(component.get_instance_count()):
    transform = component.get_instance_transform(index, world_space=True)
    items.append(
        {
            "index": index,
            "location": [
                transform.translation.x,
                transform.translation.y,
                transform.translation.z,
            ],
            "rotation": [
                transform.rotation.x,
                transform.rotation.y,
                transform.rotation.z,
                transform.rotation.w,
            ],
            "scale": [
                transform.scale3d.x,
                transform.scale3d.y,
                transform.scale3d.z,
            ],
        }
    )

row = {
    "game_time": unreal.GameplayStatics.get_time_seconds(world),
    "items": items,
}
path = (
    "D:/UE5.7/test1/TestResults/ShipAirCombatLevel/"
    "tick_alignment_recovery_timeseries.jsonl"
)
with open(path, "a", encoding="utf-8") as output:
    output.write(json.dumps(row, separators=(",", ":")) + "\n")

unreal.MCPythonHelper.submit_result(
    json.dumps({"success": True, "time": row["game_time"], "count": len(items)})
)

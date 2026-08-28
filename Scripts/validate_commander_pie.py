import json
import traceback

import unreal


OUT = "D:/UE5.7/test1/Progress/CommanderPIEValidation.json"
result = {
    "game_world": None,
    "player_states": [],
    "presentation_instances": [],
    "roster": [],
    "nav_data": [],
    "nav_bounds": [],
    "smoke_requested": False,
    "errors": [],
}
try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_game_world()
    result["game_world"] = world.get_path_name() if world else None
    if world is None:
        raise RuntimeError("PIE world is not ready")
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    # Runtime must own both the legacy default and Commander-specific nav data.
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        result["nav_data"].append(
            {
                "actor": nav_mesh.get_name(),
                "agent_radius": str(nav_mesh.get_editor_property("agent_radius")),
                "tile_methods": [name for name in dir(nav_mesh) if "tile" in name.lower()],
            }
        )
    for bounds in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.NavMeshBoundsVolume):
        entry = {
            "actor": bounds.get_name(),
            "agent_properties": [name for name in dir(bounds) if "agent" in name.lower()],
        }
        origin, extent = bounds.get_actor_bounds(False, False)
        entry["origin"] = list(origin.to_tuple())
        entry["extent"] = list(extent.to_tuple())
        for prop in ("supported_agents", "supported_agents_mask"):
            try:
                entry[prop] = str(bounds.get_editor_property(prop))
            except Exception as exc:  # noqa: BLE001
                entry[prop] = "ERROR:" + repr(exc)
        try:
            selector = bounds.get_editor_property("supported_agents")
            entry["agent0"] = bool(selector.get_editor_property("supports_agent0"))
            entry["agent1"] = bool(selector.get_editor_property("supports_agent1"))
        except Exception as exc:  # noqa: BLE001
            entry["selector_error"] = repr(exc)
        result["nav_bounds"].append(entry)
    for actor in actors:
        class_path = actor.get_class().get_path_name()
        if "GuLiCommanderPlayerState" in class_path:
            result["player_states"].append(
                {
                    "name": actor.get_name(),
                    "sync_ready": actor.is_sync_ready(),
                    "is_commander": actor.is_commander(),
                    "team": str(actor.get_team()),
                }
            )
        elif "GuLiCommanderPresentationActor" in class_path:
            components = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
            result["presentation_instances"].append(
                {
                    "actor": actor.get_name(),
                    "components": [
                        {"name": component.get_name(), "instances": component.get_instance_count()}
                        for component in components
                    ],
                }
            )
        elif "GuLiSoldierStateReplicator" in class_path:
            roster_entry = {"actor": actor.get_name()}
            for prop in ("snapshot_match_epoch", "snapshot_revision"):
                try:
                    roster_entry[prop] = int(actor.get_editor_property(prop))
                except Exception as exc:  # noqa: BLE001
                    roster_entry[prop] = "ERROR:" + repr(exc)
            try:
                fast_array = actor.get_editor_property("replicated_soldiers")
                roster_entry["count"] = len(fast_array.get_editor_property("items"))
            except Exception as exc:  # noqa: BLE001
                roster_entry["count"] = "ERROR:" + repr(exc)
            result["roster"].append(roster_entry)
    unreal.SystemLibrary.execute_console_command(world, "gs.Commander.SmokeCommandFlow")
    result["smoke_requested"] = True
except Exception as exc:  # noqa: BLE001
    result["errors"].append(repr(exc))
    result["traceback"] = traceback.format_exc()
with open(OUT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

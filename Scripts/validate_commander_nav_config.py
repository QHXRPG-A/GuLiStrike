import json
import traceback

import unreal


OUT = "D:/UE5.7/test1/Progress/CommanderNavConfigValidation.json"


def read_property(obj, name):
    try:
        value = obj.get_editor_property(name)
        if hasattr(value, "to_tuple"):
            return value.to_tuple()
        return str(value)
    except Exception as exc:  # noqa: BLE001
        return "ERROR:" + repr(exc)


result = {
    "world": None,
    "supported_agents": [],
    "nav_data": [],
    "nav_bounds_count": 0,
    "errors": [],
}
try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    result["world"] = world.get_path_name() if world else None
    nav_default = unreal.get_default_object(unreal.NavigationSystemV1)
    try:
        supported = nav_default.get_editor_property("supported_agents")
    except Exception as exc:  # noqa: BLE001
        supported = []
        result["errors"].append("supported_agents:" + repr(exc))
        result["navigation_properties"] = [
            name for name in dir(nav_default) if "agent" in name.lower() or "nav" in name.lower()
        ]
    for agent in supported:
        result["supported_agents"].append(
            {
                "name": read_property(agent, "name"),
                "agent_radius": read_property(agent, "agent_radius"),
                "agent_height": read_property(agent, "agent_height"),
                "default_query_extent": read_property(agent, "default_query_extent"),
            }
        )
    if world:
        nav_meshes = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)
        for nav_mesh in nav_meshes:
            result["nav_data"].append(
                {
                    "actor": nav_mesh.get_name(),
                    "agent_radius": read_property(nav_mesh, "agent_radius"),
                }
            )
        bounds_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.NavMeshBoundsVolume)
        result["nav_bounds_count"] = len(bounds_actors)
        result["nav_bounds"] = []
        for bounds in bounds_actors:
            origin, extent = bounds.get_actor_bounds(False, False)
            result["nav_bounds"].append(
                {
                    "actor": bounds.get_name(),
                    "origin": list(origin.to_tuple()),
                    "extent": list(extent.to_tuple()),
                    "scale": list(bounds.get_actor_scale3d().to_tuple()),
                }
            )
except Exception as exc:  # noqa: BLE001
    result["errors"].append(repr(exc))
    result["traceback"] = traceback.format_exc()
with open(OUT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

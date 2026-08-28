import json

import unreal


EXPECTED_WORLD = "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype"
SCREENSHOT_PATH = "D:/UE5.7/test1/Progress/CommanderCloudRemoved.png"
OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderCloudRemovalVerification.json"

unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor_world = unreal_editor.get_editor_world()
game_world = unreal_editor.get_game_world()
world = editor_world or game_world
if world is None:
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        pass

actors = (
    list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor))
    if game_world is not None
    else list(actor_subsystem.get_all_level_actors())
)

directional_lights = [
    actor
    for actor in actors
    if actor.get_actor_label() == "DirectionalLight"
]
cloud_actors = [
    {
        "label": actor.get_actor_label(),
        "class": actor.get_class().get_path_name(),
    }
    for actor in actors
    if "cloud" in (
        actor.get_actor_label() + " " + actor.get_class().get_path_name()
    ).lower()
]

material_path = None
component_path = None
if len(directional_lights) == 1:
    components = directional_lights[0].get_components_by_class(
        unreal.DirectionalLightComponent
    )
    if len(components) == 1:
        component_path = components[0].get_path_name()
        material = components[0].get_editor_property("light_function_material")
        material_path = material.get_path_name() if material else None

world_path = world.get_path_name() if world else None
world_is_expected = bool(
    world_path
    and (
        world_path == EXPECTED_WORLD
        or world_path.endswith("_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype")
    )
)

result = {
    "success": (
        world is not None
        and world_is_expected
        and len(directional_lights) == 1
        and component_path is not None
        and material_path is None
        and len(cloud_actors) == 0
    ),
    "world": world_path,
    "editor_world": editor_world.get_path_name() if editor_world else None,
    "game_world": game_world.get_path_name() if game_world else None,
    "directional_light_count": len(directional_lights),
    "directional_light_component": component_path,
    "light_function_material": material_path,
    "cloud_actors": cloud_actors,
    "screenshot": SCREENSHOT_PATH,
    "screenshot_requested": False,
}

if world:
    unreal.SystemLibrary.execute_console_command(
        world,
        'HighResShot 1920x1080 filename="{}"'.format(SCREENSHOT_PATH),
    )
    result["screenshot_requested"] = True

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

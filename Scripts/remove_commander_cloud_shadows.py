import json

import unreal


EXPECTED_WORLD = "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype"
OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderCloudRemoval.json"


def object_path(value):
    return value.get_path_name() if value else None


result = {
    "success": False,
    "world": None,
    "actor": None,
    "component": None,
    "old_light_function_material": None,
    "new_light_function_material": None,
    "changed": False,
    "saved": False,
}

try:
    unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal_editor.get_editor_world()
    result["world"] = world.get_path_name() if world else None

    if result["world"] != EXPECTED_WORLD:
        raise RuntimeError(
            "Refusing to edit unexpected world: " + str(result["world"])
        )

    matching_actors = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() == "DirectionalLight"
    ]
    if len(matching_actors) != 1:
        raise RuntimeError(
            "Expected exactly one DirectionalLight actor, found "
            + str(len(matching_actors))
        )

    directional_light = matching_actors[0]
    light_components = [
        component
        for component in directional_light.get_components_by_class(
            unreal.DirectionalLightComponent
        )
    ]
    if len(light_components) != 1:
        raise RuntimeError(
            "Expected exactly one DirectionalLightComponent, found "
            + str(len(light_components))
        )

    light_component = light_components[0]
    result["actor"] = directional_light.get_path_name()
    result["component"] = light_component.get_path_name()

    previous_material = light_component.get_editor_property(
        "light_function_material"
    )
    result["old_light_function_material"] = object_path(previous_material)

    if previous_material is not None:
        with unreal.ScopedEditorTransaction(
            "Remove Commander Landscape Animated Cloud Shadows"
        ):
            directional_light.modify()
            light_component.modify()
            light_component.set_editor_property("light_function_material", None)
        result["changed"] = True

    current_material = light_component.get_editor_property(
        "light_function_material"
    )
    result["new_light_function_material"] = object_path(current_material)
    if current_material is not None:
        raise RuntimeError("DirectionalLight cloud-shadow material was not cleared")

    try:
        light_component.mark_render_state_dirty()
    except Exception:
        pass

    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    result["saved"] = bool(level_editor.save_current_level())
    if not result["saved"]:
        raise RuntimeError("The prototype level could not be saved")

    result["success"] = True
except Exception as error:
    result["error"] = str(error)

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

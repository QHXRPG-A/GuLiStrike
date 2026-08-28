import json

import unreal


OUT = "D:/UE5.7/test1/Progress/CommanderSkyActorsBefore.json"

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()

tokens = ("cloud", "sky", "weather", "atmosphere", "fog", "sun")
matches = []
actors = []


def serialize_value(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (list, tuple)):
        return [serialize_value(item) for item in value]
    try:
        return str(value)
    except Exception:
        return "<unserializable>"


def collect_properties(obj, property_tokens):
    output = {}
    for property_name in sorted(dir(obj)):
        if not any(token in property_name.lower() for token in property_tokens):
            continue
        try:
            output[property_name] = serialize_value(obj.get_editor_property(property_name))
        except Exception:
            continue
    return output


details = {}
for actor in actor_subsystem.get_all_level_actors():
    label = actor.get_actor_label()
    name = actor.get_name()
    class_path = actor.get_class().get_path_name()
    components = actor.get_components_by_class(unreal.ActorComponent)
    component_classes = sorted(
        {component.get_class().get_path_name() for component in components}
    )
    haystack = " ".join((label, name, class_path, *component_classes)).lower()
    actors.append(
        {
            "label": label,
            "name": name,
            "class": class_path,
            "components": component_classes,
            "folder": str(actor.get_folder_path()),
            "hidden_editor": actor.is_hidden_ed(),
        }
    )
    if any(token in haystack for token in tokens):
        matches.append(
            {
                "label": label,
                "name": name,
                "class": class_path,
                "components": component_classes,
                "folder": str(actor.get_folder_path()),
                "hidden_editor": actor.is_hidden_ed(),
            }
        )
    if label in (
        "DirectionalLight",
        "ExponentialHeightFog",
        "PostProcessVolume",
        "SkyAtmosphere_Commander",
        "SkyLight",
        "Landscape",
    ):
        actor_detail = {
            "actor_properties": collect_properties(
                actor,
                ("cloud", "sky", "atmosphere", "fog", "blend", "material", "cube"),
            ),
            "components": [],
        }
        for component in components:
            actor_detail["components"].append(
                {
                    "name": component.get_name(),
                    "class": component.get_class().get_path_name(),
                    "properties": collect_properties(
                        component,
                        (
                            "cloud",
                            "sky",
                            "atmosphere",
                            "fog",
                            "blend",
                            "material",
                            "cube",
                            "texture",
                            "source_type",
                        ),
                    ),
                }
            )
        if label == "PostProcessVolume":
            try:
                settings = actor.get_editor_property("settings")
                actor_detail["post_process_settings"] = collect_properties(
                    settings,
                    ("blendable", "material", "cloud", "fog", "atmosphere"),
                )
            except Exception as error:
                actor_detail["post_process_error"] = str(error)
        if label == "Landscape":
            for material_property in ("landscape_material", "landscape_hole_material"):
                try:
                    material = actor.get_editor_property(material_property)
                    actor_detail[material_property] = serialize_value(material)
                    if material:
                        parameter_library = unreal.MaterialEditingLibrary
                        scalar_names = parameter_library.get_scalar_parameter_names(material)
                        vector_names = parameter_library.get_vector_parameter_names(material)
                        texture_names = parameter_library.get_texture_parameter_names(material)
                        actor_detail[material_property + "_parameters"] = {
                            "scalar": [str(name) for name in scalar_names],
                            "vector": [str(name) for name in vector_names],
                            "texture": [str(name) for name in texture_names],
                        }
                except Exception as error:
                    actor_detail[material_property + "_error"] = str(error)
        details[label] = actor_detail

result = {
    "world": world.get_path_name() if world else None,
    "actor_count": len(actor_subsystem.get_all_level_actors()),
    "actors": sorted(actors, key=lambda item: (item["class"], item["label"])),
    "matches": sorted(matches, key=lambda item: (item["class"], item["label"])),
    "details": details,
}
with open(OUT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

import json

import unreal


OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderUnitMaterialsBefore.json"
STATIC_MESH_PATH = "/Game/Commander/Units/SM_CommanderFourFRobot.SM_CommanderFourFRobot"
PROXY_MATERIAL_PATH = "/Game/Commander/Units/M_CommanderUnitProxy.M_CommanderUnitProxy"
SOURCE_MESH_PATH = (
    "/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes/"
    "cannon_war_machine.cannon_war_machine"
)


def path_of(value):
    return value.get_path_name() if value else None


def enum_text(value):
    try:
        return str(value)
    except Exception:
        return None


def material_slots(mesh, property_name):
    slots = []
    if not mesh:
        return slots
    try:
        source_slots = mesh.get_editor_property(property_name)
    except Exception as error:
        return [{"error": str(error)}]
    for index, slot in enumerate(source_slots):
        entry = {"index": index}
        for name in (
            "material_interface",
            "material_slot_name",
            "imported_material_slot_name",
        ):
            try:
                value = slot.get_editor_property(name)
                entry[name] = path_of(value) if isinstance(value, unreal.Object) else str(value)
            except Exception:
                pass
        slots.append(entry)
    return slots


def material_info(material):
    if not material:
        return None
    info = {
        "path": material.get_path_name(),
        "class": material.get_class().get_path_name(),
    }
    for name in (
        "blend_mode",
        "shading_model",
        "two_sided",
        "use_material_attributes",
        "allow_development_shader_compile",
    ):
        try:
            info[name] = enum_text(material.get_editor_property(name))
        except Exception:
            pass
    try:
        info["scalar_parameters"] = [
            str(name)
            for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)
        ]
        info["vector_parameters"] = [
            str(name)
            for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(material)
        ]
        info["texture_parameters"] = [
            str(name)
            for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(material)
        ]
    except Exception as error:
        info["parameter_error"] = str(error)
    try:
        info["used_textures"] = [
            texture.get_path_name()
            for texture in unreal.MaterialEditingLibrary.get_used_textures(material)
            if texture
        ]
    except Exception as error:
        info["used_textures_error"] = str(error)
    return info


def material_usage_info(material):
    info = {
        "path": path_of(material),
        "class": material.get_class().get_path_name() if material else None,
    }
    if not material:
        return info
    for name in (
        "used_with_instanced_static_meshes",
        "used_with_skeletal_mesh",
        "automatically_set_usage_in_editor",
    ):
        try:
            info[name] = bool(material.get_editor_property(name))
        except Exception as error:
            info[name + "_error"] = str(error)
    return info


editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor_world = editor.get_editor_world()
game_world = editor.get_game_world()
active_world = game_world or editor_world

static_mesh = unreal.load_asset(STATIC_MESH_PATH)
source_mesh = unreal.load_asset(SOURCE_MESH_PATH)
proxy_material = unreal.load_asset(PROXY_MATERIAL_PATH)

runtime_presentations = []
if active_world:
    for actor in unreal.GameplayStatics.get_all_actors_of_class(active_world, unreal.Actor):
        if actor.get_class().get_name() != "GuLiCommanderPresentationActor":
            continue
        components = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
        for component in components:
            if component.get_name() != "UnitInstances":
                continue
            runtime_presentations.append(
                {
                    "actor": actor.get_path_name(),
                    "component": component.get_path_name(),
                    "mesh": path_of(component.get_editor_property("static_mesh")),
                    "instance_count": component.get_instance_count(),
                    "material_count": component.get_num_materials(),
                    "materials": [
                        path_of(component.get_material(index))
                        for index in range(component.get_num_materials())
                    ],
                }
            )

result = {
    "editor_world": path_of(editor_world),
    "game_world": path_of(game_world),
    "active_world": path_of(active_world),
    "static_mesh": {
        "path": path_of(static_mesh),
        "class": static_mesh.get_class().get_path_name() if static_mesh else None,
        "material_slots": material_slots(static_mesh, "static_materials"),
        "material_count": static_mesh.get_num_sections(0) if static_mesh else None,
        "material_usage": [
            material_usage_info(slot.get_editor_property("material_interface"))
            for slot in static_mesh.get_editor_property("static_materials")
        ] if static_mesh else [],
    },
    "source_skeletal_mesh": {
        "path": path_of(source_mesh),
        "class": source_mesh.get_class().get_path_name() if source_mesh else None,
        "material_slots": material_slots(source_mesh, "materials"),
    },
    "proxy_material": material_info(proxy_material),
    "runtime_presentations": runtime_presentations,
}

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

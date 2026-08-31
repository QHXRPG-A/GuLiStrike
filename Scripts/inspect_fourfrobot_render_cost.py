import collections
import json

import unreal


OUTPUT_PATH = "D:/UE5.7/test1/Progress/FourFRobotRenderAudit.json"
SOURCE_ROOT = "/Game/Assets/Arma/FourFRobot"
SOURCE_MESH_PATH = (
    "/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes/"
    "cannon_war_machine.cannon_war_machine"
)
STATIC_MESH_PATH = "/Game/Commander/Units/SM_CommanderFourFRobot.SM_CommanderFourFRobot"
PROXY_MATERIAL_PATH = "/Game/Commander/Units/M_CommanderUnitProxy.M_CommanderUnitProxy"
PRESENTATION_CLASS_PATH = "/Script/GuLiStrike.GuLiCommanderPresentationActor"


def object_path(value):
    return value.get_path_name() if isinstance(value, unreal.Object) else None


def text(value):
    try:
        return str(value)
    except Exception:
        return None


def safe_property(value, property_name):
    try:
        return value.get_editor_property(property_name)
    except Exception:
        return None


def safe_call(value, method_name, *args):
    try:
        return getattr(value, method_name)(*args)
    except Exception:
        return None


def vector(value):
    if value is None:
        return None
    return {
        "x": float(value.x),
        "y": float(value.y),
        "z": float(value.z),
    }


def texture_info(texture):
    result = {
        "path": object_path(texture),
        "class": texture.get_class().get_path_name() if texture else None,
    }
    if not texture:
        return result
    size_x = safe_call(texture, "blueprint_get_size_x")
    size_y = safe_call(texture, "blueprint_get_size_y")
    if size_x is not None:
        result["size_x"] = int(size_x)
    if size_y is not None:
        result["size_y"] = int(size_y)
    for property_name in (
        "compression_settings",
        "lod_bias",
        "never_stream",
        "srgb",
        "virtual_texture_streaming",
    ):
        value = safe_property(texture, property_name)
        if value is not None:
            result[property_name] = text(value)
    return result


def material_parameter_names(material, function_name):
    try:
        return sorted(str(name) for name in getattr(unreal.MaterialEditingLibrary, function_name)(material))
    except Exception:
        return []


def material_instance_parameter_values(material, names, function_name, value_kind):
    function = getattr(unreal.MaterialEditingLibrary, function_name, None)
    if function is None:
        return {}

    values = {}
    for parameter_name in names:
        try:
            value = function(material, parameter_name)
        except Exception:
            try:
                value = function(material, unreal.Name(parameter_name))
            except Exception:
                continue

        if value_kind == "texture":
            values[parameter_name] = texture_info(value) if value else None
        elif value_kind == "vector":
            values[parameter_name] = text(value)
        elif value_kind == "scalar":
            try:
                values[parameter_name] = float(value)
            except Exception:
                values[parameter_name] = text(value)
        elif value_kind == "bool":
            values[parameter_name] = bool(value)
    return values


def material_info(material):
    result = {
        "path": object_path(material),
        "class": material.get_class().get_path_name() if material else None,
    }
    if not material:
        return result

    parent = safe_property(material, "parent")
    base_material = safe_call(material, "get_base_material")
    result["parent"] = object_path(parent)
    result["base_material"] = object_path(base_material)

    effective_material = base_material or material
    for property_name in (
        "blend_mode",
        "shading_model",
        "two_sided",
        "dithered_lod_transition",
        "fully_rough",
        "material_domain",
        "opacity_mask_clip_value",
        "use_material_attributes",
    ):
        value = safe_property(effective_material, property_name)
        if value is not None:
            result[property_name] = text(value)

    result["scalar_parameters"] = material_parameter_names(material, "get_scalar_parameter_names")
    result["vector_parameters"] = material_parameter_names(material, "get_vector_parameter_names")
    result["texture_parameters"] = material_parameter_names(material, "get_texture_parameter_names")
    result["static_switch_parameters"] = material_parameter_names(
        material, "get_static_switch_parameter_names"
    )

    result["texture_parameter_values"] = material_instance_parameter_values(
        material,
        result["texture_parameters"],
        "get_material_instance_texture_parameter_value",
        "texture",
    )
    result["static_switch_parameter_values"] = material_instance_parameter_values(
        material,
        result["static_switch_parameters"],
        "get_material_instance_static_switch_parameter_value",
        "bool",
    )

    try:
        textures = [
            texture
            for texture in unreal.MaterialEditingLibrary.get_used_textures(material)
            if texture
        ]
    except Exception:
        textures = []
    result["used_texture_count"] = len(textures)
    result["used_textures"] = [texture_info(texture) for texture in textures]
    return result


def mesh_materials(mesh, property_name):
    materials = []
    slots = safe_property(mesh, property_name) or []
    for index, slot in enumerate(slots):
        material = safe_property(slot, "material_interface")
        materials.append(
            {
                "index": index,
                "slot_name": text(safe_property(slot, "material_slot_name")),
                "material": material_info(material),
            }
        )
    return materials


def static_mesh_info(mesh):
    result = {
        "path": object_path(mesh),
        "class": mesh.get_class().get_path_name() if mesh else None,
    }
    if not mesh:
        return result

    lod_count = safe_call(mesh, "get_num_lods")
    if lod_count is None:
        source_models = safe_property(mesh, "source_models") or []
        lod_count = len(source_models)
    result["lod_count"] = int(lod_count)
    result["lods"] = []
    for lod_index in range(int(lod_count)):
        result["lods"].append(
            {
                "lod": lod_index,
                "sections": safe_call(mesh, "get_num_sections", lod_index),
                "triangles": safe_call(mesh, "get_num_triangles", lod_index),
                "vertices": safe_call(mesh, "get_num_vertices", lod_index),
            }
        )

    nanite_settings = safe_property(mesh, "nanite_settings")
    result["nanite_enabled"] = bool(safe_property(nanite_settings, "enabled")) if nanite_settings else None
    for property_name in (
        "allow_cpu_access",
        "light_map_resolution",
        "lod_group",
        "min_lod",
        "support_ray_tracing",
    ):
        value = safe_property(mesh, property_name)
        if value is not None:
            result[property_name] = text(value)

    bounds = safe_call(mesh, "get_bounds")
    if bounds:
        result["bounds"] = {
            "origin": vector(safe_property(bounds, "origin")),
            "box_extent": vector(safe_property(bounds, "box_extent")),
            "sphere_radius": safe_property(bounds, "sphere_radius"),
        }
    result["materials"] = mesh_materials(mesh, "static_materials")
    return result


def skeletal_mesh_info(mesh):
    result = {
        "path": object_path(mesh),
        "class": mesh.get_class().get_path_name() if mesh else None,
    }
    if not mesh:
        return result
    lod_info = safe_property(mesh, "lod_info") or []
    result["lod_count"] = len(lod_info)
    result["materials"] = mesh_materials(mesh, "materials")
    bounds = safe_call(mesh, "get_bounds")
    if bounds:
        result["bounds"] = {
            "origin": vector(safe_property(bounds, "origin")),
            "box_extent": vector(safe_property(bounds, "box_extent")),
            "sphere_radius": safe_property(bounds, "sphere_radius"),
        }
    return result


def presentation_component_info():
    result = {"class": PRESENTATION_CLASS_PATH}
    try:
        presentation_class = unreal.load_class(None, PRESENTATION_CLASS_PATH)
        default_object = unreal.get_default_object(presentation_class)
        unit_instances = safe_property(default_object, "unit_instances")
    except Exception as exception:
        result["error"] = text(exception)
        return result

    result["component"] = object_path(unit_instances)
    if not unit_instances:
        return result
    for property_name in (
        "cast_shadow",
        "affect_distance_field_lighting",
        "affect_dynamic_indirect_lighting",
        "visible_in_ray_tracing",
        "instance_start_cull_distance",
        "instance_end_cull_distance",
    ):
        value = safe_property(unit_instances, property_name)
        if value is not None:
            result[property_name] = text(value)
    return result


asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
asset_data = asset_registry.get_assets_by_path(SOURCE_ROOT, recursive=True)
class_counts = collections.Counter(
    str(asset.asset_class_path.asset_name) for asset in asset_data
)

static_mesh = unreal.load_asset(STATIC_MESH_PATH)
source_mesh = unreal.load_asset(SOURCE_MESH_PATH)
proxy_material = unreal.load_asset(PROXY_MATERIAL_PATH)

report = {
    "source_root": SOURCE_ROOT,
    "asset_count": len(asset_data),
    "asset_class_counts": dict(sorted(class_counts.items())),
    "static_mesh": static_mesh_info(static_mesh),
    "source_skeletal_mesh": skeletal_mesh_info(source_mesh),
    "proxy_material": material_info(proxy_material),
    "presentation": presentation_component_info(),
}

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

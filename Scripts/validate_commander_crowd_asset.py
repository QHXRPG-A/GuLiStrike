"""Validate the generated Commander Crowd mesh/material contract in a live UE editor."""

import json

import unreal


OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderCrowdAssetValidation.json"
MESH_PATH = "/Game/Commander/Units/SM_CommanderFourFRobot_Crowd"
MATERIAL_PATH = "/Game/Commander/Units/M_CommanderFourFRobot_Crowd"
BASE_COLOR_PATH = "/Game/Commander/Units/T_CommanderFourFRobot_Crowd_BaseColor"
NORMAL_PATH = "/Game/Commander/Units/T_CommanderFourFRobot_Crowd_Normal"
ORM_PATH = "/Game/Commander/Units/T_CommanderFourFRobot_Crowd_ORM"
TRIANGLE_BUDGETS = (20000, 6000, 1500)


def property_value(value, name):
    try:
        return value.get_editor_property(name)
    except Exception:
        return None


def enum_text(value):
    return str(value) if value is not None else None


def texture_contract(texture, expected_srgb, expected_compression):
    if not texture:
        return {"exists": False, "passed": False}
    size_x = int(texture.blueprint_get_size_x())
    size_y = int(texture.blueprint_get_size_y())
    srgb = bool(property_value(texture, "srgb"))
    compression = enum_text(property_value(texture, "compression_settings"))
    return {
        "exists": True,
        "path": texture.get_path_name(),
        "size": [size_x, size_y],
        "srgb": srgb,
        "compression": compression,
        "passed": (
            size_x == 2048
            and size_y == 2048
            and srgb == expected_srgb
            and expected_compression in compression
        ),
    }


mesh = unreal.load_asset(MESH_PATH)
material = unreal.load_asset(MATERIAL_PATH)
base_color = unreal.load_asset(BASE_COLOR_PATH)
normal = unreal.load_asset(NORMAL_PATH)
orm = unreal.load_asset(ORM_PATH)

errors = []
lods = []
if not mesh:
    errors.append("Crowd mesh is missing")
else:
    lod_count = int(mesh.get_num_lods())
    if lod_count != 3:
        errors.append(f"Expected 3 LODs, found {lod_count}")
    for lod_index in range(lod_count):
        triangles = int(mesh.get_num_triangles(lod_index))
        sections = int(mesh.get_num_sections(lod_index))
        budget = TRIANGLE_BUDGETS[lod_index] if lod_index < 3 else None
        lod_passed = sections == 1 and triangles > 0 and budget is not None and triangles <= budget
        lods.append(
            {
                "lod": lod_index,
                "triangles": triangles,
                "sections": sections,
                "budget": budget,
                "passed": lod_passed,
            }
        )
        if not lod_passed:
            errors.append(f"LOD{lod_index} violates section/triangle contract")

    static_materials = property_value(mesh, "static_materials") or []
    if len(static_materials) != 1:
        errors.append(f"Expected 1 material slot, found {len(static_materials)}")
    forbidden_flags = {
        "allow_cpu_access": bool(property_value(mesh, "allow_cpu_access")),
        "has_navigation_data": bool(property_value(mesh, "has_navigation_data")),
        "support_ray_tracing": bool(property_value(mesh, "support_ray_tracing")),
        "generate_mesh_distance_field": bool(
            property_value(mesh, "generate_mesh_distance_field")
        ),
    }
    nanite_settings = property_value(mesh, "nanite_settings")
    forbidden_flags["nanite_enabled"] = bool(property_value(nanite_settings, "enabled"))
    for name, enabled in forbidden_flags.items():
        if enabled:
            errors.append(f"Forbidden mesh flag is enabled: {name}")

material_report = {"exists": bool(material)}
if not material:
    errors.append("Crowd material is missing")
else:
    used_textures = sorted(
        texture.get_path_name()
        for texture in unreal.MaterialEditingLibrary.get_used_textures(material)
        if texture
    )
    material_report.update(
        {
            "path": material.get_path_name(),
            "blend_mode": enum_text(property_value(material, "blend_mode")),
            "shading_model": enum_text(property_value(material, "shading_model")),
            "two_sided": bool(property_value(material, "two_sided")),
            "dithered_lod_transition": bool(
                property_value(material, "dithered_lod_transition")
            ),
            "used_textures": used_textures,
        }
    )
    if "OPAQUE" not in material_report["blend_mode"]:
        errors.append("Crowd material is not Opaque")
    if "DEFAULT_LIT" not in material_report["shading_model"]:
        errors.append("Crowd material is not Default Lit")
    if material_report["two_sided"]:
        errors.append("Crowd material is two-sided")
    if material_report["dithered_lod_transition"]:
        errors.append("Crowd material has dithered LOD transition enabled")
    if len(used_textures) != 3:
        errors.append(f"Crowd material uses {len(used_textures)} textures instead of 3")

textures = {
    "base_color": texture_contract(base_color, True, "DEFAULT"),
    "normal": texture_contract(normal, False, "NORMALMAP"),
    "orm": texture_contract(orm, False, "MASKS"),
}
for name, result in textures.items():
    if not result["passed"]:
        errors.append(f"{name} texture contract failed")

report = {
    "passed": not errors,
    "errors": errors,
    "mesh": {
        "exists": bool(mesh),
        "path": mesh.get_path_name() if mesh else None,
        "lods": lods,
        "forbidden_flags": forbidden_flags if mesh else None,
    },
    "material": material_report,
    "textures": textures,
}

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

if errors:
    raise RuntimeError("Commander Crowd asset validation failed: " + "; ".join(errors))

unreal.log("Commander Crowd asset validation passed")

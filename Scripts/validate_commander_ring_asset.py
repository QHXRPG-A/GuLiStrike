"""Inspect the saved Commander ring and invoke its native topology validation.

This report covers Python-visible asset/render settings. The native console
validator and GuLiStrike.Commander.Presentation.RingAsset.Contract additionally
verify actual triangle winding, open boundaries, area, and material graph wiring.
No assets, actors, or material settings are modified by this script.
"""

import json
from pathlib import Path

import unreal


MESH_PATH = "/Game/Commander/Units/SM_CommanderUnitRing"
MATERIAL_PATH = "/Game/Commander/UI/M_CommanderUnitRing"
LEGACY_PATH = "/Game/Commander/QA/M_CommanderUnitRing_LegacyBenchmark"
OUTPUT_PATH = (
    Path(unreal.Paths.project_dir())
    / "outputs/commander-ring-hud-20260830/ring-asset-validation.json"
)
errors = []


def read_property(value, name):
    try:
        return value.get_editor_property(name)
    except Exception as error:
        errors.append(f"Could not read {name}: {error}")
        return None


def require_equal(label, actual, expected):
    if actual != expected:
        errors.append(f"{label}: expected {expected!r}, got {actual!r}")


def material_report(material, legacy=False):
    if not material:
        errors.append("Legacy material missing" if legacy else "Ring material missing")
        return {"exists": False}
    report = {
        "exists": True,
        "path": material.get_path_name(),
        "blend_mode": str(read_property(material, "blend_mode")),
        "shading_model": str(read_property(material, "shading_model")),
        "two_sided": read_property(material, "two_sided"),
        "disable_depth_test": read_property(material, "disable_depth_test"),
        "used_with_instanced_static_meshes": read_property(
            material, "used_with_instanced_static_meshes"
        ),
        "expression_count": unreal.MaterialEditingLibrary.get_num_material_expressions(material),
        "used_textures": [
            texture.get_path_name()
            for texture in unreal.MaterialEditingLibrary.get_used_textures(material)
            if texture
        ],
    }
    label = "Legacy material" if legacy else "Ring material"
    require_equal(f"{label} two_sided", report["two_sided"], legacy)
    require_equal(f"{label} disable_depth_test", report["disable_depth_test"], True)
    require_equal(
        f"{label} ISM usage", report["used_with_instanced_static_meshes"], True
    )
    if "TRANSLUCENT" not in report["blend_mode"]:
        errors.append(f"{label} is not Translucent")
    if not legacy:
        if "UNLIT" not in report["shading_model"]:
            errors.append("Ring material is not Unlit")
        require_equal("Ring expression count", report["expression_count"], 6)
        require_equal("Ring used textures", len(report["used_textures"]), 0)
    return report


mesh = unreal.load_asset(MESH_PATH)
material = unreal.load_asset(MATERIAL_PATH)
legacy_material = unreal.load_asset(LEGACY_PATH)
mesh_report = {"exists": bool(mesh)}
if not mesh:
    errors.append("Ring mesh missing")
else:
    bounds = mesh.get_bounds()
    slots = read_property(mesh, "static_materials") or []
    mesh_report.update(
        {
            "path": mesh.get_path_name(),
            "lod_count": mesh.get_num_lods(),
            "triangles": mesh.get_num_triangles(0),
            "sections": mesh.get_num_sections(0),
            "materials": [
                value.get_path_name() if value else None
                for value in (read_property(slot, "material_interface") for slot in slots)
            ],
            "bounds_origin": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
            "bounds_extent": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
        }
    )
    require_equal("Ring LOD count", mesh_report["lod_count"], 1)
    require_equal("Ring triangle count", mesh_report["triangles"], 128)
    require_equal("Ring section count", mesh_report["sections"], 1)
    require_equal(
        "Ring material slots", mesh_report["materials"],
        [material.get_path_name()] if material else []
    )
    for actual, expected in zip(mesh_report["bounds_extent"], (48.0, 48.0, 0.0)):
        if abs(actual - expected) > 0.01:
            errors.append(f"Ring local bounds extent differs from 48/48/0: {mesh_report['bounds_extent']}")
            break
    forbidden = {}
    for name in (
        "allow_cpu_access", "support_ray_tracing", "generate_mesh_distance_field",
        "has_navigation_data"
    ):
        forbidden[name] = read_property(mesh, name)
        require_equal(f"Ring {name}", forbidden[name], False)
    nanite_settings = read_property(mesh, "nanite_settings")
    forbidden["nanite_enabled"] = read_property(nanite_settings, "enabled")
    require_equal("Ring Nanite", forbidden["nanite_enabled"], False)
    mesh_report["forbidden_flags"] = forbidden

active_material_report = material_report(material)
legacy_material_report = material_report(legacy_material, legacy=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor_world = editor.get_editor_world()
if editor_world:
    unreal.SystemLibrary.execute_console_command(editor_world, "gs.Commander.ValidateUnitRing")

report = {
    "success": not errors,
    "python_contract_passed": not errors,
    "scope": "Python-visible settings; native topology/material wiring test is separate.",
    "native_validation_invoked": bool(editor_world),
    "native_automation_test": "GuLiStrike.Commander.Presentation.RingAsset.Contract",
    "errors": errors,
    "mesh": mesh_report,
    "material": active_material_report,
    "legacy_benchmark_material": legacy_material_report,
}
OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
OUTPUT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps(report))
if errors:
    raise RuntimeError("Commander ring asset validation failed: " + "; ".join(errors))
unreal.log(f"Commander ring Python contract passed; report: {OUTPUT_PATH}")

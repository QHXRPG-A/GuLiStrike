"""Rebuild cannon_war_machine at 30x from the persistent FBX export.

Run from a normal UnrealEditor process via ``-ExecutePythonScript``. The
Interchange importer can recurse into the task graph when AssetImportTasks is
invoked from the UnrealMCPython game-thread callback.
"""

import json
import traceback

import unreal


SOURCE_FBX = (
    "D:/UE5.7/test1/Data/SourceAssets/Arma/FourFRobot/"
    "cannon_war_machine/cannon_war_machine_unscaled.fbx"
)
ASSET_PATH = (
    "/Game/Assets/Arma/FourFRobot/cannon_war_machine/"
    "SkeletalMeshes/cannon_war_machine.cannon_war_machine"
)
DEST_PATH = "/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes"
DEST_NAME = "cannon_war_machine"
PILOT_PATH = "/Game/Developers/CodexScaleValidation"
PILOT_NAME = "SK_CannonScaleValidation"
REPORT_PATH = "D:/UE5.7/test1/Data/tmp_cannon_scale_rebuild_report.json"
SCALE = 30.0


def vec_size(bounds):
    return [
        bounds.box_extent.x * 2.0,
        bounds.box_extent.y * 2.0,
        bounds.box_extent.z * 2.0,
    ]


def make_import_ui(skeleton):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("create_physics_asset", False)
    ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("reset_to_fbx_on_material_conflict", False)

    skeletal_data = ui.get_editor_property("skeletal_mesh_import_data")
    skeletal_data.set_editor_property("import_uniform_scale", SCALE)
    skeletal_data.set_editor_property("reorder_material_to_fbx_order", False)
    return ui


def import_mesh(destination_path, destination_name, skeleton, save, replace_existing):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", SOURCE_FBX)
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", save)
    task.set_editor_property("replace_existing", replace_existing)
    task.set_editor_property("replace_existing_settings", False)
    task.set_editor_property("options", make_import_ui(skeleton))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    # UE 5.7 exposes AssetImportTask.result as a protected property. The target
    # path is deterministic because this task supplies both path and name.
    result_path = f"{destination_path}/{destination_name}.{destination_name}"
    fallback = unreal.load_object(None, result_path)
    if isinstance(fallback, unreal.SkeletalMesh):
        return fallback, [result_path]
    raise RuntimeError(f"Import produced no SkeletalMesh: {result_path}")


report = {"success": False, "source_fbx": SOURCE_FBX, "scale": SCALE}
try:
    original = unreal.load_object(None, ASSET_PATH)
    if not isinstance(original, unreal.SkeletalMesh):
        raise RuntimeError(f"Original SkeletalMesh not found: {ASSET_PATH}")

    before_bounds = vec_size(original.get_bounds())
    expected_bounds = [value * SCALE for value in before_bounds]
    skeleton = original.get_editor_property("skeleton")
    physics_asset = original.get_editor_property("physics_asset")
    before_materials = list(original.get_editor_property("materials"))
    material_by_slot = {
        str(material.get_editor_property("material_slot_name")): material.get_editor_property(
            "material_interface"
        )
        for material in before_materials
    }
    before_slots = list(material_by_slot)

    pilot_package = f"{PILOT_PATH}/{PILOT_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(pilot_package):
        unreal.EditorAssetLibrary.delete_asset(pilot_package)
    pilot, pilot_results = import_mesh(
        PILOT_PATH, PILOT_NAME, skeleton, save=False, replace_existing=True
    )
    pilot_bounds = vec_size(pilot.get_bounds())
    pilot_slots = [
        str(material.get_editor_property("material_slot_name"))
        for material in pilot.get_editor_property("materials")
    ]
    pilot_skeleton = pilot.get_editor_property("skeleton")
    pilot_physics = pilot.get_editor_property("physics_asset")

    for actual, expected in zip(pilot_bounds, expected_bounds):
        if abs(actual - expected) > max(0.5, abs(expected) * 0.01):
            raise RuntimeError(
                f"Pilot bounds mismatch: actual={pilot_bounds}, expected={expected_bounds}"
            )
    if len(pilot_slots) != len(before_slots):
        raise RuntimeError(
            f"Pilot material count mismatch: {len(pilot_slots)} != {len(before_slots)}"
        )
    if not pilot_skeleton or pilot_skeleton.get_path_name() != skeleton.get_path_name():
        raise RuntimeError(
            f"Pilot skeleton mismatch: {pilot_skeleton} != {skeleton.get_path_name()}"
        )
    if pilot_physics is not None:
        raise RuntimeError(f"Pilot unexpectedly created PhysicsAsset: {pilot_physics}")

    rebuilt, rebuilt_results = import_mesh(
        DEST_PATH, DEST_NAME, skeleton, save=True, replace_existing=True
    )
    rebuilt.modify()
    rebuilt_materials = list(rebuilt.get_editor_property("materials"))
    for material in rebuilt_materials:
        slot = str(material.get_editor_property("material_slot_name"))
        if slot in material_by_slot:
            material.set_editor_property("material_interface", material_by_slot[slot])
    rebuilt.set_editor_property("materials", rebuilt_materials)
    unreal.EditorAssetLibrary.save_loaded_asset(rebuilt, False)

    after_bounds = vec_size(rebuilt.get_bounds())
    after_skeleton = rebuilt.get_editor_property("skeleton")
    after_physics = rebuilt.get_editor_property("physics_asset")
    after_materials = list(rebuilt.get_editor_property("materials"))
    after_slots = [
        str(material.get_editor_property("material_slot_name"))
        for material in after_materials
    ]
    after_material_paths = [
        material.get_editor_property("material_interface").get_path_name()
        if material.get_editor_property("material_interface")
        else None
        for material in after_materials
    ]

    for actual, expected in zip(after_bounds, expected_bounds):
        if abs(actual - expected) > max(0.5, abs(expected) * 0.01):
            raise RuntimeError(
                f"Rebuilt bounds mismatch: actual={after_bounds}, expected={expected_bounds}"
            )
    if len(after_slots) != len(before_slots):
        raise RuntimeError(
            f"Rebuilt material count mismatch: {len(after_slots)} != {len(before_slots)}"
        )
    if not after_skeleton or after_skeleton.get_path_name() != skeleton.get_path_name():
        raise RuntimeError(
            f"Rebuilt skeleton mismatch: {after_skeleton} != {skeleton.get_path_name()}"
        )
    if after_physics is not physics_asset:
        raise RuntimeError(
            f"PhysicsAsset changed: before={physics_asset}, after={after_physics}"
        )

    report.update(
        {
            "success": True,
            "before_bounds": before_bounds,
            "expected_bounds": expected_bounds,
            "pilot_bounds": pilot_bounds,
            "after_bounds": after_bounds,
            "pilot_results": pilot_results,
            "rebuilt_results": rebuilt_results,
            "skeleton": after_skeleton.get_path_name(),
            "physics_asset": None,
            "material_slots_before": before_slots,
            "material_slots_after": after_slots,
            "material_assets_after": after_material_paths,
        }
    )
except Exception:
    report["error"] = traceback.format_exc()

with open(REPORT_PATH, "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, ensure_ascii=False, indent=2)

print(json.dumps(report, ensure_ascii=False))

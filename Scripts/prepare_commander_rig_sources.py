"""Prepare independent FourFRobot/WM01 source copies in the source UE5.7 editor.

No runtime asset, Marketplace template or game map is edited by this authoring
workflow. Generated inspection reports are not an automated test suite.
Source-preserving preparation stage; WM01 binding repairs were separately approved.
"""

import json
import hashlib
from pathlib import Path

import unreal


OUTPUT = Path(unreal.Paths.project_dir()) / "outputs" / "commander-rigs"
SOURCES = {
    "FourFRobot": "/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes/cannon_war_machine",
    "WM01": "/Game/Assets/WarMachines/CombatAvatarWM-01/WM01_Avatar/SkeletalMeshes/WM01_Avatar",
}


def write_report(name, data):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def inspect_capabilities():
    classes = [
        "ControlRigBlueprint", "ControlRigBlueprintFactory", "RigHierarchy",
        "RigHierarchyController", "RigVMController", "RigControlSettings",
        "RigControlValue", "ControlRig", "RigVMHost", "SkeletonService",
        "SkeletalMeshEditorSubsystem", "ControlRigComponent", "SkinWeightModifier",
    ]
    wanted = ("create", "control", "hierarchy", "import", "compile", "preview",
              "unit_node", "template_node", "pin_default", "link", "socket",
              "skeleton", "transform", "execute", "initialize", "bone", "weight")
    report = {"engine": unreal.SystemLibrary.get_engine_version(), "classes": {}}
    for name in classes:
        cls = getattr(unreal, name, None)
        if cls:
            report["classes"][name] = {
                "doc": cls.__doc__,
                "methods": {attr: getattr(cls, attr).__doc__ for attr in dir(cls)
                            if not attr.startswith("_") and any(key in attr.lower() for key in wanted)},
            }
    report["sources"] = {}
    for unit, source in SOURCES.items():
        mesh = unreal.load_asset(source)
        report["sources"][unit] = {
            "path": mesh.get_path_name(),
            "skeleton": mesh.get_editor_property("skeleton").get_path_name(),
            "bones": [str(b) for b in unreal.SkeletonService.list_bones(source)],
            "sockets": [str(s) for s in unreal.SkeletonService.list_sockets(source)],
            "materials": [str(m) for m in mesh.get_editor_property("materials")],
        }
    write_report("capabilities.json", report)
    print("RIG_INSPECTION_READY", str(OUTPUT / "capabilities.json"))


def serial_transform(t):
    return {"translation": [t.translation.x, t.translation.y, t.translation.z],
            "rotation": [t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w],
            "scale": [t.scale3d.x, t.scale3d.y, t.scale3d.z]}


def paths(unit):
    folder = f"/Game/GuLiStrike/Robots/{unit}/Rig"
    return {"mesh": f"{folder}/SKM_{unit}_Rig", "skeleton": f"{folder}/SK_{unit}_Rig",
            "rig": f"{folder}/CR_{unit}"}


def owned_asset(path, source):
    lib = unreal.EditorAssetLibrary
    if lib.does_asset_exist(path):
        asset = unreal.load_asset(path)
        if lib.get_metadata_tag(asset, "CommanderRigOwner") != "20260906":
            raise RuntimeError(f"Refusing to overwrite an unowned asset: {path}")
        return asset
    asset = lib.duplicate_asset(source, path)
    if not asset:
        raise RuntimeError(f"Could not create {path}")
    lib.set_metadata_tag(asset, "CommanderRigOwner", "20260906")
    return asset


def prepare_sources():
    for unit, source_path in SOURCES.items():
        dest = paths(unit)
        source = unreal.load_asset(source_path)
        mesh = owned_asset(dest["mesh"], source_path)
        skeleton = owned_asset(dest["skeleton"], source.skeleton.get_path_name())
        # UE5.7 exposes a BlueprintSetter UFunction but omits its Python alias.
        # Use the real reflected setter (no raw memory edits / source changes).
        mesh.modify()
        mesh.call_method("SetSkeleton", args=(skeleton,))
        if mesh.skeleton != skeleton:
            raise RuntimeError("Private skeleton assignment failed")
        for path in (dest["skeleton"], dest["mesh"]):
            if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
                raise RuntimeError("Could not save " + path)
    print("RIG_SOURCE_COPIES_READY")


def inspect_baseline():
    """Read back the approved source-copy invariants without modifying assets."""
    report = {"status": "awaiting_source_binding_repair_approval", "units": {}}
    for unit, source_path in SOURCES.items():
        dest = paths(unit)
        source = unreal.load_asset(source_path)
        mesh = unreal.load_asset(dest["mesh"])
        facts = {"paths": {k: v for k, v in dest.items() if k != "rig"}, "control_rig_delivered": False}
        samples = {}
        for label, asset in [("source", source), ("copy", mesh)]:
            bones = unreal.SkeletonService.list_bones(asset.get_path_name())
            bone_data = [{"name": b.bone_name, "parent": b.parent_bone_name,
                          "local": serial_transform(b.local_transform)} for b in bones]
            modifier = unreal.SkinWeightModifier()
            if not modifier.set_skeletal_mesh(asset):
                raise RuntimeError("Cannot inspect source skin weights")
            digest = hashlib.sha256()
            counts = {}
            for index in range(modifier.get_num_vertices()):
                weights = sorted((str(name), float(value)) for name, value in modifier.get_vertex_weights(index).items())
                digest.update(json.dumps(weights, ensure_ascii=True, separators=(",", ":")).encode("utf-8"))
                for name, value in weights:
                    if value > 0.01:
                        counts[name] = counts.get(name, 0) + 1
            samples[label] = {
                "bones": bone_data, "vertex_count": modifier.get_num_vertices(),
                "skin_weight_sha256": digest.hexdigest(), "weighted_vertex_counts": counts,
                "materials": [m.material_interface.get_path_name() if m.material_interface else None
                              for m in asset.get_editor_property("materials")],
                "skeleton": asset.skeleton.get_path_name(),
            }
        facts["bone_count"] = len(samples["source"]["bones"])
        facts["reference_pose_equal"] = samples["source"]["bones"] == samples["copy"]["bones"]
        facts["weights_equal"] = samples["source"]["skin_weight_sha256"] == samples["copy"]["skin_weight_sha256"]
        facts["materials_equal"] = samples["source"]["materials"] == samples["copy"]["materials"]
        facts["skeleton_is_independent"] = source.skeleton != mesh.skeleton
        facts["samples"] = samples
        if not all(facts[k] for k in ["reference_pose_equal", "weights_equal", "materials_equal", "skeleton_is_independent"]):
            raise RuntimeError("Baseline comparison failed for " + unit)
        report["units"][unit] = facts
    report["dirty_content_packages_after"] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    write_report("baseline.json", report)
    print("VERIFIED_BASELINE_ONLY; CONTROL_RIG_NOT_DELIVERED")


if __name__ == "__main__":
    prepare_sources()
    inspect_baseline()

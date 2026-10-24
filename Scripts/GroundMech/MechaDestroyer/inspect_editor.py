import unreal, json
from pathlib import Path

ART = Path(r"D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023")
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/MechaDestroyer"], True)
assets = registry.get_assets_by_path("/Game/MechaDestroyer", recursive=True)
mesh = unreal.load_asset("/Game/MechaDestroyer/SKM/SKM_Mecha")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
report = {
    "success": bool(mesh), "world": world.get_path_name(),
    "assets": [{"path": str(a.package_name), "class": str(a.asset_class_path.asset_name)} for a in assets],
    "editor_api": [n for n in dir(unreal) if any(k in n for k in ["SkeletonService", "BlueprintService", "AnimationService", "SkeletalMeshService"])],
    "mesh_api": [n for n in dir(mesh) if any(k in n for k in ["skeleton", "socket", "bound", "material", "export"]) ] if mesh else [],
    "mesh": mesh.get_path_name() if mesh else None,
    "skeleton": mesh.get_editor_property("skeleton").get_path_name() if mesh else None,
    "materials": [m.material_interface.get_path_name() if m.material_interface else None for m in mesh.get_editor_property("materials")] if mesh else [],
    "ground_assets": [{"path": str(a.package_name), "class": str(a.asset_class_path.asset_name)} for a in registry.get_assets_by_path("/Game/GuLiStrike", recursive=True) if any(k in str(a.package_name) for k in ["GroundMech", "Mech_Visual", "Mech_Skill"])],
    "pie_worlds": [w.get_path_name() for w in unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world().__class__.__mro__] if False else [],
}
ART.mkdir(parents=True, exist_ok=True)
(ART / "ue_initial_inventory.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps(report))

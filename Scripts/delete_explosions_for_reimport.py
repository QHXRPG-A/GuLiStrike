"""Delete the explicitly requested Explosion directory for a fresh Fab import."""

import json
from pathlib import Path
import time
import traceback
import unreal


ROOT = "/Game/Assets/VFX/Explosions"
PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
TARGET = PROJECT / "Content" / "Assets" / "VFX" / "Explosions"
EXPECTED = Path("D:/UE5.7/test1/Content/Assets/VFX/Explosions").resolve()
OUT = PROJECT / "TestResults" / "ExplosionMaterialRepair" / "deletion_result.json"
assert TARGET.resolve() == EXPECTED, "Refusing to delete an unexpected directory"
assert not TARGET.is_symlink(), "Refusing to follow a filesystem link"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert not world.get_path_name().startswith(ROOT + "/"), "The current level is inside the deletion target"
assert not unreal.EditorLevelLibrary.get_pie_worlds(False), "Stop PIE before deleting this directory"

registry = unreal.AssetRegistryHelpers.get_asset_registry()
before = registry.get_assets_by_path(ROOT, recursive=True)
report = {"root": ROOT, "physical_path": str(TARGET), "asset_count_before": len(before),
          "started_unix": time.time(), "saved_external_assets": [], "complete": False}
OUT.write_text(json.dumps(report, indent=2), encoding="utf-8")
try:
    report["delete_returned"] = bool(unreal.EditorAssetLibrary.delete_directory(ROOT))
    remaining = registry.get_assets_by_path(ROOT, recursive=True)
    report["remaining_assets"] = [str(a.package_name) for a in remaining]
    report["directory_exists"] = TARGET.exists()
    report["complete"] = report["delete_returned"] and not remaining and not TARGET.exists()
except Exception:
    report["error"] = traceback.format_exc()
report["finished_unix"] = time.time()
OUT.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report))

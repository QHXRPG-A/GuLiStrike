"""Inspect the requested Explosion pack through the live editor asset APIs."""

import collections
import json
from pathlib import Path

import unreal


ROOT = "/Game/Assets/VFX/Explosions"
OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "TestResults" / "ExplosionMaterialRepair"
OUT.mkdir(parents=True, exist_ok=True)


def plain(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (list, tuple, unreal.Array)):
        return [plain(item) for item in value]
    if isinstance(value, unreal.StructBase):
        result = {}
        for key in dir(value):
            if not key.startswith("_"):
                try:
                    result[key] = plain(value.get_editor_property(key))
                except Exception:
                    pass
        return result
    return str(value)


registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = registry.get_assets_by_path(ROOT, recursive=True)
inventory = [{"path": str(a.package_name), "class": str(a.asset_class_path.asset_name)} for a in assets]
counts = dict(collections.Counter(a["class"] for a in inventory))
docs = {}
for owner, methods in {
    "MaterialNodeService": ["get_material_diagnostics", "export_material_graph"],
    "MaterialService": ["get_material_info", "get_property", "set_property", "compile_material"],
    "NiagaraService": ["summarize", "compile_with_results"],
    "NiagaraEmitterService": ["list_renderers", "get_renderer_property"],
}.items():
    cls = getattr(unreal, owner)
    docs[owner] = {name: getattr(cls, name).__doc__ for name in methods if hasattr(cls, name)}
report = {"root": ROOT, "counts": counts, "assets": inventory, "api_docs": docs,
          "open_tabs": plain(unreal.ScreenshotService.get_open_editor_tabs())}
(OUT / "inventory.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"counts": counts, "materials": [a for a in inventory if a["class"] == "Material"], "report": str(OUT / "inventory.json")}))

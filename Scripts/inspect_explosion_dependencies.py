"""Inspect saved registry references to locate moved Explosion dependencies."""
import collections
import json
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "TestResults" / "ExplosionMaterialRepair"
inventory = json.loads((OUT / "inventory.json").read_text(encoding="utf-8"))
registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
report = {"dependencies": {}, "redirectors": {}, "textures": []}
for item in inventory["assets"]:
    path, cls = item["path"], item["class"]
    if cls in ("Material", "MaterialFunction", "MaterialInstanceConstant", "NiagaraSystem", "ObjectRedirector"):
        deps = [str(v) for v in registry.get_dependencies(path, options)]
        report["dependencies"][path] = deps
        if cls == "ObjectRedirector":
            report["redirectors"][path] = deps
    if cls == "Texture2D":
        report["textures"].append(path)
(OUT / "dependencies_before.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"sample": {k:v for k,v in report["dependencies"].items() if k.rsplit("/",1)[1] in ["M_Smoke_2", "M_Impact_3", "M_Distortion_2", "M_Smoke_Wisp_2", "M_Smoke_1"]}, "textures": report["textures"]}))

"""Capture compiler diagnostics and graphs without saving any UE packages."""

import json
from pathlib import Path
import traceback
import unreal

OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "TestResults" / "ExplosionMaterialRepair"
inventory = json.loads((OUT / "inventory.json").read_text(encoding="utf-8"))


def plain(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (list, tuple, unreal.Array)):
        return [plain(v) for v in value]
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


report = {"materials": [], "instances": [], "saved_assets": []}
for item in inventory["assets"]:
    path = item["path"]
    if item["class"] not in ("Material", "MaterialInstanceConstant"):
        continue
    record = {"path": path}
    try:
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if item["class"] == "Material":
            record["info"] = plain(unreal.MaterialService.get_material_info(path))
            record["diagnostics"] = plain(unreal.MaterialNodeService.get_material_diagnostics(path))
            graph_text = unreal.MaterialNodeService.export_material_graph(path)
            (OUT / (path.rsplit("/", 1)[1] + "_graph_before.json")).write_text(graph_text, encoding="utf-8")
        else:
            for field in ("parent", "texture_parameter_values", "base_property_overrides"):
                record[field] = plain(asset.get_editor_property(field))
    except Exception:
        record["error"] = traceback.format_exc()
    report["materials" if item["class"] == "Material" else "instances"].append(record)
    (OUT / "audit_before.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"materials": len(report["materials"]), "instances": len(report["instances"]),
    "failures": [{"path": m["path"], "diagnostics": m.get("diagnostics"), "error": m.get("error")} for m in report["materials"] if not m.get("diagnostics", {}).get("is_compiled_ok", False)]}))

"""Capture current artwork values without changing UE assets or the active world."""
import hashlib
import json
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / "Artifacts/CardArtworkPipeline"
WM = "/Game/GuLiStrike/Cards/Commander/WM01"
CARDS = [
    (1, "WM01_FireRate", "增加射速", 1, WM + "/FireRate/Materials/MI_FireRate_ModelComic_v9"),
    (2, "WM01_MoveSpeed", "极速机动", 1, WM + "/MoveSpeed/Materials/MI_HighSpeed_ModelComic_v9"),
    (3, "WM01_MissileDamage", "导弹伤害", 1, WM + "/MissileDamage/Materials/MI_MissileDamage_ModelComic_v9"),
    (4, "WM01_MissilePod", "重防导弹仓", 2, WM + "/MissilePod/Materials/MI_MissilePod_ModelComic_v1"),
    (5, "WM01_RainSalvo", "雨点攻势", 2, WM + "/RainSalvo/Materials/MI_RainSalvo_ModelComic_v1"),
    (6, "GroundMech_Ricochet", "折射子弹", 3,
     "/Game/GuLiStrike/Cards/GroundMech/Ricochet/Materials/MI_Ricochet_Artwork_Builtin_v1"),
]


def capture():
    edit = unreal.MaterialEditingLibrary
    result = {"success": False, "assets_modified": False, "assistant_visual_test": False, "artworks": []}
    try:
        assert (ROOT / "GuLiStrike.uproject").resolve() == Path("D:/UE5.7/test1/GuLiStrike.uproject").resolve()
        result["pie_running"] = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
        for ident, name, title, template, path in CARDS:
            mi = unreal.load_asset(path)
            assert isinstance(mi, unreal.MaterialInstanceConstant), path
            parent = mi.get_editor_property("parent")
            item = {"id": ident, "name": name, "title": title, "template_id": template,
                    "material": mi.get_path_name(), "parent": parent.get_path_name(),
                    "textures": {}, "scalars": {}, "vectors": {}}
            for key in edit.get_texture_parameter_names(mi):
                texture = edit.get_material_instance_texture_parameter_value(mi, key)
                if not texture:
                    continue
                source = ""
                try:
                    data = texture.get_editor_property("asset_import_data")
                    source = data.get_first_filename() if data else ""
                except Exception:
                    pass
                source_path = Path(source) if source else None
                if source_path and not source_path.is_absolute():
                    source_path = (ROOT / source_path).resolve()
                info = {"asset": texture.get_path_name(), "source": str(source_path) if source_path else "",
                        "source_exists": bool(source_path and source_path.is_file())}
                if info["source_exists"]:
                    info["source_sha256"] = hashlib.sha256(source_path.read_bytes()).hexdigest()
                item["textures"][str(key)] = info
            for key in edit.get_scalar_parameter_names(mi):
                item["scalars"][str(key)] = float(edit.get_material_instance_scalar_parameter_value(mi, key))
            for key in edit.get_vector_parameter_names(mi):
                v = edit.get_material_instance_vector_parameter_value(mi, key)
                item["vectors"][str(key)] = [v.r, v.g, v.b, v.a]
            custom = {str(e.get_editor_property("desc")): str(e.get_editor_property("code"))
                      for e in unreal.ObjectIterator(unreal.MaterialExpression)
                      if e.get_outer() == parent and isinstance(e, unreal.MaterialExpressionCustom)}
            item["parent_custom_code_sha256"] = hashlib.sha256(
                json.dumps(custom, sort_keys=True).encode()).hexdigest()
            result["artworks"].append(item)
        result["gameplay_cards"] = json.loads((ROOT / "data/Json/DT_GuLiStrikeRogueCards_Cards.json").read_text(encoding="utf-8"))
        result["success"] = True
    except Exception:
        result["error"] = traceback.format_exc()
    OUT.mkdir(parents=True, exist_ok=True)
    destination = OUT / "baseline.json"
    # A baseline is evidence, not a rolling source of new authoring values.
    assert not destination.exists(), "Baseline already captured; do not overwrite it"
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return result


if __name__ == "__main__":
    report = capture()
    unreal.MCPythonHelper.submit_result(json.dumps({"success": report["success"], "error": report.get("error"),
                                                  "artworks": len(report["artworks"]),
                                                  "pie_running": report.get("pie_running")}, ensure_ascii=False))

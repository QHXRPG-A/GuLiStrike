"""Read ship part / ship BP CDO values in the live editor; write JSON for the Excel seed.

Run via: python Scripts/ue_exec.py Scripts/read_ship_cdos.py
Output:  Data/tmp_cdo_read.json (consumed by Tools/DataPipeline/make_template_xlsx.py)
"""
import json
import traceback

import unreal

OUT_PATH = "D:/UE_5.7/test1/Data/tmp_cdo_read.json"

out = {"errors": []}

try:
    out["struct_live"] = unreal.find_object(None, "/Script/GuLiStrike.GuLiStrikeShipPartRow") is not None
    out["tuning_struct_live"] = unreal.find_object(None, "/Script/GuLiStrike.GuLiStrikeShipTuningRow") is not None
    ship_cls_live = unreal.find_object(None, "/Script/GuLiStrike.GuLiStrikeShip")
    out["ship_cls_live"] = ship_cls_live is not None
    try:
        cdo_check = unreal.get_default_object(ship_cls_live)
        cdo_check.get_editor_property("part_data_table")
        out["dt_prop_live"] = True
    except Exception:
        out["dt_prop_live"] = False

    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    parts = []
    for ad in ar.get_assets_by_path("/Game/GuLiStrike", recursive=True):
        try:
            asset = ad.get_asset()
            generated = None
            if isinstance(asset, unreal.Blueprint):
                generated = asset.generated_class()
            if not generated:
                continue
            cdo = unreal.get_default_object(generated)
            if not isinstance(cdo, unreal.GuLiStrikeShipPartComponent):
                continue
            entry = {
                "asset_path": str(ad.package_name),
                "is_engine": isinstance(cdo, unreal.GuLiStrikeEnginePart),
                "is_weapon": isinstance(cdo, unreal.GuLiStrikeWeaponPart),
                "part_id": str(cdo.get_editor_property("part_id")),
                "part_mass": float(cdo.get_editor_property("part_mass")),
                "display": str(cdo.get_editor_property("part_display_name")),
                "sockets": sorted(str(s) for s in cdo.get_editor_property("compatible_sockets")),
            }
            mesh = cdo.get_editor_property("static_mesh")
            entry["mesh"] = str(mesh.get_path_name()) if mesh else None
            if entry["is_engine"]:
                entry["thrust"] = float(cdo.get_editor_property("thrust"))
            if entry["is_weapon"]:
                entry["damage"] = float(cdo.get_editor_property("damage"))
                entry["fire_rate"] = float(cdo.get_editor_property("fire_rate"))
                mo = cdo.get_editor_property("muzzle_offset")
                entry["muzzle_offset"] = [round(mo.x, 2), round(mo.y, 2), round(mo.z, 2)]
                pc = cdo.get_editor_property("projectile_class")
                entry["projectile_class"] = str(pc.get_path_name()) if pc else None
            parts.append(entry)
        except Exception as e:  # noqa: BLE001
            out["errors"].append(f"{ad.package_name}: {e!r}")
    out["parts"] = parts
except Exception:  # noqa: BLE001
    out["errors"].append(traceback.format_exc())

try:
    ship_bp = unreal.load_object(None, "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip.BP_GuLiStrikeShip")
    if ship_bp:
        scdo = unreal.get_default_object(ship_bp.generated_class())
        props = [
            "hull_mass", "base_max_speed", "base_acceleration", "nominal_thrust_ratio",
            "speed_multiplier_min", "speed_multiplier_max", "boost_thrust_multiplier",
            "mouse_pitch_scale", "mouse_yaw_scale", "yaw_rate", "yaw_response_speed",
            "yaw_stop_damping", "max_bank_angle", "bank_interp_speed", "orient_turn_speed",
            "orient_to_movement", "orient_min_forward_dot", "camera_pitch_min", "camera_pitch_max",
        ]
        tuning = {}
        for p in props:
            v = scdo.get_editor_property(p)
            tuning[p] = bool(v) if isinstance(v, bool) else round(float(v), 4)
        hmo = scdo.get_editor_property("hull_mesh_offset")
        tuning["hull_mesh_offset"] = [round(hmo.x, 2), round(hmo.y, 2), round(hmo.z, 2)]
        out["ship_tuning"] = tuning
        dps = []
        for dp in scdo.get_editor_property("default_parts"):
            pc = dp.get_editor_property("part_class")
            dps.append({
                "socket": str(dp.get_editor_property("socket_name")),
                "part": str(pc.get_path_name()) if pc else None,
            })
        out["default_parts"] = dps
        out["part_catalogue"] = [
            str(c.get_path_name()) for c in scdo.get_editor_property("part_catalogue") if c
        ]
    else:
        out["ship_tuning"] = None
except Exception:  # noqa: BLE001
    out["errors"].append(traceback.format_exc())

with open(OUT_PATH, "w", encoding="utf-8") as f:
    json.dump(out, f, ensure_ascii=False, indent=2, default=str)

print(f"wrote {OUT_PATH}")

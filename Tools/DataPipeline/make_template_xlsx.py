"""Generate Data/Excel/GuLiStrikeShip.xlsx from CDO seed values (one-shot, reusable).

Input:  Data/tmp_cdo_read.json  (produced by Scripts/read_ship_cdos.py in the live editor)
Output: Data/Excel/GuLiStrikeShip.xlsx with sheets "Parts" and "Tuning"

Run: python Tools/DataPipeline/make_template_xlsx.py
"""
import json
import sys
from pathlib import Path

from openpyxl import Workbook
from openpyxl.styles import Alignment, Font, PatternFill
from openpyxl.utils import get_column_letter

PROJECT_ROOT = Path(__file__).resolve().parents[2]
SEED_PATH = PROJECT_ROOT / "Data" / "tmp_cdo_read.json"
XLSX_PATH = PROJECT_ROOT / "Data" / "Excel" / "GuLiStrikeShip.xlsx"

# 蓝图资产名 → 策划可读显示名（蓝图上 PartDisplayName 为空时的兜底）
DISPLAY_FALLBACK = {
    "Engine_Standard": "标准引擎",
    "Engine_Heavy": "重型引擎",
    "Weapon_Laser": "激光炮",
    "Weapon_RocketPod": "火箭巢",
}

PARTS_HEADERS = [
    "PartId", "Type", "DisplayName", "PartMass", "Thrust", "Damage", "FireRate",
    "MuzzleX", "MuzzleY", "MuzzleZ", "ProjectileClass", "备注",
]

TUNING_HEADERS = [
    "Preset", "HullMass", "BaseMaxSpeed", "BaseAcceleration", "NominalThrustRatio",
    "SpeedMultiplierMin", "SpeedMultiplierMax", "BoostThrustMultiplier",
    "MousePitchScale", "MouseYawScale", "YawRate", "YawResponseSpeed", "YawStopDamping",
    "MaxBankAngle", "BankInterpSpeed", "OrientTurnSpeed", "OrientToMovement",
    "OrientMinForwardDot", "CameraPitchMin", "CameraPitchMax",
    "HullMeshOffsetX", "HullMeshOffsetY", "HullMeshOffsetZ", "备注",
]

HEADER_FILL = PatternFill("solid", fgColor="DDEBF7")
HEADER_FONT = Font(bold=True)


def style_header(ws, ncols):
    for col in range(1, ncols + 1):
        cell = ws.cell(row=1, column=col)
        cell.fill = HEADER_FILL
        cell.font = HEADER_FONT
        cell.alignment = Alignment(horizontal="center")
    ws.freeze_panes = "A2"
    for col in range(1, ncols + 1):
        width = max(len(str(ws.cell(row=1, column=col).value or "")) * 1.6, 12)
        ws.column_dimensions[get_column_letter(col)].width = min(width, 42)


def part_id_from_asset(asset_path: str) -> str:
    """PartId 约定 = 蓝图资产名去掉 BP_ 前缀。"""
    name = asset_path.rsplit("/", 1)[-1]
    return name[3:] if name.startswith("BP_") else name


def build_parts_sheet(ws, parts):
    ws.append(PARTS_HEADERS)
    for p in sorted(parts, key=lambda x: (not x["is_engine"], x["asset_path"])):
        pid = part_id_from_asset(p["asset_path"])
        display = p.get("display") or DISPLAY_FALLBACK.get(pid, pid)
        row = [pid, "Engine" if p["is_engine"] else "Weapon", display, p["part_mass"]]
        if p["is_engine"]:
            row += [p["thrust"], None, None, None, None, None, None]
        else:
            mo = p["muzzle_offset"]
            row += [
                None, p["damage"], round(p["fire_rate"], 4),
                mo[0], mo[1], mo[2], p.get("projectile_class"),
            ]
        row.append(None)  # 备注
        ws.append(row)
    style_header(ws, len(PARTS_HEADERS))


def build_tuning_sheet(ws, tuning):
    ws.append(TUNING_HEADERS)
    hmo = tuning["hull_mesh_offset"]
    ws.append([
        "Default",
        tuning["hull_mass"], tuning["base_max_speed"], tuning["base_acceleration"],
        tuning["nominal_thrust_ratio"], tuning["speed_multiplier_min"],
        tuning["speed_multiplier_max"], tuning["boost_thrust_multiplier"],
        tuning["mouse_pitch_scale"], tuning["mouse_yaw_scale"], tuning["yaw_rate"],
        tuning["yaw_response_speed"], tuning["yaw_stop_damping"], tuning["max_bank_angle"],
        tuning["bank_interp_speed"], tuning["orient_turn_speed"],
        tuning["orient_to_movement"], tuning["orient_min_forward_dot"],
        tuning["camera_pitch_min"], tuning["camera_pitch_max"],
        hmo[0], hmo[1], hmo[2],
        None,
    ])
    style_header(ws, len(TUNING_HEADERS))


def main() -> int:
    if not SEED_PATH.exists():
        print(f"seed file missing: {SEED_PATH}\nrun Scripts/read_ship_cdos.py in the editor first")
        return 1
    seed = json.loads(SEED_PATH.read_text(encoding="utf-8"))
    if seed.get("errors"):
        print("seed has errors, aborting:")
        for e in seed["errors"]:
            print(" ", e)
        return 1

    XLSX_PATH.parent.mkdir(parents=True, exist_ok=True)
    wb = Workbook()
    build_parts_sheet(wb.active, seed["parts"])
    wb.active.title = "Parts"
    build_tuning_sheet(wb.create_sheet("Tuning"), seed["ship_tuning"])
    wb.save(XLSX_PATH)
    print(f"wrote {XLSX_PATH} ({len(seed['parts'])} part rows, 1 tuning preset)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Read-only UE5.7 validation for the Wingman-only ground missile contract.

Run through ``Scripts/ue_exec.py`` with PIE stopped. The script never mutates or
saves assets; it records the exact projectile sharing boundary, table-driven AOE
radius, derived explosion scale, cook inclusion, and Niagara compile readback.
"""
import json
from pathlib import Path
import traceback

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
REPORT_PATH = ROOT / "TestResults/WingmanAttack/vfx-validation.json"
COMMANDER_PROJECTILE = "/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile"
WINGMAN_PROJECTILE = "/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile"
WEAPON_TABLE = "/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons"
EXPECTED_COOK_DIRECTORY = '/Game/GuLiStrike/FX/WingmanWeapons'


def object_path(value):
    return value.get_path_name() if value and hasattr(value, "get_path_name") else ""


def package_path(value):
    return object_path(value).split(".", 1)[0]


def close(actual, expected, tolerance=0.001):
    return abs(float(actual) - float(expected)) <= tolerance


def compile_system(path):
    result = unreal.NiagaraService.compile_with_results(path)
    emitters = [
        str(item.get_editor_property("emitter_name"))
        for item in unreal.NiagaraService.list_emitters(path)
    ]
    return {
        "path": path,
        "success": bool(result.get_editor_property("success")),
        "errors": list(result.get_editor_property("errors")),
        "warnings": list(result.get_editor_property("warnings")),
        "emitters": emitters,
    }


def main():
    command_line = unreal.SystemLibrary.get_command_line().lower()
    commandlet = "-run=pythonscript" in command_line
    if not commandlet:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level.is_in_play_in_editor():
            raise RuntimeError("Stop PIE before Wingman combat-effects validation")

    commander = unreal.load_asset(COMMANDER_PROJECTILE)
    wingman = unreal.load_asset(WINGMAN_PROJECTILE)
    table = unreal.load_asset(WEAPON_TABLE)
    if not commander or not wingman or not table:
        raise RuntimeError("Required Commander/Wingman projectile or weapon table is missing")

    commander_flight = commander.get_editor_property("flight_system")
    wingman_flight = wingman.get_editor_property("flight_system")
    commander_field = commander.get_editor_property("impact_field")
    wingman_field = wingman.get_editor_property("impact_field")
    if not commander_flight or not commander_field:
        raise RuntimeError("Commander WM01 projectile references are incomplete")

    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    by_name = {row["Name"]: row for row in rows}
    machine = by_name.get("WingmanMachineGun")
    ground = by_name.get("WingmanGroundMissile")
    if not machine or not ground:
        raise RuntimeError("Wingman weapon table rows are incomplete")

    reference_radius = float(commander_field.get_editor_property("radius"))
    frozen_radius = float(ground["ExplosionRadiusCentimeters"])
    explosion_scale = frozen_radius / max(reference_radius, 1.0)
    variants = list(commander_field.get_editor_property("activation_variants"))
    variant_rows = []
    systems = {package_path(commander_flight)}
    for variant in variants:
        system = variant.get_editor_property("system")
        path = package_path(system)
        if path:
            systems.add(path)
        variant_rows.append({
            "system": path,
            "authoring_scale": float(variant.get_editor_property("scale")),
            "runtime_component_scale": float(variant.get_editor_property("scale")) * explosion_scale,
            "maximum_lifetime": float(variant.get_editor_property("maximum_lifetime")),
        })

    checks = {
        "commander_visual_scale_is_one": close(commander.get_editor_property("visual_scale"), 1.0),
        "wingman_visual_scale_is_two": close(wingman.get_editor_property("visual_scale"), 2.0),
        "flight_system_is_shared": object_path(commander_flight) == object_path(wingman_flight),
        "impact_field_is_shared": object_path(commander_field) == object_path(wingman_field),
        "shared_reference_radius_is_800": close(reference_radius, 800.0),
        "wingman_ground_radius_is_4000": close(frozen_radius, 4000.0),
        "derived_explosion_scale_is_five": close(explosion_scale, 5.0),
        "runtime_user_radius_is_4000": close(frozen_radius, 4000.0),
        "activation_variants_keep_unit_authoring_scale": bool(variants) and all(
            close(row["authoring_scale"], 1.0) for row in variant_rows
        ),
        "both_attack_flight_speeds_are_9000": close(
            machine["FlightSpeedCentimetersPerSecond"], 9000.0
        ) and close(ground["FlightSpeedCentimetersPerSecond"], 9000.0),
        "ground_targeting_contract_unchanged": close(ground["RangeCentimeters"], 150000.0)
        and int(ground["MissileCount"]) == 10
        and close(ground["StripLengthCentimeters"], 12000.0),
        "machine_aoe_radius_unchanged": close(machine["ExplosionRadiusCentimeters"], 800.0),
        "wingman_fx_directory_is_cooked": (
            f'+DirectoriesToAlwaysCook=(Path="{EXPECTED_COOK_DIRECTORY}")'
            in (ROOT / "Config/DefaultGame.ini").read_text(encoding="utf-8")
        ),
    }

    compile_rows = [compile_system(path) for path in sorted(systems) if path]
    checks["niagara_systems_compile"] = bool(compile_rows) and all(
        row["success"] and not row["errors"] for row in compile_rows
    )
    flight_row = next(
        (row for row in compile_rows if row["path"] == package_path(commander_flight)), None
    )
    checks["flight_contains_body_exhaust_and_trail_emitters"] = bool(flight_row) and all(
        any(token in emitter.lower() for emitter in flight_row["emitters"])
        for token in ("core", "exhaust", "trail")
    )

    errors = [name for name, passed in checks.items() if not passed]
    return {
        "success": not errors,
        "commandlet": commandlet,
        "errors": errors,
        "checks": checks,
        "projectiles": {
            "commander": {
                "path": object_path(commander),
                "visual_scale": float(commander.get_editor_property("visual_scale")),
            },
            "wingman": {
                "path": object_path(wingman),
                "visual_scale": float(wingman.get_editor_property("visual_scale")),
            },
            "shared_flight_system": object_path(commander_flight),
            "shared_impact_field": object_path(commander_field),
        },
        "explosion": {
            "reference_radius": reference_radius,
            "frozen_radius": frozen_radius,
            "field_scale": explosion_scale,
            "user_radius": frozen_radius,
            "dissipation_seconds": float(commander_field.get_editor_property("dissipation_seconds")),
            "activation_variants": variant_rows,
        },
        "weapon_rows": {"machine_gun": machine, "ground_missile": ground},
        "niagara": compile_rows,
        "dirty_packages_after_readback": sorted(
            str(package.get_name())
            for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        ),
    }


try:
    result = main()
except Exception:
    result = {"success": False, "error": traceback.format_exc()}

REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
REPORT_PATH.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
if hasattr(unreal, "MCPythonHelper"):
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))

"""Audit the authored Ship Ability v2 contract through public editor APIs.

Run twice with an editor restart between runs to turn ``restart_verified`` true:
    python Scripts/ue_exec.py Scripts/audit_ship_ability_assets.py
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import struct
import traceback
from typing import Any

import unreal


OWNER = "GuLi.ShipAbilityAssets.v1"
SHIP_BLUEPRINT = "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip"
ABILITY_SET = "/Game/GuLiStrike/Ship/Abilities/DA_ShipAbilitySet_WingmanV1"
FORMATION = (
    "/Game/GuLiStrike/Ship/Abilities/Formations/"
    "DA_WingmanFormation_DoubleRing"
)
SWARM_FORMATION = (
    "/Game/GuLiStrike/Ship/Abilities/Formations/"
    "DA_WingmanFormation_SwarmOrbit"
)
BASIC = (
    "/Game/GuLiStrike/Ship/Abilities/Weapons/"
    "DA_WingmanWeapon_BasicAuto"
)
MISSILE = (
    "/Game/GuLiStrike/Ship/Abilities/Weapons/"
    "DA_WingmanWeapon_MissileSalvo"
)
ALL_ASSETS = (FORMATION, SWARM_FORMATION, BASIC, MISSILE, ABILITY_SET)

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "ShipAbilityAssets",
    "index.json",
)
CONFIG_PATH = os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini")
ALWAYS_COOK_LINE = (
    '+DirectoriesToAlwaysCook=(Path="/Game/GuLiStrike/Ship/Abilities")'
)

FORMATION_VALUES = {
    "revision": 2,
    "expected_wingman_count": 25,
    "flight_count": 5,
    "model": "DoubleRingLegacy",
    "guidance_algorithm_version": 1,
    "inner_ring_slots": 13,
    "outer_ring_slots": 12,
    "inner_ring_radius_centimeters": 60000.0,
    "outer_ring_radius_centimeters": 90000.0,
    "inner_ring_height_centimeters": 15000.0,
    "outer_ring_height_centimeters": -15000.0,
    "inner_angular_speed_radians_per_second": 0.08,
    "outer_angular_speed_radians_per_second": 0.06,
    "minimum_flight_speed_centimeters_per_second": 3000.0,
    "cruise_flight_speed_centimeters_per_second": 4500.0,
    "catch_up_flight_speed_centimeters_per_second": 7500.0,
    "maximum_turn_rate_degrees_per_second": 20.0,
    "maximum_acceleration_centimeters_per_second_squared": 1000.0,
    "maximum_deceleration_centimeters_per_second_squared": 800.0,
    "maximum_bank_degrees": 45.0,
    "agent_radius_centimeters": 1500.0,
    "separation_radius_centimeters": 3000.0,
    "obstacle_look_ahead_centimeters": 5000.0,
    "catch_up_distance_centimeters": 120000.0,
    "recovery_distance_centimeters": 250000.0,
}
SWARM_FORMATION_VALUES = {
    "revision": 1,
    "expected_wingman_count": 25,
    "flight_count": 5,
    "model": "SwarmOrbit",
    "guidance_algorithm_version": 1,
    "minimum_flight_speed_centimeters_per_second": 3000.0,
    "cruise_flight_speed_centimeters_per_second": 4500.0,
    "catch_up_flight_speed_centimeters_per_second": 7500.0,
    "maximum_turn_rate_degrees_per_second": 20.0,
    "maximum_acceleration_centimeters_per_second_squared": 1000.0,
    "maximum_deceleration_centimeters_per_second_squared": 800.0,
    "maximum_bank_degrees": 45.0,
    "agent_radius_centimeters": 1500.0,
    "separation_radius_centimeters": 3000.0,
    "obstacle_look_ahead_centimeters": 5000.0,
    "catch_up_distance_centimeters": 60000.0,
    "recovery_distance_centimeters": 90000.0,
}
SWARM_TUNING_VALUES = {
    "inner_soft_radius_centimeters": 24000.0,
    "outer_soft_radius_centimeters": 52000.0,
    "vertical_half_extent_centimeters": 14000.0,
    "hull_exclusion_radius_centimeters": 14000.0,
    "swirl_speed_min_centimeters_per_second": 3600.0,
    "swirl_speed_max_centimeters_per_second": 5200.0,
    "curl_strength_centimeters_per_second": 1400.0,
    "noise_spatial_scale_centimeters": 22000.0,
    "noise_temporal_scale_seconds": 6.0,
    "axis_precession_amount": 0.28,
    "axis_precession_radians_per_second": 0.03,
    "boundary_return_speed_centimeters_per_second": 2800.0,
    "preferred_radius_return_speed_centimeters_per_second": 450.0,
    "vertical_return_speed_centimeters_per_second": 1400.0,
    "alignment_weight": 0.08,
    "catch_up_style_weight": 0.20,
    "recovery_style_weight": 0.05,
    "response_time_seconds": 1.25,
}
LEGACY_MODEL_HASH_ORDER = (
    "inner_ring_slots",
    "outer_ring_slots",
    "inner_ring_radius_centimeters",
    "outer_ring_radius_centimeters",
    "inner_ring_height_centimeters",
    "outer_ring_height_centimeters",
    "inner_angular_speed_radians_per_second",
    "outer_angular_speed_radians_per_second",
)
FORMATION_COMMON_HASH_ORDER = (
    "minimum_flight_speed_centimeters_per_second",
    "cruise_flight_speed_centimeters_per_second",
    "catch_up_flight_speed_centimeters_per_second",
    "maximum_turn_rate_degrees_per_second",
    "maximum_acceleration_centimeters_per_second_squared",
    "maximum_deceleration_centimeters_per_second_squared",
    "maximum_bank_degrees",
    "agent_radius_centimeters",
    "separation_radius_centimeters",
    "obstacle_look_ahead_centimeters",
    "catch_up_distance_centimeters",
    "recovery_distance_centimeters",
)
SWARM_TUNING_HASH_ORDER = tuple(SWARM_TUNING_VALUES)

BASIC_VALUES = {
    "revision": 1,
    "kind": "BasicAutomatic",
    "damage": 10.0,
    "range_centimeters": 150000.0,
    "cooldown_seconds": 2.0,
    "projectile_speed_centimeters_per_second": 120000.0,
    "projectile_lifetime_seconds": 1.5,
    "sweep_radius_centimeters": 45.0,
    "target_cone_half_angle_degrees": 20.0,
    "requires_line_of_sight": True,
    "maximum_homing_turn_rate_degrees_per_second": 0.0,
}
MISSILE_VALUES = {
    "revision": 1,
    "kind": "Missile",
    "damage": 100.0,
    "range_centimeters": 250000.0,
    "cooldown_seconds": 8.0,
    "projectile_speed_centimeters_per_second": 45000.0,
    "projectile_lifetime_seconds": 8.0,
    "sweep_radius_centimeters": 150.0,
    "target_cone_half_angle_degrees": 8.0,
    "requires_line_of_sight": True,
    "maximum_homing_turn_rate_degrees_per_second": 45.0,
}
WEAPON_HASH_ORDER = (
    "revision",
    "kind",
    "damage",
    "range_centimeters",
    "cooldown_seconds",
    "projectile_speed_centimeters_per_second",
    "projectile_lifetime_seconds",
    "sweep_radius_centimeters",
    "target_cone_half_angle_degrees",
    "requires_line_of_sight",
    "maximum_homing_turn_rate_degrees_per_second",
)


class AuditError(RuntimeError):
    pass


def _asset_object_path(package_path: str) -> str:
    return package_path + "." + package_path.rsplit("/", 1)[-1]


def _object_path(value: Any) -> str:
    return str(value.get_path_name()) if value is not None else ""


def _tag_name(value: Any) -> str:
    if value is None or not unreal.GameplayTagLibrary.is_gameplay_tag_valid(value):
        return ""
    return str(unreal.GameplayTagLibrary.get_tag_name(value))


def _slot_name(value: Any) -> str:
    mapping = {
        unreal.GuLiShipAbilitySlot.NONE: "None",
        unreal.GuLiShipAbilitySlot.FORMATION: "Formation",
        unreal.GuLiShipAbilitySlot.BASIC_WEAPON: "BasicWeapon",
        unreal.GuLiShipAbilitySlot.MISSILE: "Missile",
    }
    return mapping.get(value, str(value))


def _read_formation(asset: Any, expected_values: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, expected in expected_values.items():
        value = asset.get_editor_property(name)
        if name == "model":
            result[name] = (
                "SwarmOrbit"
                if value == unreal.GuLiWingmanFormationModel.SWARM_ORBIT
                else "DoubleRingLegacy"
                if value == unreal.GuLiWingmanFormationModel.DOUBLE_RING_LEGACY
                else str(value)
            )
        else:
            result[name] = float(value) if isinstance(expected, float) else int(value)
    return result


def _read_swarm_tuning(asset: Any) -> dict[str, float]:
    tuning = asset.get_editor_property("swarm_orbit")
    return {
        name: float(tuning.get_editor_property(name))
        for name in SWARM_TUNING_VALUES
    }


def _read_weapon(asset: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, expected in BASIC_VALUES.items():
        value = asset.get_editor_property(name)
        if name == "kind":
            result[name] = (
                "Missile"
                if value == unreal.GuLiWingmanWeaponKind.MISSILE
                else "BasicAutomatic"
                if value == unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC
                else str(value)
            )
        elif isinstance(expected, float):
            result[name] = float(value)
        elif isinstance(expected, bool):
            result[name] = bool(value)
        else:
            result[name] = int(value)
    return result


def _grant_record(grant: Any) -> dict[str, Any]:
    return {
        "ability_id": _tag_name(grant.get_editor_property("ability_id")),
        "slot": _slot_name(grant.get_editor_property("slot")),
        "ability_class": _object_path(grant.get_editor_property("ability_class")),
        "ability_level": int(grant.get_editor_property("ability_level")),
        "input_tag": _tag_name(grant.get_editor_property("input_tag")),
        "formation_definition": _object_path(
            grant.get_editor_property("formation_definition")
        ),
        "weapon_definition": _object_path(
            grant.get_editor_property("weapon_definition")
        ),
    }


def _expected_grants() -> list[dict[str, Any]]:
    return [
        {
            "ability_id": "Ship.Ability.Formation.SwarmOrbit",
            "slot": "Formation",
            "ability_class": "/Script/GuLiStrike.GuLiShipSwarmOrbitFormationAbility",
            "ability_level": 1,
            "input_tag": "",
            "formation_definition": _asset_object_path(SWARM_FORMATION),
            "weapon_definition": "",
        },
        {
            "ability_id": "Ship.Ability.Formation.DoubleRing",
            "slot": "Formation",
            "ability_class": "/Script/GuLiStrike.GuLiShipDoubleRingFormationAbility",
            "ability_level": 1,
            "input_tag": "",
            "formation_definition": _asset_object_path(FORMATION),
            "weapon_definition": "",
        },
        {
            "ability_id": "Ship.Ability.Weapon.Basic.Auto",
            "slot": "BasicWeapon",
            "ability_class": "/Script/GuLiStrike.GuLiShipBasicAutomaticWeaponAbility",
            "ability_level": 1,
            "input_tag": "",
            "formation_definition": "",
            "weapon_definition": _asset_object_path(BASIC),
        },
        {
            "ability_id": "Ship.Ability.Weapon.Missile.Salvo",
            "slot": "Missile",
            "ability_class": "/Script/GuLiStrike.GuLiShipMissileSalvoAbility",
            "ability_level": 1,
            "input_tag": "InputTag.Ship.Wingman.Missile",
            "formation_definition": "",
            "weapon_definition": _asset_object_path(MISSILE),
        },
    ]


FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
U64_MASK = (1 << 64) - 1


def _hash_bytes(value: int, data: bytes) -> int:
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & U64_MASK
    return value


def _hash_u32(value: int, item: int) -> int:
    return _hash_bytes(value, int(item).to_bytes(4, "little", signed=False))


def _hash_u64(value: int, item: int) -> int:
    return _hash_bytes(value, int(item).to_bytes(8, "little", signed=False))


def _hash_float(value: int, item: float) -> int:
    return _hash_bytes(value, struct.pack("<f", float(item)))


def _hash_bool(value: int, item: bool) -> int:
    return _hash_bytes(value, b"\x01" if item else b"\x00")


def _hash_string(value: int, item: str) -> int:
    encoded = item.encode("utf-8")
    return _hash_bytes(_hash_u32(value, len(encoded)), encoded)


def _finish(value: int) -> int:
    return value if value != 0 else 1


def _hash_numeric_fields(
    value: int, values: dict[str, Any], names: tuple[str, ...]
) -> int:
    for name in names:
        item = values[name]
        value = (
            _hash_float(value, item)
            if isinstance(item, float)
            else _hash_u32(value, item)
        )
    return value


def _formation_checksum(
    values: dict[str, Any], swarm_tuning: dict[str, float] | None = None
) -> int:
    value = _hash_string(FNV_OFFSET, "GuLi.WingmanFormation.v2")
    value = _hash_u32(value, values["revision"])
    value = _hash_u32(value, values["expected_wingman_count"])
    value = _hash_u32(value, values["flight_count"])
    is_swarm = values["model"] == "SwarmOrbit"
    value = _hash_u32(value, 1 if is_swarm else 0)
    value = _hash_u32(value, values["guidance_algorithm_version"])
    if is_swarm:
        if swarm_tuning is None:
            raise AuditError("SwarmOrbit checksum requires its nested tuning payload")
        value = _hash_numeric_fields(value, swarm_tuning, SWARM_TUNING_HASH_ORDER)
    else:
        value = _hash_numeric_fields(value, values, LEGACY_MODEL_HASH_ORDER)
    value = _hash_numeric_fields(value, values, FORMATION_COMMON_HASH_ORDER)
    return _finish(value)


def _weapon_checksum(values: dict[str, Any]) -> int:
    value = _hash_string(FNV_OFFSET, "GuLi.WingmanWeapon.v1")
    for name in WEAPON_HASH_ORDER:
        item = values[name]
        if name == "kind":
            value = _hash_u32(value, 1 if item == "Missile" else 0)
        elif isinstance(BASIC_VALUES[name], float):
            value = _hash_float(value, item)
        elif isinstance(BASIC_VALUES[name], bool):
            value = _hash_bool(value, item)
        else:
            value = _hash_u32(value, item)
    return _finish(value)


def _set_checksum(
    revision: int,
    grants: list[dict[str, Any]],
    definition_checksums: dict[str, int],
) -> int:
    value = _hash_string(FNV_OFFSET, "GuLi.ShipAbilitySet.v1")
    value = _hash_u32(value, revision)
    value = _hash_u32(value, 2)  # FGuLiShipAbilityLoadoutState::MakeNativeV2
    slot_values = {"Formation": 1, "BasicWeapon": 2, "Missile": 3}
    selected_ids = (
        "Ship.Ability.Formation.SwarmOrbit",
        "Ship.Ability.Weapon.Basic.Auto",
        "Ship.Ability.Weapon.Missile.Salvo",
    )
    grants_by_id = {grant["ability_id"]: grant for grant in grants}
    for ability_id in selected_ids:
        grant = grants_by_id[ability_id]
        value = _hash_string(value, grant["ability_id"])
        value = _hash_u32(value, slot_values[grant["slot"]])
        value = _hash_string(value, grant["ability_class"])
        value = _hash_u32(value, grant["ability_level"])
        value = _hash_string(value, grant["input_tag"])
        if grant["slot"] == "Formation":
            definition_revision = SWARM_FORMATION_VALUES["revision"]
            definition_checksum = definition_checksums["swarm_formation"]
        elif grant["slot"] == "BasicWeapon":
            definition_revision = BASIC_VALUES["revision"]
            definition_checksum = definition_checksums["basic"]
        else:
            definition_revision = MISSILE_VALUES["revision"]
            definition_checksum = definition_checksums["missile"]
        value = _hash_u32(value, definition_revision)
        value = _hash_u64(value, definition_checksum)
    return _finish(value)


def _checksum_record(value: int) -> dict[str, Any]:
    return {"decimal": value, "hex": f"0x{value:016X}"}


def _equal_values(actual: dict[str, Any], expected: dict[str, Any]) -> bool:
    for name, expected_value in expected.items():
        actual_value = actual.get(name)
        if isinstance(expected_value, float):
            if not math.isclose(
                float(actual_value), expected_value, rel_tol=0.0, abs_tol=1.0e-4
            ):
                return False
        elif actual_value != expected_value:
            return False
    return True


def _load_exact(path: str, expected_class_path: str) -> Any:
    asset = unreal.load_asset(path)
    if asset is None:
        raise AuditError(f"Missing asset: {path}")
    actual_class = str(asset.get_class().get_path_name())
    if actual_class != expected_class_path:
        raise AuditError(
            f"Wrong object class at {path}: {actual_class} != {expected_class_path}"
        )
    return asset


def _referencers(path: str) -> list[str]:
    return sorted(
        str(value)
        for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
            path, True
        )
    )


def _read_previous() -> dict[str, Any] | None:
    if not os.path.exists(REPORT_PATH):
        return None
    try:
        with open(REPORT_PATH, encoding="utf-8") as stream:
            value = json.load(stream)
        return value if isinstance(value, dict) else None
    except Exception:
        return None


def _write_report(report: dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def main() -> dict[str, Any]:
    previous = _read_previous()
    report: dict[str, Any] = {
        "schema": 1,
        "success": False,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "editor_process_id": os.getpid(),
    }
    try:
        formation = _load_exact(
            FORMATION, "/Script/GuLiStrike.GuLiWingmanFormationDefinition"
        )
        swarm_formation = _load_exact(
            SWARM_FORMATION, "/Script/GuLiStrike.GuLiWingmanFormationDefinition"
        )
        basic = _load_exact(
            BASIC, "/Script/GuLiStrike.GuLiWingmanWeaponDefinition"
        )
        missile = _load_exact(
            MISSILE, "/Script/GuLiStrike.GuLiWingmanWeaponDefinition"
        )
        ability_set = _load_exact(
            ABILITY_SET, "/Script/GuLiStrike.GuLiShipAbilitySet"
        )

        formation_values = _read_formation(formation, FORMATION_VALUES)
        swarm_formation_values = _read_formation(
            swarm_formation, SWARM_FORMATION_VALUES
        )
        swarm_tuning_values = _read_swarm_tuning(swarm_formation)
        basic_values = _read_weapon(basic)
        missile_values = _read_weapon(missile)
        grants = [
            _grant_record(grant)
            for grant in list(ability_set.get_editor_property("grants"))
        ]
        set_revision = int(ability_set.get_editor_property("revision"))

        actual_checksums = {
            "formation": _formation_checksum(formation_values),
            "swarm_formation": _formation_checksum(
                swarm_formation_values, swarm_tuning_values
            ),
            "basic": _weapon_checksum(basic_values),
            "missile": _weapon_checksum(missile_values),
        }
        actual_checksums["ability_set_loadout_v2"] = _set_checksum(
            set_revision, grants, actual_checksums
        )
        expected_checksums = {
            "formation": _formation_checksum(FORMATION_VALUES),
            "swarm_formation": _formation_checksum(
                SWARM_FORMATION_VALUES, SWARM_TUNING_VALUES
            ),
            "basic": _weapon_checksum(BASIC_VALUES),
            "missile": _weapon_checksum(MISSILE_VALUES),
        }
        expected_checksums["ability_set_loadout_v2"] = _set_checksum(
            2, _expected_grants(), expected_checksums
        )

        blueprint_class = unreal.EditorAssetLibrary.load_blueprint_class(
            SHIP_BLUEPRINT
        )
        if blueprint_class is None:
            raise AuditError("Production Ship Blueprint class is missing")
        cdo = unreal.get_default_object(blueprint_class)
        cdo_reference = _object_path(cdo.get_editor_property("ship_ability_set"))

        dirty_packages = sorted(
            {
                str(package.get_name())
                for package in (
                    list(
                        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
                    )
                    + list(
                        unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
                    )
                )
            }
        )
        metadata = {
            path: str(
                unreal.EditorAssetLibrary.get_metadata_tag(asset, "GuLi.Deployment.Owner")
                or ""
            )
            for path, asset in (
                (FORMATION, formation),
                (SWARM_FORMATION, swarm_formation),
                (BASIC, basic),
                (MISSILE, missile),
                (ABILITY_SET, ability_set),
            )
        }
        referencers = {
            FORMATION: _referencers(FORMATION),
            SWARM_FORMATION: _referencers(SWARM_FORMATION),
            BASIC: _referencers(BASIC),
            MISSILE: _referencers(MISSILE),
            ABILITY_SET: _referencers(ABILITY_SET),
        }
        with open(CONFIG_PATH, encoding="utf-8-sig") as stream:
            config_text = stream.read()
        always_cooked = ALWAYS_COOK_LINE in config_text

        contract = {
            "assets": {
                FORMATION: {
                    "object_path": _object_path(formation),
                    "object_class": _object_path(formation.get_class()),
                    "values": formation_values,
                    "stable_checksum": _checksum_record(
                        actual_checksums["formation"]
                    ),
                },
                SWARM_FORMATION: {
                    "object_path": _object_path(swarm_formation),
                    "object_class": _object_path(swarm_formation.get_class()),
                    "values": swarm_formation_values,
                    "swarm_orbit": swarm_tuning_values,
                    "stable_checksum": _checksum_record(
                        actual_checksums["swarm_formation"]
                    ),
                },
                BASIC: {
                    "object_path": _object_path(basic),
                    "object_class": _object_path(basic.get_class()),
                    "values": basic_values,
                    "stable_checksum": _checksum_record(actual_checksums["basic"]),
                },
                MISSILE: {
                    "object_path": _object_path(missile),
                    "object_class": _object_path(missile.get_class()),
                    "values": missile_values,
                    "stable_checksum": _checksum_record(
                        actual_checksums["missile"]
                    ),
                },
                ABILITY_SET: {
                    "object_path": _object_path(ability_set),
                    "object_class": _object_path(ability_set.get_class()),
                    "revision": set_revision,
                    "grants": grants,
                    "stable_loadout_checksum": _checksum_record(
                        actual_checksums["ability_set_loadout_v2"]
                    ),
                },
            },
            "blueprint_class": _object_path(blueprint_class),
            "blueprint_cdo_ship_ability_set": cdo_reference,
            "metadata_owner": metadata,
            "referencers": referencers,
            "always_cook_configured": always_cooked,
        }
        fingerprint_payload = json.dumps(
            contract, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        fingerprint = hashlib.sha256(fingerprint_payload).hexdigest()

        fields_match = (
            _equal_values(formation_values, FORMATION_VALUES)
            and _equal_values(swarm_formation_values, SWARM_FORMATION_VALUES)
            and _equal_values(swarm_tuning_values, SWARM_TUNING_VALUES)
            and _equal_values(basic_values, BASIC_VALUES)
            and _equal_values(missile_values, MISSILE_VALUES)
            and set_revision == 2
            and grants == _expected_grants()
        )
        checksums_match = actual_checksums == expected_checksums
        metadata_matches = all(value == OWNER for value in metadata.values())
        hard_reference_chain = (
            cdo_reference == _asset_object_path(ABILITY_SET)
            and ABILITY_SET in referencers[FORMATION]
            and ABILITY_SET in referencers[SWARM_FORMATION]
            and ABILITY_SET in referencers[BASIC]
            and ABILITY_SET in referencers[MISSILE]
            and SHIP_BLUEPRINT in referencers[ABILITY_SET]
        )
        core_success = (
            fields_match
            and checksums_match
            and metadata_matches
            and hard_reference_chain
            and always_cooked
            and not dirty_packages
        )

        previous_pid = previous.get("editor_process_id") if previous else None
        previous_fingerprint = previous.get("contract_fingerprint") if previous else None
        restart_verified = bool(
            previous
            and previous.get("success")
            and isinstance(previous_pid, int)
            and previous_pid != os.getpid()
            and previous_fingerprint == fingerprint
        )
        report.update(
            {
                "success": core_success,
                "contract": contract,
                "expected_stable_checksums": {
                    name: _checksum_record(value)
                    for name, value in expected_checksums.items()
                },
                "contract_fingerprint": fingerprint,
                "fields_match_native_v2": fields_match,
                "stable_checksums_match_native_v2": checksums_match,
                "metadata_matches": metadata_matches,
                "hard_reference_chain_valid": hard_reference_chain,
                "package_dirty": bool(dirty_packages),
                "dirty_packages": dirty_packages,
                "restart_verified": restart_verified,
                "restart_evidence": {
                    "previous_editor_process_id": previous_pid,
                    "current_editor_process_id": os.getpid(),
                    "different_editor_process": bool(
                        isinstance(previous_pid, int) and previous_pid != os.getpid()
                    ),
                    "same_contract_fingerprint": previous_fingerprint == fingerprint,
                },
            }
        )
        if not core_success:
            raise AuditError(
                "Ship Ability asset contract failed one or more strict audit gates"
            )
    except Exception as error:
        report["success"] = False
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
    _write_report(report)
    if not report["success"]:
        raise AuditError(report["error"])
    unreal.log("Ship Ability asset audit: " + REPORT_PATH)
    return report


RESULT = main()

"""Create the authored Ship Ability v1 assets and wire the production Ship CDO.

Run with PIE stopped in a clean editor:
    python Scripts/ue_exec.py Scripts/deploy_ship_ability_assets.py

The deployment is deliberately conservative. Existing assets are accepted only
when their complete contract already matches v1; a wrong class/value/reference
or any unsaved package causes a failure before the script overwrites anything.
"""

from __future__ import annotations

import json
import math
import os
import traceback
from typing import Any

import unreal


OWNER = "GuLi.ShipAbilityAssets.v1"
SHIP_BLUEPRINT = "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip"
DERIVED_SHIP_BLUEPRINTS = {"/Game/GuLiStrike/Ship/BP_CombatAvatarFly01"}
ABILITY_SET = "/Game/GuLiStrike/Ship/Abilities/DA_ShipAbilitySet_WingmanV1"
FORMATION = (
    "/Game/GuLiStrike/Ship/Abilities/Formations/"
    "DA_WingmanFormation_DoubleRing"
)
BASIC = (
    "/Game/GuLiStrike/Ship/Abilities/Weapons/"
    "DA_WingmanWeapon_BasicAuto"
)
MISSILE = (
    "/Game/GuLiStrike/Ship/Abilities/Weapons/"
    "DA_WingmanWeapon_MissileSalvo"
)
ALL_ASSETS = (FORMATION, BASIC, MISSILE, ABILITY_SET)

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "ShipAbilityAssets",
    "deployment.json",
)

FORMATION_VALUES = {
    "revision": 1,
    "expected_wingman_count": 25,
    "flight_count": 5,
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

BASIC_VALUES = {
    "revision": 1,
    "kind": unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC,
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
    "kind": unreal.GuLiWingmanWeaponKind.MISSILE,
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


class DeploymentError(RuntimeError):
    pass


def _object_path(value: Any) -> str:
    return str(value.get_path_name()) if value is not None else ""


def _asset_object_path(package_path: str) -> str:
    return package_path + "." + package_path.rsplit("/", 1)[-1]


def _package_name(package: Any) -> str:
    return str(package.get_name()) if package is not None else ""


def _dirty_packages() -> list[Any]:
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    unique = {_package_name(package): package for package in packages}
    return [unique[name] for name in sorted(unique)]


def _require_clean(context: str) -> None:
    dirty = [_package_name(package) for package in _dirty_packages()]
    if dirty:
        raise DeploymentError(f"{context}: editor has dirty packages: {dirty}")


def _tag(name: str) -> Any:
    container = unreal.GameplayTagContainer()
    container.import_text(f'(GameplayTags=("{name}"))')
    tags = list(unreal.GameplayTagLibrary.break_gameplay_tag_container(container))
    if len(tags) != 1 or not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tags[0]):
        raise DeploymentError(f"Native GameplayTag is not registered: {name}")
    return tags[0]


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


def _values_match(actual: Any, expected: Any) -> bool:
    if isinstance(expected, float):
        return math.isclose(float(actual), expected, rel_tol=0.0, abs_tol=1.0e-4)
    return actual == expected


def _read_values(asset: Any, expected: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, expected_value in expected.items():
        value = asset.get_editor_property(name)
        if isinstance(expected_value, float):
            result[name] = float(value)
        elif isinstance(expected_value, bool):
            result[name] = bool(value)
        elif name == "kind":
            result[name] = (
                "Missile"
                if value == unreal.GuLiWingmanWeaponKind.MISSILE
                else "BasicAutomatic"
                if value == unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC
                else str(value)
            )
        else:
            result[name] = int(value)
    return result


def _require_values(asset: Any, expected: dict[str, Any], label: str) -> None:
    mismatches = {}
    for name, expected_value in expected.items():
        actual = asset.get_editor_property(name)
        if not _values_match(actual, expected_value):
            mismatches[name] = {"actual": str(actual), "expected": str(expected_value)}
    if mismatches:
        raise DeploymentError(
            f"Refusing to overwrite existing {label} with wrong values: {mismatches}"
        )


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


def _create_data_asset(path: str, data_class: Any) -> Any:
    directory, name = path.rsplit("/", 1)
    unreal.EditorAssetLibrary.make_directory(directory)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", data_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, directory, data_class, factory
    )
    if asset is None:
        raise DeploymentError(f"Unable to create DataAsset: {path}")
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "GuLi.Deployment.Owner", OWNER)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "GuLi.Deployment.Schema", "1")
    return asset


def _load_existing(path: str, expected_class: Any) -> Any | None:
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    asset = unreal.load_asset(path)
    if asset is None:
        raise DeploymentError(f"Asset Registry path exists but cannot be loaded: {path}")
    expected_class_path = str(expected_class.static_class().get_path_name())
    actual_class_path = str(asset.get_class().get_path_name())
    if actual_class_path != expected_class_path:
        raise DeploymentError(
            f"Path/type conflict at {path}: {actual_class_path} != {expected_class_path}"
        )
    return asset


def _make_grants(formation: Any, basic: Any, missile: Any) -> list[Any]:
    return [
        unreal.GuLiShipAbilityGrant(
            ability_id=_tag("Ship.Ability.Formation.DoubleRing"),
            slot=unreal.GuLiShipAbilitySlot.FORMATION,
            ability_class=unreal.GuLiShipDoubleRingFormationAbility.static_class(),
            ability_level=1,
            input_tag=unreal.GameplayTag(),
            formation_definition=formation,
            weapon_definition=None,
        ),
        unreal.GuLiShipAbilityGrant(
            ability_id=_tag("Ship.Ability.Weapon.Basic.Auto"),
            slot=unreal.GuLiShipAbilitySlot.BASIC_WEAPON,
            ability_class=unreal.GuLiShipBasicAutomaticWeaponAbility.static_class(),
            ability_level=1,
            input_tag=unreal.GameplayTag(),
            formation_definition=None,
            weapon_definition=basic,
        ),
        unreal.GuLiShipAbilityGrant(
            ability_id=_tag("Ship.Ability.Weapon.Missile.Salvo"),
            slot=unreal.GuLiShipAbilitySlot.MISSILE,
            ability_class=unreal.GuLiShipMissileSalvoAbility.static_class(),
            ability_level=1,
            input_tag=_tag("InputTag.Ship.Wingman.Missile"),
            formation_definition=None,
            weapon_definition=missile,
        ),
    ]


def _write_report(report: dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def main() -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema": 1,
        "success": False,
        "owner": OWNER,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "editor_process_id": os.getpid(),
        "assets": {},
    }
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if editor.get_game_world() is not None:
            raise DeploymentError("PIE/SIE is active")
        _require_clean("preflight")

        formation = _load_existing(FORMATION, unreal.GuLiWingmanFormationDefinition)
        basic = _load_existing(BASIC, unreal.GuLiWingmanWeaponDefinition)
        missile = _load_existing(MISSILE, unreal.GuLiWingmanWeaponDefinition)
        ability_set = _load_existing(ABILITY_SET, unreal.GuLiShipAbilitySet)

        # Validate every existing object before the first mutation. An authored
        # object is never silently adopted or normalized over a saved user edit.
        if formation is not None:
            _require_values(formation, FORMATION_VALUES, "formation definition")
        if basic is not None:
            _require_values(basic, BASIC_VALUES, "basic weapon definition")
        if missile is not None:
            _require_values(missile, MISSILE_VALUES, "missile definition")
        if ability_set is not None:
            existing_grants = [
                _grant_record(grant)
                for grant in list(ability_set.get_editor_property("grants"))
            ]
            if int(ability_set.get_editor_property("revision")) != 1:
                raise DeploymentError(
                    "Refusing to overwrite existing ability set with wrong revision"
                )
            if existing_grants != _expected_grants():
                raise DeploymentError(
                    "Refusing to overwrite existing ability set with wrong grants: "
                    + repr(existing_grants)
                )

        created: list[str] = []
        if formation is None:
            formation = _create_data_asset(
                FORMATION, unreal.GuLiWingmanFormationDefinition
            )
            formation.set_editor_properties(FORMATION_VALUES)
            created.append(FORMATION)
        if basic is None:
            basic = _create_data_asset(BASIC, unreal.GuLiWingmanWeaponDefinition)
            basic.set_editor_properties(BASIC_VALUES)
            created.append(BASIC)
        if missile is None:
            missile = _create_data_asset(MISSILE, unreal.GuLiWingmanWeaponDefinition)
            missile.set_editor_properties(MISSILE_VALUES)
            created.append(MISSILE)
        if ability_set is None:
            ability_set = _create_data_asset(ABILITY_SET, unreal.GuLiShipAbilitySet)
            ability_set.set_editor_properties(
                {
                    "revision": 1,
                    "grants": _make_grants(formation, basic, missile),
                }
            )
            created.append(ABILITY_SET)

        # Re-read the complete contract after creation as a transaction guard.
        _require_values(formation, FORMATION_VALUES, "formation definition")
        _require_values(basic, BASIC_VALUES, "basic weapon definition")
        _require_values(missile, MISSILE_VALUES, "missile definition")
        grants = [
            _grant_record(grant)
            for grant in list(ability_set.get_editor_property("grants"))
        ]
        if int(ability_set.get_editor_property("revision")) != 1:
            raise DeploymentError("Created ability set revision is not v1")
        if grants != _expected_grants():
            raise DeploymentError(f"Created ability grants differ from v1: {grants}")

        blueprint = unreal.load_asset(SHIP_BLUEPRINT)
        blueprint_class = unreal.EditorAssetLibrary.load_blueprint_class(SHIP_BLUEPRINT)
        if blueprint is None or blueprint_class is None:
            raise DeploymentError("Production Ship Blueprint/class is missing")
        cdo = unreal.get_default_object(blueprint_class)
        prior_reference = _object_path(cdo.get_editor_property("ship_ability_set"))
        expected_set_object_path = _asset_object_path(ABILITY_SET)
        blueprint_changed = prior_reference != expected_set_object_path
        if prior_reference and prior_reference != expected_set_object_path:
            raise DeploymentError(
                "Refusing to replace a non-v1 ShipAbilitySet on the production CDO: "
                + prior_reference
            )
        if blueprint_changed:
            blueprint.modify()
            cdo.modify()
            cdo.set_editor_property("ship_ability_set", ability_set)
            if (_object_path(cdo.get_editor_property("ship_ability_set"))
                    != expected_set_object_path):
                raise DeploymentError(
                    "Ship Blueprint CDO did not retain ShipAbilitySet"
                )

        allowed_packages = set(ALL_ASSETS) | {SHIP_BLUEPRINT} | DERIVED_SHIP_BLUEPRINTS
        dirty = _dirty_packages()
        dirty_names = {_package_name(package) for package in dirty}
        unexpected = sorted(dirty_names - allowed_packages)
        if unexpected:
            raise DeploymentError(
                f"Deployment dirtied unrelated packages: {unexpected}"
            )
        # Changing a native parent Blueprint CDO propagates its inherited value
        # to loaded derived CDOs, which marks those derived packages dirty even
        # though we did not author an override. Save only the explicit targets,
        # then reload derived packages from disk so no unrelated binary changes.
        derived_dirty = [
            package
            for package in dirty
            if _package_name(package) in DERIVED_SHIP_BLUEPRINTS
        ]
        packages_to_save = [
            package
            for package in dirty
            if _package_name(package) not in DERIVED_SHIP_BLUEPRINTS
        ]
        if (packages_to_save
                and not unreal.EditorLoadingAndSavingUtils.save_packages(
                    packages_to_save, True
                )):
            raise DeploymentError("save_packages returned false")
        if derived_dirty:
            reloaded, reload_error = (
                unreal.EditorLoadingAndSavingUtils.reload_packages(
                    derived_dirty,
                    unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE,
                )
            )
            if not reloaded or str(reload_error):
                raise DeploymentError(
                    "Unable to discard propagated derived-Blueprint dirtiness: "
                    + str(reload_error)
                )
        _require_clean("post-save")

        report["assets"] = {
            FORMATION: {
                "class": _object_path(formation.get_class()),
                "values": _read_values(formation, FORMATION_VALUES),
            },
            BASIC: {
                "class": _object_path(basic.get_class()),
                "values": _read_values(basic, BASIC_VALUES),
            },
            MISSILE: {
                "class": _object_path(missile.get_class()),
                "values": _read_values(missile, MISSILE_VALUES),
            },
            ABILITY_SET: {
                "class": _object_path(ability_set.get_class()),
                "revision": int(ability_set.get_editor_property("revision")),
                "grants": grants,
            },
        }
        report.update(
            {
                "success": True,
                "created_assets": created,
                "idempotent_assets": sorted(set(ALL_ASSETS) - set(created)),
                "blueprint_changed": blueprint_changed,
                "blueprint_cdo_reference": _object_path(
                    cdo.get_editor_property("ship_ability_set")
                ),
                "saved_packages": sorted(
                    _package_name(package) for package in packages_to_save
                ),
                "reloaded_without_save": sorted(
                    _package_name(package) for package in derived_dirty
                ),
                "package_dirty": False,
            }
        )
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        report["dirty_packages_after_failure"] = [
            _package_name(package) for package in _dirty_packages()
        ]
    _write_report(report)
    if not report["success"]:
        raise DeploymentError(report["error"])
    unreal.log("Ship Ability asset deployment: " + REPORT_PATH)
    return report


RESULT = main()

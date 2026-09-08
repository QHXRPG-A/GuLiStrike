"""Create/upgrade the authored Ship Ability v2 assets and wire the production Ship CDO.

Run with PIE stopped in a clean editor:
    python Scripts/ue_exec.py Scripts/deploy_ship_ability_assets.py

The deployment is deliberately conservative. Existing assets are accepted only
when their complete contract matches the known v1 migration source or v2 target; a wrong class/value/reference
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
    "deployment.json",
)

FORMATION_VALUES = {
    "revision": 2,
    "expected_wingman_count": 25,
    "flight_count": 5,
    "model": unreal.GuLiWingmanFormationModel.DOUBLE_RING_LEGACY,
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
    "model": unreal.GuLiWingmanFormationModel.SWARM_ORBIT,
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

# When the v1/legacy class defaults are restored, an already-created Swarm asset
# that had values equal to the former CDO can inherit these legacy distances.
# Accept only that exact drift shape, then serialize the intended Swarm override.
SWARM_FORMATION_DEFAULT_DRIFT_VALUES = {
    **SWARM_FORMATION_VALUES,
    "catch_up_distance_centimeters": 120000.0,
    "recovery_distance_centimeters": 250000.0,
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

LEGACY_FORMATION_VALUES = {
    **FORMATION_VALUES,
    "revision": 1,
}

# Exact short-lived v2 values written by the first SwarmOrbit deployment in this
# branch. They are recognized only as a migration source so the rollback skill
# returns to the frozen v1 geometry; arbitrary authored edits still fail closed.
INTERIM_V2_FORMATION_VALUES = {
    **FORMATION_VALUES,
    "inner_ring_radius_centimeters": 30000.0,
    "outer_ring_radius_centimeters": 45000.0,
    "inner_ring_height_centimeters": 7500.0,
    "outer_ring_height_centimeters": -7500.0,
    "catch_up_distance_centimeters": 60000.0,
    "recovery_distance_centimeters": 90000.0,
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
        if name == "model":
            result[name] = (
                "SwarmOrbit"
                if value == unreal.GuLiWingmanFormationModel.SWARM_ORBIT
                else "DoubleRingLegacy"
                if value == unreal.GuLiWingmanFormationModel.DOUBLE_RING_LEGACY
                else str(value)
            )
        elif isinstance(expected_value, float):
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


def _matches_values(asset: Any, expected: dict[str, Any]) -> bool:
    return all(
        _values_match(asset.get_editor_property(name), expected_value)
        for name, expected_value in expected.items()
    )


def _read_swarm_tuning(asset: Any) -> dict[str, float]:
    tuning = asset.get_editor_property("swarm_orbit")
    return {
        name: float(tuning.get_editor_property(name))
        for name in SWARM_TUNING_VALUES
    }


def _require_swarm_tuning(asset: Any) -> None:
    tuning = asset.get_editor_property("swarm_orbit")
    mismatches = {
        name: {
            "actual": str(tuning.get_editor_property(name)),
            "expected": str(expected),
        }
        for name, expected in SWARM_TUNING_VALUES.items()
        if not _values_match(tuning.get_editor_property(name), expected)
    }
    if mismatches:
        raise DeploymentError(
            "Refusing to overwrite existing SwarmOrbit tuning with wrong values: "
            + repr(mismatches)
        )


def _set_swarm_values(asset: Any) -> None:
    asset.set_editor_properties(SWARM_FORMATION_VALUES)
    tuning = unreal.GuLiWingmanSwarmOrbitTuning(**SWARM_TUNING_VALUES)
    asset.set_editor_property("swarm_orbit", tuning)


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


def _expected_v1_grants() -> list[dict[str, Any]]:
    return _expected_grants()[1:]


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


def _make_grants(
    formation: Any, swarm_formation: Any, basic: Any, missile: Any
) -> list[Any]:
    return [
        unreal.GuLiShipAbilityGrant(
            ability_id=_tag("Ship.Ability.Formation.SwarmOrbit"),
            slot=unreal.GuLiShipAbilitySlot.FORMATION,
            ability_class=unreal.GuLiShipSwarmOrbitFormationAbility.static_class(),
            ability_level=1,
            input_tag=unreal.GameplayTag(),
            formation_definition=swarm_formation,
            weapon_definition=None,
        ),
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
        swarm_formation = _load_existing(
            SWARM_FORMATION, unreal.GuLiWingmanFormationDefinition
        )
        basic = _load_existing(BASIC, unreal.GuLiWingmanWeaponDefinition)
        missile = _load_existing(MISSILE, unreal.GuLiWingmanWeaponDefinition)
        ability_set = _load_existing(ABILITY_SET, unreal.GuLiShipAbilitySet)

        # Validate every existing object before the first mutation. An authored
        # object is never silently adopted or normalized over a saved user edit.
        upgrade_legacy_formation = False
        if formation is not None:
            if (
                _matches_values(formation, LEGACY_FORMATION_VALUES)
                or _matches_values(formation, INTERIM_V2_FORMATION_VALUES)
            ):
                upgrade_legacy_formation = True
            else:
                _require_values(formation, FORMATION_VALUES, "formation definition")
        upgrade_swarm_defaults = False
        if swarm_formation is not None:
            if _matches_values(
                swarm_formation, SWARM_FORMATION_DEFAULT_DRIFT_VALUES
            ):
                upgrade_swarm_defaults = True
            else:
                _require_values(
                    swarm_formation,
                    SWARM_FORMATION_VALUES,
                    "SwarmOrbit formation definition",
                )
            _require_swarm_tuning(swarm_formation)
        if basic is not None:
            _require_values(basic, BASIC_VALUES, "basic weapon definition")
        if missile is not None:
            _require_values(missile, MISSILE_VALUES, "missile definition")
        upgrade_v1_ability_set = False
        if ability_set is not None:
            existing_grants = [
                _grant_record(grant)
                for grant in list(ability_set.get_editor_property("grants"))
            ]
            existing_revision = int(ability_set.get_editor_property("revision"))
            if existing_revision == 1 and existing_grants == _expected_v1_grants():
                upgrade_v1_ability_set = True
            elif existing_revision != 2 or existing_grants != _expected_grants():
                raise DeploymentError(
                    "Refusing to overwrite unknown ability-set revision/grants: "
                    + repr(existing_grants)
                )

        created: list[str] = []
        updated: list[str] = []
        if formation is None:
            formation = _create_data_asset(
                FORMATION, unreal.GuLiWingmanFormationDefinition
            )
            formation.set_editor_properties(FORMATION_VALUES)
            created.append(FORMATION)
        elif upgrade_legacy_formation:
            formation.modify()
            formation.set_editor_properties(FORMATION_VALUES)
            updated.append(FORMATION)
        if swarm_formation is None:
            swarm_formation = _create_data_asset(
                SWARM_FORMATION, unreal.GuLiWingmanFormationDefinition
            )
            _set_swarm_values(swarm_formation)
            created.append(SWARM_FORMATION)
        elif upgrade_swarm_defaults:
            swarm_formation.modify()
            _set_swarm_values(swarm_formation)
            updated.append(SWARM_FORMATION)
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
                    "revision": 2,
                    "grants": _make_grants(
                        formation, swarm_formation, basic, missile
                    ),
                }
            )
            created.append(ABILITY_SET)
        elif upgrade_v1_ability_set:
            ability_set.modify()
            ability_set.set_editor_properties(
                {
                    "revision": 2,
                    "grants": _make_grants(
                        formation, swarm_formation, basic, missile
                    ),
                }
            )
            updated.append(ABILITY_SET)

        # Re-read the complete contract after creation as a transaction guard.
        _require_values(formation, FORMATION_VALUES, "formation definition")
        _require_values(
            swarm_formation,
            SWARM_FORMATION_VALUES,
            "SwarmOrbit formation definition",
        )
        _require_swarm_tuning(swarm_formation)
        _require_values(basic, BASIC_VALUES, "basic weapon definition")
        _require_values(missile, MISSILE_VALUES, "missile definition")
        grants = [
            _grant_record(grant)
            for grant in list(ability_set.get_editor_property("grants"))
        ]
        if int(ability_set.get_editor_property("revision")) != 2:
            raise DeploymentError("Created ability set revision is not v2")
        if grants != _expected_grants():
            raise DeploymentError(f"Created ability grants differ from v2: {grants}")

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
            SWARM_FORMATION: {
                "class": _object_path(swarm_formation.get_class()),
                "values": _read_values(
                    swarm_formation, SWARM_FORMATION_VALUES
                ),
                "swarm_orbit": _read_swarm_tuning(swarm_formation),
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
                "updated_assets": updated,
                "idempotent_assets": sorted(
                    set(ALL_ASSETS) - set(created) - set(updated)
                ),
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

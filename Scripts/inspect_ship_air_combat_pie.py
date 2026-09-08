"""Capture read-only runtime evidence from the active Ship air-combat PIE."""

from __future__ import annotations

import json
import os
import tempfile
import traceback
from typing import Any

import unreal


EXPECTED_MAP_TOKEN = "LVL_ShipWingmanAirCombatPrototype"
EXPECTED_GAME_MODE = "/Script/GuLiStrike.GuLiShipTestGameMode"
EXPECTED_SHIP_CLASS_SUFFIX = "BP_CombatAvatarFly01_C"
EXPECTED_ABILITY_SET = (
    "/Game/GuLiStrike/Ship/Abilities/"
    "DA_ShipAbilitySet_WingmanV3.DA_ShipAbilitySet_WingmanV3"
)
EXPECTED_NAV_DATA = (
    "/Game/GuLiStrike/Navigation/Baked/LVL_ShipWingmanAirCombatPrototype/"
    "DA_FlightNav_LVL_ShipWingmanAirCombatPrototype."
    "DA_FlightNav_LVL_ShipWingmanAirCombatPrototype"
)
EXPECTED_BASE_MAX_SPEED = 5_400.0
EXPECTED_CURRENT_MAX_SPEED = 2_700.0
EXPECTED_OWNER_WINGMEN = 25

REPORT_PATH = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "TestResults",
    "ShipAirCombatLevel",
    "pie_runtime_report.json",
)


def _atomic_write(report: dict[str, Any]) -> None:
    directory = os.path.dirname(REPORT_PATH)
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix="ship_air_pie_", suffix=".json.tmp", dir=directory
    )
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary, REPORT_PATH)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _object_path(value: Any) -> str | None:
    return str(value.get_path_name()) if value is not None else None


def main() -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema": "guli.ship-wingman-air-combat-pie.v1",
        "success": False,
        "checks": {},
        "players": [],
        "presentations": [],
        "flight_navigation": [],
        "combat_effects": {},
        "editor_settings": {},
        "errors": [],
    }
    try:
        performance_settings_class = unreal.load_class(
            None, "/Script/UnrealEd.EditorPerformanceSettings"
        )
        performance_settings = unreal.get_default_object(performance_settings_class)
        report["editor_settings"] = {
            "background_cpu_throttle": bool(
                performance_settings.get_editor_property(
                    "bThrottleCPUWhenNotForeground"
                )
            )
        }

        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        world = editor.get_game_world()
        if world is None:
            raise RuntimeError("PIE world is not ready")
        report["world"] = str(world.get_path_name())

        game_mode = unreal.GameplayStatics.get_game_mode(world)
        report["game_mode"] = _object_path(game_mode.get_class()) if game_mode else None

        effects = next(
            (
                subsystem
                for subsystem in unreal.ObjectIterator(
                    unreal.GuLiCombatEffectRuntimeSubsystem
                )
                if subsystem.get_outer() == world
            ),
            None,
        )
        if effects:
            counters = effects.get_counters()
            report["combat_effects"] = {
                "active_effect_count": int(effects.get_active_effect_count()),
                "projectiles_launched": int(counters.projectiles_launched),
                "fields_created": int(counters.fields_created),
                "pulses": int(counters.pulses),
                "damage_commits": int(counters.damage_commits),
                "shots_published": int(counters.shots_published),
                "candidate_checks": int(counters.candidate_checks),
            }

        actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
        ships = [
            actor
            for actor in actors
            if actor.get_class().get_path_name().endswith(EXPECTED_SHIP_CLASS_SUFFIX)
        ]
        for ship in ships:
            controller = ship.get_controller()
            player_state = controller.get_editor_property("player_state") if controller else None
            movement = ship.get_ship_movement()
            ability_set = ship.get_editor_property("ship_ability_set")
            target = ship.get_wingman_attack_target()
            target_handle = target.target
            target_location = target.location
            ship_location = ship.get_actor_location()
            report["players"].append(
                {
                    "ship": str(ship.get_name()),
                    "class": str(ship.get_class().get_path_name()),
                    "controller": str(controller.get_name()) if controller else None,
                    "tuning_preset": str(ship.get_editor_property("tuning_preset")),
                    "base_max_speed": float(ship.get_editor_property("base_max_speed")),
                    "current_max_speed": float(ship.get_current_max_speed()),
                    "movement_max_fly_speed": float(
                        movement.get_editor_property("max_fly_speed")
                    )
                    if movement
                    else None,
                    "ship_ready": bool(ship.is_ship_ready()),
                    "ability_set": _object_path(ability_set),
                    "team": str(player_state.get_team()) if player_state else None,
                    "role": str(player_state.get_battle_role()) if player_state else None,
                    "battle_ready": bool(player_state.is_battle_ready())
                    if player_state
                    else False,
                    "location": [
                        float(ship_location.x),
                        float(ship_location.y),
                        float(ship_location.z),
                    ],
                    "wingman_target": str(target),
                    "wingman_target_details": {
                        "kind": str(target_handle.kind),
                        "location": [
                            float(target_location.x),
                            float(target_location.y),
                            float(target_location.z),
                        ],
                        "radius": float(target.radius),
                        "ground": bool(target.ground),
                        "specified": bool(target.specified),
                        "authority_enemy_gate_required": True,
                        "opposing_team_from_red_ship": "Blue",
                    },
                }
            )

        for actor in actors:
            class_path = str(actor.get_class().get_path_name())
            if class_path.endswith("GuLiWingmanPresentationActor"):
                owner = actor.get_owner_instances()
                remote = actor.get_remote_instances()
                report["presentations"].append(
                    {
                        "actor": str(actor.get_name()),
                        "owner_instances": int(owner.get_instance_count()) if owner else 0,
                        "remote_instances": int(remote.get_instance_count()) if remote else 0,
                        "owner_mesh": _object_path(
                            owner.get_editor_property("static_mesh")
                        )
                        if owner
                        else None,
                    }
                )
            elif class_path.endswith("GuLiFlightNavigationVolume"):
                data = actor.get_editor_property("navigation_data")
                report["flight_navigation"].append(
                    {
                        "actor": str(actor.get_name()),
                        "enabled": bool(actor.get_editor_property("navigation_enabled")),
                        "data": _object_path(data),
                    }
                )

        checks = report["checks"]
        checks["target_map_loaded"] = EXPECTED_MAP_TOKEN in report["world"]
        checks["ship_test_game_mode"] = report["game_mode"] == EXPECTED_GAME_MODE
        checks["player_ship_spawned"] = bool(report["players"])
        checks["all_players_air_role"] = bool(report["players"]) and all(
            "AIR" in player["role"].upper() for player in report["players"]
        )
        checks["all_ships_ready"] = bool(report["players"]) and all(
            player["ship_ready"] for player in report["players"]
        )
        checks["base_speed_5400"] = bool(report["players"]) and all(
            abs(player["base_max_speed"] - EXPECTED_BASE_MAX_SPEED) <= 0.1
            for player in report["players"]
        )
        checks["effective_speed_2700"] = bool(report["players"]) and all(
            abs(player["current_max_speed"] - EXPECTED_CURRENT_MAX_SPEED) <= 0.1
            and abs(player["movement_max_fly_speed"] - EXPECTED_CURRENT_MAX_SPEED)
            <= 0.1
            for player in report["players"]
        )
        checks["wingman_v3_ability_set"] = bool(report["players"]) and all(
            player["ability_set"] == EXPECTED_ABILITY_SET
            for player in report["players"]
        )
        checks["owner_wingmen_25"] = any(
            row["owner_instances"] == EXPECTED_OWNER_WINGMEN
            for row in report["presentations"]
        )
        checks["map_specific_flight_nav"] = any(
            row["enabled"] and row["data"] == EXPECTED_NAV_DATA
            for row in report["flight_navigation"]
        )
        checks["background_cpu_throttle_disabled"] = not report[
            "editor_settings"
        ]["background_cpu_throttle"]
        checks["blue_ground_target_acquired"] = bool(report["players"]) and all(
            "COMMANDER_SOLDIER" in player["wingman_target_details"]["kind"].upper()
            and player["wingman_target_details"]["ground"]
            and player["wingman_target_details"]["opposing_team_from_red_ship"]
            == "Blue"
            for player in report["players"]
        )
        checks["wingman_projectiles_launched"] = (
            report["combat_effects"].get("projectiles_launched", 0) > 0
        )
        checks["wingman_damage_committed"] = (
            report["combat_effects"].get("damage_commits", 0) > 0
        )
        report["success"] = all(checks.values())
    except Exception as error:
        report["errors"].append(str(error))
        report["traceback"] = traceback.format_exc()
    _atomic_write(report)
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
    return report


RESULT = main()

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
EXPECTED_BASE_MAX_SPEED = 16_200.0
EXPECTED_BASE_ACCELERATION = 2_400.0
EXPECTED_EFFECTIVE_ACCELERATION = 1_200.0
EXPECTED_CURRENT_MAX_SPEED = 8_100.0
EXPECTED_YAW_RATE = 13.333333
EXPECTED_OWNER_WINGMEN = 25

REPORT_PATH = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "TestResults",
    "ShipAirCombatLevel",
    "pie_runtime_report.json",
)
NETWORK_SAMPLE_PATH = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "TestResults",
    "WingmanAttack",
    "pie-sample.json",
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
        "schema": "guli.ship-wingman-air-combat-pie.v2",
        "success": False,
        "checks": {},
        "players": [],
        "presentations": [],
        "flight_navigation": [],
        "combat_effects": {},
        "network_sample": [],
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
                "gun_bursts_started": int(counters.gun_bursts_started),
                "logical_gun_shots": int(counters.logical_gun_shots),
                "candidate_checks": int(counters.candidate_checks),
            }

        presentation = next(
            (
                subsystem
                for subsystem in unreal.ObjectIterator(
                    unreal.GuLiCombatEffectPresentationSubsystem
                )
                if subsystem.get_outer() == world
            ),
            None,
        )
        if presentation:
            counters = presentation.get_counters()
            report["combat_presentation"] = {
                "received_states": int(counters.received_states),
                "received_network_shots": int(counters.received_shots),
                "synthesized_gun_shots": int(counters.synthesized_gun_shots),
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
                    "base_acceleration": float(
                        ship.get_editor_property("base_acceleration")
                    ),
                    "yaw_rate": float(ship.get_editor_property("yaw_rate")),
                    "current_max_speed": float(ship.get_current_max_speed()),
                    "movement_max_fly_speed": float(
                        movement.get_editor_property("max_fly_speed")
                    )
                    if movement
                    else None,
                    "movement_max_acceleration": float(
                        movement.get_editor_property("max_acceleration")
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
        if os.path.isfile(NETWORK_SAMPLE_PATH):
            with open(NETWORK_SAMPLE_PATH, "r", encoding="utf-8-sig") as stream:
                report["network_sample"] = json.load(stream)
        network_worlds = report["network_sample"]
        dedicated_worlds = [row for row in network_worlds if row.get("net_mode") == 1]
        client_worlds = [row for row in network_worlds if row.get("net_mode") == 3]
        server_sample = dedicated_worlds[0] if len(dedicated_worlds) == 1 else {}
        server_relays = server_sample.get("relays", [])
        server_air_checkpoints = [
            checkpoint
            for relay in server_relays
            for checkpoint in relay.get("checkpoints", [])
            if checkpoint.get("slot") == "AirWeapon"
        ]
        checks["target_map_loaded"] = EXPECTED_MAP_TOKEN in report["world"]
        checks["ship_test_or_qa_game_mode"] = report["game_mode"] in {
            EXPECTED_GAME_MODE,
            "/Script/GuLiStrike.GuLiWingmanQAGameMode",
        }
        checks["player_ship_spawned"] = bool(report["players"])
        checks["all_players_air_role"] = bool(report["players"]) and all(
            "AIR" in player["role"].upper() for player in report["players"]
        )
        checks["base_speed_16200"] = bool(report["players"]) and all(
            abs(player["base_max_speed"] - EXPECTED_BASE_MAX_SPEED) <= 0.1
            for player in report["players"]
        )
        checks["effective_speed_8100"] = bool(report["players"]) and all(
            abs(player["current_max_speed"] - EXPECTED_CURRENT_MAX_SPEED) <= 0.1
            and abs(player["movement_max_fly_speed"] - EXPECTED_CURRENT_MAX_SPEED)
            <= 0.1
            for player in report["players"]
        )
        checks["base_acceleration_2400"] = bool(report["players"]) and all(
            abs(player["base_acceleration"] - EXPECTED_BASE_ACCELERATION) <= 0.1
            for player in report["players"]
        )
        checks["effective_acceleration_1200"] = bool(report["players"]) and all(
            abs(
                player["movement_max_acceleration"]
                - EXPECTED_EFFECTIVE_ACCELERATION
            )
            <= 0.1
            for player in report["players"]
        )
        checks["yaw_rate_13_333333"] = bool(report["players"]) and all(
            abs(player["yaw_rate"] - EXPECTED_YAW_RATE) <= 0.001
            for player in report["players"]
        )
        checks["wingman_v3_ability_set"] = bool(report["players"]) and all(
            player["ability_set"] == EXPECTED_ABILITY_SET
            for player in report["players"]
        )
        checks["dedicated_server_plus_two_clients"] = (
            len(dedicated_worlds) == 1 and len(client_worlds) == 2
        )
        checks["owner_wingmen_25"] = any(
            len(row.get("planes", [])) == EXPECTED_OWNER_WINGMEN
            for row in client_worlds
        )
        checks["map_specific_flight_nav"] = any(
            row["enabled"] and row["data"] == EXPECTED_NAV_DATA
            for row in report["flight_navigation"]
        )
        checks["background_cpu_throttle_disabled"] = not report[
            "editor_settings"
        ]["background_cpu_throttle"]
        checks["air_target_acquired"] = any(
            not relay.get("ground", True) and relay.get("target", 0) != 0
            for relay in server_relays
        )
        checks["wingman_sustained_burst_started"] = (
            server_sample.get("gun_bursts", 0) > 0
        )
        checks["no_per_shot_server_packets"] = (
            server_sample.get("network_shot_cues", -1) == 0
        )
        checks["wingman_damage_committed"] = (
            server_sample.get("damage", 0) > 0
        )
        checks["logical_shots_advance_inside_bursts"] = (
            server_sample.get("logical_gun_shots", 0)
            > server_sample.get("gun_bursts", 0)
        )
        checks["only_burst_start_records"] = bool(server_air_checkpoints) and all(
            checkpoint.get("shot") == 0 for checkpoint in server_air_checkpoints
        )
        checks["both_clients_received_effect_states"] = len(client_worlds) == 2 and all(
            row.get("received_effect_states", 0) > 0 for row in client_worlds
        )
        checks["both_clients_synthesized_gunfire"] = len(client_worlds) == 2 and all(
            row.get("synthesized_gun_shots", 0) > 0 for row in client_worlds
        )
        checks["no_per_shot_client_packets"] = len(client_worlds) == 2 and all(
            row.get("received_network_shots", -1) == 0 for row in client_worlds
        )
        checks["air_cycle_phases_observed"] = any(
            any(plane.get("phase") in {6, 7, 8, 9} for plane in row.get("planes", []))
            for row in client_worlds
        )
        report["success"] = all(checks.values())
    except Exception as error:
        report["errors"].append(str(error))
        report["traceback"] = traceback.format_exc()
    _atomic_write(report)
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
    return report


RESULT = main()

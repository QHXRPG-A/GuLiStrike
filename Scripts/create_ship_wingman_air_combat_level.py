"""Create or update the Ship wingman air-combat prototype map.

Run in a stopped source-build editor through::

    python Scripts/commander_editor_python.py \
        --file Scripts/create_ship_wingman_air_combat_level.py

The first run clones the saved Commander prototype through LevelEditorSubsystem.
Later runs update the owned actors in place.
"""

from __future__ import annotations

import json
import math
import os
import tempfile
import traceback
from typing import Any

import unreal


SOURCE_MAP = "/Game/Maps/LVL_CommanderMassPrototype"
TARGET_MAP = "/Game/Maps/LVL_ShipWingmanAirCombatPrototype"
GAME_MODE_CLASS = "/Script/GuLiStrike.GuLiShipTestGameMode"
REPORT_PATH = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "TestResults",
    "ShipAirCombatLevel",
    "authoring_report.json",
)

START_TAG = "GuLi.ShipWingmanAirCombat.PlayerStart.v1"
CENTER_TAG = "GuLi.ShipWingmanAirCombat.Center.v1"
FLIGHT_NAV_MARKER = "GuLiFlightNavigation.Managed.v1"
FLIGHT_NAV_MAP_TAG = "GuLiFlightNavigation.Map.LVL_ShipWingmanAirCombatPrototype"

# The inherited Commander armies are centered near (-100 m, 1275 m) and
# (1100 m, 375 m).  Keep all Ship starts inside the same verified south-east
# attack corridor: this preserves multiplayer separation without letting random
# PlayerStart selection choose terrain geometry that blocks every ground run.
RED_ARMY_CENTER = (-10_000.0, 127_500.0, 0.0)
BLUE_ARMY_CENTER = (110_000.0, 37_500.0, 0.0)
COMBAT_CENTER = (50_000.0, 82_500.0, 15_000.0)
PLAYER_STARTS = (
    (10_000.0, 102_500.0, 15_000.0),
    (30_000.0, 102_500.0, 15_000.0),
    (10_000.0, 122_500.0, 15_000.0),
    (30_000.0, 122_500.0, 15_000.0),
)


class AuthoringError(RuntimeError):
    pass


def _package_name(value: Any) -> str:
    return str(value.get_outermost().get_name()) if value is not None else ""


def _actor_tags(actor: Any) -> set[str]:
    return {str(tag) for tag in actor.get_editor_property("tags")}


def _write_tags(actor: Any, tags: set[str]) -> None:
    actor.set_editor_property(
        "tags", [unreal.Name(value) for value in sorted(tags)]
    )


def _set_managed_tag(actor: Any, tag: str) -> None:
    tags = _actor_tags(actor)
    tags.add(tag)
    _write_tags(actor, tags)


def _atomic_write(report: dict[str, Any]) -> None:
    directory = os.path.dirname(REPORT_PATH)
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix="ship_air_combat_", suffix=".json.tmp", dir=directory
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


def _rotation_toward(location: tuple[float, float, float]) -> unreal.Rotator:
    delta_x = COMBAT_CENTER[0] - location[0]
    delta_y = COMBAT_CENTER[1] - location[1]
    delta_z = COMBAT_CENTER[2] - location[2]
    horizontal = math.hypot(delta_x, delta_y)
    yaw = math.degrees(math.atan2(delta_y, delta_x))
    pitch = math.degrees(math.atan2(delta_z, max(horizontal, 1.0)))
    # Python's positional Rotator constructor is (roll, pitch, yaw).
    return unreal.Rotator(0.0, pitch, yaw)


def _vector(value: Any) -> list[float]:
    return [float(value.x), float(value.y), float(value.z)]


def _ensure_target_world(editor: Any) -> tuple[Any, bool, str]:
    world = editor.get_editor_world()
    current_map = _package_name(world)
    target_exists = unreal.EditorAssetLibrary.does_asset_exist(TARGET_MAP)

    if target_exists:
        if current_map != TARGET_MAP:
            world = None
            world = unreal.EditorLoadingAndSavingUtils.load_map(TARGET_MAP)
            if world is None:
                raise AuthoringError(f"Unable to load existing target map: {TARGET_MAP}")
        return world, False, current_map

    if current_map != SOURCE_MAP:
        raise AuthoringError(
            "First run requires the Commander prototype to be the current editor map; "
            f"loaded={current_map!r}"
        )

    # The subsystem owns the map switch and package creation lifecycle. It
    # deliberately clones the last saved source package and never saves a dirty
    # source map as a side effect.
    world = None
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.new_level_from_template(TARGET_MAP, SOURCE_MAP):
        raise AuthoringError(f"Template clone failed for target map: {TARGET_MAP}")
    world = editor.get_editor_world()
    if world is None or _package_name(world) != TARGET_MAP:
        raise AuthoringError("Target map was saved but could not be loaded exactly")
    return world, True, current_map


def _configure_player_starts(actor_subsystem: Any) -> list[dict[str, Any]]:
    existing = sorted(
        (
            actor
            for actor in actor_subsystem.get_all_level_actors()
            if isinstance(actor, unreal.PlayerStart)
        ),
        key=lambda actor: (START_TAG not in _actor_tags(actor), actor.get_actor_label()),
    )

    while len(existing) < len(PLAYER_STARTS):
        actor = actor_subsystem.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(), unreal.Rotator()
        )
        if actor is None:
            raise AuthoringError("Unable to spawn an air-combat PlayerStart")
        existing.append(actor)

    # The derived map owns its spawn contract. Extra inherited starts would let
    # GameMode choose a position outside the guaranteed 1500 m combat envelope.
    for extra in existing[len(PLAYER_STARTS) :]:
        if not actor_subsystem.destroy_actor(extra):
            raise AuthoringError(f"Unable to remove extra PlayerStart: {extra.get_name()}")
    existing = existing[: len(PLAYER_STARTS)]

    records: list[dict[str, Any]] = []
    for index, (actor, location) in enumerate(zip(existing, PLAYER_STARTS), start=1):
        actor.modify()
        actor.set_actor_label(f"AirCombat_PlayerStart_{index:02d}", False)
        actor.set_folder_path(unreal.Name("Gameplay/AirCombat/PlayerStarts"))
        actor.set_actor_location(unreal.Vector(*location), False, False)
        actor.set_actor_rotation(_rotation_toward(location), False)
        _set_managed_tag(actor, START_TAG)
        actual_location = actor.get_actor_location()
        actual_components = (
            float(actual_location.x),
            float(actual_location.y),
            float(actual_location.z),
        )
        if max(
            abs(actual - expected)
            for actual, expected in zip(actual_components, location)
        ) > 0.1:
            raise AuthoringError(
                f"PlayerStart {index} failed to retain its authored location"
            )
        rotation = actor.get_actor_rotation()
        records.append(
            {
                "label": str(actor.get_actor_label()),
                "path": str(actor.get_path_name()),
                "location": _vector(actual_location),
                "rotation": [
                    float(rotation.pitch),
                    float(rotation.yaw),
                    float(rotation.roll),
                ],
            }
        )
    return records


def _configure_center(actor_subsystem: Any) -> dict[str, Any]:
    managed = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if CENTER_TAG in _actor_tags(actor)
    ]
    if len(managed) > 1:
        raise AuthoringError("Multiple managed air-combat center actors exist")
    if managed:
        center = managed[0]
        if not isinstance(center, unreal.TargetPoint):
            raise AuthoringError("Managed air-combat center is not a TargetPoint")
    else:
        center = actor_subsystem.spawn_actor_from_class(
            unreal.TargetPoint, unreal.Vector(*COMBAT_CENTER), unreal.Rotator()
        )
        if center is None:
            raise AuthoringError("Unable to spawn the air-combat center marker")
    center.modify()
    center.set_actor_label("AirCombat_Center", False)
    center.set_folder_path(unreal.Name("Gameplay/AirCombat"))
    center.set_actor_location(unreal.Vector(*COMBAT_CENTER), False, False)
    _set_managed_tag(center, CENTER_TAG)
    return {
        "label": str(center.get_actor_label()),
        "path": str(center.get_path_name()),
        "location": _vector(center.get_actor_location()),
    }


def _retarget_flight_navigation(actor_subsystem: Any) -> dict[str, Any]:
    volume_class = unreal.load_class(
        None,
        "/Script/GuLiFlightNavigationRuntime.GuLiFlightNavigationVolume",
    )
    if volume_class is None:
        raise AuthoringError("GuLiFlightNavigationVolume class is unavailable")
    volumes = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_class().get_path_name() == volume_class.get_path_name()
    ]
    if len(volumes) != 1:
        raise AuthoringError(
            f"Expected exactly one inherited FlightNav volume, found {len(volumes)}"
        )
    volume = volumes[0]
    volume.modify()
    volume.set_actor_label("FlightNav_LVL_ShipWingmanAirCombatPrototype", False)
    volume.set_folder_path(unreal.Name("GuLiStrike/Navigation"))
    tags = {
        value
        for value in _actor_tags(volume)
        if not value.startswith("GuLiFlightNavigation.Map.")
    }
    tags.update((FLIGHT_NAV_MARKER, FLIGHT_NAV_MAP_TAG))
    _write_tags(volume, tags)
    data = volume.get_editor_property("navigation_data")
    return {
        "label": str(volume.get_actor_label()),
        "path": str(volume.get_path_name()),
        "inherited_navigation_data": str(data.get_path_name()) if data else None,
        "tags": sorted(tags),
    }


def main() -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema": "guli.ship-wingman-air-combat-level.v1",
        "success": False,
        "source_map": SOURCE_MAP,
        "target_map": TARGET_MAP,
        "game_mode": GAME_MODE_CLASS,
    }
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        if editor.get_game_world() is not None:
            raise AuthoringError("PIE/SIE is active; stop play before authoring the map")

        world, created, original_map = _ensure_target_world(editor)
        report.update(created=created, original_editor_map=original_map)

        world_settings = world.get_world_settings()
        game_mode_class = unreal.load_class(None, GAME_MODE_CLASS)
        if game_mode_class is None:
            raise AuthoringError(f"GameMode class is unavailable: {GAME_MODE_CLASS}")
        world_settings.modify()
        world_settings.set_editor_property("default_game_mode", game_mode_class)

        report["player_starts"] = _configure_player_starts(actor_subsystem)
        report["combat_center"] = _configure_center(actor_subsystem)
        report["flight_navigation"] = _retarget_flight_navigation(actor_subsystem)

        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
            raise AuthoringError("Saving the configured target level failed")
        if _package_name(editor.get_editor_world()) != TARGET_MAP:
            raise AuthoringError("Editor switched away from the target level while saving")

        dirty_maps = [
            str(package.get_name())
            for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        ]
        if TARGET_MAP in dirty_maps:
            raise AuthoringError("Target map remains dirty after save")

        report.update(
            success=True,
            loaded_world=str(editor.get_editor_world().get_path_name()),
            saved=True,
            dirty_maps=dirty_maps,
            maximum_start_separation_centimeters=max(
                math.dist(first, second)
                for first in PLAYER_STARTS
                for second in PLAYER_STARTS
            ),
            maximum_enemy_army_distance_centimeters=max(
                math.dist(start, army_center)
                for start in PLAYER_STARTS
                for army_center in (RED_ARMY_CENTER, BLUE_ARMY_CENTER)
            ),
        )
    except Exception as error:
        report.update(
            success=False,
            error=str(error),
            traceback=traceback.format_exc(),
            dirty_maps=[
                str(package.get_name())
                for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
            ],
        )
    _atomic_write(report)
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
    return report


RESULT = main()

"""Idempotently add the second ship spawn required by the two-client smoke map.

The selected transform is verified against the saved FlightNav graph for the
carrier and all 25 default double-ring endpoints before the map is modified.
Run in a clean, non-PIE editor through Scripts/ue_exec.py.
"""

from __future__ import annotations

import json
import math
import os
import tempfile
import traceback

import unreal


MAP = "/Game/Maps/LVL_ShipTest"
NAV_DATA = (
    "/Game/GuLiStrike/Navigation/Baked/LVL_ShipTest/"
    "DA_FlightNav_LVL_ShipTest"
)
MANAGED_TAG = "GuLi.ShipTest.MultiplayerSpawn.v1"
ACTOR_LABEL = "PS_ShipSpawn_Remote"
LOCATION = (20_000.0, 20_000.0, 0.0)
AGENT_RADIUS = 1_500.0
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "AssetDeployment",
    "shiptest_multiplayer_spawns.json",
)


class DeploymentError(RuntimeError):
    pass


def _atomic_write(value):
    directory = os.path.dirname(REPORT_PATH)
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix="shiptest_spawns_", suffix=".json.tmp", dir=directory
    )
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary, REPORT_PATH)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _package_name(package):
    return str(package.get_name())


def _dirty_packages():
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return {_package_name(package): package for package in packages}


def _actor_tags(actor):
    return {str(tag) for tag in actor.get_editor_property("tags")}


def _set_tag(actor, tag):
    tags = _actor_tags(actor)
    tags.add(tag)
    actor.set_editor_property("tags", [unreal.Name(value) for value in sorted(tags)])


def _formation_endpoints(center):
    yield center
    for slots, radius, height in ((13, 60_000.0, 15_000.0), (12, 90_000.0, -15_000.0)):
        for slot in range(slots):
            phase = slot * math.tau / slots
            yield (
                center[0] + radius * math.cos(phase),
                center[1] + radius * math.sin(phase),
                center[2] + height,
            )


def _validate_formation(nav_data, center):
    results = []
    for index, endpoint in enumerate(_formation_endpoints(center)):
        raw = unreal.GuLiFlightNavigationEditorLibrary.validate_navigation_data_endpoints(
            nav_data,
            unreal.Vector(*center),
            unreal.Vector(*endpoint),
            AGENT_RADIUS,
        )
        values = list(raw) if isinstance(raw, tuple) else []
        results.append(
            {
                "index": index,
                "endpoint": endpoint,
                "valid": raw is not None,
                "start_cell": int(values[0]) if len(values) > 0 else -1,
                "end_cell": int(values[1]) if len(values) > 1 else -1,
                "status": int(values[2]) if len(values) > 2 else -1,
                "error": str(values[3]) if len(values) > 3 else "",
            }
        )
    if not all(result["valid"] for result in results):
        failed = [result["index"] for result in results if not result["valid"]]
        raise DeploymentError(
            f"Managed spawn formation is not usable in one FlightNav component: {failed}"
        )
    return results


def main():
    report = {
        "schema": "guli.shiptest-multiplayer-spawns.v1",
        "success": False,
        "map": MAP,
        "managed_tag": MANAGED_TAG,
        "actor_label": ACTOR_LABEL,
        "location": LOCATION,
    }
    try:
        if _dirty_packages():
            raise DeploymentError("Editor has dirty packages before spawn deployment")
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        if editor.get_game_world() is not None:
            raise DeploymentError("PIE/SIE is active")
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        if world is None or _package_name(world.get_outermost()) != MAP:
            raise DeploymentError("Unable to load the exact ShipTest map")
        if _dirty_packages():
            raise DeploymentError("Loading ShipTest unexpectedly dirtied packages")

        nav_data = unreal.EditorAssetLibrary.load_asset(NAV_DATA)
        if nav_data is None:
            raise DeploymentError("ShipTest FlightNav data is missing")
        probes = _validate_formation(nav_data, LOCATION)

        level_actors = list(actors.get_all_level_actors())
        managed = [actor for actor in level_actors if MANAGED_TAG in _actor_tags(actor)]
        if len(managed) > 1:
            raise DeploymentError("Multiple managed remote PlayerStarts exist")
        collisions = [
            actor for actor in level_actors
            if str(actor.get_actor_label()) == ACTOR_LABEL and actor not in managed
        ]
        if collisions:
            raise DeploymentError("The managed PlayerStart label belongs to an unowned actor")

        created = False
        if managed:
            player_start = managed[0]
            if not isinstance(player_start, unreal.PlayerStart):
                raise DeploymentError("Managed actor is not a PlayerStart")
        else:
            player_start = actors.spawn_actor_from_class(
                unreal.PlayerStart, unreal.Vector(*LOCATION), unreal.Rotator()
            )
            if player_start is None:
                raise DeploymentError("Unable to spawn the managed PlayerStart")
            created = True

        player_start.modify()
        player_start.set_actor_label(ACTOR_LABEL, False)
        player_start.set_actor_location(unreal.Vector(*LOCATION), False, False)
        _set_tag(player_start, MANAGED_TAG)
        actual_location = player_start.get_actor_location()
        if max(
            abs(float(actual_location.x) - LOCATION[0]),
            abs(float(actual_location.y) - LOCATION[1]),
            abs(float(actual_location.z) - LOCATION[2]),
        ) > 0.1:
            raise DeploymentError("Managed PlayerStart did not retain its transform")

        allowed_packages = {
            _package_name(world.get_outermost()),
            _package_name(player_start.get_outermost()),
        }
        dirty = _dirty_packages()
        unexpected = sorted(set(dirty) - allowed_packages)
        if unexpected:
            raise DeploymentError(f"Spawn deployment dirtied unrelated packages: {unexpected}")
        if dirty and not unreal.EditorLoadingAndSavingUtils.save_packages(
            list(dirty.values()), True
        ):
            raise DeploymentError("save_packages returned false")
        if _dirty_packages():
            raise DeploymentError("Packages remain dirty after spawn deployment")

        report.update(
            success=True,
            created=created,
            actor_path=str(player_start.get_path_name()),
            formation_probe_count=len(probes),
            all_formation_endpoints_valid=True,
            saved_packages=sorted(dirty),
        )
    except Exception as error:
        report.update(
            success=False,
            error=str(error),
            traceback=traceback.format_exc(),
            dirty_packages=sorted(_dirty_packages()),
        )
    _atomic_write(report)
    if not report["success"]:
        raise DeploymentError(report.get("error", "Spawn deployment failed"))
    unreal.log("ShipTest multiplayer spawn deployment: " + REPORT_PATH)
    return report


RESULT = main()

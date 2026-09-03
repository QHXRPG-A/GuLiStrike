"""Move both LVL_ShipTest PlayerStarts onto FlightNav-safe formation anchors.

The relay bootstrap can legitimately arrive after the ship has settled roughly
1600 cm along local -Y.  Each authored start and that observed settled position
must therefore support the carrier plus all 25 default double-ring endpoints.
The script is idempotent and saves only LVL_ShipTest.
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
AGENT_RADIUS = 1_500.0
SETTLE_OFFSET = (0.0, -1_600.0, 0.0)
SPAWNS = (
    {
        "label": "PS_ShipSpawn",
        "location": (-20_000.0, 20_000.0, 0.0),
        "managed_tag": "GuLi.ShipTest.PrimarySpawn.v1",
    },
    {
        "label": "PS_ShipSpawn_Remote",
        "location": (20_000.0, 20_000.0, 0.0),
        "managed_tag": "GuLi.ShipTest.MultiplayerSpawn.v1",
    },
)
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "AssetDeployment",
    "shiptest_safe_player_starts.json",
)


class DeploymentError(RuntimeError):
    pass


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


def _offset(point, delta):
    return tuple(float(value + change) for value, change in zip(point, delta))


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
    probes = []
    for index, endpoint in enumerate(_formation_endpoints(center)):
        raw = unreal.GuLiFlightNavigationEditorLibrary.validate_navigation_data_endpoints(
            nav_data,
            unreal.Vector(*center),
            unreal.Vector(*endpoint),
            AGENT_RADIUS,
        )
        values = list(raw) if isinstance(raw, tuple) else []
        probes.append(
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
    failed = [probe["index"] for probe in probes if not probe["valid"]]
    if failed:
        raise DeploymentError(
            f"Formation at {center} is not usable in one FlightNav component: {failed}"
        )
    return probes


def _atomic_write(value):
    directory = os.path.dirname(REPORT_PATH)
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix="shiptest_safe_starts_", suffix=".json.tmp", dir=directory
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


def main():
    report = {
        "schema": "guli.shiptest-safe-player-starts.v1",
        "success": False,
        "map": MAP,
        "agent_radius": AGENT_RADIUS,
        "settle_offset": SETTLE_OFFSET,
        "spawns": [],
    }
    try:
        if _dirty_packages():
            raise DeploymentError("Editor has dirty packages before spawn deployment")
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
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
        actors = list(actor_subsystem.get_all_level_actors())

        for spec in SPAWNS:
            matches = [
                actor for actor in actors
                if str(actor.get_actor_label()) == spec["label"]
            ]
            if len(matches) != 1 or not isinstance(matches[0], unreal.PlayerStart):
                raise DeploymentError(
                    f"Expected exactly one PlayerStart labelled {spec['label']}"
                )
            authored_probes = _validate_formation(nav_data, spec["location"])
            settled_location = _offset(spec["location"], SETTLE_OFFSET)
            settled_probes = _validate_formation(nav_data, settled_location)
            actor = matches[0]
            before = actor.get_actor_location()
            actor.modify()
            actor.set_actor_location(unreal.Vector(*spec["location"]), False, False)
            _set_tag(actor, spec["managed_tag"])
            after = actor.get_actor_location()
            if max(
                abs(float(after.x) - spec["location"][0]),
                abs(float(after.y) - spec["location"][1]),
                abs(float(after.z) - spec["location"][2]),
            ) > 0.1:
                raise DeploymentError(f"{spec['label']} did not retain its transform")
            report["spawns"].append(
                {
                    "label": spec["label"],
                    "actor_path": str(actor.get_path_name()),
                    "managed_tag": spec["managed_tag"],
                    "before": [float(before.x), float(before.y), float(before.z)],
                    "after": [float(after.x), float(after.y), float(after.z)],
                    "settled_location": settled_location,
                    "authored_probe_count": len(authored_probes),
                    "settled_probe_count": len(settled_probes),
                    "all_formation_endpoints_valid": True,
                }
            )

        map_package = _package_name(world.get_outermost())
        dirty = _dirty_packages()
        unexpected = sorted(set(dirty) - {map_package})
        if unexpected:
            raise DeploymentError(f"Spawn deployment dirtied unrelated packages: {unexpected}")
        if dirty and not unreal.EditorLoadingAndSavingUtils.save_packages(
            list(dirty.values()), True
        ):
            raise DeploymentError("save_packages returned false")
        if _dirty_packages():
            raise DeploymentError("Packages remain dirty after spawn deployment")
        report.update(success=True, saved_packages=sorted(dirty))
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
    unreal.log("ShipTest safe PlayerStart deployment: " + REPORT_PATH)
    return report


RESULT = main()

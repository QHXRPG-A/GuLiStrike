"""Read-only audit of LVL_ShipTest actors near the two observed ship spawns."""

from __future__ import annotations

import json
import math
import os

import unreal


MAP = "/Game/Maps/LVL_ShipTest"
POINTS = ((0.0, 0.0, 0.0), (0.0, -1600.0, 0.0))
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUTPUT = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "AssetAudit",
    "shiptest_spawn_geometry.json",
)


def _vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def _distance_to_bounds(point, origin, extent):
    squared = 0.0
    for coordinate, center, half_size in zip(point, origin, extent):
        delta = max(abs(coordinate - center) - half_size, 0.0)
        squared += delta * delta
    return math.sqrt(squared)


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
if world is None:
    raise RuntimeError("Unable to load " + MAP)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
records = []
for actor in actors:
    try:
        origin_value, extent_value = actor.get_actor_bounds(False)
        origin = _vector(origin_value)
        extent = [abs(value) for value in _vector(extent_value)]
        distances = [_distance_to_bounds(point, origin, extent) for point in POINTS]
        is_player_start = isinstance(actor, unreal.PlayerStart)
        if not is_player_start and min(distances) > 20_000.0:
            continue
        components = []
        for component in actor.get_components_by_class(unreal.PrimitiveComponent):
            item = {
                "name": str(component.get_name()),
                "class": str(component.get_class().get_name()),
            }
            for property_name in ("mobility", "collision_enabled", "collision_object_type"):
                try:
                    item[property_name] = str(component.get_editor_property(property_name))
                except Exception as error:
                    item[property_name + "_error"] = str(error)
            components.append(item)
        records.append(
            {
                "label": str(actor.get_actor_label()),
                "name": str(actor.get_name()),
                "class": str(actor.get_class().get_name()),
                "location": _vector(actor.get_actor_location()),
                "bounds_origin": origin,
                "bounds_extent": extent,
                "distance_to_observed_points": distances,
                "is_player_start": is_player_start,
                "components": components,
            }
        )
    except Exception as error:
        records.append({"name": str(actor.get_name()), "error": str(error)})


candidate_centers = (
    (0.0, 0.0, 0.0),
    (0.0, -1600.0, 0.0),
    (0.0, 16000.0, 0.0),
    (0.0, 24000.0, 0.0),
    (16000.0, 0.0, 0.0),
    (-16000.0, 0.0, 0.0),
    (20000.0, 20000.0, 0.0),
    (20000.0, 18400.0, 0.0),
    (-20000.0, 20000.0, 0.0),
    (-20000.0, 18400.0, 0.0),
)
candidate_records = []
formation = unreal.load_asset('/Game/GuLiStrike/Ship/Abilities/Formations/DA_WingmanFormation_SwarmOrbit')
for center in candidate_centers:
    positions = unreal.GuLiWingmanSimulationSubsystem.get_clear_formation_positions(
        world, unreal.Transform(location=unreal.Vector(*center)), formation.get_resolved_flight_config())
    candidate_records.append({'center': center, 'all_formation_endpoints_valid': positions is not None,
                              'positions': [list(p.to_tuple()) for p in positions] if positions else []})

result = {
    "schema": "guli.shiptest-spawn-geometry.v1",
    "map": MAP,
    "points": POINTS,
    "actors": sorted(records, key=lambda item: (not item.get("is_player_start", False), item.get("label", ""))),
    "formation_candidate_centers": candidate_records,
}
os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
with open(OUTPUT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2, sort_keys=True)
    stream.write("\n")
unreal.log("ShipTest spawn geometry audit: " + OUTPUT)

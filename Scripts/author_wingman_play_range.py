"""Author or read the local Hybrid wingman trials in the current Commander map. Never starts PIE."""
import json
import math
from pathlib import Path

import unreal


MAP = "/Game/Maps/LVL_CommanderMassPrototype"
FOLDER = "Gameplay/Tests/Wingman"
TAG = "GuLi.WingmanRange."
# Centimeters. Altitudes are relative to the saved Landscape. Air combat uses a common flight level.
MARKERS = {
    "FlightStart": (-20000, 44000, 3500, -135),
    "RidgeCrest": (-32000, 32000, 3000, -135),
    "RidgeExit": (-44000, 20000, 3000, -135),
    "AirStart": (-40000, 12000, 9000, 0),
    "AirTarget": (-24000, 12000, 9000, 180),
    "GroundStart": (-10000, -37000, 6000, -90),
    "GroundTarget": (-10000, -50000, 0, 90),
}


def landscape_height(world, x, y):
    hit = unreal.SystemLibrary.line_trace_single_for_objects(
        world, unreal.Vector(x, y, 110000), unreal.Vector(x, y, -110000),
        [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], False, [], unreal.DrawDebugTrace.NONE)
    if hit:
        values = hit.to_tuple()
        if values[0] and isinstance(values[9], unreal.LandscapeProxy):
            return values[5].z
    raise RuntimeError(f"No Landscape surface at {x}, {y}")


def marker_poses(world):
    ground = {name: landscape_height(world, x, y) for name, (x, y, _, _) in MARKERS.items()}
    air_level = max(ground["AirStart"], ground["AirTarget"]) + MARKERS["AirStart"][2]
    poses = {}
    for name, (x, y, offset, yaw) in MARKERS.items():
        z = air_level if name in ("AirStart", "AirTarget") else ground[name] + offset
        poses[name] = (unreal.Vector(x, y, z), unreal.Rotator(yaw=yaw), ground[name])
    return poses


def existing_markers(actors):
    found = {}
    for actor in actors.get_all_level_actors():
        for tag in actor.get_editor_property("tags"):
            tag = str(tag)
            if not tag.startswith(TAG):
                continue
            name = tag[len(TAG):]
            if name not in MARKERS or name in found or not isinstance(actor, unreal.TargetPoint):
                raise RuntimeError(f"Unexpected or duplicate range marker {tag}")
            found[name] = actor
    return found


def author(world, actors):
    poses = marker_poses(world)
    markers = existing_markers(actors)
    for name, (location, rotation, _) in poses.items():
        actor = markers.get(name)
        if actor is None:
            actor = actors.spawn_actor_from_class(unreal.TargetPoint, location, rotation)
            if actor is None:
                raise RuntimeError(f"Cannot place {name}")
        actor.modify()
        actor.set_actor_location_and_rotation(location, rotation, False, True)
        actor.set_actor_label("WingmanRange_" + name)
        actor.set_folder_path(FOLDER)
        actor.set_editor_property("tags", [unreal.Name(TAG + name)])
        actor.set_editor_property("is_editor_only_actor", True)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
        raise RuntimeError(f"Cannot save {MAP}")


def readback(world, actors):
    found = existing_markers(actors)
    if set(found) != set(MARKERS):
        raise RuntimeError(f"Missing range markers {set(MARKERS) - set(found)}")
    markers = {}
    for name, (expected, rotation, terrain) in marker_poses(world).items():
        actor = found[name]
        pos = actor.get_actor_location()
        yaw = actor.get_actor_rotation().yaw
        assert math.dist((pos.x, pos.y, pos.z), (expected.x, expected.y, expected.z)) < 1.0, name
        assert abs(math.remainder(yaw - rotation.yaw, 360.0)) < 0.01, name
        assert str(actor.get_folder_path()) == FOLDER and actor.get_editor_property("is_editor_only_actor"), name
        markers[name] = dict(actor=actor.get_actor_label(), location_cm=[pos.x, pos.y, pos.z],
                             yaw_degrees=yaw, ground_z_cm=terrain, height_m=(pos.z-terrain)/100)
    runs = json.loads(unreal.GuLiHybridAuthoringLibrary.inspect_wingman_play_range())
    if not runs["ok"]:
        raise RuntimeError(runs["error"])
    for trial, infantry in (("flight", 0), ("air", 0), ("ground", 6), ("combined", 36)):
        run = runs["trials"][trial]
        duel = trial in ("air", "combined")
        assert run["clear_spawn_slots"] == 25 and run["enemy_spawn_slots"] == (25 if duel else 0), trial
        assert run["protected_ship_hulls"] == duel and run["opponent_hangar_node"] == ("08" if duel else ""), trial
        assert run["startup_infantry"] == infantry and len(run["blue_targets"]) == infantry + int(duel), trial
        assert run["startup_economy_vehicles"] == 0 and run["world_activity_hold"] == 3, trial
        troops = [t for t in run["blue_targets"] if t["definition"] == "unit/1"]
        assert len(troops) == infantry and all(t["starts_idle"] and not t["automatic_fire"] for t in troops), trial
        if trial == "combined":
            sectors = {}
            for t in troops:
                ix, iy = (round(t[key] / 62000) for key in ("x_cm", "y_cm"))
                sectors.setdefault((ix, iy), []).append(t)
            assert set(sectors) == {(x,y) for x in (-1,0,1) for y in (-1,0,1)}, sectors
            assert all(len(group) == 4 for group in sectors.values()), sectors
            centers = [(sum(t["x_cm"] for t in group)/400, sum(t["y_cm"] for t in group)/400) for group in sectors.values()]
            separation = min(math.dist(a,b) for i,a in enumerate(centers) for b in centers[i+1:])
            assert separation > 400
            run["ground_squads"] = dict(count=len(sectors), soldiers_per_squad=4, centers_xy_m=centers,
                nearest_centers_m=separation, width_m=max(x for x,y in centers)-min(x for x,y in centers),
                depth_m=max(y for x,y in centers)-min(y for x,y in centers))
    rise = (markers["RidgeCrest"]["ground_z_cm"] - markers["FlightStart"]["ground_z_cm"]) / 100
    assert rise > 35
    report = dict(map=MAP, folder=FOLDER, markers=markers, ridge_rise_m=rise,
                  trials=runs["trials"], verification="saved map, roster, flight clearance and attack geometry; no play session")
    path = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "TestResults/WingmanPredictiveFlight/play-range-readback.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(".tmp")
    pending.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    pending.replace(path)
    unreal.log(f"Wingman range read back from {MAP}, saved {path}")


def run(action):
    if action not in ("author", "readback"):
        raise ValueError(action)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("Stop play before working on the wingman range.")
    world = editor.get_editor_world()
    if world.get_path_name().split(".")[0] != MAP:
        raise RuntimeError(f"Open {MAP} before authoring or inspecting the range.")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if action == "author":
        author(world, actors)
    else:
        readback(world, actors)


if __name__ == "__main__":
    run("author")

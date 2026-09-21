"""Author/read back the FireReview target range in the live editor, without playing.

Run via Scripts/ue_exec.py. FIRE_REVIEW_STAGE may be prepare, deploy (default),
readback, or repair_navigation. Prepare saves navigation and player starts while
a native build is pending; repair_navigation fixes only decorative navigation
contributors and saves rebuilt navigation; deploy requires the new fire field.
"""
import json
import math
from pathlib import Path
import traceback

import unreal

MAP = "/Game/Maps/LVL_GroundMech_FireReview"
FOLDER = "Gameplay/FireReviewRange"
TAG = unreal.Name("GuLiFireReviewRange")
NAV_LABEL = "FireReview_RangeNavigation"
NAV_CENTER = unreal.Vector(9000, -3250, 600)
NAV_EXTENT = unreal.Vector(8000, 6250, 2600)
STARTS = (("GroundMech_Start_1", 8600, -7500), ("GroundMech_Start_2", 9400, -7500))
DEPLOYMENTS = (
    # The planned X=5000 leaves one slot 415 cm off navigation; shift this block 5 m east.
    ("FireReview_Red_Sweeper", unreal.GuLiTeam.RED, 1, 5500, -5000, 700),
    ("FireReview_Red_WarMachine", unreal.GuLiTeam.RED, 2, 5000, 0, 2000),
    ("FireReview_Blue_Sweeper", unreal.GuLiTeam.BLUE, 1, 13000, -5000, 700),
    ("FireReview_Blue_WarMachine", unreal.GuLiTeam.BLUE, 2, 13000, 0, 2000),
)
# These animated meshes were adding/removing navigation octree entries every frame.
# Keep the stationary RockLong2 pedestals of the spinning rocks navigation-relevant.
DECORATIVE_NAVIGATION = (
    ("Floating_Rock_2", "BP_Floating_Rock_Dark_C", ("StaticMesh", "RockLong1", "RockLong2", "RockLong3", "RockLong4")),
    ("Floating_Rock_Light_BP_5", "BP_Floating_Rock_Light_C", ("StaticMesh", "RockLong1", "RockLong2", "RockLong3", "RockLong4")),
    ("Spinning_Rock_BP_8", "BP_Spinning_Rock_C", ("RockFlat2",)),
    ("Spinning_Rock_BP2_11", "BP_Spinning_Rock_C", ("RockFlat2",)),
)
OUTPUT = Path(unreal.Paths.project_dir()).resolve() / "outputs/firereview-20260921"
STAGE = globals().get("FIRE_REVIEW_STAGE", "deploy")


def vector(value):
    return [round(float(v), 4) for v in value.to_tuple()]


def actor_record(actor):
    return {"label": actor.get_actor_label(), "path": actor.get_path_name(),
            "class": actor.get_class().get_path_name(),
            "location": vector(actor.get_actor_location()),
            "rotation": vector(actor.get_actor_rotation()),
            "scale": vector(actor.get_actor_scale3d())}


def table_rows(path):
    table = unreal.load_asset(path)
    assert table is not None, path
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def by_label(actors, label, expected):
    matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    assert len(matches) <= 1, ("Duplicate label", label)
    if matches:
        assert isinstance(matches[0], expected), (label, matches[0].get_class().get_path_name())
        return matches[0]
    return None


def mark(actor, label):
    actor.modify()
    actor.set_actor_label(label)
    actor.set_folder_path(unreal.Name(FOLDER))
    actor.set_editor_property("tags", list(set(actor.get_editor_property("tags")) | {TAG}))


def decorative_navigation(world, apply=False):
    """Map-instance overrides only; also usable for the user's active PIE repair."""
    assert world.get_name() == "LVL_GroundMech_FireReview", "Unexpected world"
    available = {actor.get_name(): actor for actor in
                 unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)}
    targets = []
    # Resolve every target before writing, so a renamed/missing actor stops safely.
    for name, class_name, component_names in DECORATIVE_NAVIGATION:
        actor = available.get(name)
        assert actor and actor.get_class().get_name() == class_name, ("Missing decorative actor", name)
        components = {component.get_name(): component for component in
                      actor.get_components_by_class(unreal.StaticMeshComponent)}
        for component_name in component_names:
            assert component_name in components, (name, component_name)
            targets.append((actor, component_name))
    records = []
    for actor, component_name in targets:
        # Property notifications may reconstruct Blueprint components. Never keep
        # a component pointer across a previous component's PostEditChange call.
        component = next(c for c in actor.get_components_by_class(unreal.StaticMeshComponent)
                         if c.get_name() == component_name)
        before = component.get_editor_property("can_ever_affect_navigation")
        if apply and before:
            component.modify()
            component.set_editor_property("can_ever_affect_navigation", False)
        component = next(c for c in actor.get_components_by_class(unreal.StaticMeshComponent)
                         if c.get_name() == component_name)
        after = component.get_editor_property("can_ever_affect_navigation")
        assert not after, ("Animated decoration still affects navigation", actor.get_name(), component.get_name())
        records.append({"actor": actor.get_name(), "component": component_name,
                        "can_ever_affect_navigation": after, "changed": before != after})
    for actor, component_name in targets:
        component = next(c for c in actor.get_components_by_class(unreal.StaticMeshComponent)
                         if c.get_name() == component_name)
        assert not component.get_editor_property("can_ever_affect_navigation"), (
            "Blueprint reconstruction restored navigation", actor.get_name(), component_name)
    return records


def repair_navigation(world):
    records = decorative_navigation(world, apply=True)
    result = unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world, True)
    assert result.success, str(result.message)
    return {"saved": True, "decorative_navigation": records,
            "ground_rebuilds": result.ground_rebuilds, "navigation_prepare": str(result.message)}


def navigation(world, actors):
    assert not unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world), "Navigation still building"
    matches = [a for a in actors.get_all_level_actors()
               if isinstance(a, unreal.RecastNavMesh) and "CommanderSoldier" in a.get_name()]
    assert len(matches) == 1, "Expected the CommanderSoldier navigation data"
    return matches[0]


def project(world, nav, point):
    result = unreal.NavigationSystemV1.project_point_to_navigation(
        world, point, nav, None, unreal.Vector(150, 150, 50000))
    assert result is not None, ("Off navigation", vector(point))
    assert math.hypot(result.x - point.x, result.y - point.y) <= 150.01, "Projection exceeds authority tolerance"
    return result


def slots(world, nav, location, rotation, spacing, radius):
    # Match GetSlotLocation and the authority's minimum-spacing adjustment.
    spacing = max(spacing, radius * 2 + 20)
    transform = unreal.Transform(location=location, rotation=rotation)
    result = []
    for index in range(4):
        offset = unreal.Vector((index // 2 - .5) * spacing, (index % 2 - .5) * spacing, 0)
        wanted = unreal.MathLibrary.transform_location(transform, offset)
        result.append(vector(project(world, nav, wanted)))
    return result


def prepare(world, actors, level):
    decorative_navigation(world, apply=True)
    starts = [by_label(actors, label, unreal.PlayerStart) for label, _, _ in STARTS]
    assert all(starts), "Both original FireReview PlayerStarts must exist"
    baseline = OUTPUT / "scene-before.json"
    if not baseline.exists():
        original = [actor_record(a) for a in actors.get_all_level_actors()
                    if a in starts or a.get_actor_label().endswith(("SizeReference", "Styled_Reference"))]
        baseline.write_text(json.dumps({"map": MAP, "actors": original}, indent=2), encoding="utf-8")
    for start, (label, x, y) in zip(starts, STARTS):
        hit = unreal.SystemLibrary.capsule_trace_single_for_objects(
            world, unreal.Vector(x, y, 10000), unreal.Vector(x, y, -10000), 235, 380,
            [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],
            False, starts, unreal.DrawDebugTrace.NONE).to_tuple()
        assert hit[0] and not hit[1] and hit[6].z > .9, ("Blocked start", label)
        start.modify()
        start.set_actor_location(hit[4] + unreal.Vector(0, 0, 40), False, False)
        start.set_actor_rotation(unreal.Rotator(pitch=0, yaw=90, roll=0), False)

    bounds = by_label(actors, NAV_LABEL, unreal.NavMeshBoundsVolume)
    if bounds is None:
        bounds = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, NAV_CENTER)
    assert bounds is not None, "Could not create navigation volume"
    mark(bounds, NAV_LABEL)
    bounds.set_actor_rotation(unreal.Rotator(), False)
    bounds.set_actor_scale3d(unreal.Vector(1, 1, 1))
    _, extent = bounds.get_actor_bounds(False, False)
    assert min(extent.to_tuple()) > 0, "Navigation volume needs a box brush"
    bounds.set_actor_scale3d(unreal.Vector(NAV_EXTENT.x / extent.x, NAV_EXTENT.y / extent.y, NAV_EXTENT.z / extent.z))
    bounds.set_actor_location(NAV_CENTER, False, False)
    selector = bounds.get_editor_property("supported_agents")
    for index in range(16):
        selector.set_editor_property("supports_agent" + str(index), index < 2)
    bounds.set_editor_property("supported_agents", selector)
    unreal.NavigationSystemV1.get_navigation_system(world).on_navigation_bounds_updated(bounds)
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    assert level.save_current_level(), "Map save failed"
    return {"saved": True, "navigation": actor_record(bounds),
            "starts": [actor_record(a) for a in starts], "deployment_pending": True}


def deploy(world, actors, level, radii):
    # Fail before authoring live deployment points if the editor still has old native code.
    cdo = unreal.get_default_object(unreal.GuLiCommanderDeploymentPoint)
    assert cdo.get_editor_property("allow_automatic_fire") is True
    decorative_navigation(world)
    nav = navigation(world, actors)
    candidates = []
    for label, team, kind, x, y, spacing in DEPLOYMENTS:
        center = project(world, nav, unreal.Vector(x, y, 0))
        location = unreal.Vector(x, y, center.z)
        rotation = unreal.Rotator(pitch=0, yaw=-90, roll=0)
        slots(world, nav, location, rotation, spacing, radii[kind])
        candidates.append((label, team, kind, spacing, location, rotation))
    for label, team, kind, spacing, location, rotation in candidates:
        actor = by_label(actors, label, unreal.GuLiCommanderDeploymentPoint)
        if actor is None:
            actor = actors.spawn_actor_from_class(unreal.GuLiCommanderDeploymentPoint, location, rotation)
        assert actor is not None, label
        mark(actor, label)
        actor.set_actor_location(location, False, False)
        actor.set_actor_rotation(rotation, False)
        actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
        for name, value in (("team", team), ("unit_type_id", kind), ("rows", 2), ("columns", 2),
                            ("spacing_centimeters", spacing), ("allow_automatic_fire", False)):
            actor.set_editor_property(name, value)
    # Check all authored values before saving, and read the saved map again separately.
    report = readback(world, actors, radii)
    assert level.save_current_level(), "Map save failed"
    report["saved"] = True
    return report


def readback(world, actors, radii):
    decoration = decorative_navigation(world)
    nav = navigation(world, actors)
    actual = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.GuLiCommanderDeploymentPoint)]
    assert len(actual) == 4, ("Unexpected deployment count", len(actual))
    records, projected, teams = [], [], {}
    for label, team, kind, x, y, spacing in DEPLOYMENTS:
        actor = by_label(actors, label, unreal.GuLiCommanderDeploymentPoint)
        assert actor is not None, label
        for name, value in (("team", team), ("unit_type_id", kind), ("rows", 2), ("columns", 2),
                            ("spacing_centimeters", spacing), ("allow_automatic_fire", False)):
            assert actor.get_editor_property(name) == value, (label, name)
        location = actor.get_actor_location()
        assert abs(location.x - x) < .01 and abs(location.y - y) < .01
        assert abs(actor.get_actor_rotation().yaw + 90) < .01
        assert vector(actor.get_actor_scale3d()) == [1, 1, 1]
        assert not actor.get_editor_property("is_editor_only_actor")
        points = slots(world, nav, location, actor.get_actor_rotation(), spacing, radii[kind])
        record = actor_record(actor)
        record.update(team=str(team), unit_type_id=kind, rows=2, columns=2, spacing_cm=spacing,
                      allow_automatic_fire=False, projected_slots=points)
        records.append(record)
        projected.extend((point, radii[kind]) for point in points)
        teams[str(team)] = teams.get(str(team), 0) + 4
    for index, (left, left_radius) in enumerate(projected):
        for right, right_radius in projected[index + 1:]:
            assert math.hypot(left[0] - right[0], left[1] - right[1]) >= left_radius + right_radius
    starts = []
    for label, x, y in STARTS:
        actor = by_label(actors, label, unreal.PlayerStart)
        assert actor is not None, label
        p = actor.get_actor_location()
        assert abs(p.x - x) < .01 and abs(p.y - y) < .01 and abs(actor.get_actor_rotation().yaw - 90) < .01
        starts.append(actor_record(actor))
    bounds = by_label(actors, NAV_LABEL, unreal.NavMeshBoundsVolume)
    assert bounds is not None
    origin, extent = bounds.get_actor_bounds(False, False)
    assert all(abs(a - b) < 1 for a, b in zip(vector(origin), vector(NAV_CENTER)))
    assert all(abs(a - b) < 1 for a, b in zip(vector(extent), vector(NAV_EXTENT)))
    selector = bounds.get_editor_property("supported_agents")
    assert selector.get_editor_property("supports_agent0") and selector.get_editor_property("supports_agent1")
    mode = world.get_world_settings().get_editor_property("default_game_mode")
    assert mode.get_path_name().endswith("BP_GroundMech_FireReviewMode_C")
    mode_defaults = unreal.get_default_object(mode)
    assert mode_defaults.get_editor_property("initial_role_priority")[0] == unreal.GuLiCommanderRole.GROUND
    pc = unreal.get_default_object(mode_defaults.get_editor_property("player_controller_class"))
    camera = pc.get_editor_property("player_camera_manager_class")
    assert camera.get_path_name() == "/Script/GuLiStrike.GuLiTeamOutlineCameraManager"
    assert unreal.get_default_object(camera).get_editor_property("outline_vfx_id") == 15
    vfx = next(row for row in table_rows("/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects") if row["Id"] == 15)
    material = unreal.load_asset(vfx["ResourcePath"])
    assert isinstance(material, unreal.MaterialInterface)
    color = unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(material, "OutlineColor")
    assert color.r > .9 and color.g < .05 and color.b < .05
    return {"deployments": records, "total_units": len(projected), "team_counts": teams,
            "starts": starts, "navigation": actor_record(bounds), "nav_data": nav.get_path_name(),
            "navigation_extent": vector(extent), "outline_vfx_id": 15,
            "outline_color": [color.r, color.g, color.b, color.a],
            "decorative_navigation": decoration,
            "game_mode": mode.get_path_name(), "deployment_pending": False}


def run():
    assert STAGE in ("prepare", "deploy", "readback", "repair_navigation"), STAGE
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    assert world and world.get_path_name().split(".")[0] == MAP, "Open the requested FireReview map first"
    assert editor.get_game_world() is None, "Stop here while a user is playing"
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    report = {"stage": STAGE, "map": MAP, "success": False, "runtime_played": False}
    try:
        rows = table_rows("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers")
        radii = {row["Id"]: row["ModelWidthMeters"] * 50 for row in rows if row["Id"] in (1, 2)}
        assert set(radii) == {1, 2}
        if STAGE == "repair_navigation":
            report.update(repair_navigation(world))
        elif STAGE == "prepare":
            report.update(prepare(world, actors, level))
        elif STAGE == "deploy":
            report.update(deploy(world, actors, level, radii))
        else:
            report.update(readback(world, actors, radii))
        report["success"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    path = OUTPUT / ("scene-" + STAGE + ".json")
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"success": report["success"], "stage": STAGE, "report": str(path),
                      "total_units": report.get("total_units"), "error": report.get("error")}))


if globals().get("FIRE_REVIEW_RUN", True):
    run()

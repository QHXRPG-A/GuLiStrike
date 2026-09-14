"""Author four persistent Mass deployment points in the current Ship prototype map."""
import json
from pathlib import Path
import unreal

MAP = "/Game/Maps/LVL_ShipWingmanAirCombatPrototype"
OUTPUT = Path(unreal.Paths.project_dir()) / "TestResults/WORK-20260912-004"
DEPLOYMENTS = (
    ("Ground_Red_DefaultSoldier_North", unreal.GuLiTeam.RED, 1, 120000, 150000, -90, 2400),
    ("Ground_Red_WM01_North", unreal.GuLiTeam.RED, 2, 145000, 150000, -90, 7200),
    ("Ground_Blue_DefaultSoldier_South", unreal.GuLiTeam.BLUE, 1, 100000, 35000, 90, 2400),
    ("Ground_Blue_WM01_South", unreal.GuLiTeam.BLUE, 2, 125000, 35000, 90, 7200),
)

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name().split(".")[0] == MAP
assert editor.get_game_world() is None
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.save_current_level()
nav = next(a for a in actors.get_all_level_actors() if a.get_name() == "RecastNavMesh-CommanderSoldier")
existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
records = []
for label, team, unit_type, x, y, yaw, spacing in DEPLOYMENTS:
    point = unreal.NavigationSystemV1.project_point_to_navigation(
        world, unreal.Vector(x, y, 0), nav, None, unreal.Vector(750, 750, 50000))
    assert point is not None, label
    actor = existing.get(label)
    if actor is None:
        actor = actors.spawn_actor_from_class(unreal.GuLiCommanderDeploymentPoint, point)
    actor.modify()
    actor.set_actor_label(label)
    actor.set_folder_path(unreal.Name("Gameplay/AirCombat/GroundUnits/" + ("Red" if team == unreal.GuLiTeam.RED else "Blue")))
    actor.set_actor_location(point, False, False)
    actor.set_actor_rotation(unreal.Rotator(pitch=0, yaw=yaw, roll=0), False)
    actor.set_editor_property("team", team)
    actor.set_editor_property("unit_type_id", unit_type)
    actor.set_editor_property("rows", 3)
    actor.set_editor_property("columns", 4)
    actor.set_editor_property("spacing_centimeters", spacing)
    slots = []
    for index in range(12):
        offset = unreal.Vector((index // 4 - 1) * spacing, (index % 4 - 1.5) * spacing, 0)
        wanted = unreal.MathLibrary.transform_location(actor.get_actor_transform(), offset)
        projected = unreal.NavigationSystemV1.project_point_to_navigation(
            world, wanted, nav, None, unreal.Vector(750, 750, 50000))
        assert projected is not None, (label, index)
        slots.append([projected.x, projected.y, projected.z])
    records.append({"label": actor.get_actor_label(), "team": str(actor.get_editor_property("team")),
                    "unit_type_id": actor.get_editor_property("unit_type_id"),
                    "rows": actor.get_editor_property("rows"), "columns": actor.get_editor_property("columns"),
                    "spacing_centimeters": actor.get_editor_property("spacing_centimeters"),
                    "location": [point.x, point.y, point.z], "slots": slots})
assert level.save_current_level()
OUTPUT.mkdir(parents=True, exist_ok=True)
report = {"map": MAP, "success": True, "count": 48, "deployments": records}
(OUTPUT / "placement.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps({"success": True, "count": 48, "report": str(OUTPUT / "placement.json")}))

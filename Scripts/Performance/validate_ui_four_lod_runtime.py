"""Focused live validation outside any performance measurement."""
import json
import sys
import time
from pathlib import Path
from capture_live_pie import editor, commands, status, stop_owned_pie, SNAPSHOT, run
from capture_ui_four_lod import visual_stats

OUT = Path("D:/UE5.7/PerformanceCaptures/GuLiStrike/ui-four-lod-20261020")


def save(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def state():
    return editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(w,0)
cam=pc.get_view_target()
lod=next(s for s in unreal.ObjectIterator(unreal.GuLiCommanderLODSubsystem) if s.get_outer()==w)
r={'camera_height':cam.get_target_height_meters(),'camera_tier':str(cam.get_camera_tier()),
   'transition':cam.is_camera_transitioning(),'viewport':list(pc.get_viewport_size()),
   'lod':json.loads(lod.get_mesh_lod_stats_json())}""")


if "--reuse" not in sys.argv:
    assert not status().get("active")
    editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(w,'gs.Commander.MeshLOD 1')
unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 2359 1745')
r={'started':True}""")
for _ in range(60):
    session = status()
    if session.get("world_ready") and session.get("can_issue_orders") and session.get("commander_view_active"):
        break
    time.sleep(1)
else:
    raise RuntimeError("PIE not ready")

rows = []
for height, tier in [(99.9, 0), (100, 1), (299.9, 1), (300, 2), (700, 2)]:
    commands([f"gs.GM.Commander.Camera.Height {height}"])
    time.sleep(1.5)
    value = state()
    assert value["viewport"] == [2359, 1748]
    assert value["lod"]["applied_tier"] == tier, value
    assert value["lod"]["model_outlines"] == (tier == 0)
    if tier:
        assert any(c["suppressed_outline_slots"] for c in value["lod"]["components"])
    else:
        assert not any(c["suppressed_outline_slots"] for c in value["lod"]["components"])
    rows.append(value)
    print(json.dumps({"height": height, "tier": tier, "outlines": tier == 0}), flush=True)

editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.GameplayStatics.get_player_controller(w,0).get_view_target().add_zoom_input(1)
r={'requested_overview':True}""")
time.sleep(2)
value = state()
assert value["lod"]["applied_tier"] == 3, value
rows.append(value)
editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.GameplayStatics.get_player_controller(w,0).get_view_target().add_zoom_input(-1)
r={'return':True}""")
time.sleep(2)
value = state()
assert value["lod"]["applied_tier"] == 2 and abs(value["camera_height"] - 700) < .01
rows.append(value)
commands(["gs.GM.Commander.Camera.Height 133.4"])
editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(w,0)
pc.set_mouse_location(1179,874)
cam=pc.get_view_target()
cam.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)
cam.jump_to_world_location(unreal.Vector(6000,69000,930.525390625))
r={'focused':True}""")
time.sleep(2)
before = state()
time.sleep(2)
after = state()
save("runtime-boundaries-outlines.json", {"success": True, "boundaries": rows,
     "stable_before": before, "stable_after": after,
     "stable_state_changes": after["lod"]["state_changes"] - before["lod"]["state_changes"]})
save("runtime-ui-outlines.json", visual_stats())
save("runtime-context-outlines.json", run(SNAPSHOT))
commands([f"Shot showui filename={OUT.as_posix()}/wm01-lod1-no-outline-ui.png"])
time.sleep(1)
commands([f"HighResShot 1 filename={OUT.as_posix()}/wm01-lod1-no-outline-final.png"])
print(json.dumps({"success": True, "stable_state_changes":
                 after["lod"]["state_changes"] - before["lod"]["state_changes"]}), flush=True)

"""Emit read-only Wingman runtime summaries for the active PIE world."""

import json

import unreal


editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
if world is None:
    raise RuntimeError("PIE world is not ready")

for command in (
    "gs.Wingman.Stats",
    "gs.Wingman.RelayStats",
    "gs.Wingman.LeaseWatchdog",
):
    unreal.SystemLibrary.execute_console_command(world, command)

unreal.MCPythonHelper.submit_result(
    json.dumps({"success": True, "world": str(world.get_path_name())})
)

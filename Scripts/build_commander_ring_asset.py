"""Queue the native Commander ring asset builder after capturing the old baseline.

Run this inside the UE editor after compiling GuLiStrikeEditor, with PIE stopped.
The command preserves the old material before modifying/saving the active assets.
It executes on the next editor tick; this script reports dispatch, not completion.
Then run validate_commander_ring_asset.py and the native RingAsset.Contract test.
"""

import json

import unreal


editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
if not world or editor.get_game_world():
    raise RuntimeError("Stop PIE and keep an editor world open before building the ring.")

unreal.SystemLibrary.execute_console_command(world, "gs.Commander.BuildUnitRing")
result = {
    "build_dispatched": True,
    "completion": "Check the native log and run validation after the next editor tick.",
    "mesh": "/Game/Commander/Units/SM_CommanderUnitRing",
    "material": "/Game/Commander/UI/M_CommanderUnitRing",
    "legacy_mesh": "/Engine/BasicShapes/Cylinder",
    "legacy_material": "/Game/Commander/QA/M_CommanderUnitRing_LegacyBenchmark",
    "native_validation": "gs.Commander.ValidateUnitRing",
    "automation_test": "GuLiStrike.Commander.Presentation.RingAsset.Contract",
}
unreal.log(json.dumps(result, ensure_ascii=False))
unreal.MCPythonHelper.submit_result(json.dumps(result))

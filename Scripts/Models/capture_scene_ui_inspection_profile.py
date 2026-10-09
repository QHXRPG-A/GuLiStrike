"""Capture the actual Slate layer after a profile has had a rendered frame."""
import json
from pathlib import Path
import unreal
OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')
profile = globals().pop('GULI_SCENE_UI_PROFILE', 'None')
w = next(w for w in unreal.ObjectIterator(unreal.World) if '/UEDPIE_2_' in w.get_path_name())
pc = unreal.GameplayStatics.get_player_controller(w, 0)
path = OUT / 'Images' / ('scene_ui_profile_' + profile + '.png')
ok = unreal.GuLiModelAuthoringLibrary.capture_runtime_viewport(pc, str(path))
unreal.MCPythonHelper.submit_result(json.dumps({'profile': profile, 'captured': ok, 'file': str(path)}))

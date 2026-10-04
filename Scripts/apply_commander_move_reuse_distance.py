"""Load the project's move reuse radius into the live CDO without restarting PIE."""
import json
import re
from pathlib import Path

import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
source = (root / 'Config/DefaultGame.ini').read_text(encoding='utf-8-sig')
section = re.search(r'(?ms)^\[/Script/GuLiStrike.GuLiUnitTaskSettings\]\s*\n(.*?)(?=^\[|\Z)', source)
assert section
value = re.search(r'(?m)^MoveReuseDistanceCentimeters\s*=\s*([0-9.]+)\s*$', section[1])
assert value
radius = float(value[1])
assert radius == 100
settings = unreal.get_default_object(unreal.load_class(None, '/Script/GuLiStrike.GuLiUnitTaskSettings'))
before = settings.get_editor_property('MoveReuseDistanceCentimeters')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
paused = unreal.GameplayStatics.is_game_paused(world) if world else None
settings.set_editor_property('MoveReuseDistanceCentimeters', radius)
after = settings.get_editor_property('MoveReuseDistanceCentimeters')
assert after == 100
assert not world or unreal.GameplayStatics.is_game_paused(world) == paused
result = {'success': True, 'config': 'Config/DefaultGame.ini', 'settings': settings.get_path_name(),
          'before_cm': before, 'after_cm': after, 'inclusive_horizontal_radius_m': after / 100,
          'game_world': world.get_path_name() if world else None, 'game_paused': paused,
          'PIE_restarted': False, 'native_build_required': False}
destination = root / 'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/Reports/move_reuse_1m_apply.json'
destination.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))

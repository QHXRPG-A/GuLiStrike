"""Apply the approved Pioneer locomotion rate tuning without interrupting live PIE."""
import json
import math
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DELIVERY = ROOT / 'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1'
SETTINGS = json.loads((DELIVERY / 'runtime_presentation_settings.json').read_text(encoding='utf-8'))
META = json.loads((DELIVERY / SETTINGS['source_stride_metadata']).read_text(encoding='utf-8'))
NAMES = set(SETTINGS['locomotion_clips'])
RATE = float(SETTINGS['locomotion_rate_multiplier'])
assert NAMES == {'Forward', 'Backward', 'Left', 'Right'} and math.isfinite(RATE) and 0 < RATE <= .2
LIB = unreal.EditorAssetLibrary
ASSET = unreal.load_asset('/Game/GuLiStrike/Robots/RSGMech/VAT/DA_Pioneer_VAT')
assert isinstance(ASSET, unreal.GuLiVATDefinition) and ASSET.is_valid_definition()
assert ASSET.get_path_name() == '/Game/GuLiStrike/Robots/RSGMech/VAT/DA_Pioneer_VAT.DA_Pioneer_VAT'
assert len(ASSET.get_editor_property('bones')) == 44 and len(ASSET.get_editor_property('bone_deltas')) == 44 * 311
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
was_paused = unreal.GameplayStatics.is_game_paused(world) if world else None
if not world:
    assert LIB.get_metadata_tag(ASSET, 'GuLi.Owner') == 'GuLiStrike.Pioneer.B-v1.Delivery_UE_v1'
SOURCE = {clip['name']: clip for clip in META['clips']}
CLIPS = list(ASSET.get_editor_property('clips'))
BEFORE = {str(clip.get_editor_property('name')): {'stride_cm': float(clip.get_editor_property('stride_centimeters')), 'duration_seconds': float(clip.get_editor_property('duration_seconds'))}
          for clip in CLIPS}
for clip in CLIPS:
    name = str(clip.get_editor_property('name'))
    if name in NAMES:
        # Idempotent: derive from the frozen measured stride, never multiply live values again.
        clip.set_editor_property('stride_centimeters', SOURCE[name]['stride_cm'] / RATE)
ASSET.set_editor_property('clips', CLIPS)
assert ASSET.is_valid_definition()
if not world:
    LIB.set_metadata_tag(ASSET, 'GuLi.LocomotionRateMultiplier', str(RATE))
    LIB.set_metadata_tag(ASSET, 'GuLi.PresentationVersion', SETTINGS['version'])
# Save only this shared content package. EditorAssetLibrary metadata helpers reject active PIE.
assert unreal.EditorLoadingAndSavingUtils.save_packages([ASSET.get_outer()], False)

AFTER = {str(clip.get_editor_property('name')): {'stride_cm': float(clip.get_editor_property('stride_centimeters')), 'duration_seconds': float(clip.get_editor_property('duration_seconds'))}
         for clip in ASSET.get_editor_property('clips')}
for name in NAMES:
    assert math.isclose(SOURCE[name]['stride_cm'] / AFTER[name]['stride_cm'], RATE, rel_tol=1e-6)
for name in set(SOURCE) - NAMES:
    assert AFTER[name] == BEFORE[name]
assert ASSET.get_editor_property('frames_per_second') == META['frames_per_second']
assert editor.get_game_world() == world
assert not world or unreal.GameplayStatics.is_game_paused(world) == was_paused
result = {'success': True, 'version': SETTINGS['version'], 'asset': ASSET.get_path_name(),
          'rate_multiplier': RATE, 'slowdown_factor': 1 / RATE, 'before': BEFORE, 'after': AFTER,
          'full_speed_cycles_per_second': {name: 1440 / AFTER[name]['stride_cm'] for name in sorted(NAMES)},
          'movement_speed_cm_per_second': 1440, 'game_world': world.get_path_name() if world else None,
          'game_paused': unreal.GameplayStatics.is_game_paused(world) if world else None,
          'gameplay_interrupted': False, 'source_frames_rebaked': False}
(DELIVERY / 'Reports/locomotion_slow_v1_apply.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))

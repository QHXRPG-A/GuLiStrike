"""Shared table-to-Niagara mapping for authoring, preview and saved-asset readback."""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARAMETERS = {
    'SmokeInitialWidthCentimeters': ('smoke_initial_width_centimeters', 'User.MissileSmokeInitialWidth'),
    'SmokeMaximumWidthCentimeters': ('smoke_maximum_width_centimeters', 'User.MissileSmokeMaximumWidth'),
    'FlameWidthCentimeters': ('flame_width_centimeters', 'User.MissileFlameWidth'),
    'FlameLengthCentimeters': ('flame_length_centimeters', 'User.MissileFlameLength'),
}


def read_profile(unreal=None):
    rows = json.loads((ROOT/'Data/Json/DT_GuLiStrikeSecondaryWeapons_Projectiles.json').read_text(encoding='utf-8'))
    row = next(r for r in rows if r['Name'] == 'WM01_Missile')
    values = {name: float(row[name]) for name in PARAMETERS}
    assert all(math.isfinite(v) and v > 0 for v in values.values()), values
    assert values['SmokeMaximumWidthCentimeters'] >= values['SmokeInitialWidthCentimeters'], values
    if unreal is not None:
        definition = unreal.load_asset(row['ProjectileAsset'])
        visual = definition.resolve_visual_settings()
        motion = definition.resolve_motion_settings()
        assert visual is not None and motion is not None, 'Import the current Projectiles table first'
        for name, (prop, _) in PARAMETERS.items():
            assert math.isclose(float(visual.get_editor_property(prop)), values[name], abs_tol=1e-3), name
        assert math.isclose(motion.speed, row['SpeedCentimetersPerSecond'], abs_tol=1e-3)
    return row, {parameter: values[name] for name, (_, parameter) in PARAMETERS.items()}

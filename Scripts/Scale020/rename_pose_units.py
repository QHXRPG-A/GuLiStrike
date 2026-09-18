"""Mechanical identifier migration: protocol 13 uses quantization units, not meters."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[2]
RENAMES = {'WorldXMeters': 'WorldXUnits', 'WorldYMeters': 'WorldYUnits', 'WorldZDecimeters': 'WorldZUnits',
           'VelocityXMetersPerSecond': 'VelocityXUnits', 'VelocityYMetersPerSecond': 'VelocityYUnits',
           'VelocityZMetersPerSecond': 'VelocityZUnits'}
changes = []
for path in (ROOT / 'Source/GuLiStrike').rglob('*'):
    if path.suffix not in ('.h', '.cpp'):
        continue
    before = path.read_bytes().decode('utf-8')
    after = before
    for old, new in RENAMES.items():
        after = after.replace(old, new)
    if after != before:
        path.write_bytes(after.encode('utf-8'))
        changes.append(str(path.relative_to(ROOT)))
print(json.dumps({'changed_files': changes}))

"""Replay 500/1000 local movers using the same frozen scene before and after migration."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, default=Path('Artifacts/MassAuthority20261111'))
parser.add_argument('--version', choices=['baseline', 'mass'], required=True)
args = parser.parse_args()
root = args.root.resolve()
manifest = {}
for count in (1000, 500):
    for scenario in ('crossing', 'hold'):
        key = f'{scenario}-{count}'
        manifest[key] = [f'{args.version}-{key}-{i:02}' for i in range(1, 4)]
(root / f'{args.version}-suite.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
for key, names in manifest.items():
    scenario, count = key.split('-')
    recipe = root / f'baseline-{key}-01' / 'recipe.json'
    for name in names:
        output = root / name
        if output.exists():
            if json.loads((output / 'result.json').read_text(encoding='utf-8'))['valid_window']:
                print(json.dumps({'existing':name}), flush=True)
                continue
        cmd = [sys.executable, '-X', 'utf8', str(Path(__file__).with_name('capture_circular_avoidance.py')),
               '--scenario', scenario, '--moving-units', count, '--output', str(output)]
        if recipe.exists():
            cmd += ['--recipe', str(recipe)]
        subprocess.run(cmd, check=True)
        print(json.dumps({'completed':name}), flush=True)

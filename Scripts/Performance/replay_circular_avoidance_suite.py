"""Replay each qualified old recipe three times, sequentially, in disposable PIE."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, default=Path('Artifacts/CircularAvoidance20261110'))
parser.add_argument('--version', required=True)
parser.add_argument('--suite-file', default='new-suite.json', help='Keep different algorithm runs in distinct manifests')
parser.add_argument('--stop-file', default='stop-after-window', help='Stop this suite before its next window')
args = parser.parse_args()
root = args.root.resolve()
assert json.loads((root / 'baseline-qualification-current.json').read_text(encoding='utf-8'))['baseline_gate_passed']
old = json.loads((root / 'old-suite.json').read_text(encoding='utf-8'))
new = {scenario: [f'new-{scenario}-{args.version}-{n:02}' for n in range(1, 4)] for scenario in old}
(root / args.suite_file).write_text(json.dumps(new, indent=2), encoding='utf-8')
count = 0
for scenario in ('buildings', 'ore', 'work', 'mixed', 'gather', 'attack', 'crossing', 'hold'):
    for name in new[scenario]:
        if (root / args.stop_file).exists():
            raise SystemExit('Stopped before next window as requested by local work controller')
        output = root / name
        if output.exists():
            assert (output / 'result.json').is_file(), f'Inspect incomplete attempt before resuming: {output}'
            assert json.loads((output / 'result.json').read_text(encoding='utf-8'))['valid_window']
            count += 1
            print(json.dumps({'completed': count, 'total': 24, 'existing': name}), flush=True)
            continue
        subprocess.run([sys.executable, str(Path(__file__).with_name('capture_circular_avoidance.py')),
                        '--scenario', scenario, '--recipe', str(root / old[scenario][0] / 'recipe.json'),
                        '--output', str(output)], check=True)
        count += 1
        print(json.dumps({'completed': count, 'total': 24, 'captured': name}), flush=True)
print(f'Complete: use report_circular_avoidance.py with @old-suite.json and @{args.suite_file}', flush=True)

"""Freeze source/config/table hashes before the grid migration. No UE binary reads."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/Navigation/Grid300/Baseline/source.json'
EXCLUDE = ['Intermediate', 'Saved', 'Binaries', 'DerivedDataCache', 'Content/Assets', 'Downloads']

def main():
    if OUT.exists():
        print('Existing immutable baseline:', OUT)
        return
    args = ['rg', '--files', 'Source/GuLiStrike', 'Config', 'data/Excel', 'data/Json', 'Tools/DataPipeline']
    for folder in EXCLUDE: args.extend(['--glob', f'!{folder}/**'])
    files = subprocess.check_output(args, cwd=ROOT, text=True, encoding='utf-8').splitlines()
    rows = []
    for name in sorted(files):
        p = ROOT / name
        if '/Grid/' in p.as_posix() or p.suffix in ('.pyc',): continue
        rows.append({'path': p.relative_to(ROOT).as_posix(), 'bytes': p.stat().st_size,
                     'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
    report = {'schema': 'guli-grid300/source-baseline-v1',
              'captured_at': datetime.now(timezone.utc).isoformat(),
              'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'preexisting_tracked_status': subprocess.check_output(['git', 'status', '--short', '--untracked-files=no'], cwd=ROOT, text=True, encoding='utf-8'),
              'files': rows, 'cell_cm': 300, 'world_planning_ms': 2, 'route_cache_mib': 128,
              'simulation_hz': 60, 'pose_hz': 10, 'resource_backup_seconds': 5,
              'transport_contract': 'hybrid-sync-unique-v2', 'default_optimization_mask': 31}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open('x', encoding='utf-8') as stream: json.dump(report, stream, ensure_ascii=False, indent=2)
    print('Captured', len(rows), 'hashes:', OUT)

if __name__ == '__main__': main()

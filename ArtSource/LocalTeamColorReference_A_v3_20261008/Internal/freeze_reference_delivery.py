"""Freeze and read back only the A_v3 reference artifact files; no image or UE edits."""
import hashlib
import json
from datetime import datetime, timezone, timedelta
from pathlib import Path

OUT = Path(__file__).resolve().parents[1]
manifest = json.loads((OUT / 'review-manifest.json').read_text(encoding='utf8'))
delivery = json.loads((OUT / 'delivery-check.json').read_text(encoding='utf8'))
if delivery['status'] != 'passed':
    raise SystemExit('Reference delivery readback must pass before freezing')
suffixes = {'.png', '.jpg', '.json', '.txt', '.py', '.html', '.css', '.js', '.md'}
files = []
for path in sorted(OUT.rglob('*')):
    if not path.is_file() or path.suffix.lower() not in suffixes or path.name == 'frozen_delivery_manifest.json':
        continue
    raw = path.read_bytes()
    files.append({'path': path.relative_to(OUT).as_posix(), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
frozen = {
    'version': manifest['version'], 'stage': 'reference_A',
    'frozen_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
    'models': len(manifest['models']), 'selected_boards': manifest['board_count'],
    'A_approval': 'all_pending', 'B_construction': 'not_started', 'formal_ue_update': False,
    'file_count': len(files), 'excluded': ['frozen_delivery_manifest.json (self)', '__pycache__ and diagnostics'],
    'files': files,
}
path = OUT / 'frozen_delivery_manifest.json'
path.write_text(json.dumps(frozen, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
readback = json.loads(path.read_text(encoding='utf8'))
changes = [r['path'] for r in readback['files'] if hashlib.sha256((OUT / r['path']).read_bytes()).hexdigest() != r['sha256']]
if changes:
    raise SystemExit('Frozen file mismatch: ' + str(changes))
print(json.dumps({'files': len(files), 'models': len(manifest['models']), 'boards': manifest['board_count'], 'freeze_sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'readback': 'passed'}, ensure_ascii=False))

import hashlib
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

root = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
target = root / 'approval_A_v3_20261003.json'
manifest_path = root / 'References_A_v3/reference_manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
for artifact in manifest['artifacts']:
    assert hashlib.sha256((root / artifact['path']).read_bytes()).hexdigest() == artifact['sha256'], artifact['path']
record = {
    'stage': 'A', 'version': 'A-v3', 'status': 'approved',
    'decision_date': '2026-10-03',
    'recorded_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
    'user_message': '审核通过，blender已开，开始制作，根据原模型一比一还原参考图中的形象，美术风格三渲二、三档明暗、结构线',
    'manifest': 'References_A_v3/reference_manifest.json',
    'manifest_sha256': hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
    'approved_images': [a for a in manifest['artifacts'] if a['role'].startswith(('orthographic_', 'three_quarter_'))],
    'production_version': 'B-v1', 'B_approval': 'pending',
    'formal_ue_import_authorized': False,
    'interpretation': 'A-v3 passed; begin Blender production from original source with approved shape and palette.'}
if target.exists():
    existing = json.loads(target.read_text(encoding='utf-8'))
    assert existing['manifest_sha256'] == record['manifest_sha256'] and existing['status'] == 'approved'
else:
    target.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'A': 'approved', 'version': 'A-v3', 'B': 'pending', 'approved_artifacts_verified': True}, ensure_ascii=False))

"""Pin the human B decision to the delivered manifest and verify its artifacts."""
import hashlib
import json
from datetime import datetime
from pathlib import Path

root = Path(__file__).resolve().parents[2]
art = root / 'ArtSource/CommanderLOD_20261005'
file = art / 'review_manifest.json'
manifest = json.loads(file.read_text(encoding='utf8'))
expected = '761bc5edd06b08c598771943b46ebcbd7bc8285b42d345e234fea0907d02783d'
payload = dict(manifest)
payload.pop('content_sha256')
assert hashlib.sha256(json.dumps(payload, ensure_ascii=False, indent=2).encode()).hexdigest() == expected
assert manifest['content_sha256'] == expected
verified = 0
for unit in manifest['units']:
    for artifact in unit['artifacts']:
        path = art / artifact['path']
        assert path.suffix.lower() not in {'.uasset', '.umap', '.ubulk', '.uexp', '.pak'}
        assert hashlib.sha256(path.read_bytes()).hexdigest() == artifact['sha256'], path
        verified += 1
for item in manifest['evidence']:
    path = root / item['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256'], path
approval = dict(version=manifest['version'], content_sha256=expected, approval_B='approved',
    user_message='审核通过', date='2026-10-05',
    context='After opening the delivered LVL_CommanderMassPrototype three-tier review map.',
    scope='Soldiers ID1–6, CommanderLOD_3Tier_v1 actual candidate version; gameplay and performance remain separately unverified.',
    units=[dict(id=u['id'], name=u['name']) for u in manifest['units']],
    verified_artifacts=verified, native_compile='not_authorized', pie='not_run', network='not_run', fps='not_run')
target = art / 'approval_B.json'
if target.exists():
    assert json.loads(target.read_text(encoding='utf8')) == approval
else:
    target.write_text(json.dumps(approval, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(dict(success=True, approval=approval), ensure_ascii=False))

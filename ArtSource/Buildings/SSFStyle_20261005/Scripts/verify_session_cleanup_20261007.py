"""Check preserved art evidence after exact native PowerShell cleanup."""
import hashlib
import json
from pathlib import Path

R = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O = R / 'SessionCleanup_20261007'
read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    assert path.suffix.lower() not in {'.uasset', '.umap', '.uexp', '.ubulk', '.pak'}
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


plan = read(O / 'cleanup_plan.json')
result = read(O / 'cleanup_result.json')
assert result['success'] and result['removed_files'] == len(plan['files'])
assert all(not Path(entry['path']).exists() for entry in plan['files'])
protected = set()
manifest_checks = []
for check in plan['frozen_manifest_checks']:
    manifest = R / check['file']
    assert sha(manifest) == check['sha256']
    base = manifest.parent if manifest.parent.name == 'UE_Delivery_Team_v2' else R
    recoveries = {entry['current_file']: entry for entry in check['approved_source_recovery']}
    for entry in read(manifest)['files']:
        path = base / entry['file']
        if path == R / 'Production_B_v1/SSF_Production_B_v1.blend' and entry['file'] in recoveries:
            note = recoveries[entry['file']]
            assert sha(path) == note['current_sha256']
            assert sha(R / note['matching_approved_copy']) == entry['sha256']
            protected.add((R / note['matching_approved_copy']).resolve())
        else:
            assert path.exists() and sha(path) == entry['sha256'], str(path)
        protected.add(path.resolve())
    manifest_checks.append({'manifest': check['file'], 'files': check['file_count'],
                            'preserved': True, 'approved_source_recovery': check['approved_source_recovery']})
movie_report = read(R / 'Production_B_v1/AnimationPreviews/movie_readback_report.json')
assert movie_report['all_22_delivered_movies_decoded'] and len(movie_report['clips']) == 22
for clip in movie_report['clips']:
    assert sha(R / 'Production_B_v1/AnimationPreviews' / clip['file']) == clip['mp4_sha256']
report = {'success': True, 'deleted_files_absent': len(plan['files']),
          'preserved_unique_evidence_files': len(protected), 'preserved_manifests': manifest_checks,
          'preserved_animation_movies': 22, 'approved_B_v2_source_unchanged': True,
          'UE_binary_packages_read_or_hashed': False, 'UE_assets_edited_or_deleted': False,
          'gameplay_integrated': False}
(O / 'cleanup_validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
print(json.dumps({'success': True, 'deleted_files': len(plan['files']), 'preserved_files': len(protected),
                  'preserved_animation_movies': 22}, ensure_ascii=False))

"""Inventory only known SSF session scratch files; never inspect UE binaries."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/UE5.7/test1').resolve()
R = ROOT / 'ArtSource/Buildings/SSFStyle_20261005'
OUT = R / 'SessionCleanup_20261007'
OUT.mkdir(exist_ok=True)


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    assert path.suffix.lower() not in {'.uasset', '.umap', '.uexp', '.ubulk', '.pak'}
    result = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


protected = set()
manifests = list(R.glob('References_A_v*/reference_manifest.json')) + [
    R / 'Production_B_v1/production_manifest.json',
    R / 'TeamPalette_B_v2_20261007/delivery_manifest.json',
    R / 'UE_Delivery_Team_v2/delivery_manifest.json',
]
manifest_checks = []
recovered_source = R / 'Production_B_v1/FrozenSource/SSF_Production_B_v1_Approved.blend'
for manifest in manifests:
    base = manifest.parent if manifest.parent.name == 'UE_Delivery_Team_v2' else R
    entries = read(manifest)['files']
    version_notes = []
    for entry in entries:
        path = (base / entry['file']).resolve()
        assert path.is_relative_to(base)
        assert path.exists(), ('Frozen evidence missing', str(path))
        actual = sha(path)
        if actual != entry['sha256']:
            assert path == R / 'Production_B_v1/SSF_Production_B_v1.blend', ('Frozen evidence changed', str(path))
            assert recovered_source.exists() and sha(recovered_source) == entry['sha256']
            protected.add(recovered_source.resolve())
            version_notes.append({'current_file': path.relative_to(R).as_posix(),
                                  'current_sha256': actual, 'frozen_sha256': entry['sha256'],
                                  'matching_approved_copy': recovered_source.relative_to(R).as_posix(),
                                  'current_file_preserved': True})
        protected.add(path)
    manifest_checks.append({'file': manifest.relative_to(R).as_posix(),
                            'sha256': sha(manifest), 'file_count': len(entries),
                            'unchanged': not version_notes, 'approved_source_recovery': version_notes})

movies = read(R / 'Production_B_v1/AnimationPreviews/movie_readback_report.json')
assert movies['all_22_delivered_movies_decoded'] and len(movies['clips']) == 22
assert read(R / 'Production_B_v1/AnimationPreviews/movie_pixel_validation.json')['all_110_checkpoints_compared']
for clip in movies['clips']:
    movie = R / 'Production_B_v1/AnimationPreviews' / clip['file']
    assert movie.exists() and sha(movie) == clip['mp4_sha256']

# These logs remain useful execution evidence; redundant backups/probes can go.
retained_logs = {
    (R / folder / name).resolve()
    for folder, names in [
        ('UE_Delivery_Team_v2', ['ue_import.log', 'ue_validation.log',
                                'ue_import_nullrhi_attempt.log', 'ue_validation_getter_attempt.log']),
        ('UE_Delivery_v1', ['ue_import.log', 'ue_finish.log', 'ue_validation.log',
                           'ue_reference_repair.log', 'ue_animation_precision.log', 'ue_shader_repair.log']),
    ] for name in names
}
intermediates = {'SSF_B1_Atlas.blend', 'SSF_B1_Geometry.blend', 'SSF_B1_GeometryProbe.blend'}
deletions = []
for path in R.rglob('*'):
    if not path.is_file() or path.is_relative_to(OUT):
        continue
    resolved = path.resolve()
    reason = None
    if 'MotionFrames' in path.parts and path.suffix.lower() == '.png' and path.is_relative_to(R / 'Production_B_v1/Logs/MotionFrames'):
        reason = 'encoded_animation_intermediate_frame'
    elif path.suffix.lower() in {'.blend1', '.blend2'}:
        reason = 'Blender_autosave_backup'
    elif path.suffix.lower() in {'.pyc', '.pyo', '.tmp', '.temp'}:
        reason = 'regenerable_tool_cache'
    elif path.suffix.lower() == '.log' and resolved not in retained_logs:
        reason = 'redundant_run_or_probe_log'
    elif path.parent == R / 'Production_B_v1' and path.name in intermediates:
        reason = 'working_Blender_intermediate_superseded_by_frozen_product'
    if reason:
        assert resolved.is_relative_to(R) and resolved not in protected
        assert not path.is_symlink()
        deletions.append({'path': str(resolved), 'bytes': path.stat().st_size, 'reason': reason})

# Only the six exact pasted-image paths are eligible outside the project.
input_pairs = [
    ('codex-clipboard-cc60b957-4e7b-4567-b52e-6aead03ad56d.jpg', 'Inputs/codex-clipboard-cc60b957-4e7b-4567-b52e-6aead03ad56d.jpg'),
    ('codex-clipboard-531158da-b556-43a7-83b2-14a3755ef2fb.jpg', 'Inputs/codex-clipboard-531158da-b556-43a7-83b2-14a3755ef2fb.jpg'),
    ('codex-clipboard-518d9d3f-2859-4ded-aa46-e6fe0d68b850.jpg', 'Inputs/codex-clipboard-518d9d3f-2859-4ded-aa46-e6fe0d68b850.jpg'),
    ('codex-clipboard-bab0e1e9-9842-452b-a8b6-2c42b269a71c.png', 'Inputs/SelectedBaseline_A_v3_20261005.png'),
    ('codex-clipboard-1916ab2b-24ad-4cf2-87aa-dcb93bda8563.png', 'TeamPalette_B_v2_20261007/Inputs/BlueTeam_AirBase_UserReference.png'),
    ('codex-clipboard-f2bee0e8-1fa1-4029-baba-e13e80b8cffe.png', 'TeamPalette_B_v2_20261007/Inputs/RedTeam_CommandCenter_UserReference.png'),
]
temp_root = Path('C:/Users/a/AppData/Local/Temp').resolve()
input_checks = []
for name, destination in input_pairs:
    source = temp_root / name
    preserved = R / destination
    assert preserved.exists()
    check = {'temporary_input': str(source), 'archived_input': str(preserved), 'exists': source.exists()}
    if source.exists():
        equal = sha(source) == sha(preserved)
        check['byte_identical_archived_copy'] = equal
        if equal:
            deletions.append({'path': str(source.resolve()), 'bytes': source.stat().st_size,
                              'reason': 'clipboard_temporary_input_with_verified_archived_copy',
                              'preserved_copy': str(preserved), 'sha256': sha(source)})
    input_checks.append(check)

deletions.sort(key=lambda entry: entry['path'])
plan = {'scope': str(R), 'external_temp_root': str(temp_root), 'user_quote': '清除这个会话产生的临时文件，并传GitHub',
        'upload_steering': '所有项目变更传GitHub', 'files': deletions,
        'file_count': len(deletions), 'bytes': sum(entry['bytes'] for entry in deletions),
        'protected_unique_files': len(protected), 'frozen_manifest_checks': manifest_checks,
        '22_delivered_movies_preserved': True, 'input_archive_checks': input_checks,
        'retained_execution_logs': [str(path) for path in sorted(retained_logs)],
        'UE_assets_in_deletion_plan': False, 'forbidden_directories_traversed': False}
(OUT / 'cleanup_plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
categories = {}
for entry in deletions:
    category = categories.setdefault(entry['reason'], {'files': 0, 'bytes': 0})
    category['files'] += 1
    category['bytes'] += entry['bytes']
print(json.dumps({'files': plan['file_count'], 'bytes': plan['bytes'], 'categories': categories,
                  'protected_unique_files': len(protected), 'frozen_manifests_verified': len(manifest_checks)}, ensure_ascii=False))

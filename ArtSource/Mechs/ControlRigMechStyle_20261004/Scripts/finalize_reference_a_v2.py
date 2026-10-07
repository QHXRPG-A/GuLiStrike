"""Validate and freeze a palette-only Stage A revision; never approve on user's behalf."""
import copy
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
OUT = ROOT / 'References_A_v2'
MANIFEST = OUT / 'reference_manifest.json'
if MANIFEST.exists():
    raise RuntimeError('A-v2 already frozen; do not overwrite a published version.')
old_manifest_path = ROOT / 'References_A_v1/reference_manifest.json'
old_manifest = json.loads(old_manifest_path.read_text(encoding='utf-8'))
old_manifest_sha = '93324b32cb12fb9273bdb3d7ad5c60ce59798655f30bb657f53cca7f18db4fb4'


def sha(path):
    assert path.suffix.lower() not in {'.uasset', '.umap', '.uexp', '.ubulk', '.pak'}
    h = hashlib.sha256()
    with path.open('rb') as f:
        while block := f.read(1024*1024):
            h.update(block)
    return h.hexdigest()


assert sha(old_manifest_path) == old_manifest_sha, 'A-v1 manifest changed'
for record in old_manifest['files']:
    path = ROOT / record['path']
    assert path.stat().st_size == record['bytes'] and sha(path) == record['sha256'], f'Frozen A-v1 changed: {path.name}'

setup = json.loads((OUT / 'reference_setup.json').read_text(encoding='utf-8'))
assert setup['version'] == 'A-v2' and setup['A_approval'] == 'pending' and not setup['production_model']
assert setup['palette_srgb'] == {'DeepTealGray': '#2C3735', 'RustRed': '#8E3A2A',
                                'MutedTeal': '#557B78', 'SandBeige': '#D5C09C'}
assert setup['geometry_skinweight_hash_before'] == setup['geometry_skinweight_hash_after'] == old_manifest['geometry_and_skinweight_sha256']
assert setup['cameras'] == old_manifest['cameras']
assert not setup['geometry_changed'] and not setup['decimation_applied']
assert setup['same_material_regions_as_A_v1'] and setup['source_corner_normals_unchanged']
assert setup['object_transforms_bones_reference_pose_unchanged']
assert setup['tone_thresholds'] == [0, .12, .55] and setup['tone_linear_factors'] == [.42, .74, 1]
log = (OUT / 'render_v2.log').read_text(encoding='utf-8', errors='replace')
assert 'CONTROLRIG_REFERENCE_A_V2_RENDER_OK' in log, 'Missing successful render completion'

attachment_source = Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-c3aa84b1-b5f6-42da-b3f0-f88bd525adcb.jpg')
attachment_copy = OUT / 'Inputs/UserPalette_20261004.jpg'
assert sha(attachment_source) == sha(attachment_copy), 'Archived user palette differs from attachment'
files = []
for view in ('Hero', 'Front', 'Left', 'Back'):
    files += [OUT / f'ControlRigMech_A_v2_{view}.png', ROOT / f'Baseline/ControlRigMech_Source_{view}_2048.png']
files += [OUT / 'ControlRigMech_A_v2_ReferenceStudy.blend', OUT / 'reference_setup.json',
          OUT / 'README.md', attachment_copy, ROOT / 'Scripts/render_reference_a_v2.py',
          ROOT / 'Scripts/finalize_reference_a_v2.py']
records = []
for path in files:
    record = {'path': path.relative_to(ROOT).as_posix(), 'bytes': path.stat().st_size, 'sha256': sha(path)}
    if path.suffix.lower() in {'.png', '.jpg'}:
        with Image.open(path) as im:
            record.update(width=im.width, height=im.height)
            if path.suffix.lower() == '.png':
                assert im.size == (2048, 2048), f'Image is not native 2K: {path.name}'
            im.verify()
    records.append(record)

manifest = {k: copy.deepcopy(v) for k, v in old_manifest.items() if k != 'files'}
manifest.update({
    'version': 'A-v2', 'created_utc': datetime.now(timezone.utc).isoformat(),
    'revises_version': 'A-v1', 'revision_basis': '2026-10-04 user: 改成这种配色',
    'previous_manifest': {'path': 'References_A_v1/reference_manifest.json', 'sha256': old_manifest_sha,
                          'all_frozen_files_verified_unchanged': True, 'verified_file_count': len(old_manifest['files'])},
    'palette_srgb': setup['palette_srgb'], 'ink_srgb': setup['ink_srgb'],
    'functional_lens_srgb': setup['functional_lens_srgb'],
    'palette_roles': {'DeepTealGray': 'frame, hoses, lower chassis and joint cores',
                      'RustRed': 'muzzle casing, service armor and piston covers',
                      'MutedTeal': 'main armor, cannon casing and leg plates',
                      'SandBeige': 'joint caps, piston rods, metal fittings and functional lenses'},
    'user_palette_attachment': {'source_path': str(attachment_source),
                               'archived_path': attachment_copy.relative_to(ROOT).as_posix(),
                               'sha256': sha(attachment_copy), 'copied_without_modification': True},
    'same_camera_and_material_regions_as_A_v1': True,
    'source_corner_normals_and_object_bone_pose_unchanged': True,
    'tone_thresholds': setup['tone_thresholds'], 'tone_linear_factors': setup['tone_linear_factors'],
    'armature_pose_snapshot_sha256': setup['armature_pose_snapshot_sha256'],
    'files': records,
    'technical_verification': 'passed render, 2K images, palette, frozen-input hashes, geometry/weights/normals/pose/camera checks; production and UE not run',
})
MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
manifest_sha = sha(MANIFEST)
decision_path = ROOT / 'review_decisions.json'
decisions = json.loads(decision_path.read_text(encoding='utf-8'))
assert decisions['A']['submitted_version'] == 'A-v1' and decisions['A']['status'] == 'pending'
old_decision = copy.deepcopy(decisions['A'])
old_decision.update(status='revision_requested', user_decision='修改后再审（配色）',
                    user_message_evidence='改成这种配色', decision_date='2026-10-04',
                    attachment_evidence='References_A_v2/Inputs/UserPalette_20261004.jpg')
decisions.setdefault('A_history', []).append(old_decision)
decisions['A'] = {'status': 'pending', 'submitted_version': 'A-v2',
                  'manifest_path': 'References_A_v2/reference_manifest.json', 'manifest_sha256': manifest_sha,
                  'user_decision': None, 'user_message_evidence': None, 'decision_date': None}
decisions['asset_palette_revision'] = {'version': 'A-v2', 'date': '2026-10-04',
                                      'user_message_evidence': '改成这种配色',
                                      'palette_srgb': setup['palette_srgb'],
                                      'global_art_revision_changed': False}
decision_path.write_text(json.dumps(decisions, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'version': 'A-v2', 'manifest_sha256': manifest_sha,
                  'frozen_files': len(records), 'A_v1_files_unchanged': len(old_manifest['files']),
                  'A': 'pending', 'B': decisions['B']['status'], 'palette': setup['palette_srgb']}, ensure_ascii=False))

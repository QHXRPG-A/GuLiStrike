"""Freeze A-v3 and verify its images, source geometry, and historical artifacts."""
import copy
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT / 'References_A_v3'
if (OUT / 'reference_manifest.json').exists():
    raise SystemExit('A-v3 already frozen; create a new sibling version.')

previous = json.loads((ROOT / 'References_A_v2/reference_manifest.json').read_text(encoding='utf-8'))
old_setup = json.loads((ROOT / 'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
setup = json.loads((OUT / 'reference_setup.json').read_text(encoding='utf-8'))
assert setup['geometry_digest_before'] == setup['geometry_digest_after']
assert setup['geometry_digest_before'] == old_setup['geometry_digest_after']
assert setup['cameras'] == old_setup['cameras']
old_colors = {p['component']: p['base_color'] for p in old_setup['component_color_assignments']}
new_colors = {p['component']: p['base_color'] for p in setup['component_color_assignments']}
assert old_colors.keys() == new_colors.keys()
assert {cid: color for cid, color in new_colors.items() if old_colors[cid] != color} == {
    77: 'SkyBlue', 28551: 'PaleYellow'}
assert len(setup['retained_A_v2_blue_parts']) == 8
assert all(new_colors[p['component']] == 'SkyBlue' for p in setup['retained_A_v2_blue_parts'])
assert setup['palette_srgb']['SkyBlue'] == '#87CEEB'
assert setup['palette_srgb']['PaleYellow'] == '#F4E4A1'

for version in ('A_v1', 'A_v2'):
    old_manifest = json.loads((ROOT / f'References_{version}/reference_manifest.json').read_text(encoding='utf-8'))
    for record in old_manifest['artifacts']:
        assert hashlib.sha256((ROOT / record['path']).read_bytes()).hexdigest() == record['sha256'], record['path']


def artifact(relative, role):
    path = ROOT / relative
    record = {'path': relative, 'role': role, 'bytes': path.stat().st_size,
              'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    if path.suffix == '.png':
        with Image.open(path) as image:
            record['resolution_px'] = list(image.size)
    return record


artifacts = []
for view in ('Hero', 'Front', 'Left', 'Back'):
    record = artifact(f'References_A_v3/RSG_A_v3_{view}_SourceStyle.png',
                      'three_quarter_effect_reference' if view == 'Hero' else 'orthographic_' + view.lower())
    assert record['resolution_px'] == [2048, 2048]
    artifacts.append(record)

draft = artifact('References_A_v3/RSG_A_v3_ImagegenColorDraft.png', 'draft_not_selected_for_A')
artifacts += [
    artifact('References_A_v3/RSG_A_v3_ReferenceMaterialStudy.blend', 'reference_material_study_not_production_model'),
    artifact('References_A_v3/reference_setup.json', 'palette_revision_same_cameras_and_source_geometry'),
    artifact('Scripts/render_style_reference_a_v3.py', 'reproducible_palette_reference_render'),
    artifact('Scripts/finalize_reference_a_v3.py', 'reproducible_reference_integrity_check'),
    artifact('README_A_v3.md', 'current_review_notes'),
    artifact('References_A_v3/imagegen_color_prompt.txt', 'builtin_imagegen_color_draft_prompt'),
    draft,
    artifact('References_A_v2/reference_manifest.json', 'previous_frozen_version'),
    artifact('References_A_v1/reference_manifest.json', 'first_frozen_version'),
    artifact('Source/source_manifest.json', 'authoritative_original_source')]

manifest = copy.deepcopy(previous)
manifest.pop('sky_blue_visibility_estimates', None)
manifest.update({
    'version': 'A-v3', 'published_utc': datetime.now(timezone.utc).isoformat(),
    'previous_version': 'A-v2', 'user_revision_request': setup['user_revision_request'],
    'artifacts': artifacts, 'palette_srgb': setup['palette_srgb'],
    'palette_revision': setup['palette_revision'],
    'retained_A_v2_blue_parts': setup['retained_A_v2_blue_parts'],
    'source_geometry_digest_before': setup['geometry_digest_before'],
    'source_geometry_digest_after': setup['geometry_digest_after'],
    'all_A_v1_artifact_hashes_intact': True, 'all_A_v2_artifact_hashes_intact': True,
    'same_cameras_and_pose_as_A_v2': True,
    'visual_review': {'all_four_images_inspected': True,
                      'rear_guard_sky_blue': True, 'head_dome_pale_yellow': True,
                      'user_approval_inferred': False},
    'image_generation': {
        'mode': 'builtin_imagegen', 'operation': 'reference_image_palette_edit',
        'selected_for_review': False, 'draft': draft['path'],
        'draft_resolution_px': draft['resolution_px'],
        'reason': 'Native draft is below 2K; review uses four consistent source-based 2K views.',
        'prompt': 'References_A_v3/imagegen_color_prompt.txt',
        'input': 'References_A_v2/RSG_A_v2_Hero_SourceStyle.png',
        'review_images_method': 'source-based Blender reference material renders'},
    'approvals': {'A': {'status': 'pending', 'user_decision': None, 'version': 'A-v3'},
                  'B': {'status': 'not_started', 'user_decision': None, 'version': None}}})
(OUT / 'reference_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'version': 'A-v3', 'four_images_2048': True,
                  'A_v1_and_A_v2_intact': True, 'geometry_and_cameras_unchanged': True,
                  'rear_ring': '#87CEEB', 'head_dome': '#F4E4A1',
                  'draft_resolution_px': draft['resolution_px'], 'A': 'pending'}, ensure_ascii=False))

"""Freeze review A-v1 artifacts; technical integrity is separate from user approval."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
REF = ROOT / 'References_A_v1'
target = REF / 'reference_manifest.json'
if target.exists():
    raise SystemExit('A-v1 is already frozen; use a new reference version.')
source = json.loads((ROOT/'Source/source_manifest.json').read_text(encoding='utf-8'))
inspection = json.loads((ROOT/'Baseline/blender_source_inspection.json').read_text(encoding='utf-8'))
setup = json.loads((REF/'reference_setup.json').read_text(encoding='utf-8'))
assert len(source['bones']) == 44 and len(source['animations']) == 7
assert sum(o['triangles'] for o in inspection['meshes']) == int(source['source_tags']['Triangles']) == 68124
assert sum(o['vertices'] for o in inspection['meshes']) == 57588
assert setup['geometry_changed'] is False and setup['decimation_applied'] is False
scales = [v['ortho_scale_m'] for v in setup['cameras'].values()]
assert max(scales)-min(scales) < 1e-6
ue_dimensions = [(b-a)/100 for a,b in zip(source['bounds_cm']['min'],source['bounds_cm']['max'])]
residual = [b-a for a,b in zip(ue_dimensions, inspection['bounds_m']['dimensions'])]
assert max(abs(x) for x in residual) < 1e-5


def artifact(path, role):
    full = ROOT/path
    item = {'path': path, 'role': role, 'bytes': full.stat().st_size,
            'sha256': hashlib.sha256(full.read_bytes()).hexdigest()}
    if full.suffix == '.png':
        with Image.open(full) as image:
            item['resolution_px'] = list(image.size)
            assert image.size[0] >= 2048 and image.size[1] >= 2048
    return item


artifacts = []
for view in ('Hero','Front','Left','Back'):
    artifacts.append(artifact(f'References_A_v1/RSG_A_v1_{view}_SourceStyle.png',
                              'three_quarter_effect_reference' if view=='Hero' else f'orthographic_{view.lower()}'))
    artifacts.append(artifact(f'Baseline/RSG_Source_{view}_2048.png', f'source_gray_{view.lower()}'))
artifacts += [artifact('Source/SK_FPS_Mech_Source_LOD0.fbx','original_source_reference_export'),
              artifact('Source/source_manifest.json','source_44_bones_reference_transforms_and_7_animations'),
              artifact('References_A_v1/RSG_A_v1_ReferenceMaterialStudy.blend','reference_material_study_not_production_model'),
              artifact('References_A_v1/reference_setup.json','camera_palette_and_presentation_setup'),
              artifact('References_A_v1/imagegen_Hero_prompt.txt','builtin_imagegen_draft_prompt'),
              artifact('review_A_v1.html','interactive_reference_review'),
              artifact('README.md','color_moving_parts_scope_and_approval_notes')]
manifest = {
    'version': 'A-v1', 'published_utc': datetime.now(timezone.utc).isoformat(),
    'source_asset': source['source_mesh'], 'planned_formal_destination': '/Game/GuLiStrike/Robots/RSGMech',
    'scope': 'reference_design_and_source_baseline_only',
    'artifacts': artifacts, 'palette_srgb': setup['palette_srgb'],
    'geometry_integrity': {'source_body_triangles': 68124, 'source_vertices': 57588,
        'ue_bones': 44, 'source_animations': 7, 'fbx_blender_bones': 43,
        'fbx_blender_armature_root_object': 'DeformationSystem',
        'dimensions_m': inspection['bounds_m']['dimensions'], 'dimension_residual_m': residual,
        'same_pose_and_geometry_for_all_reference_views': True,
        'same_orthographic_scale_m': scales[0], 'paired_color_components': setup['symmetric_palette_check']['paired_components']},
    'image_generation': {'mode': 'builtin_imagegen', 'selected_for_review': False,
        'draft': 'References_A_v1/RSG_A_v1_Hero_ImagegenDraft.png', 'draft_resolution_px': [1254,1254],
        'reason': 'Native output below 2048px and extra continuous highlights; retained only as draft.',
        'review_images_method': 'source-based Blender reference material renders'},
    'technical_reference_integrity': 'passed',
    'approvals': {'A': {'status': 'pending', 'user_decision': None, 'version': 'A-v1'},
                 'B': {'status': 'not_started', 'user_decision': None, 'version': None}},
    'production_modeling': 'not_started', 'production_export_readback': 'not_started',
    'formal_ue_import': 'not_started', 'ue_packages_written': False,
    'lod_target_triangles': [
        {'lod': 0, 'body': 32000, 'outline': 8000, 'total': 40000, 'screen_size': 1.0},
        {'lod': 1, 'body': 14000, 'outline': 3000, 'total': 17000, 'screen_size': .40},
        {'lod': 2, 'body': 5000, 'outline': 1000, 'total': 6000, 'screen_size': .16},
        {'lod': 3, 'body': 2000, 'outline': 0, 'total': 2000, 'screen_size': .06}],
    'actual_new_lod_triangles': None, 'new_material_sections': None, 'new_texture_budget': None,
    'animation_compatibility_on_remade_mesh': 'not_run', 'combat_framerate_measurement': 'not_run',
}
target.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'version':'A-v1','main_images':4,'baseline_images':4,
                  'all_main_images_2048':True,'source_geometry_intact':True,
                  'A':'pending','B':'not_started','formal_ue_import':'not_started'}, ensure_ascii=False))

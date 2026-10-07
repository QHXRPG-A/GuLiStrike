"""Validate native renders, freeze Stage A hashes and register pending user decisions."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / 'References_A_v1'
assert not (REF / 'reference_manifest.json').exists(), 'Published references are immutable'
source = json.loads((ROOT / 'Source/source_manifest.json').read_text(encoding='utf-8'))
rig = json.loads((ROOT / 'Source/rig_animation_baseline.json').read_text(encoding='utf-8'))
setup = json.loads((REF / 'reference_setup.json').read_text(encoding='utf-8'))
baseline = json.loads((ROOT / 'Baseline/blender_source_inspection.json').read_text(encoding='utf-8'))
audit = json.loads((ROOT / 'Baseline/fbx_count_audit.json').read_text(encoding='utf-8'))
assert setup['geometry_skinweight_hash_before'] == setup['geometry_skinweight_hash_after']
assert len(source['bones']) == 152
assert source['control_rig']['preview_mesh'] == source['source_mesh']
assert len(source['lods']) == 1 and source['lods'][0]['ue_sections'] == 14
assert audit['meshes'][0]['fbx_polygon_count'] == int(source['source_tags']['Triangles'])
assert audit['meshes'][0]['fbx_polygon_count']-audit['meshes'][0]['duplicate_faces_by_vertex_indices'] == baseline['meshes'][0]['triangles']
assert len(rig['animations']) == 3
assert len(rig['graphs']) == 31
assert sum(len(g['nodes']) for g in rig['graphs']) == 811
assert sum(len(g['links']) for g in rig['graphs']) == 1057
assert not any('optional_read_note' in n for g in rig['graphs'] for n in g['nodes'] + g['links'])
assert all(len(s['bones']) == 152 for a in rig['animations'] for s in a['samples'])
assert len({c['ortho_scale_m'] for c in setup['cameras'].values()}) == 1
assert all(c['type'] == 'ORTHO' for c in setup['cameras'].values())
render_paths = []
for view in ('Hero','Front','Left','Back'):
    for p in (REF / f'ControlRigMech_A_v1_{view}.png', ROOT / f'Baseline/ControlRigMech_Source_{view}_2048.png'):
        with Image.open(p) as im:
            assert im.size == (2048,2048), (p,im.size)
            im.verify()
        render_paths.append(p)
paths = render_paths + [
    REF / 'ControlRigMech_A_v1_ReferenceStudy.blend', REF / 'reference_setup.json', ROOT / 'README.md',
    ROOT / 'Source/SKM_Mech_Source_LOD0.fbx', ROOT / 'Source/SKM_Mech_Source_AllLODs.fbx',
    ROOT / 'Source/source_manifest.json', ROOT / 'Source/rig_animation_baseline.json',
    ROOT / 'Baseline/blender_source_inspection.json', ROOT / 'Baseline/source_lod_inspection.json',
    ROOT / 'Baseline/fbx_count_audit.json', ROOT / 'Baseline/source_connected_parts.json',
    ROOT / 'Baseline/ControlRigMech_SourceImported.blend', ROOT / 'Baseline/ControlRigMech_SourceInspection.blend',
] + sorted((ROOT / 'Scripts').glob('*.py'))
files = []
for p in paths:
    files.append({'path': p.relative_to(ROOT).as_posix(), 'bytes': p.stat().st_size,
                  'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
                  **({'width':2048,'height':2048} if p in render_paths else {})})
manifest = {
    'version': 'A-v1', 'created_utc': datetime.now(timezone.utc).isoformat(), 'art_revision': '1.2',
    'stage': 'reference_design_submitted_for_user_A', 'A': 'pending', 'B': 'not_started',
    'approved_plan_is_not_approval_A_or_B': True,
    'source_mesh': source['source_mesh'], 'source_skeleton': source['source_skeleton'],
    'source_control_rig': source['control_rig']['path'],
    'source_statistics': {'bone_count':152, 'triangles_UE':284700, 'triangles_Blender_reference':284660,
        'vertices_UE':203324, 'source_lod_count':1, 'source_lod0_sections':14,
        'source_duplicate_faces_removed_by_Blender_validation':40,
        'source_physics_asset':source['source_physics_asset'], 'source_socket_count':source['socket_count'],
        'dimensions_m': baseline['bounds_m']['dimensions']},
    'reference_method': 'material and camera study on exported actual source geometry, no AI bitmap generation',
    'geometry_and_weights_unchanged_during_reference_design': True, 'active_decimation_applied': False,
    'reference_scene_is_not_a_production_asset': True,
    'geometry_and_skinweight_sha256': setup['geometry_skinweight_hash_after'],
    'same_source_pose_all_views': True, 'orthographic_scale_m': setup['cameras']['Hero']['ortho_scale_m'],
    'palette_srgb': setup['palette_srgb'], 'cameras':setup['cameras'],
    'animation_sources': [{'name':a['name'], 'duration_s':a['duration_s'], 'fps':a['frame_rate'],
        'sampled_keys':a['sampled_keys'], 'bone_tracks':a['raw_bone_track_count']} for a in rig['animations']],
    'control_rig_snapshot': {'graphs':31,'nodes':811,'links':1057,'static_default_hierarchy_items':1,
        'static_hierarchy_is_not_runtime_control_count':True},
    'production_lod_caps': [
        {'lod':0,'body':32000,'outline':8000,'total':40000,'screen_size':1.0},
        {'lod':1,'body':14000,'outline':3000,'total':17000,'screen_size':.40},
        {'lod':2,'body':5000,'outline':1000,'total':6000,'screen_size':.16},
        {'lod':3,'body':2000,'outline':0,'total':2000,'screen_size':.06}],
    'production_lods_or_atlas_created': False, 'UE_formal_assets_created_or_modified': False,
    'native_compile_or_gameplay_run': False, 'target_after_B': '/Game/GuLiStrike/Mechs/ControlRigMech',
    'technical_verification': 'passed source/query/render/hash checks; production compatibility and UE checks not run',
    'user_visual_approval': 'pending', 'files': files,
}
target = REF / 'reference_manifest.json'
target.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
decisions = {
    'work_id':'WORK-20261004-002', 'art_revision':'1.2',
    'A': {'status':'pending','submitted_version':'A-v1',
          'manifest_path':'References_A_v1/reference_manifest.json',
          'manifest_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
          'user_decision':None,'user_message_evidence':None,'decision_date':None},
    'B': {'status':'not_started','production_version':None,'user_decision':None,'user_message_evidence':None},
    'formal_UE': {'status':'not_started','target':'/Game/GuLiStrike/Mechs/ControlRigMech'},
}
(ROOT / 'review_decisions.json').write_text(json.dumps(decisions,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':True,'version':'A-v1','render_count':len(render_paths),
    'render_size':[2048,2048],'manifest_sha256':decisions['A']['manifest_sha256'],
    'file_count':len(files),'A':'pending','B':'not_started'},ensure_ascii=False))

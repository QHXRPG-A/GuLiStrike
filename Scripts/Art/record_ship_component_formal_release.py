"""Record the user's explicit 2026-09-17 implementation and formal UE release instruction."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917')
MESSAGE = '开始实施，然后把这些组件导入至ue替换正式资源'

def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def main():
    batch = ROOT / 'Batch04'
    manifest_path = batch / 'References/reference_A_manifest_v1.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    approval = {'schema': 'gulistrike-art-approval/v1', 'work_id': 'WORK-20260917-006',
        'recorded_at': datetime.now(timezone.utc).isoformat(), 'art_revision': '1.0',
        'stage': 'A', 'decision': 'approved', 'user_message': MESSAGE,
        'message_context': 'User replies to the displayed Batch04 reference review and explicitly requests implementation followed by formal UE replacement.',
        'scope': 'Original geometry retained; approved surface references only.',
        'reference_manifest': {'path': 'References/reference_A_manifest_v1.json',
            'sha256': hashlib.sha256(manifest_path.read_bytes()).hexdigest()}, 'parts': {}}
    for key, part in manifest['parts'].items():
        approval['parts'][key] = {k: part[k] for k in ('reference_version', 'reference_sheet',
            'original_authoring_source', 'original_inspection_blend', 'original_static_exports', 'original_geometry_snapshot')}
    if not (batch / 'approval_A_20260917.json').exists():
        dump(batch / 'approval_A_20260917.json', approval)
    release = {'schema': 'gulistrike-user-release-authorization/v1',
        'recorded_at': datetime.now(timezone.utc).isoformat(), 'user_message': MESSAGE,
        'decision': 'implementation_and_formal_UE_replacement_authorized',
        'scope': 'All 13 unique styled Ship components; Thor Lv2 reuses the Lv1 mesh.',
        'geometry_policy': 'Preserve original body geometry, rigs, transforms, attachment and muzzle contracts.',
        'visual_review_status': {'Batch01_v4': 'B_approved_in_prior_user_message',
            'Batch02_v2': 'actual_candidate_previously_shown; no_separate_explicit_B_visual_pass',
            'Batch03_v3': 'B_approved_in_prior_user_message',
            'Batch04': 'A_approved; no_separate_B_visual_pass'},
        'workflow_exception': 'The latest explicit user instruction authorizes implementation followed by formal UE replacement. This record is release authorization, not a fabricated B visual approval.',
        'required_validation': ['immutable_body_geometry', 'FBX_readback', 'UE_bones_sockets_bounds',
            'UE_actual_visuals', 'rollback_copies', 'formal_blueprint_references']}
    if not (ROOT / 'UE_Integration/release_authorization_20260917.json').exists():
        dump(ROOT / 'UE_Integration/release_authorization_20260917.json', release)
    print('SHIP_COMPONENT_IMPLEMENTATION_AND_RELEASE_RECORDED')

if __name__ == '__main__':
    main()

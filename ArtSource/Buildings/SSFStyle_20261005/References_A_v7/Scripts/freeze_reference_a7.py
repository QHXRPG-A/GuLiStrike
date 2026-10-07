"""Freeze the line/tone correction after actual reference visual review; user A remains pending."""
from pathlib import Path
import hashlib
import json
import shutil
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');OUT=ROOT/'References_A_v7'
FINAL=OUT/'reference_manifest.json'
assert not FINAL.exists()
qa=json.loads((OUT/'visual_qa.json').read_text(encoding='utf-8'))
assert qa['review_status']=='reference_ready_for_user_A' and qa['user_approval_A']=='pending'
for name in ('validation.json','revision_validation.json','palette_preservation_validation.json','style_preservation_validation.json','style_visibility_validation.json'):
    assert json.loads((OUT/name).read_text(encoding='utf-8'))['success'],name
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p,role):return {'file':p.relative_to(ROOT).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p),'role':role}
def records(value):
    if isinstance(value,dict):
        if 'file' in value and 'sha256' in value:yield value
        for v in value.values():yield from records(v)
    elif isinstance(value,list):
        for v in value:yield from records(v)
manifest=json.loads((OUT/'candidate_manifest.json').read_text(encoding='utf-8'))
for r in records(manifest):assert sha(ROOT/r['file'])==r['sha256'],r['file']
manifest['user_revision']='线稿和三档明暗好像没加？'
manifest['source_version']='SSF_Reference_A_v6'
manifest['all_palette_HEX_preserved']=True
manifest['files'].append(record(ROOT/'References_A_v6/SSF_ReferenceDesign_v6.blend','palette_geometry_source_scene'))
snapshot=OUT/'Scripts';snapshot.mkdir(exist_ok=True)
for name in ('audit_line_tone_a6.py','render_style_reference_a7.py','refine_assembly_lines_a7.py','verify_style_reference_a7.py',
             'build_style_evidence_a7.py','finalize_reference_a7.py','build_reference_contacts.py','validate_style_reference_a7.py','freeze_reference_a7.py'):
    dest=snapshot/name;shutil.copyfile(ROOT/'Scripts'/name,dest)
    manifest['files'].append(record(dest,'frozen_reference_script'))
for name,role in [('palette_revision.json','all_palette_roles_preserved'),('style_revision.json','line_tone_style_correction'),
                  ('packed_scene_report.json','packed_textures'),('palette_preservation_validation.json','actual_saved_Blender_RGB_check'),
                  ('style_preservation_validation.json','source_structure_palette_and_camera_check'),
                  ('style_visibility_validation.json','actual_band_visibility_measurement'),('A_v6_native_node_audit.json','previous_reference_style_audit'),
                  ('revision_validation.json','frozen_history_and_source_checks'),('visual_qa.json','assistant_visual_review'),('candidate_manifest.json','layout_candidate')]:
    manifest['files'].append(record(OUT/name,role))
for p in sorted((OUT/'Renders').glob('*.png')):manifest['files'].append(record(p,'native_Blender_reference_render'))
for p in sorted((OUT/'QA').glob('*.png')):manifest['files'].append(record(p,'reference_style_visual_evidence'))
for r in qa['actual_images_inspected']:assert sha(OUT/r['file'])==r['sha256'],r['file']
FINAL.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
files={r['file']:r for r in records(manifest)}
assert all(sha(ROOT/f)==r['sha256'] for f,r in files.items())
report={'success':True,'version':manifest['version'],'manifest_sha256':sha(FINAL),'tracked_files_checked':len(files),
        'all_hashes_equal':True,'all_palette_HEX_preserved_from_A_v6':True,
        'user_approval_A':'pending','user_approval_B':'not_started','production_modeling_started':False,'ue_formal_import_performed':False}
(ROOT/'DeliveryValidation_A_v7.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

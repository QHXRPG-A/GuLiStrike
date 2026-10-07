"""Freeze the reviewed-by-assistant A-v4 reference package; this does not approve user A/B."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v4';FINAL=OUT/'reference_manifest.json'
assert not FINAL.exists(), 'Reference version already frozen'
qa=json.loads((OUT/'visual_qa.json').read_text(encoding='utf-8'))
assert qa['version']=='SSF_Reference_A_v4' and qa['user_approval_A']=='pending'
assert qa['review_status']=='reference_ready_for_user_A'
assert json.loads((OUT/'validation.json').read_text(encoding='utf-8'))['success']
assert json.loads((OUT/'revision_validation.json').read_text(encoding='utf-8'))['success']
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def record(path,role):return {'file':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size,'sha256':sha(path),'role':role}
def records(value):
    if isinstance(value,dict):
        if 'file' in value and 'sha256' in value:yield value
        for v in value.values():yield from records(v)
    elif isinstance(value,list):
        for v in value:yield from records(v)
manifest=json.loads((OUT/'candidate_manifest.json').read_text(encoding='utf-8'))
manifest['user_revision']='深色的颜色再浅一些，不要有太深的颜色'
for r in records(manifest):assert sha(ROOT/r['file'])==r['sha256'],r['file']
snapshot=OUT/'Scripts';snapshot.mkdir(exist_ok=True)
names=['prepare_light_palette_a4.py','render_reference_a.py','package_reference_scene.py',
       'finalize_reference_a.py','build_reference_contacts.py','validate_reference_revision.py','freeze_reference_a4.py']
for name in names:
    dest=snapshot/name;shutil.copyfile(ROOT/'Scripts'/name,dest)
    manifest['files'].append(record(dest,'frozen_reference_script'))
for name,role in [('palette_revision.json','original_and_lightened_colors'),('packed_scene_report.json','packed_textures'),
                  ('revision_validation.json','source_and_history_checks'),('visual_qa.json','assistant_visual_review'),
                  ('candidate_manifest.json','layout_candidate')]:
    manifest['files'].append(record(OUT/name,role))
for r in qa['actual_images_inspected']:
    assert sha(OUT/r['file'])==r['sha256'],r['file']
    if r['file'].startswith('QA/'):
        manifest['files'].append(record(OUT/r['file'],'visual_inspection_contact'))
FINAL.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
files={r['file']:r for r in records(manifest)}
assert all(sha(ROOT/f)==r['sha256'] for f,r in files.items())
report={'success':True,'version':manifest['version'],'manifest_sha256':sha(FINAL),'tracked_files_checked':len(files),
        'all_hashes_equal':True,'user_approval_A':'pending','user_approval_B':'not_started',
        'production_modeling_started':False,'ue_formal_import_performed':False}
(ROOT/'DeliveryValidation_A_v4.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

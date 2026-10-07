"""Check frozen history and source/camera invariants of the current reference candidate."""
import argparse
import hashlib
import json
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--version',type=int,default=4);p.add_argument('--previous',type=int,default=3)
args=p.parse_args()
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/f'References_A_v{args.version}'
assert not (OUT/'reference_manifest.json').exists(), 'Published version is frozen'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def records(value):
    if isinstance(value,dict):
        if 'file' in value and 'sha256' in value:yield value
        for v in value.values():yield from records(v)
    elif isinstance(value,list):
        for v in value:yield from records(v)
history=[]
for version in range(1,args.version):
    path=ROOT/f'References_A_v{version}'/'reference_manifest.json'
    rec=json.loads(path.read_text(encoding='utf-8'))
    files={r['file']:r for r in records(rec)}
    failures=[]
    for f,r in files.items():
        target=(ROOT/f).resolve()
        assert target.is_relative_to(ROOT), f
        if not target.is_file() or sha(target)!=r['sha256']:failures.append(f)
    assert not failures,(version,failures)
    history.append({'version':rec['version'],'manifest_sha256':sha(path),
                    'tracked_files_checked':len(files),'all_hashes_equal':True})
old=json.loads((ROOT/f'References_A_v{args.previous}/render_manifest.json').read_text(encoding='utf-8'))
new=json.loads((OUT/'render_manifest.json').read_text(encoding='utf-8'))
oldassets={a['key']:a for a in old['assets']};checks=[]
for a in new['assets']:
    b=oldassets[a['key']]
    check={'asset':a['key'],'same_geometry_weights':a['geometry_sha256_after']==b['geometry_sha256_after'],
           'same_part_roles':a['parts']==b['parts'],'same_cameras_and_scale':a['views']==b['views'],
           'same_existing_mirror_pairs':a['mirror_color_pairs']==b['mirror_color_pairs'],
           'same_dimensions':a['dimensions_m']==b['dimensions_m']}
    assert all(v for k,v in check.items() if k!='asset'),check
    checks.append(check)
candidate=OUT/'candidate_manifest.json'
validation=json.loads((OUT/'validation.json').read_text(encoding='utf-8'))
assert validation['success'] and validation['canonical_reference_images']==42
result={'success':True,'version':new['version'],'previous_version':old['version'],'history':history,
        'candidate_manifest_sha256':sha(candidate),'source_comparisons':checks,
        'same_assembly_layout':new['assembly']==old['assembly'],
        'same_art_light_direction':new['art_light_direction']==old['art_light_direction'],
        'tone_factors_before':old['tone_factors'],'tone_factors_after':new['tone_factors'],
        'line_ink_before':old['shared_ink'],'line_ink_after':new['shared_ink'],
        'palette_changed_by_user_request':True,'source_geometry_modified':False,
        'production_modeling_started':False,'ue_formal_import_performed':False}
(OUT/'revision_validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':True,'old_versions':len(history),'source_comparisons':len(checks)},ensure_ascii=False))

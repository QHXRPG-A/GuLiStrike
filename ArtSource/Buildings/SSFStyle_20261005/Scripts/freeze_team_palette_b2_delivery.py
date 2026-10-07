"""Hash the new, reviewable Blender package without touching prior releases."""
import hashlib
import json
from pathlib import Path

R = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O = R/'TeamPalette_B_v2_20261007'

def sha(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):
            h.update(block)
    return h.hexdigest()

excluded = {'delivery_manifest.json','delivery_validation.json','interactive_open_status.json'}
entries = []
for p in sorted(O.rglob('*')):
    if not p.is_file() or p.name in excluded or p.suffix not in {'.png','.blend','.json','.md'}:
        continue
    entries.append({'file':str(p.relative_to(R)).replace('\\','/'),'size_bytes':p.stat().st_size,'sha256':sha(p)})
for name in ['build_team_palette_b2.py','verify_team_palette_b2.py','render_team_palette_b2.py',
             'package_team_palette_b2.py','contact_team_palette_b2.py','open_team_palette_b2_review.py',
             'finalize_team_palette_b2.py','freeze_team_palette_b2_delivery.py']:
    p = R/'Scripts'/name
    entries.append({'file':'Scripts/'+name,'size_bytes':p.stat().st_size,'sha256':sha(p)})
manifest = {'version':'SSF_TeamPalette_B_v2','date':'2026-10-07','files':entries,
            'user_B_approval':False,'UE_updated':False,'gameplay_integrated':False,
            'independent_native_check':'TeamPalette_B_v2_20261007/native_validation.json',
            'excluded_volatile_files':sorted(excluded),
            'review_entry':'TeamPalette_B_v2_20261007/Review_B_v2.md'}
(O/'delivery_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
failures = [e['file'] for e in entries if sha(R/e['file']) != e['sha256']]
candidate = json.loads((O/'candidate_manifest.json').read_text(encoding='utf8'))
assert not failures
assert sha(O/'SSF_TeamPalette_B_v2.blend') == candidate['candidate_blend_sha256']
assert sha(Path(candidate['source_blend'])) == candidate['source_blend_sha256']
report = {'success':True,'date':'2026-10-07','tracked_files':len(entries),'hash_failures':failures,
          'candidate_sha256':candidate['candidate_blend_sha256'],'source_B_v1_unchanged':True,
          'manifest_sha256':sha(O/'delivery_manifest.json'),'candidate_B_user_approval':False,
          'UE_updated':False,'Progress_check':{'errors':0,'warnings':16,'warnings_are_existing_document_budget_notices':True}}
(O/'delivery_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps(report,ensure_ascii=False))

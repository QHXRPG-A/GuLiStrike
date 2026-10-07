"""Pin the user's specific Blender release and preserve all reviewed source files."""
import hashlib, json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O=R/'TeamPalette_B_v2_20261007'; D=R/'UE_Delivery_Team_v2'
D.mkdir(exist_ok=True)
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
candidate=json.loads((O/'candidate_manifest.json').read_text(encoding='utf8'))
frozen=json.loads((O/'delivery_manifest.json').read_text(encoding='utf8'))
fail=[r['file'] for r in frozen['files'] if sha(R/r['file'])!=r['sha256']]
assert not fail,fail
assert sha(O/'SSF_TeamPalette_B_v2.blend')==candidate['candidate_blend_sha256']
assert sha(Path(candidate['source_blend']))==candidate['source_blend_sha256']
auth={'date':'2026-10-07','status':'released_for_formal_UE_storage','version':'SSF_TeamPalette_B_v2',
      'user_quote':'导入至ue作为正式资源','meaning':'The user explicitly releases the just-delivered visible Blender B_v2 color candidate for formal UE asset storage.',
      'source_blend':str(O/'SSF_TeamPalette_B_v2.blend'),'source_blend_sha256':candidate['candidate_blend_sha256'],
      'frozen_manifest_sha256':sha(O/'delivery_manifest.json'),'verified_source_files':len(frozen['files']),
      'target_root':'/Game/GuLiStrike/Buildings/SSFStylized','team_roots':{'Blue':'/Game/GuLiStrike/Buildings/SSFStylized/Blue','Red':'/Game/GuLiStrike/Buildings/SSFStylized/Red'},
      'source_B_v1_unchanged':True,'old_formal_assets_preserved':True,'gameplay_integration_authorized':False,
      'inherited_budget_differences_retained':'Existing B_v1 triangle/outline differences stay documented. This is not a higher global budget or performance acceptance.'}
(R/'approval_B_v2_import_20261007.json').write_text(json.dumps(auth,ensure_ascii=False,indent=2),encoding='utf8')
(D/'source_validation.json').write_text(json.dumps({'success':True,'tracked_files':len(frozen['files']),'mismatches':fail,'candidate_sha256':candidate['candidate_blend_sha256'],'source_B_v1_unchanged':True},indent=2),encoding='utf8')
print('SSF_TEAM_RELEASE_PINNED',len(frozen['files']),candidate['candidate_blend_sha256'])

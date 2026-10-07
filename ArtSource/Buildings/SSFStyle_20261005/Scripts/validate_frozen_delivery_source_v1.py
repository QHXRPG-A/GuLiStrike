"""Check explicitly listed source files; never traverse project asset/cache folders."""
import hashlib,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');P=R/'Production_B_v1';D=R/'UE_Delivery_v1'
auth=json.loads((R/'approval_B_import_20261006.json').read_text(encoding='utf8'))
def digest(path):
 assert path.suffix.lower() not in ('.uasset','.umap','.uexp','.ubulk','.pak')
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(8*1024*1024),b''):h.update(chunk)
 return h.hexdigest()
manifest=json.loads((P/'production_manifest.json').read_text(encoding='utf8'));errors=[]
for row in manifest['files']:
 p=(R/row['file']).resolve();assert p.is_relative_to(R.resolve())
 if not p.exists() or digest(p)!=row['sha256']:errors.append(row['file'])
blend=digest(P/'SSF_Production_B_v1.blend');mf=digest(P/'production_manifest.json')
assert blend==auth['source_blend_sha256'] and mf==auth['manifest_sha256'] and not errors
report={'success':True,'source_blend_sha256':blend,'production_manifest_sha256':mf,'frozen_files_verified':len(manifest['files']),'changed_files':errors,'source_or_original_UE_packages_written':False,'historical_B_approvals_preserved_in_frozen_manifest':True,'current_release_record':str(R/'approval_B_import_20261006.json')}
(D/'frozen_source_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False))

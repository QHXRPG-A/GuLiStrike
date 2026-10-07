"""Consolidate erratum hashes and check human-readable Commander LOD routes."""
import ast,hashlib,json,os,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/CommanderLOD_20261005/Reports'
skip={'Intermediate','Saved','Binaries','DerivedDataCache','Downloads'}
old=re.compile(r'(?:四|4)\s*(?:档|级).{0,16}LOD|LOD.{0,16}(?:四|4)\s*(?:档|级)|\bLOD\s*3(?![\w])|LOD\s*0\s*[–—~～/-]\s*3|LOD0\s*[/、]\s*1\s*[/、]\s*2\s*[/、]\s*3|LOD1\s*[/、]\s*2\s*[/、]\s*3|四\s*LOD|四档.{0,8}(?:静态|模型|FBX|骨骼|顶点)|三档远景|主模型.{0,20}(?:另外|另做|另有).{0,12}三档')
files=[];hits=[]
for folder in ['Progress/RequirementDocument','Progress/DevelopmentDocumentation','Progress/Archive','Progress/Gameplay','Progress/_Index',
 'ArtSource','.agents/skills/guli-model-production']:
 for directory,dirs,names in os.walk(ROOT/folder):
  dirs[:]=[d for d in dirs if d not in skip]
  for name in names:
   file=Path(directory)/name
   if file.suffix not in ['.md','.html']:continue
   text=file.read_text(encoding='utf-8-sig');files.append(str(file.relative_to(ROOT)))
   for match in old.finditer(text):hits.append(dict(path=str(file.relative_to(ROOT)),line=text.count('\n',0,match.start())+1,text=match.group()))
records=[]
with zipfile.ZipFile(OUT/'document_text_before_erratum.zip') as archive:
 for name in archive.namelist():
  file=ROOT/name
  if not file.exists():continue
  a=hashlib.sha256(archive.read(name)).hexdigest();b=hashlib.sha256(file.read_bytes()).hexdigest()
  records.append(dict(path=name,original_sha256=a,current_sha256=b,
   changed=a!=b,authority='2026-10-05 explicit user LOD correction; unrelated content preserved',
   history_archive='document_text_before_erratum.zip'))
(OUT/'document_erratum.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'document_check.json').write_text(json.dumps(dict(success=not hits,checked_files=len(files),obsolete_lod_hits=hits,
 preserved_unrelated='palette, four directional gait, four views, teleport skill levels, historical camera heights and network timing',
 frozen_machine_readbacks='Preserved as historical evidence, outside current production routing'),ensure_ascii=False,indent=2),encoding='utf8')
print('DOCUMENT_LOD_HITS',json.dumps(hits,ensure_ascii=False),'CHECKED',len(files))
assert not hits

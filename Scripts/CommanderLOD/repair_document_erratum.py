"""Preserve unrelated teleport/camera/timing history and VFX pages during LOD cleanup."""
import json,hashlib,zipfile,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/CommanderLOD_20261005/Reports'
manifest=json.loads((OUT/'document_erratum.json').read_text(encoding='utf8'))
last={r['path']:r for r in manifest}
restore=[
 'Progress/Archive/20260828-指挥官3C与运行时GM调参.md',
 'Progress/Archive/20260911-指挥官传送配置归并与范围扩展.md',
 'Progress/Archive/20260911-指挥官双点传送技能验收.md',
 'Progress/Archive/20261004-飞行物启停同步与实际时间预算实施验收.md',
 'Progress/DevelopmentDocumentation/20260828-指挥官3C与运行时GM调参.md',
 'Progress/DevelopmentDocumentation/20260910-指挥官双点传送技能.md',
 'Progress/DevelopmentDocumentation/20260914-指挥官白模据点占领与建筑体系.md',
 'Progress/Gameplay/指挥官传送.md','Progress/Gameplay/指挥官.md',
 'Progress/RequirementDocument/20260828-指挥官3C与运行时GM调参.md',
 'Progress/RequirementDocument/20260910-指挥官双点传送技能.md',
 'ArtSource/WarMachineHover_20260930/LOD2/Before/Progress/Gameplay/指挥官.md',
 'ArtSource/WarMachineHover_20260930/LOD2/Build/Before/Progress/Gameplay/指挥官.md']
rows=[]
with zipfile.ZipFile(OUT/'document_text_before_erratum.zip') as archive:
 for name in restore:
  file=ROOT/name;actual=file.read_bytes()
  original=archive.read(name)
  assert hashlib.sha256(actual).hexdigest() in [last[name]['after_sha256'],hashlib.sha256(original).hexdigest()],('Concurrent document change',name)
  # These mentions concern camera heights, skill levels or network timing.
  assert not re.search(r'四\s*档.{0,12}LOD|LOD.{0,12}四\s*档|LOD\s*3',original.decode('utf8'))
  file.write_bytes(original);rows.append(dict(path=name,operation='restore unrelated original text'))
 name='ArtSource/FX/WM01MissileCluster/Candidate_v1/review.html';file=ROOT/name
 source=archive.read(name).decode('utf8')
 source=source.replace('容量绑定及四级导弹仓 LOD 回读通过','容量绑定及导弹仓网格回读通过；当前指挥官模型规范总共三档')
 # Atomic replacement also works when a preview service has the old file open.
 temp=file.with_suffix('.replacement.html');temp.write_text(source,encoding='utf8');temp.replace(file)
 rows.append(dict(path=name,operation='retain original smoke/trail gallery; correct only LOD wording'))
(OUT/'document_scope_repair.json').write_text(json.dumps(dict(success=True,files=rows),ensure_ascii=False,indent=2),encoding='utf8')
print('UNRELATED_HISTORY_PRESERVED',len(rows))

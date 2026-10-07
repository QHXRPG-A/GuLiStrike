"""Check rigid part boundaries on the actual candidate meshes, independent of path-whitelisted tools."""
import bpy,json,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
report=dict(success=False,units=[])
for name,allowed in [('WM01',set(range(20))),('SweeperSummon',{0,2,6,7,8,9})]:
 bpy.ops.wm.open_mainfile(filepath=str(ART/name/(name+'_3Tier.blend')))
 rows=[]
 for lod in range(3):
  meshes=[o for o in bpy.data.collections[name+'_LOD'+str(lod)].objects if o.type=='MESH']
  assert meshes
  counts={};mixed=0;bad=0
  for ob in meshes:
   me=ob.data;assert len(me.uv_layers)==3
   for face in me.polygons:
    parts=[];pivots=[]
    for li in face.loop_indices:
     uv=me.uv_layers[2].data[li].uv;part=1-uv.y;pid=round(part)
     bad+=abs(part-pid)>.001 or pid not in allowed
     parts.append(pid);xy=me.uv_layers[1].data[li].uv;pivots.append((xy.x,xy.y,uv.x))
     counts[pid]=counts.get(pid,0)+1
    mixed+=len(set(parts))!=1 or any(sum((a-b)**2 for a,b in zip(p,pivots[0]))>.001 for p in pivots)
  assert bad==0 and mixed==0,(name,lod,bad,mixed)
  rows.append(dict(lod=lod,part_ids=sorted(counts),nonintegral_or_invalid_corners=int(bad),cross_part_triangles=int(mixed)))
 report['units'].append(dict(name=name,lods=rows))
report['success']=True
(ART/'Reports/mechanical_uv_readback.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report),flush=True)

"""Measure body/contour sections from actual candidates, recording native versus interchange totals."""
import bpy,json,sys,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
native=json.loads((ART/'Reports/candidate_readback.json').read_text(encoding='utf8'))
rows=[]
for unit in native['units']:
 name=unit['name'];bpy.ops.wm.open_mainfile(filepath=str(ART/name/(name+'_3Tier.blend')))
 lods=[]
 for lod in range(3):
  if name=='BiZhiMao':
   meta=json.loads((ART/name/'vertex_metadata.json').read_text(encoding='utf8'))['lods'][lod]
   body=meta['sections'][0]['triangles'];outline=sum(s['triangles'] for s in meta['sections'][1:]);sections=len(meta['sections'])
  else:
   objects=[o for o in bpy.data.collections[name+'_LOD'+str(lod)].objects if o.type=='MESH']
   body=0;outline=0;sections=0
   for ob in objects:
    ob.data.calc_loop_triangles();used=set()
    for tri in ob.data.loop_triangles:
     material=ob.data.materials[tri.material_index] if len(ob.data.materials)>tri.material_index else None
     is_outline=bool(material and re.search(r'outline|contour|ink',material.name,re.I))
     outline+=is_outline;body+=not is_outline;used.add(tri.material_index)
    sections+=len(used)
  total=unit['triangles'][lod]
  caps=[(32000,8000),(14000,3000),(2000,0)] if name in ['DefaultSoldier','BiZhiMao'] else None
  cap=sum(caps[lod]) if caps else (6000 if lod==1 else 1500 if lod==2 else None) if name in ['ElectromagneticMiner','ConstructionVehicle'] else None
  lods.append(dict(lod=lod,native_triangles=total,interchange_body_triangles=body,interchange_contour_triangles=outline,
   interchange_triangles=body+outline,material_sections_per_unit=sections,existing_or_confirmed_total_budget=cap,
   native_total_over_budget=max(0,total-cap) if cap is not None else None,
   body_budget=caps[lod][0] if caps else None,contour_budget=caps[lod][1] if caps else None,
   interchange_body_over_budget=max(0,body-caps[lod][0]) if caps else None,
   interchange_contour_over_budget=max(0,outline-caps[lod][1]) if caps else None))
 rows.append(dict(name=name,lod_count=3,lods=lods,
  budget_source='Retained selected source-tier art budget' if name in ['DefaultSoldier','BiZhiMao'] else
   'User confirmed complete vehicle middle/far budgets' if name in ['ElectromagneticMiner','ConstructionVehicle'] else
   'No asset-specific triangle cap found; preserve selected source geometry; no new budget assigned'))
(ART/'Reports/metrics.json').write_text(json.dumps(dict(success=True,units=rows),ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(rows),flush=True)

import bpy,json
from pathlib import Path
from mathutils import Vector
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/Production_B_v1')
bpy.ops.wm.open_mainfile(filepath=str(R/'SSF_B1_Atlas.blend'))
out=[]
for key in ('MilitaryFactory','AirBase','Floor','Lamp','StrategyCenter'):
 s=bpy.data.scenes['Production_'+key];bpy.context.window.scene=s
 d={'asset':key,'camera':[list(s.camera.location),s.camera.data.ortho_scale], 'objects':[]}
 for name in (key+'_LOD0_Body', 'Rig_'+key):
  ob=bpy.data.objects.get(name)
  if not ob:continue
  item={'name':name,'matrix':[list(row) for row in ob.matrix_world],'local_bounds':list(ob.dimensions),'parent':ob.parent.name if ob.parent else None}
  if ob.type=='MESH':
   dg=bpy.context.evaluated_depsgraph_get();ev=ob.evaluated_get(dg)
   pts=[ev.matrix_world@v.co for v in ev.data.vertices]
   item['evaluated_bounds']=[[min(v[i] for v in pts) for i in range(3)],[max(v[i] for v in pts) for i in range(3)]]
   item['raw_bounds']=[[min(v.co[i] for v in ob.data.vertices) for i in range(3)],[max(v.co[i] for v in ob.data.vertices) for i in range(3)]]
   item['materials']=[m.name for m in ob.data.materials]
  d['objects'].append(item)
 edit=bpy.data.collections[key+'_EDITABLE_MECHANICAL_PARTS'];p=next(o for o in edit.objects if o.get('LOD')==0)
 d['part']={'name':p.name,'matrix':[list(row) for row in p.matrix_world],'bounds':list(p.dimensions),'sample_vertex':list(p.data.vertices[0].co)}
 out.append(d)
(R/'geometry_diagnosis.json').write_text(json.dumps(out,indent=2))
print('DIAGNOSIS',json.dumps(out),flush=True)

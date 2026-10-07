import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'SSF_Production_B_v1.blend'))
d=json.loads((O/'construction_report.json').read_text(encoding='utf8'))
for a in d['assets']:
 bpy.context.window.scene=bpy.data.scenes[a['scene']]
 for e in a['lods']:
  ob=bpy.data.objects[e['body']];m=ob.data
  if a['rig'] and e['LOD']==0 and a['key']!='CloningCenter':
   names=[g.name for g in ob.vertex_groups]
   badweights=[{'index':v.index,'weights':[(names[g.group],g.weight) for g in v.groups if g.weight>1e-7]} for v in m.vertices if sum(g.weight>1e-7 for g in v.groups)>1]
   if badweights:print('SOURCE_NONRIGID',a['key'],badweights,flush=True)
  bad=[p for p in m.polygons if any(m.corner_normals[i].vector.length<.99 for i in p.loop_indices)]
  if bad:
   print(a['key'],e['LOD'],[{'polygon':p.index,'area':p.area,'normal':list(p.normal),'part':m.attributes['SSF_ComponentID'].data[p.index].value,'coords':[list(m.vertices[i].co) for i in p.vertices]} for p in bad],flush=True)

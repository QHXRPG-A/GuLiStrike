import bpy,numpy as np
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
bpy.ops.wm.open_mainfile(filepath=str(R/'Production_B_v2/ControlRigMech_B_v2_Production.blend'))
data=[]
for i in range(4):
 m=bpy.data.objects[f'ControlRigMech_LOD{i}_Body'].data;m.calc_loop_triangles()
 ids=np.array([t.loops[:] for t in m.loop_triangles]).ravel()
 data.append(np.array([n.vector[:] for n in m.corner_normals],dtype=np.float32)[ids])
bpy.ops.wm.open_mainfile(filepath=str(R/'Production_B_v3/ControlRigMech_B_v3_Production.blend'))
for i in range(4):
 m=bpy.data.objects[f'ControlRigMech_LOD{i}_Body'].data
 a=data[i];b=np.array([n.vector[:] for n in m.corner_normals],dtype=np.float32)
 d=np.linalg.norm(a-b,axis=1); oldlen=np.linalg.norm(a,axis=1);newlen=np.linalg.norm(b,axis=1)
 valid=(oldlen>.99)&(newlen>.99)
 print('NORMAL_STATS',i,'count',len(a),'old_zero',int((oldlen<.001).sum()),'new_zero',int((newlen<.001).sum()),'valid_delta_percentiles',np.percentile(d[valid],[50,99,100]).tolist(),flush=True)
 print('LENGTHS',i,'old',np.percentile(oldlen,[0,1,50,100]).tolist(),'new',np.percentile(newlen,[0,1,50,100]).tolist(),'new_nonunit',int((newlen<.99).sum()),flush=True)
 for index in np.where(newlen<.99)[0][:5]: print('NONUNIT',int(index),a[index].tolist(),b[index].tolist(),float(newlen[index]),'AREA',m.polygons[int(index)//3].area,flush=True)
 for index in np.where(d>.009)[0][:5]:
  p=m.polygons[int(index)//3]
  print('BAD',int(index),'old',a[index].tolist(),'new',b[index].tolist(),'triangle_area',p.area,'verts',list(p.vertices),flush=True)

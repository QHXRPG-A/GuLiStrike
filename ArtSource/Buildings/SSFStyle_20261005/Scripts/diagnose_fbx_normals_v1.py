import bpy,json
from pathlib import Path
from io_scene_fbx import parse_fbx
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1')
p=R/'FBX/AirBase_LOD2.fbx'
root,ver=parse_fbx.parse(str(p))
def walk(e):
 if e.id==b'Normals':
  vals=e.props[0];lens=[sum(x*x for x in vals[i:i+3])**.5 for i in range(0,len(vals),3)]
  print('RAW_NORMALS',len(lens),min(lens),sum(x<.99 for x in lens))
 for c in e.elems:walk(c)
walk(root)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(p),use_anim=False,colors_type='LINEAR')
o=next(o for o in bpy.context.scene.objects if o.type=='MESH');m=o.data
bad=[(i,list(n.vector),n.vector.length) for i,n in enumerate(m.corner_normals) if n.vector.length<.99]
print('IMPORTED_BAD',bad)
for i,n,l in bad:
 f=next(f for f in m.polygons if i in f.loop_indices)
 print('BAD_FACE',f.index,list(f.vertices),f.area,list(f.normal),'smooth',f.use_smooth)
 print('POINTS',[list(m.vertices[j].co) for j in f.vertices])

"""Rebuild confirmed circular solid caps from measured source profiles and editable 32-step screws."""
import bpy,bmesh,json,math
import numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v1_NormalRefined.blend'))
scene=bpy.context.scene; col=bpy.data.collections['EDITABLE_MECHANICAL_PARTS']
axis_col=bpy.data.collections.new('EDITABLE_CIRCULAR_PROFILE_AXES'); scene.collection.children.link(axis_col)
changes=[]

def hull(points):
    p=sorted(set(points))
    def cross(o,a,b): return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    lo=[]; hi=[]
    for x in p:
        while len(lo)>=2 and cross(lo[-2],lo[-1],x)<=1e-12: lo.pop()
        lo.append(x)
    for x in reversed(p):
        while len(hi)>=2 and cross(hi[-2],hi[-1],x)<=1e-12: hi.pop()
        hi.append(x)
    return lo[:-1]+hi[:-1]

for ob in list(col.objects):
    if ob['source_bone']=='SOURCE_FLEX': continue
    helper=bpy.data.objects[ob['source_normal_helper']]
    colors=[helper.data.materials[p.material_index].name for p in helper.data.polygons]
    if not colors or sum('SandBeige' in c for c in colors)<.99*len(colors): continue
    xyz=np.unique(np.round(np.array([v.co[:] for v in helper.data.vertices],dtype=float),6),axis=0)
    center=xyz.mean(axis=0); centered=xyz-center
    eig,axes=np.linalg.eigh(centered.T@centered/max(len(xyz),1))
    if eig[1]<=0 or eig[0]/eig[1]>.16 or eig[2]/eig[1]>1.3: continue
    a=axes[:,0]; u=axes[:,1]; v=np.cross(a,u)
    t=centered@a; r=np.sqrt((centered@u)**2+(centered@v)**2); radius=np.quantile(r,.99)
    if radius<.09: continue
    ring=r>.97*radius
    angles=np.arctan2(centered[ring]@v,centered[ring]@u)
    coverage=len(set(np.floor((angles+math.pi)/(2*math.pi)*32).astype(int)))
    if coverage<27: continue
    if np.std(r[ring])>.015*radius: continue
    bvh=BVHTree.FromPolygons([v.co for v in helper.data.vertices], [list(p.vertices) for p in helper.data.polygons])
    hit=bvh.ray_cast(Vector(center)-Vector(a)*radius*4,Vector(a),radius*8)
    if hit[0] is None or abs(hit[1].dot(Vector(a)))<.9: continue
    profile=hull([(round(float(x),5),round(float(y),5)) for x,y in zip(t,r)]+[(round(float(t.min()),5),0),(round(float(t.max()),5),0)])
    if len(profile)<3:
        outside=float(t.max())
        profile=[(outside,0),(outside,round(float(radius),5)),(outside-.003,round(float(radius),5)),(outside-.003,0)]
    if len(profile)>22: continue
    vertices=[Vector(center)+Vector(a)*x+Vector(u)*y for x,y in profile]
    mesh=bpy.data.meshes.new(ob.name+'_MeasuredCircularProfile32')
    mesh.from_pydata(vertices,[(i,(i+1)%len(vertices)) for i in range(len(vertices))],[])
    sand=next(m for m in helper.data.materials if 'SandBeige' in m.name); mesh.materials.append(sand)
    ob.data=mesh
    for n in bpy.data.objects['Armature'].data.bones.keys():
        if n not in ob.vertex_groups: ob.vertex_groups.new(name=n)
    group=ob.vertex_groups[ob['source_bone']]; group.add(list(range(len(vertices))),1.0,'REPLACE')
    for mod in list(ob.modifiers):
        if mod.type=='DECIMATE': ob.modifiers.remove(mod)
    axis=bpy.data.objects.new(ob.name+'_ProfileAxis',None); axis_col.objects.link(axis)
    rot=Matrix((Vector(u),Vector(v),Vector(a))).transposed().to_4x4(); rot.translation=Vector(center)
    axis.matrix_world=rot; axis.empty_display_size=.05; axis.hide_render=True
    screw=ob.modifiers.new('MeasuredProfile_32_RadialSegments','SCREW')
    screw.object=axis; screw.axis='Z'; screw.angle=2*math.pi
    screw.steps=screw.render_steps=32; screw.use_merge_vertices=True; screw.merge_threshold=.000002
    screw.use_smooth_shade=True; screw.use_normal_calculate=True
    ob.modifiers.move(len(ob.modifiers)-1,0)
    ob['circular_segments']=32; ob['source_measured_profile']=json.dumps(profile)
    ob['circular_source_radius_m']=float(radius)
    changes.append({'object':ob.name,'radius_m':float(radius),'source_ring_coverage_bins':coverage,
                    'segments':32,'profile':profile,'retained_solid_cap':True})

scene.render.filepath=str(O/'RegularCaps_Hero.png')
(O/'regular_caps_report.json').write_text(json.dumps(changes,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v1_RegularCaps.blend'))
bpy.ops.render.render(write_still=True)
print('REGULAR_CAPS_READY',len(changes),flush=True)

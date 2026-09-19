"""Fit added Mecha 01 rings to measured source cap surfaces, preserving bone bindings."""
import bpy,math
from mathutils import Vector,Quaternion
from mathutils.bvhtree import BVHTree
import mech_production_common as m
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.visible(['Mecha_01'])
body=m.meshes('Mecha_01')[0];tree=BVHTree.FromObject(body,bpy.context.evaluated_depsgraph_get());inv=body.matrix_world.inverted();fixed=[]
for o in m.meshes('Mecha_01'):
    if not o.name.startswith('DETAIL_') or not any(s in o.name for s in ['Hatch_','Joint_']):continue
    sign=1 if o.location.x>0 else -1
    if 'Hatch_' in o.name:center=Vector((sign*.91,-.019,3.630));outer=.302;inner=.275
    elif 'upperleg' in o.name:center=Vector((sign*.99,0,2.389));outer=.213;inner=.180
    else:center=Vector((sign*.919,.487,1.055));outer=.067;inner=.051
    o.rotation_mode='QUATERNION';o.rotation_quaternion=Quaternion();o.location=center;bpy.context.view_layer.update();oi=o.matrix_world.inverted();n=len(o.data.vertices)//4;count=0
    for idx,v in enumerate(o.data.vertices):
        row,i=divmod(idx,n);radius=outer if row<2 else inner;t=2*math.pi*i/n
        p=center+Vector((0,math.cos(t)*radius,math.sin(t)*radius))
        h=tree.ray_cast(inv@Vector((sign*4,p.y,p.z)),(inv.to_3x3()@Vector((-sign,0,0))).normalized())
        if h[0]:p.x=(body.matrix_world@h[0]).x+sign*(.002+(.009 if row in [1,2] else 0));count+=1
        v.co=oi@p
    o.data.update();fixed.append({'object':o.name,'matched':count,'vertices':len(o.data.vertices)})
m.save();result={'surface_fitting':fixed}

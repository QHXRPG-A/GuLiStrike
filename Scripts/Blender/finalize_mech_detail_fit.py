import bpy, math, json
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
import mech_production_common as m

helpers=open('D:/UE5.7/test1/Scripts/Blender/refine_mech_surfaces_and_details.py',encoding='utf-8').read().split("m.visible(['SpiderMech'")[0]
exec(compile(helpers,'detail_helpers','exec'))

# Rebuild only the added rings. Dimensions come from the original cap surfaces.
for key in ['Mecha_01','Mecha_02']:
    m.visible([key])
    for o in list(m.meshes(key)):
        if o.name.startswith('DETAIL_') and any(s in o.name for s in ['Hatch_','Joint_']):
            bpy.data.objects.remove(o,do_unlink=True)
    body=m.meshes(key)[0];tree=BVHTree.FromObject(body,bpy.context.evaluated_depsgraph_get());inv=body.matrix_world.inverted()
    for sign in [-1,1]:
        blue=key=='Mecha_01';y,z=(.17,3.56) if blue else (.001,3.597)
        hit=tree.ray_cast(inv@Vector((sign*4,y,z)),(inv.to_3x3()@Vector((-sign,0,0))).normalized())
        normal=(body.matrix_world.to_3x3().inverted().transposed()@hit[1]).normalized()
        if normal.x*sign<0:normal=-normal
        pos=body.matrix_world@hit[0]+normal*.003
        o=ring(key,'Hatch_Frame_'+str(sign),pos,.326 if blue else .347,.290 if blue else .306,.017,'metal','upperbody',segments=8 if blue else 12)
        o.rotation_mode='QUATERNION';o.rotation_quaternion=Vector((1,0,0)).rotation_difference(normal)
    for side,sign in [('l',1),('r',-1)]:
        specs=[('upperleg',1.032,0,2.389,.305,.268),('lowerleg',.921,.487,1.055,.181,.143)] if key=='Mecha_01' else [('upperleg',.951,-.0075,2.2825,.220,.181),('lowerleg',.739,.546,1.0945,.098,.072)]
        for part,x,y,z,outer,inner in specs:
            ring(key,'Joint_'+side+'_'+part,(sign*x,y,z),outer,inner,sign*.013,'blue' if key=='Mecha_01' and part=='upperleg' else 'yellow' if part=='upperleg' else 'dark',side+'_'+part,segments=12)

# The source projectile is neutral silver; dark separators express the original necks.
o=m.meshes('Missile_01')[0];a=o.data.materials[0];p=next(n for n in a.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
for socket in ['Base Color','Roughness','Metallic']:
    for link in list(p.inputs[socket].links):a.node_tree.links.remove(link)
p.inputs['Base Color'].default_value=color('A5ADB1');p.inputs['Metallic'].default_value=.5;p.inputs['Roughness'].default_value=.38
a.diffuse_color=color('A5ADB1')
dark=mat('ART_Projectile_Necks','323E43')
if dark.name not in o.data.materials:o.data.materials.append(dark)
idx=list(o.data.materials).index(dark)
for f in o.data.polygons:
    z=sum(o.data.vertices[i].co.z for i in f.vertices)/len(f.vertices)
    if 12.14<z<17.39 or 74.20<z<78.60:f.material_index=idx
o['art_finish']='Original neutral silver family, restrained metallic finish and dark neck separators'
m.visible(['Mech_Lightest']);m.save();result={'detail_fit':'matched original joint and cap dimensions','projectile_finish':'silver charcoal'}

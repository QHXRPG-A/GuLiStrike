import bpy,bmesh,math,json,sys,colorsys,collections
import numpy as np
from mathutils import Vector,Matrix,Quaternion
import mech_production_common as m

def linear(c):return c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4
def color(h):return tuple(linear(int(h[i:i+2],16)/255) for i in (0,2,4))+(1,)
PALETTE={'blue':color('60728F'),'red':color('915B56'),'gold':color('B48A3F'),'teal':color('355153'),'dark':color('283135'),'spider':color('435360'),'ochre':color('B98B35')}

def shader_paint(mat,key):
    n=mat.node_tree.nodes;l=mat.node_tree.links;p=next(x for x in n if x.type=='BSDF_PRINCIPLED');tex=n.get('SOURCE_BASE_COLOR')
    if not tex:return
    if p.inputs['Base Color'].is_linked:l.remove(p.inputs['Base Color'].links[0])
    sep=n.new('ShaderNodeSeparateColor');l.new(tex.outputs['Color'],sep.inputs[0]);r,g,b=sep.outputs[:3]
    def mathnode(op,a,c):
        q=n.new('ShaderNodeMath');q.operation=op
        if hasattr(a,'bl_rna'):l.new(a,q.inputs[0])
        else:q.inputs[0].default_value=a
        if hasattr(c,'bl_rna'):l.new(c,q.inputs[1])
        else:q.inputs[1].default_value=c
        return q.outputs[0]
    def more(a,b,ratio=1):return mathnode('GREATER_THAN',a,mathnode('MULTIPLY',b,ratio))
    def both(a,b):return mathnode('MULTIPLY',a,b)
    def paint(prev,mask,c):
        q=n.new('ShaderNodeMixRGB');q.blend_type='MIX';q.label='Clean source color region';l.new(mask,q.inputs[0]);l.new(prev,q.inputs[1]);q.inputs[2].default_value=c;return q.outputs[0]
    output=tex.outputs['Color']
    if key in ('Mecha_01','FireWeapon_01'):
        mask=both(more(b,r,1.8),mathnode('GREATER_THAN',b,.08));output=paint(output,mask,PALETTE['blue'])
    elif key=='MissileWeapon_01':
        mask=both(more(r,g,1.7),mathnode('GREATER_THAN',r,.04));output=paint(output,mask,PALETTE['red'])
    elif key=='Mecha_02':
        mask=both(more(g,b,2),mathnode('GREATER_THAN',r,.2));output=paint(output,mask,color('D3A842'))
    elif key=='SpiderMech':
        mask=both(more(b,r,1.15),mathnode('GREATER_THAN',b,.005));output=paint(output,mask,PALETTE['spider'])
        gold=both(more(r,g,1.1),more(g,b,1.4));output=paint(output,gold,PALETTE['ochre'])
    l.new(output,p.inputs['Base Color'])
    # Same PBR lighting model; paint/finish values are editable art parameters.
    if key in ('Mecha_01','FireWeapon_01'):
        p.inputs['Roughness'].default_value=.48
    for q in n:
        if q.type=='NORMAL_MAP':q.inputs['Strength'].default_value=.12 if key=='SpiderMech' else .3

def weld_mesh(o):
    bm=bmesh.new();bm.from_mesh(o.data);d=bm.verts.layers.deform.active
    pairs=bmesh.ops.find_doubles(bm,verts=list(bm.verts),dist=.0001)['targetmap']
    def same(a,b):return not d or (set(a[d].keys())==set(b[d].keys()) and all(abs(a[d][i]-b[d][i])<.0001 for i in a[d].keys()))
    bmesh.ops.weld_verts(bm,targetmap={a:b for a,b in pairs.items() if same(a,b)})
    bmesh.ops.dissolve_limit(bm,angle_limit=.001,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL','UV'})
    bm.to_mesh(o.data);bm.free();o.data.update()

def bevel(o,width=.65):
    b=o.modifiers.new('Machined_Edge_Chamfer','BEVEL');b.width=width;b.segments=1;b.limit_method='ANGLE';b.angle_limit=math.radians(35);b.harden_normals=True
    o.modifiers.move(len(o.modifiers)-1,0)
    for f in o.data.polygons:f.use_smooth=True
    if hasattr(o.data,'set_sharp_from_angle'):o.data.set_sharp_from_angle(angle=math.radians(45))
    w=o.modifiers.new('Panel_Area_Normals','WEIGHTED_NORMAL');w.keep_sharp=True;w.weight=50
    o.modifiers.move(len(o.modifiers)-1,1)

def clean_light_paint(o):
    """Use the source UV regions to assign clean editable face paint, not new topology."""
    mat=o.data.materials[0];tex=mat.node_tree.nodes.get('SOURCE_BASE_COLOR');im=tex.image
    pix=np.empty(len(im.pixels),dtype=np.float32);im.pixels.foreach_get(pix);pix=pix.reshape((im.size[1],im.size[0],4))
    uv=o.data.uv_layers[0].data
    attr=o.data.color_attributes.get('SourceRegion_CleanPaint') or o.data.color_attributes.new(name='SourceRegion_CleanPaint',type='FLOAT_COLOR',domain='CORNER')
    for f in o.data.polygons:
        coords=np.array([uv[i].uv[:] for i in f.loop_indices]);center=coords.mean(axis=0)
        samples=[]
        for u,v in [center,*[center*.65+q*.35 for q in coords]]:
            samples.append(pix[int((v%1)*(im.size[1]-1)),int((u%1)*(im.size[0]-1)),:3])
        rgb=np.median(samples,axis=0);r,g,b=rgb
        if r>g*1.12 and g>b*1.12 and r>.025:c=PALETTE['gold']
        elif g>r*1.1 and b>r*1.05:c=PALETTE['teal']
        elif max(rgb)>.4 and max(rgb)-min(rgb)<.08:c=color('B4B8B2')
        else:c=PALETTE['dark']
        for i in f.loop_indices:attr.data[i].color=c
    n=mat.node_tree.nodes;l=mat.node_tree.links;p=next(x for x in n if x.type=='BSDF_PRINCIPLED')
    for link in list(p.inputs['Base Color'].links):l.remove(link)
    a=n.new('ShaderNodeVertexColor');a.layer_name=attr.name;l.new(a.outputs['Color'],p.inputs['Base Color'])
    for socket,value in [('Roughness',.58),('Metallic',.25)]:
        for link in list(p.inputs[socket].links):l.remove(link)
        p.inputs[socket].default_value=value
    for link in list(p.inputs['Normal'].links):l.remove(link)
    o['paint_method']='Clean face palette derived from original UV texture regions; original UVs retained'

def restore_root(key):
    rows=m.SNAP['meshes'][key].get('bones',[])
    rigs=[a for a in m.objects(key) if a.type=='ARMATURE']
    if not rigs or not rows:return
    arm=rigs[0];missing=[r for r in rows if r['name'] not in arm.data.bones]
    if len(missing)!=1:return
    root=missing[0]
    if root['parent']:return
    m.active(arm);bpy.ops.object.mode_set(mode='EDIT')
    bone=arm.data.edit_bones.new(root['name']);loc=root['global']['location'];bone.head=(loc[0],-loc[1],loc[2]);bone.tail=bone.head+Vector((0,0,10));bone.use_deform=True
    for b in arm.data.edit_bones:
        if b!=bone and not b.parent:b.parent=bone
    bpy.ops.object.mode_set(mode='OBJECT');arm['restored_ue_root']=root['name']

bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1']
m.visible([k for k in m.SNAP['meshes'] if k!='SpiderMech'])
for key in m.SNAP['meshes']:
    if key=='SpiderMech':continue
    for o in m.meshes(key):
        if o.get('style_processed'):continue
        if key in ('Cockpit_Jet','Mech_Legs_Lt','HalfShoulder_Box','Machinegun_lvl1'):clean_light_paint(o)
        else:
            for slot in o.material_slots:shader_paint(slot.material,key)
        weld_mesh(o);bevel(o,width=.5 if key in ('FireWeapon_01','Missile_01','Machinegun_lvl1') else .75)
        o['style_processed']=True
    restore_root(key)
m.save()
result={'processed':[k for k in m.SNAP['meshes'] if k!='SpiderMech'],'saved':bpy.data.filepath}

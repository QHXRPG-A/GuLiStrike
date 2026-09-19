"""Symmetric paint accents on the unchanged v3 body, ink and cel materials."""
import bpy,json,hashlib,math
import numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v4_Accents';OUT.mkdir(exist_ok=True);(OUT/'Previews').mkdir(exist_ok=True)
base_report=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())
report={'success':False,'source':bpy.data.filepath,'assets':{},'palette_srgb':{'red':'B5544D','blue':'587F9B','white':'E3E6E0'},'symmetry':'Paint masks use absolute rest-local X; patterns travel with skinned vertices.'}
def linear(h):
    c=[int(h[i:i+2],16)/255 for i in (0,2,4)];return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c)+(1,)
def digest(me):
    h=hashlib.sha256()
    for data,prop,n,dtype in [(me.vertices,'co',len(me.vertices)*3,np.float32),(me.loops,'vertex_index',len(me.loops),np.int32)]:
        a=np.empty(n,dtype);data.foreach_get(prop,a);h.update(a.tobytes())
    return h.hexdigest()
def accent(o,kind):
    me=o.data;before=digest(me);co=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',co)
    attr=me.attributes.new(name='StyleRestPosition',type='FLOAT_VECTOR',domain='POINT');attr.data.foreach_set('vector',co)
    for old in list(me.materials):
        mat=old.copy();mat.name='ACCENT4_'+old.name
        me.materials[list(me.materials).index(old)]=mat
        n=mat.node_tree.nodes;l=mat.node_tree.links
        shade=n['Paint_x_Three_Tones'];paint=shade.inputs[1].links[0].from_socket if shade.inputs[1].is_linked else None
        if paint is None:
            rgb=n.new('ShaderNodeRGB');rgb.outputs[0].default_value=shade.inputs[1].default_value;paint=rgb.outputs[0]
        pos=n.new('ShaderNodeAttribute');pos.attribute_name='StyleRestPosition';pos.name='Rest_Local_Paint_Coordinates'
        sep=n.new('ShaderNodeSeparateXYZ');l.new(pos.outputs['Vector'],sep.inputs[0])
        def op(name,a,b=None):
            node=n.new('ShaderNodeMath');node.operation=name
            for idx,val in enumerate((a,b) if b is not None else (a,)):
                if isinstance(val,(int,float)):node.inputs[idx].default_value=val
                else:l.new(val,node.inputs[idx])
            return node.outputs[0]
        x=op('ABSOLUTE',sep.outputs['X']);y=sep.outputs['Y'];z=sep.outputs['Z']
        def between(v,lo,hi):return op('MULTIPLY',op('GREATER_THAN',v,lo),op('LESS_THAN',v,hi))
        def allof(*terms):
            value=terms[0]
            for term in terms[1:]:value=op('MULTIPLY',value,term)
            return value
        def anyof(*terms):
            value=terms[0]
            for term in terms[1:]:value=op('MAXIMUM',value,term)
            return value
        rgb=n.new('ShaderNodeSeparateColor');l.new(paint,rgb.inputs[0]);r,g,b=rgb.outputs[:3]
        gold=allof(op('GREATER_THAN',r,op('MULTIPLY',g,1.13)),op('GREATER_THAN',g,op('MULTIPLY',b,1.25)))
        if kind=='spider':
            armor=op('GREATER_THAN',b,op('MULTIPLY',r,1.12))
            shoulders=allof(between(x,142,164),op('GREATER_THAN',z,218))
            cheeks=allof(between(x,27,40),between(z,177,236),op('LESS_THAN',y,-137))
            tail=allof(op('LESS_THAN',x,7),between(y,148,205),op('GREATER_THAN',z,272))
            layers=[('Red paired shoulder bands and cheeks',allof(armor,anyof(shoulders,cheeks,tail)),'red')]
        elif kind=='legs':
            panels=allof(op('GREATER_THAN',x,48),between(z,205,252))
            white=allof(op('GREATER_THAN',x,48),between(z,226,233))
            layers=[('Blue paired thigh panels',allof(gold,panels),'blue'),('White paired thigh stripe',allof(gold,white),'white')]
        elif kind=='rear':
            panels=allof(op('GREATER_THAN',x,29),op('GREATER_THAN',y,35),between(z,15,95))
            stripe=allof(panels,between(z,31,38))
            layers=[('Blue paired rear panels',allof(gold,panels),'blue'),('White paired rear stripe',allof(gold,stripe),'white')]
        else:
            plate=allof(op('LESS_THAN',y,18),between(z,50,99))
            layers=[('Blue paired front armor stripes',allof(plate,between(x,9,22)),'blue'),('White paired front armor borders',allof(plate,between(x,22,27)),'white')]
        # Paint belongs to armor; preserve the original amber emitter pixels.
        p=next(node for node in n if node.type=='BSDF_PRINCIPLED');em=p.inputs['Emission Color']
        emitter_free=None
        if em.is_linked and p.inputs['Emission Strength'].default_value>0:
            channels=n.new('ShaderNodeSeparateColor');l.new(em.links[0].from_socket,channels.inputs[0])
            emitter_free=op('LESS_THAN',op('ADD',op('ADD',channels.outputs[0],channels.outputs[1]),channels.outputs[2]),.03)
        for name,mask,color in layers:
            if emitter_free:mask=allof(mask,emitter_free)
            mix=n.new('ShaderNodeMixRGB');mix.name=name;l.new(mask,mix.inputs[0]);l.new(paint,mix.inputs[1]);mix.inputs[2].default_value=linear(report['palette_srgb'][color]);paint=mix.outputs[0]
        l.new(paint,shade.inputs[1]);mat['accent_version']='4 symmetric rest-coordinate paint; existing ink/cel output unchanged'
    assert digest(me)==before
    report['assets'][o.name]={'geometry_unchanged':True,'geometry_hash':before,'pattern':kind,'symmetry_axis':'local X','method':'analytic ABS(X); no facewise approximation'}

accent(bpy.data.objects['WORK_SpiderMech__SpiderMech.002'],'spider')
accent(bpy.data.objects['WORK_Mech_Lightest__Mech_Legs_Lt.001'],'legs')
accent(bpy.data.objects['WORK_Mech_Lightest__Cockpit_Jet'],'rear')
accent(bpy.data.objects['WORK_Mech_Lightest__Sealed_Armor_Fairing'],'front')

def render(key,view):
    scene=bpy.data.scenes['Review_'+key];bpy.context.window.scene=scene;bpy.context.view_layer.update();cam=scene.camera
    bounds=base_report['assets'][key]['bounds'];lo=Vector(bounds['min']);hi=Vector(bounds['max']);center=(lo+hi)/2;span=max(hi-lo)
    direction={'Hero':Vector((1.5,-2,1.05)),'Front':Vector((0,-1,0)),'Side':Vector((1,0,0)),'Rear':Vector((0,1,0))}[view].normalized()
    cam.location=center+direction*span*3;rot=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rot.to_euler()
    pts=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)];right=rot@Vector((1,0,0));up=rot@Vector((0,1,0))
    w=max(p.dot(right) for p in pts)-min(p.dot(right) for p in pts);h=max(p.dot(up) for p in pts)-min(p.dot(up) for p in pts)
    cam.data.ortho_scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
    scene.render.filepath=str(OUT/'Previews'/(key+'_'+view+'.png'));bpy.ops.render.render(write_still=True)

for key in ('Mech_Lightest','SpiderMech'):
    for view in ('Hero','Front','Side','Rear'):render(key,view)
    changed=[]
    for o in bpy.data.collections['WORK_'+key].objects:
        if o.type!='MESH' or o.get('ink_layer'):continue
        for mat in o.data.materials:
            for e in mat.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp.elements:
                changed.append((e,e.color[:]));e.color=(1,1,1,1)
    scene=bpy.context.scene;scene.render.filepath=str(OUT/'Previews'/(key+'_Rear_Paint.png'));bpy.ops.render.render(write_still=True)
    for e,c in changed:e.color=c
bpy.context.window.scene=bpy.data.scenes['Review_Mech_Lightest']
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_SymmetricAccents_v4.blend'),check_existing=False)
report['success']=True
(OUT/'accent_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('SYMMETRIC_ACCENTS_READY',flush=True)

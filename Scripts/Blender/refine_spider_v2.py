"""Spider-only iteration; reads v1 and always writes a separate candidate."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector,Quaternion
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
OUT=m.ROOT/'Production_v2_Spider';OUT.mkdir(exist_ok=True);m.OUT=OUT
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.stage();m.visible(['SpiderMech'])
scene=bpy.context.scene;scene.cycles.samples=24
o=m.meshes('SpiderMech')[0]
o.data=o.data.copy()
def srgb(h):
    a=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in a)+(1,)
for i,old in enumerate(o.data.materials):
    mat=old.copy();mat.name='Spider_v2_'+old.name;o.data.materials[i]=mat
    n=mat.node_tree.nodes;l=mat.node_tree.links
    tex=n.get('SOURCE_BASE_COLOR');p=next(x for x in n if x.type=='BSDF_PRINCIPLED')
    for socket in ('Base Color','Normal'):
        for link in list(p.inputs[socket].links):l.remove(link)
    sep=n.new('ShaderNodeSeparateColor');l.new(tex.outputs['Color'],sep.inputs[0]);r,g,b=sep.outputs[:3]
    def calc(op,a,c):
        q=n.new('ShaderNodeMath');q.operation=op
        if isinstance(a,(int,float)):q.inputs[0].default_value=a
        else:l.new(a,q.inputs[0])
        if isinstance(c,(int,float)):q.inputs[1].default_value=c
        else:l.new(c,q.inputs[1])
        return q.outputs[0]
    blue=calc('MULTIPLY',calc('GREATER_THAN',b,calc('MULTIPLY',r,1.12)),calc('GREATER_THAN',b,.004))
    gold=calc('MULTIPLY',calc('MULTIPLY',calc('GREATER_THAN',r,calc('MULTIPLY',g,1.13)),calc('GREATER_THAN',g,calc('MULTIPLY',b,1.25))),calc('GREATER_THAN',r,.012))
    q=n.new('ShaderNodeMixRGB');q.inputs[1].default_value=srgb('283135');q.inputs[2].default_value=srgb('435360');l.new(blue,q.inputs[0])
    q2=n.new('ShaderNodeMixRGB');l.new(q.outputs[0],q2.inputs[1]);q2.inputs[2].default_value=srgb('B98B35');l.new(gold,q2.inputs[0]);l.new(q2.outputs[0],p.inputs['Base Color'])
    p.inputs['Metallic'].default_value=.18;p.inputs['Roughness'].default_value=.58
    mat['color_method']='Continuous source UV color regions; no per-triangle color quantization'
for f in o.data.polygons:f.use_smooth=True
o.data.set_sharp_from_angle(angle=math.radians(42))
for mod in list(o.modifiers):
    if mod.type=='WEIGHTED_NORMAL':o.modifiers.remove(mod)
normal=o.modifiers.new('Panel_Area_Normals','WEIGHTED_NORMAL');normal.keep_sharp=True;normal.weight=50
o.modifiers.move(len(o.modifiers)-1,0)
scene.render.resolution_x=1200;scene.render.resolution_y=1050
cam=scene.camera;center=Vector((-.381766,.508270,1.572287));q=Quaternion((.64234668,.72117835,.19371103,.17253549))
cam.location=center+q@Vector((0,0,4.980691));cam.rotation_euler=q.to_euler();cam.data.type='PERSP';cam.data.lens=45
scene.render.filepath=str(OUT/'MaterialNormal_Candidate_Close.png');bpy.ops.render.render(write_still=True)
m.render(['SpiderMech'],'MaterialNormal_Candidate','Hero')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Spider_MaterialNormal_Candidate.blend'),check_existing=False)

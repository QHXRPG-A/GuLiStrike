"""Author the approved RPF asset in Blender. Run in an isolated Blender process.

Only owns GS_RPF_* scenes and ArtSource/Buildings/ResourceProcessingFactory.
Editable construction, export geometry, collision, actions and studio are separate.
"""
from pathlib import Path
import json
import math
import shutil
import argparse
import sys
import bpy
import bmesh
import numpy as np
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1')
ASSET = ROOT/'ArtSource/Buildings/ResourceProcessingFactory'
OUT = ROOT/'outputs/resource-processing-factory-20260909'
HINGE = (11.5, 0, 15.15)
BODY, DOOR, COLLISION = [], [], []
MATERIALS = []
CAMO = (.83, .90, .98, 1)
DARK = (.20, .25, .30, 0)
PANEL = (.51, .58, .65, 0)
YELLOW = (.92, .57, .105, 0)
WHITE = (.72, .80, .84, 0)
BLACK = (.022, .034, .048, 0)
BLUE = (.01, .36, 1.0, 1)
GREEN = (.015, .9, .25, .8)
RED = (1.0, .025, .009, .75)


def log(s):
    print('RPF: '+s, flush=True)


def image_file(name, a, data=False):
    h,w = a.shape[:2]
    im=bpy.data.images.new(name,width=w,height=h,alpha=False)
    im.colorspace_settings.name='Non-Color' if data else 'sRGB'
    rgba=np.ones((h,w,4),np.float32)
    rgba[:,:,:3]=np.clip(a[:,:,None] if a.ndim==2 else a,0,1)
    im.pixels.foreach_set(rgba.ravel())
    im.filepath_raw=str(ASSET/'Textures'/(name+'.png'))
    im.file_format='PNG'; im.save()
    return im


def texture_set(label,n,interior=False):
    rng=np.random.default_rng(907 if interior else 903)
    yy,xx=np.indices((n,n),dtype=np.int32)
    grid=rng.random((12,12)).astype(np.float32)
    blocks=grid[(yy*12//n)%12,(xx*12//n)%12]
    large=rng.random((4,4)).astype(np.float32)[yy*4//n,xx*4//n]
    grain=rng.random((n,n),dtype=np.float32)-.5
    digital=np.select([blocks*.7+large*.3<.37,blocks*.7+large*.3>.67],[.24,.34],.29).astype(np.float32)
    color=(.37+.006*grain) if interior else digital+.003*grain
    pitch=n//4
    dx=np.minimum(xx%pitch,pitch-1-xx%pitch)
    dy=np.minimum(yy%pitch,pitch-1-yy%pitch)
    seams=(dx<3)|(dy<3)
    rims=((dx>=3)&(dx<6))|((dy>=3)&(dy<6))
    color=color-seams*.14+rims*.045
    # Small panel screws and short inset slots repeat with the trim tile.
    screw=(((xx%pitch-17)**2+(yy%pitch-17)**2)<24)|(((xx%pitch-pitch+18)**2+(yy%pitch-pitch+18)**2)<24)
    color-=screw*.20
    line=((yy%pitch>pitch*.77)&(yy%pitch<pitch*.78)&(xx%pitch>pitch*.3)&(xx%pitch<pitch*.82))
    color-=line*.11
    rgb=np.stack((color*.94,color*1.0,color*1.055),axis=2)
    base=image_file('T_RPF_'+label+'_BaseColor',rgb)
    height=seams*(-.002)+rims*.0005+screw*(-.003)+grain*.000002
    gx=(np.roll(height,-1,1)-np.roll(height,1,1))*n/8
    gy=(np.roll(height,-1,0)-np.roll(height,1,0))*n/8
    nn=np.stack((-gx,-gy,np.ones_like(gx)),axis=2); nn/=np.linalg.norm(nn,axis=2,keepdims=True)
    normal=image_file('T_RPF_'+label+'_NormalGL',nn*.5+.5,True)
    nn[:,:,1]*=-1
    image_file('T_RPF_'+label+'_NormalDX',nn*.5+.5,True)
    orm=np.stack((np.where(seams|screw,.68,1),np.clip(.53+grain*.08+seams*.12,0,1),np.full_like(color,.65 if interior else .72)),axis=2)
    occlusion=image_file('T_RPF_'+label+'_ORM',orm,True)
    image_file('T_RPF_'+label+'_EmissiveMask',np.zeros((n,n),np.float32),True)
    mat=bpy.data.materials.new('M_RPF_'+label); mat.use_nodes=True
    nodes=mat.node_tree.nodes; links=mat.node_tree.links
    bsdf=nodes.get('Principled BSDF')
    v=nodes.new('ShaderNodeVertexColor');v.layer_name='Color'
    tint=nodes.new('ShaderNodeMixRGB'); tint.blend_type='MULTIPLY';tint.inputs[0].default_value=1
    tex=nodes.new('ShaderNodeTexImage');tex.image=base;tex.interpolation='Linear'
    if not interior:
        paint=nodes.new('ShaderNodeMixRGB');paint.blend_type='MIX';paint.inputs[1].default_value=(.27,.286,.306,1)
        links.new(v.outputs['Alpha'],paint.inputs[0]);links.new(tex.outputs['Color'],paint.inputs[2]);links.new(paint.outputs[0],tint.inputs[1])
    else:links.new(tex.outputs['Color'],tint.inputs[1])
    links.new(v.outputs['Color'],tint.inputs[2]); links.new(tint.outputs[0],bsdf.inputs['Base Color'])
    tex=nodes.new('ShaderNodeTexImage');tex.image=normal
    nm=nodes.new('ShaderNodeNormalMap');links.new(tex.outputs['Color'],nm.inputs['Color']);links.new(nm.outputs[0],bsdf.inputs['Normal'])
    tex=nodes.new('ShaderNodeTexImage');tex.image=occlusion
    sep=nodes.new('ShaderNodeSeparateColor');links.new(tex.outputs[0],sep.inputs[0]);links.new(sep.outputs[1],bsdf.inputs['Roughness']);links.new(sep.outputs[2],bsdf.inputs['Metallic'])
    mat.diffuse_color=(.30,.34,.40,1)
    return mat


def make_materials():
    MATERIALS.append(texture_set('Armor',4096))
    MATERIALS.append(texture_set('Interior',2048,True))
    m=bpy.data.materials.new('M_RPF_Details'); m.use_nodes=True
    ns=m.node_tree.nodes; ls=m.node_tree.links; p=ns.get('Principled BSDF')
    v=ns.new('ShaderNodeVertexColor');v.layer_name='Color'
    ls.new(v.outputs['Color'],p.inputs['Base Color']);ls.new(v.outputs['Color'],p.inputs['Emission Color'])
    mul=ns.new('ShaderNodeMath');mul.operation='MULTIPLY';mul.inputs[1].default_value=5
    ls.new(v.outputs['Alpha'],mul.inputs[0]);ls.new(mul.outputs[0],p.inputs['Emission Strength'])
    p.inputs['Metallic'].default_value=.45;p.inputs['Roughness'].default_value=.4
    MATERIALS.append(m)


def collection(name,scene):
    c=bpy.data.collections.new(name);scene.collection.children.link(c);return c


def decorate_mesh(obj,mat=0,color=CAMO,bevel=.05):
    mesh=obj.data;mesh.materials.append(MATERIALS[mat])
    colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for c in colors.data:c.color=color
    uv=mesh.uv_layers.new(name='UV0_Surface4m')
    for p in mesh.polygons:
        axis=max(range(3),key=lambda i:abs(p.normal[i]))
        axes=[i for i in range(3) if i!=axis]
        for li in p.loop_indices:
            co=mesh.vertices[mesh.loops[li].vertex_index].co
            uv.data[li].uv=(co[axes[0]]/4,co[axes[1]]/4)
        p.use_smooth=False
    if bevel:
        b=obj.modifiers.new('Manufactured edge bevel','BEVEL');b.width=bevel;b.segments=2
        b.affect='EDGES'
        b=obj.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL');b.keep_sharp=True;b.weight=35


def mesh_object(name,verts,faces,mat=0,color=CAMO,bevel=.06,moving=False,collision=False):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    ob=bpy.data.objects.new(name,mesh)
    (COL_C if collision else COL_D if moving else COL_B).objects.link(ob)
    if collision:
        COLLISION.append(ob);ob.display_type='WIRE';ob.hide_render=True
    else:
        decorate_mesh(ob,mat,color,bevel)
        if moving:
            for mod in ob.modifiers:
                if mod.type=='BEVEL':mod.segments=1
        (DOOR if moving else BODY).append(ob)
    return ob


def box(name,loc,size,mat=0,color=CAMO,bevel=.06,moving=False,collision=False):
    x,y,z=loc;a,b,c=[s/2 for s in size]
    verts=[(x+u*a,y+v*b,z+w*c) for u,v,w in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    return mesh_object(name,verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],mat,color,bevel,moving,collision)


def prism(name,poly,lo,hi,axis='Y',mat=0,color=CAMO,bevel=.08,moving=False,collision=False):
    def co(p,t):
        return (p[0],t,p[1]) if axis=='Y' else (p[0],p[1],t) if axis=='Z' else (t,p[0],p[1])
    n=len(poly);vs=[co(p,t) for t in (lo,hi) for p in poly]
    fs=[tuple(range(n-1,-1,-1)),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh_object(name,vs,fs,mat,color,bevel,moving,collision)


def cylinder_between(name,a,b,r=.12,mat=0,color=PANEL,moving=False,segments=12):
    a,b=Vector(a),Vector(b);direction=(b-a).normalized()
    u=direction.cross(Vector((0,0,1)))
    if u.length<.01:u=direction.cross(Vector((0,1,0)))
    u.normalize();v=direction.cross(u)
    vs=[tuple(p+r*(math.cos(i*2*math.pi/segments)*u+math.sin(i*2*math.pi/segments)*v)) for p in (a,b) for i in range(segments)]
    fs=[tuple(range(segments-1,-1,-1)),tuple(range(segments,2*segments))]+[(i,(i+1)%segments,(i+1)%segments+segments,i+segments) for i in range(segments)]
    return mesh_object(name,vs,fs,mat,color,.015,moving)


def text_label(name,body,loc,size,rotation=(0,0,0),color=WHITE,moving=False):
    curve=bpy.data.curves.new(name,'FONT');curve.body=body;curve.size=size;curve.align_x='CENTER';curve.extrude=.002;curve.space_character=1.1
    ob=bpy.data.objects.new(name,curve);(COL_D if moving else COL_B).objects.link(ob)
    ob.location=loc;ob.rotation_euler=rotation
    bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob
    bpy.ops.object.convert(target='MESH');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    decorate_mesh(ob,2,color,0);(DOOR if moving else BODY).append(ob)
    return ob


def bolt(name,loc,axis='X',moving=False):
    delta=Vector((.10,0,0) if axis=='X' else (0,.10,0) if axis=='Y' else (0,0,.10))
    cylinder_between(name,Vector(loc)-delta,Vector(loc)+delta,.095,0,PANEL,moving,6)


def build_architecture():
    outline=[(-24,-27),(-21,-30),(21,-30),(24,-27),(24,27),(21,30),(-21,30),(-24,27)]
    prism('Chamfered foundation',outline,0,.65,'Z',color=DARK)
    box('Interior structural floor',(-4.7,0,.9),(34,36, .5),1,WHITE)
    # 36m wide threshold, ramp and two inset vehicle guide rails.
    prism('Vehicle approach ramp',[(10, .15),(24,.15),(24,.69),(12,1.2),(10,1.2)],-18,18,'Y',1,WHITE,.02)
    for y in [-12,-4,4,12]:
        prism('Apron deck tile',[(13.1,1.15),(22.9,.73),(22.9,.77),(13.1,1.19)],y-3.85,y+3.85,'Y',0,PANEL,.015)
    for y in [-8,8]:
        prism('Apron rail channel',[(12,1.22),(23.6,.73),(23.6,.76),(12,1.25)],y-.32,y+.32,'Y',0,DARK,.01)
        for x in np.arange(12.0,23.2,.52):box('Apron rail cross tie',(float(x),y,1.26-(float(x)-12)*.51/12),(.10,.7,.04),0,PANEL,.007)
    for y in np.arange(-17.5,18,1.15):
        box('Threshold hazard stripe',(23.4,float(y),.743),(.72,.54,.022),2,YELLOW,.005)
    # Interior: unobstructed width 36m; split collision never bridges the bay.
    box('Rear structural wall',(-22,0,9.5),(1.2,36,18),1,WHITE)
    box('Structural roof',(-5,0,18.7),(34,38,1.2),0,DARK)
    for side in [-1,1]:
        wing=[(-23,.65),(21,.65),(21,16.1),(16.5,17.8),(9.0,21.2),(-18.5,21.2),(-23,18.5)]
        prism('Port armor spine' if side<0 else 'Starboard armor spine',wing,side*21-3,side*21+3,'Y',color=CAMO,bevel=.18)
        for x in [-17,-12,-7,-2,3,7]:
            box('Spine top armor segment',(x,side*21,21.30),(3.75,5.5,.19),0,CAMO,.06)
            box('Spine top inset rail',(x,side*21,21.43),(2.85,.75,.12),0,DARK,.045)
            for yoff in [-2.2,2.2]:bolt('Spine top captive bolt',(x,side*21+yoff,21.46),'Z')
        for x in [10.3,12.9,15.5]:
            zz=21.2-(x-9)*3.4/7.5
            prism('Sloping forward armor plate',[(x-1,zz+.47),(x+1,zz-.43),(x+1,zz-.26),(x-1,zz+.64)],side*21-2.65,side*21+2.65,'Y',0,CAMO,.035)
        # Outer shoulder cassettes, dark inset groove and raised armor covers.
        box('Shoulder service recess',(-5,side*24.07,16),(27,.18,4.2),0,DARK)
        for x in [-17,-12,-7,-2,3,8]:
            box('Shoulder armor cassette',(x,side*24.22,18),(4.7,.4,3.0),0,CAMO,.12)
            for dx in [-1.9,1.9]:bolt('Shoulder fastener',(x+dx,side*24.47,18.9),'Y')
            for dx in np.arange(-1.4,1.5,.35):box('Shoulder vent',(x+float(dx),side*24.45,17.85),(.13,.08,.72),0,DARK,.018)
        # Strong forward armored nose and yellow edge/marker bands.
        box('Nose vertical inset',(21.13,side*21,8.8),(.25,5.25,12.5),0,DARK,.12)
        for z in [4.4,6.0,7.6,9.2,10.8,12.4,14.0]:box('Nose grille bar',(21.35,side*21,z),(.32,5.12,.43),0,PANEL,.06)
        box('Nose yellow edge',(21.34,side*23.4,9.2),(.19,.2,14.5),2,YELLOW,.012)
        for z in [3.2,4.0]:box('Nose safety band',(21.5,side*21,z),(.24,5.25,.42),2,YELLOW,.015)
        # Outboard processing equipment block, chamfered casing.
        side_poly=[(-17,.8),(9,.8),(9,9.3),(6.7,11.2),(-15.2,11.2),(-17,8.4)]
        prism('Auxiliary processing housing',side_poly,side*27-2.6,side*27+2.6,'Y',color=DARK,bevel=.18)
        for x in [-12,-4,4]:
            box('Equipment lid',(x,side*27,11.3),(7.6,4.85,.3),0,PANEL,.12)
            box('Equipment fascia',(x,side*29.65,6.8),(7.55,.4,7.4),0,CAMO,.1)
            box('Equipment access inset',(x,side*29.9,7),(6.35,.14,4.9),0,DARK,.04)
            for dx in [-2,2]:box('Cabinet stiffener',(x+dx,side*29.93,7),(.20,.08,3.6),0,PANEL,.015)
            box('Status console',(x,side*29.91,5.8),(2.2,.12,1.4),0,PANEL,.05)
            box('Green readout',(x,side*29.975,5.8),(1.35,.04,.18),2,GREEN,.01)
            for z in [7.6,8.15]:box('Console vent line',(x,side*29.975,z),(3.1,.04,.08),0,PANEL,.008)
        # Side and front barriers.
        for x in [-19,-15,-11,-7,-3,1,5,9,13,17]:
            prism('Outboard crash barrier',[(side*27.6,.65),(side*29.8,.65),(side*29.45,2.1),(side*28.9,2.7),(side*28.2,2.7)],x-1.8,x+1.8,'X',0,PANEL,.08)
            box('Outboard orange reflector',(x,side*29.43,2.15),(3.3,.055,.10),2,YELLOW,.005)
        for yoff in [-2,0,2]:
            yy=side*22+yoff
            prism('Forward crash barrier',[(21,.65),(23,.65),(22.8,2.1),(22.1,2.7),(21.5,2.7)],yy-.86,yy+.86,'Y',0,PANEL,.07)
            box('Forward barrier reflector',(22.82,yy,2.15),(.04,1.5,.12),2,YELLOW,.005)
        # Inner wall lining and service columns, all behind the doorway.
        box('Inner wall lining',(-5,side*17.86,8.25),(33,.20,14.1),1,WHITE,.03)
        for x in [-19,-14,-9,-4,1,6]:
            box('Interior service pillar',(x,side*17.50,7.7),(.70,.65,12.9),0,PANEL,.06)
            box('Interior dark panel',(x+2.3,side*17.62,8.2),(3.6,.22,7.8),1,WHITE,.09)
            box('Interior blue luminaire',(x,side*17.13,10.0),(.38,.15,1.35),2,BLUE,.035)
            cylinder_between('Conduit',(x-.5,side*17.35,2),(x-.5,side*17.35,14),.12,0,DARK)
        # Header warning lights illuminate the forward apron.
        for x in [-15,-8,-1,6]:box('Amber outer running light',(x,side*24.48,15.55),(1.1,.06,.14),2,(1,.24,.01,.5),.01)
    # Detailed roof skin surrounding a raised central machine room.
    for x in [-18,-12,-6,0,6]:
        for y in [-14,-7,0,7,14]:box('Roof armored tile',(x,y,19.47),(5.77,6.72,.35),0,PANEL,.09)
    roofpoly=[(-15,-10),(-13,-12),(2,-12),(4,-10),(4,10),(2,12),(-13,12),(-15,10)]
    prism('Raised central roof housing',roofpoly,19.65,21.6,'Z',0,CAMO,.12)
    prism('Central roof cap',roofpoly,21.6,21.8,'Z',0,DARK,.06)
    for x in [-11,-5,1]:
        for y in [-7.5,0,7.5]:box('Upper roof hatch',(x,y,21.9),(5.45,6.9,.20),0,PANEL,.045)
    # Roof grilles, ribs and tiedowns.
    for y in [-15.8,15.8]:
        box('Long roof vent well',(-5,y,19.75),(29,2.1,.25),0,DARK,.05)
        for x in np.arange(-19,9.3,.46):box('Roof cooling fin',(float(x),y,20.02),(.15,1.85,.30),0,PANEL,.025)
    for y in [-12.3,12.3]:
        for x in [-12,1]:
            box('Roof clamp bracket',(x,y,20.8),(2.1,1.2,1.9),0,DARK,.16)
            box('Roof red release',(x+.55,y,21.47),(.85,1.22,.18),2,(.60,.035,.02,0),.03)
            box('Roof navigation lamp',(x,y,21.86),(.8,.66,.12),2,BLUE,.02)
    box('Front lintel armored beam',(10.65,0,17.45),(2.0,36,4.3),0,PANEL,.14)
    for y in np.arange(-16,17,4):
        box('Lintel lock pocket',(11.73,float(y),17.15),(.23,3.25,1.85),0,DARK,.04)
        cylinder_between('Door hinge sleeve',(11.6,float(y)-1.45,15.2),(11.6,float(y)+1.45,15.2),.38,0,PANEL)
    for y in np.arange(-15,16,3):
        box('Lintel ventilation blade',(11.91,float(y),18.65),(.1,2.2,.10),0,DARK,.018)
    text_label('Roof building designation','R P F  /  0 7',(-7,0,22.002),1.3)
    text_label('Header designation','RESOURCE PROCESSING',(11.98,0,18.02),.59,(math.pi/2,0,math.pi/2))
    # Interior rear equipment wall and painted floor parking lanes.
    for y in [-13,-6.5,0,6.5,13]:
        box('Back wall recess',(-21.33,y,7.6),(.25,5.9,10.0),0,DARK,.08)
        box('Back wall inset',(-21.16,y,8),(.20,5.1,8.5),1,WHITE,.09)
        box('Rear lower cabinet',(-20.98,y,2.65),(.55,5.1,1.6),0,PANEL,.08)
        for z in [5,10]:box('Rear blue status bar',(-21.00,y,z),(.06,2.0,.16),2,BLUE,.01)
    for y in [-12,-4,4,12]:
        for x in np.arange(-18,9,2.5):box('Interior lane marking',(float(x),y,1.214),(1.65,.15,.012),2,YELLOW,.002)
    for x in [-17,-11,-5,1,7]:
        for y in [-14,-7,0,7,14]:box('Bay deck plate',(x,y,1.17),(5.8,6.8,.07),1,WHITE,.04)
    # Rear exterior remains finished and closed.
    for y in [-14,-7,0,7,14]:
        box('Rear facade panel',(-22.68,y,10.1),(.22,6.7,14.2),0,CAMO,.1)
        for z in [6,7,8,9,10]:box('Rear exhaust louver',(-22.84,y,z),(.20,5.5,.34),0,DARK,.025)
    # UCX pieces: floor, apron, side walls, rear and ceiling.
    box('UCX_SM_RPF_Body_00',(-4.7,0,.7),(34,36,.9),collision=True)
    prism('UCX_SM_RPF_Body_01',[(10,.15),(24,.15),(24,.69),(12,1.2),(10,1.2)],-18,18,'Y',collision=True)
    for index,side in enumerate([-1,1],2):
        box('UCX_SM_RPF_Body_%02d'%index,(-1,side*21,9),(46,6,17.5),collision=True)
        box('UCX_SM_RPF_Body_%02d'%(index+2),(-4,side*27,5.8),(26,6,10.5),collision=True)
    box('UCX_SM_RPF_Body_06',(-22,0,8.9),(1.2,36,16),collision=True)
    box('UCX_SM_RPF_Body_07',(-5,0,19),(34,36,1.8),collision=True)
    box('UCX_SM_RPF_Body_08',(10.65,0,17.45),(2,36,4.3),collision=True)


def build_door():
    box('Door rigid core',(11.5,0,8.2),(1.15,35.75,13.9),0,DARK,.12,True)
    for yi,y in enumerate([-14,-7,0,7,14]):
        box('Door layered armored face',(12.13,y,8.35),(.43,6.8,12.85),0,CAMO,.11,True)
        box('Door upper service strip',(12.43,y,13.6),(.18,6.45,.60),0,PANEL,.04,True)
        box('Door armor inset',(12.41,y,7.9),(.12,5.75,5.8),0,DARK,.08,True)
        box('Door inset face',(12.50,y,7.95),(.11,5.15,5.05),0,PANEL,.13,True)
        # Recessed angled X braces and retaining bolts.
        for sign in [-1,1]:
            cylinder_between('Door diagonal stiffener',(12.61,y-2.15,7.95+sign*1.8),(12.61,y+2.15,7.95-sign*1.8),.10,0,DARK,True,6)
        for dy in [-2.6,2.6]:
            for z in [5.0,10.9,13.6]:bolt('Door retaining bolt',(12.61,y+dy,z),'X',True)
        for yy in np.arange(y-2.7,y+2.8,.6):box('Door lower heat vent',(12.48,float(yy),2.7),(.12,.23,1.42),0,DARK,.022,True)
        box('Door service light',(12.65,y,12.3),(.04,.6,.13),2,(1,.65,.04,.25),.006,True)
        # The visible underside receives real lining for the open pose.
        box('Door interior lining',(10.87,y,8.2),(.22,6.7,12.8),1,WHITE,.08,True)
    for y in np.arange(-17,17.5,1.0):box('Door continuous hazard stripe',(12.50,float(y),11.4),(.06,.45,.22),2,YELLOW,.008,True)
    for y in np.arange(-16,17,4):cylinder_between('Moving hinge knuckle',(11.5,float(y)-.35,15.15),(11.5,float(y)+.35,15.15),.43,0,DARK,True)
    # Hinged lifting arms are rigidly attached to the door, with fixed header mounts.
    for y in [-16.9,16.9]:
        cylinder_between('Lift reinforcement',(10.7,y,3.0),(10.7,y,13.6),.22,0,PANEL,True)
        box('Lift clevis',(10.8,y,14.1),(.85,.65,1.0),0,DARK,.08,True)
    text_label('Door clearance legend','CLEARANCE 14 M',(12.70,0,3.8),.6,(math.pi/2,0,math.pi/2),WHITE,True)


def merge_evaluated(objects,name,target):
    bpy.ops.object.select_all(action='DESELECT')
    copies=[];deps=bpy.context.evaluated_depsgraph_get()
    for src in objects:
        m=bpy.data.meshes.new_from_object(src.evaluated_get(deps),depsgraph=deps,preserve_all_data_layers=True)
        ob=bpy.data.objects.new(src.name+'_export',m);target.objects.link(ob);ob.matrix_world=src.matrix_world.copy();ob.select_set(True);copies.append(ob)
    bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();ob=bpy.context.object;ob.name=name;ob.data.name=name
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    bm=bmesh.new();bm.from_mesh(ob.data);bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(ob.data);bm.free();ob.data.update()
    return ob


def rig_and_export(scene):
    exp=collection('GS_RPF_90_Export',scene)
    body=merge_evaluated(BODY,'SM_RPF_Body',exp)
    door=merge_evaluated(DOOR,'SK_RPF_Door',exp)
    arm=bpy.data.armatures.new('SKEL_RPF_Door');rig=bpy.data.objects.new('RPF_Rig',arm);exp.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig;bpy.ops.object.mode_set(mode='EDIT')
    root=arm.edit_bones.new('root');root.head=(0,0,0);root.tail=(0,0,1)
    bone=arm.edit_bones.new('door_hinge');bone.head=HINGE;bone.tail=(HINGE[0],1,HINGE[2]);bone.parent=root
    bpy.ops.object.mode_set(mode='OBJECT')
    vg=door.vertex_groups.new(name='door_hinge');vg.add(list(range(len(door.data.vertices))),1,'REPLACE')
    mod=door.modifiers.new('Door rigid skeleton','ARMATURE');mod.object=rig;door.parent=rig
    pb=rig.pose.bones['door_hinge'];pb.rotation_mode='XYZ'
    actions={}
    for name,angles in [('Door_Open',(0,-math.pi/2)),('Door_Close',(-math.pi/2,0))]:
        rig.animation_data_create();rig.animation_data.action=None
        for frame,angle in zip([1,91],angles):
            pb.rotation_euler=(0,angle,0);pb.keyframe_insert(data_path='rotation_euler',frame=frame,group='Door hinge')
        action=rig.animation_data.action;action.name=name;action.use_fake_user=True;actions[name]=action
    rig.animation_data.action=actions['Door_Open'];scene.frame_set(1)
    # The editable pieces follow the same rig for useful animation preview.
    for obj in DOOR:
        vg=obj.vertex_groups.new(name='door_hinge');vg.add(list(range(len(obj.data.vertices))),1,'REPLACE')
        m=obj.modifiers.new('Door preview skeleton','ARMATURE');m.object=rig
    kwargs=dict(use_selection=True,global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,add_leaf_bones=False,path_mode='RELATIVE',bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,bake_anim_simplify_factor=0.0)
    bpy.ops.object.select_all(action='DESELECT');body.select_set(True)
    for ob in COLLISION:ob.hide_set(False);ob.select_set(True)
    bpy.context.view_layer.objects.active=body
    bpy.ops.export_scene.fbx(filepath=str(ASSET/'Exports/SM_RPF_Body.fbx'),object_types={'MESH'},bake_anim=False,**kwargs)
    bpy.ops.object.select_all(action='DESELECT');door.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
    bpy.ops.export_scene.fbx(filepath=str(ASSET/'Exports/SK_RPF_Door.fbx'),object_types={'MESH','ARMATURE'},bake_anim=False,**kwargs)
    for name,act in actions.items():
        rig.animation_data.action=act;scene.frame_set(1)
        bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
        bpy.ops.export_scene.fbx(filepath=str(ASSET/'Exports'/('A_RPF_'+name+'.fbx')),object_types={'ARMATURE'},bake_anim=True,**kwargs)
    rig.animation_data.action=actions['Door_Open'];scene.frame_set(1)
    body.hide_render=True;door.hide_render=True;body.hide_set(True);door.hide_set(True)
    for ob in COLLISION:ob.hide_set(True)
    def info(ob):
        return {'triangles':len(ob.data.polygons),'vertices':len(ob.data.vertices),'materials':[m.name for m in ob.data.materials], 'dimensions_m':list(ob.dimensions),'scale':list(ob.scale)}
    manifest={'asset':'ResourceProcessingFactory','version':1,'source_scene':scene.name,'body':info(body),'door':info(door),'closed_footprint_m':{'width':60,'depth':48,'height':22},'door_hinge_blender_m':HINGE,'doorway_clearance_m':[36,14],'fps':30,'animation_seconds':3,'actions':list(actions),'collision_hulls':len(COLLISION),'front_blender':'+X','ue_import_verified':False}
    (ASSET/'asset_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    log('Geometry '+json.dumps(manifest))
    return rig


def studio(scene):
    lights=collection('GS_RPF_95_Studio',scene)
    world=bpy.data.worlds.new('GS_RPF_Studio');world.use_nodes=True;scene.world=world
    world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.055,.070,.090,1)
    world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.32
    def aim(obj,p):obj.rotation_euler=(Vector(p)-obj.location).to_track_quat('-Z','Y').to_euler()
    for name,loc,power,size,color in [('Key',(50,-45,70),170000,45,(1,.9,.78)),('Fill',(20,50,42),110000,38,(.64,.8,1)),('Rim',(-38,5,60),200000,35,(.75,.86,1))]:
        d=bpy.data.lights.new('RPF_'+name,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=color
        ob=bpy.data.objects.new(d.name,d);lights.objects.link(ob);ob.location=loc;aim(ob,(0,0,7))
    for x in [-14,-4,6]:
        for y in [-15,15]:
            d=bpy.data.lights.new('RPF_InteriorBlue','AREA');d.energy=3200;d.color=(.025,.32,1);d.size=3
            ob=bpy.data.objects.new(d.name,d);lights.objects.link(ob);ob.location=(x,y,12);aim(ob,(x,0,2))
    # Studio floor is presentation only.
    bpy.ops.mesh.primitive_plane_add(size=400,location=(0,0,-.12));ground=bpy.context.object;ground.name='RPF_StudioGround_NotExported'
    for c in list(ground.users_collection):c.objects.unlink(ground)
    lights.objects.link(ground)
    m=bpy.data.materials.new('RPF_StudioGround');m.diffuse_color=(.027,.035,.046,1);m.use_nodes=True
    m.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.027,.035,.046,1);m.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.77;ground.data.materials.append(m)
    cameras={}
    for name,pos,target,scale in [('Hero',(78,-92,69),(0,0,8),83),('Front',(105,0,29),(0,0,9),71),('Top',(44,-58,115),(0,0,6),83),('Rear',(-85,74,51),(0,0,9),83),('Interior',(32,-6,9),(-14,0,7),None)]:
        d=bpy.data.cameras.new('RPF_CAM_'+name);ob=bpy.data.objects.new(d.name,d);lights.objects.link(ob);ob.location=pos;aim(ob,target)
        if scale:d.type='ORTHO';d.ortho_scale=scale
        else:d.type='PERSP';d.lens=24
        d.clip_end=1000;cameras[name]=ob
    scene.camera=cameras['Hero'];scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
    prefs=bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type='OPTIX';prefs.refresh_devices()
        for dev in prefs.devices:dev.use=dev.type=='OPTIX'
        scene.cycles.device='GPU' if any(d.type=='OPTIX' for d in prefs.devices) else 'CPU'
    except Exception:scene.cycles.device='CPU'
    scene.render.resolution_x=1600;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
    scene.render.image_settings.file_format='PNG'
    return cameras


def configure_preview_cycle(scene,rig):
    """A nine-second preview, independent of the two portable three-second clips."""
    action=bpy.data.actions.get('Door_Preview_Cycle')
    if action is None:
        rig.animation_data.action=None
        pb=rig.pose.bones['door_hinge'];pb.rotation_mode='XYZ'
        for frame,angle in [(1,0),(31,0),(121,-math.pi/2),(151,-math.pi/2),(241,0),(271,0)]:
            pb.rotation_euler=(0,angle,0);pb.keyframe_insert(data_path='rotation_euler',frame=frame,group='Door preview')
        action=rig.animation_data.action;action.name='Door_Preview_Cycle';action.use_fake_user=True
    rig.animation_data.action=action
    scene.frame_start=1;scene.frame_end=271;scene.render.fps=30
    for name,frame in [('CLOSED',1),('OPENING',31),('OPEN',121),('CLOSING',151),('CLOSED / LOOP',241)]:
        if not scene.timeline_markers.get(name):scene.timeline_markers.new(name,frame=frame)
    scene.frame_set(1)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                sp=area.spaces.active;sp.clip_end=2000;sp.region_3d.view_perspective='CAMERA'
                sp.overlay.show_overlays=False
                sp.shading.type='MATERIAL';sp.shading.use_scene_world=True;sp.shading.use_scene_lights=True


def main(render=True):
    global COL_B,COL_D,COL_C
    for folder in ['References','Textures','Exports']:(ASSET/folder).mkdir(parents=True,exist_ok=True)
    OUT.mkdir(parents=True,exist_ok=True)
    ids=['f7260da8-41cf-43ca-b2a3-8fd8fb24671c','14220639-938e-4341-b84c-de31af4847d4','9d7e1f5b-ba4c-485f-a1de-69d23ffa5d0f','f58f8cfd-debb-4b38-bcb1-2580a883e104','a1d48e87-1aff-4165-bcec-a3c91ec8c854','3e6dd5ed-8c0c-4d6c-9d9e-83996907b031']
    for i,ref in enumerate(ids,1):
        src=Path('C:/Users/a/AppData/Local/Temp')/('codex-clipboard-'+ref+'.png')
        if src.exists():shutil.copy2(src,ASSET/'References'/('Reference_%02d.png'%i))
    if bpy.data.scenes.get('GS_RPF_Authoring'):raise RuntimeError('Existing authoring scene; use an isolated Blender process for regeneration')
    scene=bpy.data.scenes.new('GS_RPF_Authoring');bpy.context.window.scene=scene
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1;scene.render.fps=30;scene.frame_start=1;scene.frame_end=91
    COL_B=collection('GS_RPF_01_EditableBody',scene);COL_D=collection('GS_RPF_02_EditableDoor',scene);COL_C=collection('GS_RPF_80_Collision',scene)
    log('Generating PBR maps');make_materials()
    log('Building armor, equipment, interior and apron');build_architecture();build_door()
    log('Baking export copies and door actions');rig=rig_and_export(scene)
    cams=studio(scene)
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(ASSET/'ResourceProcessingFactory_v01.blend'))
    if render:
        for title,cam,frame in [('01_closed_hero','Hero',1),('02_open_hero','Hero',91),('03_open_front','Front',91),('04_top','Top',1),('05_rear','Rear',1),('06_interior','Interior',91)]:
            scene.camera=cams[cam];scene.frame_set(frame);scene.render.filepath=str(OUT/(title+'.png'));log('Rendering '+title);bpy.ops.render.render(write_still=True)
    scene.camera=cams['Hero'];configure_preview_cycle(scene,rig)
    # Persist portable textures in the deliverable .blend.
    bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(ASSET/'ResourceProcessingFactory_v01.blend'))
    log('AUTHORING_COMPLETE')


if __name__=='__main__':main('--no-render' not in sys.argv)

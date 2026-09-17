"""Author both tactical units from approved drawings, without imported geometry.

Only analytic plates, regular lathe sections and rigid mechanical parts are
used. Concept PNGs are retained as references; no Tripo mesh or texture is read.
Run with Blender --background --python this_file -- Sweeper|WarMachine|all.
"""
import bpy, bmesh, math, json, sys
from pathlib import Path
from mathutils import Vector, Matrix

ROOT=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916')
OUT=ROOT/'Production_Handbuilt'
OUT.mkdir(exist_ok=True)
PALETTE=['DD6038','F2EBDD','52687A','283844','FFD166','8295A1','AC452B']
PARTS=[]

def linear(h):
    return tuple((v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4) for v in [int(h[i:i+2],16)/255 for i in (0,2,4)])+(1,)

def active(o):
    if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o

def material(i,clay=False):
    name=('Clay_' if clay else 'Paint_')+str(i)
    if name in bpy.data.materials:return bpy.data.materials[name]
    m=bpy.data.materials.new(name);m.diffuse_color=linear('B2BAC0' if clay else PALETTE[i]);m.use_nodes=True
    n,l=m.node_tree.nodes,m.node_tree.links;n.clear()
    out=n.new('ShaderNodeOutputMaterial');em=n.new('ShaderNodeEmission');l.new(em.outputs[0],out.inputs[0])
    geo=n.new('ShaderNodeNewGeometry');dot=n.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized();l.new(geo.outputs['Normal'],dot.inputs[0])
    ramp=n.new('ShaderNodeValToRGB');ramp.color_ramp.interpolation='CONSTANT';ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for j,(p,c) in enumerate([(0,(.46,.52,.60,1)),(.12,(.72,.77,.83,1)),(.55,(1,1,1,1))]):
        e=ramp.color_ramp.elements[0] if not j else ramp.color_ramp.elements.new(p);e.position=p;e.color=c
    l.new(dot.outputs['Value'],ramp.inputs[0]);mul=n.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1;mul.inputs[1].default_value=m.diffuse_color
    l.new(ramp.outputs[0],mul.inputs[2])
    ao=n.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.65;ao.inputs['Color'].default_value=(1,1,1,1)
    contact=n.new('ShaderNodeMixRGB');contact.blend_type='MULTIPLY';contact.inputs[0].default_value=.58;l.new(mul.outputs[0],contact.inputs[1]);l.new(ao.outputs['Color'],contact.inputs[2])
    l.new(contact.outputs[0],em.inputs[0])
    bs=n.new('ShaderNodeBsdfPrincipled');bs.inputs['Roughness'].default_value=.66;bs.inputs['Metallic'].default_value=.025;bs.inputs['Specular IOR Level'].default_value=.22
    l.new(mul.outputs[0],bs.inputs['Base Color'])
    blend=n.new('ShaderNodeMixShader');blend.inputs[0].default_value=.16;l.new(bs.outputs[0],blend.inputs[1]);l.new(em.outputs[0],blend.inputs[2]);l.new(blend.outputs[0],out.inputs[0])
    return m

def finish(o,color=0,motion='Body',bevel=0):
    o['motion']=motion;o['authorship']='Blender primitives and authored polygon sections; no generated mesh'
    for i in range(len(PALETTE)):o.data.materials.append(material(i))
    for p in o.data.polygons:p.material_index=color
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
    if bevel:
        mod=o.modifiers.new('Uniform mechanical chamfer','BEVEL');mod.width=bevel;mod.segments=1;mod.affect='EDGES'
        mod=o.modifiers.new('Keep armor faces planar','WEIGHTED_NORMAL');mod.keep_sharp=True;mod.weight=80
        for p in o.data.polygons:p.use_smooth=True
        o.data.set_sharp_from_angle(angle=math.radians(50))
    PARTS.append(o);return o

def mesh(name,verts,faces,color=0,motion='Body',bevel=0):
    me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update()
    o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);return finish(o,color,motion,bevel)

def box(name,loc,dim,color=0,bevel=.04,motion='Body',rot=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.dimensions=dim;o.rotation_euler.y=rot
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True);return finish(o,color,motion,bevel)

def prism(name,profile,y0,y1,color=0,bevel=.04,motion='Body'):
    n=len(profile);v=[(x,y,z) for y in (y0,y1) for x,z in profile]
    f=[tuple(reversed(range(n))),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,v,f,color,motion,bevel)

def panel(name,points,normal,thickness=.025,color=1,ink=True,motion='Body'):
    # One deliberately authored panel: a flat top, a narrow side, no noisy emboss.
    normal=Vector(normal).normalized();pts=[Vector(p) for p in points];center=sum(pts,Vector())/len(pts)
    if ink:
        rim=[center+(p-center)*1.016-normal*.003 for p in pts]
        panel(name+' seam',rim,normal,.009,3,False,motion)
    n=len(pts);v=[tuple(p) for p in pts]+[tuple(p+normal*thickness) for p in pts]
    f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,v,f,color,motion)

def roof_panel(name,xy,z_at_x,normal,thickness=.018,color=1,ink=True,motion='Body'):
    return panel(name,[(x,y,z_at_x(x)) for x,y in xy],normal,thickness,color,ink,motion)

def cyl(name,loc,radius,length,color=2,axis='Y',sides=12,motion='Body'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides,radius=radius,depth=length,location=loc)
    o=bpy.context.object;o.name=name
    if axis=='X':o.rotation_euler.y=math.pi/2
    elif axis=='Y':o.rotation_euler.x=math.pi/2
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True);return finish(o,color,motion)

def lathe(name,center,profile,colors,axis='Y',sides=16,motion='Body'):
    center=Vector(center);v=[]
    for along,radius in profile:
        for i in range(sides):
            angle=2*math.pi*i/sides+math.pi/sides
            q=(radius*math.cos(angle),along,radius*math.sin(angle)) if axis=='Y' else (radius*math.cos(angle),radius*math.sin(angle),along)
            v.append(tuple(center+Vector(q)))
    f=[];ci=[]
    for ring in range(len(profile)-1):
        for i in range(sides):
            k=(i+1)%sides;f.append((ring*sides+i,ring*sides+k,(ring+1)*sides+k,(ring+1)*sides+i));ci.append(colors[ring])
    f+=[tuple(reversed(range(sides))),tuple((len(profile)-1)*sides+i for i in range(sides))];ci +=[colors[0],colors[-1]]
    o=mesh(name,v,f,colors[0],motion)
    for p,c in zip(o.data.polygons,ci):p.material_index=c
    return o

def mirror(o,new_motion=None):
    other=o.copy();other.data=o.data.copy();other.name=o.name.replace('_L','_R') if '_L' in o.name else o.name+'_R';bpy.context.collection.objects.link(other)
    other.data.transform(Matrix.Diagonal((1,-1,1,1)))
    bm=bmesh.new();bm.from_mesh(other.data);bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(other.data);bm.free()
    if new_motion:other['motion']=new_motion
    PARTS.append(other);return other

def pair(build):
    before=len(PARTS);build()
    for o in list(PARTS[before:]):mirror(o)

def slot_y(name,x,y,z,length=.26,angle=0,motion='Body'):
    # Flush vents, with the structure of a real straight slot.
    d=Vector((math.cos(angle),0,math.sin(angle)))*length/2;q=Vector((-math.sin(angle),0,math.cos(angle)))*.025;c=Vector((x,y,z))
    return panel(name,[c-d-q,c+d-q,c+d+q,c-d+q],(0,1 if y>0 else -1,0),.005,3,False,motion)

def light_x(name,x,y,z,width=.40,height=.20):
    box(name+' housing',(x,y,z),(.09,width+.10,height+.10),3,.025)
    box(name+' lens',(x+.05,y,z),(.025,width,height),4,0)

def swept_body(name,sections,color=0):
    # Cross sections are symmetric octagons in the YZ plane.
    verts=[]
    for x,width,base,top,ch in sections:
        yz=[(-width+ch,base),(width-ch,base),(width,base+ch),(width,top-ch),(width-ch,top),(-width+ch,top),(-width,top-ch),(-width,base+ch)]
        verts += [(x,y,z) for y,z in yz]
    faces=[tuple(reversed(range(8))),tuple((len(sections)-1)*8+i for i in range(8))]
    for s in range(len(sections)-1):
        for i in range(8):faces.append((s*8+i,s*8+(i+1)%8,(s+1)*8+(i+1)%8,(s+1)*8+i))
    return mesh(name,verts,faces,color,bevel=.045)

def octagon_xy(x0,x1,half_y,cut=.09):
    return [(x0,-half_y+cut),(x0,half_y-cut),(x0+cut,half_y),(x1-cut,half_y),(x1,half_y-cut),(x1,-half_y+cut),(x1-cut,-half_y),(x0+cut,-half_y)]

def beam(name,p0,p1,width,height,color=2):
    p0,p1=Vector(p0),Vector(p1);c=(p0+p1)*.5;d=p1-p0
    o=box(name,(0,0,0),(d.length,width,height),color,.045)
    o.data.transform(Matrix.Translation(c)@d.to_track_quat('X','Z').to_matrix().to_4x4());return o

def recessed_muzzle(name,center,length,width,height,motion='Body'):
    # Real bevelled rim and recessed throat, not a black square on a solid block.
    cx,cy,cz=center
    def ring(x,wy,hz,ch):
        yz=[(-wy+ch,-hz),(wy-ch,-hz),(wy,-hz+ch),(wy,hz-ch),(wy-ch,hz),(-wy+ch,hz),(-wy,hz-ch),(-wy,-hz+ch)]
        return [(cx+x,cy+y,cz+z) for y,z in yz]
    v=ring(-length/2,width*.43,height*.43,.035)+ring(-length*.30,width/2,height/2,.045)+ring(length*.40,width/2,height/2,.045)+ring(length/2,width*.43,height*.43,.035)+ring(length/2,width*.28,height*.29,.025)+ring(length*.1,width*.28,height*.29,.025)
    f=[];colors=[]
    for j in range(5):
        for i in range(8):f.append((j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i));colors.append(2 if j<4 else 3)
    f.extend([tuple(reversed(range(8))),tuple(40+i for i in range(8))]);colors.extend([2,3])
    o=mesh(name,v,f,2,motion)
    for p,c in zip(o.data.polygons,colors):p.material_index=c
    return o

def armor_sector(name,center,angle,color=1):
    # A separate formed cover over the disc rim, with a broad bevel and a seam.
    profile=[(-.14,1.30),(.14,1.30),(.32,1.15),(.35,.98),(.315,.965),(.09,1.245),(-.14,1.245)]
    v=[];steps=4
    for z,r in profile:
        for k in range(steps+1):
            a=angle+math.radians(-34+68*k/steps);v.append(tuple(Vector(center)+Vector((math.cos(a)*r,math.sin(a)*r,z))))
    f=[];n=steps+1
    for j in range(len(profile)):
        for k in range(steps):f.append((j*n+k,j*n+k+1,((j+1)%len(profile))*n+k+1,((j+1)%len(profile))*n+k))
    f.extend([tuple(j*n for j in reversed(range(len(profile)))),tuple(j*n+steps for j in range(len(profile)))])
    return mesh(name,v,f,color)

def build_sweeper():
    # The approved concept's sloping shoulder silhouette is the primary datum.
    swept_body('Lower drive chassis',[(-3.15,.64,.75,1.38,.15),(-1.85,.78,.72,1.46,.16),(1.65,.58,.69,1.20,.12),(3.10,.40,.72,1.07,.10)],2)
    prism('Rear engine shell',[(-3.08,1.26),(-3.00,3.44),(-2.71,3.65),(-1.50,3.53),(-.73,3.20),(-.78,2.54),(-1.43,2.03),(-1.80,1.28)],-.86,.86,0,.065)
    roof_panel('Rear deck ivory',octagon_xy(-2.61,-1.64,.59,.09),lambda x:3.666-(x+2.71)*.12/1.21,(.10,0,1))
    roof_panel('Rear deck latch',[(-1.83,-.17),(-1.83,.17),(-1.77,.17),(-1.77,-.17)],lambda x:3.688-(x+2.71)*.12/1.21,(.10,0,1),.005,3,False)
    roof_panel('Rear deck brow',[(-1.43,-.62),(-1.43,.62),(-.83,.62),(-.83,-.62)],lambda x:3.548-(x+1.50)*.33/.77,(.43,0,1))
    box('Rear exhaust core',(-3.11,0,2.60),(.21,1.34,.71),2,.075)
    for y in (-.40,0,.40):box('Rear exhaust vent',(-3.23,y,2.59),(.012,.17,.32),3,.015)
    # Antenna is centered, straight and common to both sides.
    box('Antenna shoe',(-2.38,0,3.69),(.36,.37,.095),2,.035)
    cyl('Antenna base',(-2.38,0,4.00),.072,.58,2,'Z',8)
    cyl('Antenna rod',(-2.38,0,4.99),.045,1.47,2,'Z',8)
    shoulder=[(-2.72,3.55),(-2.56,3.89),(-1.91,3.69),(.64,2.27),(1.04,1.64),(.77,1.30),(-.58,1.28),(-2.31,1.94)]
    def shoulders():
        start=len(PARTS)
        cx,cz=-.82,2.58
        trim=[(cx+(x-cx)*1.004,cz+(z-cz)*1.004) for x,z in shoulder]
        prism('Shoulder structural gasket_L',trim,1.54,1.577,3,.012)
        prism('Shoulder armor_L',shoulder,1.03,1.58,0,.095)
        roof_panel('Shoulder white upper cap_L',[(-2.52,1.07),(-2.52,1.53),(-1.98,1.53),(-1.98,1.07)],lambda x:3.899-(x+2.56)*.20/.65,(.308,0,1),.015,1,False)
        prism('Shoulder top ivory_L',[(-2.55,3.866),(-1.98,3.687),(-1.44,3.392),(-1.62,3.19),(-2.45,3.50)],1.586,1.599,1,0)
        panel('Continuous shoulder ivory stripe_L',[(-1.99,1.586,3.57),(-1.43,1.586,3.19),(-1.17,1.586,2.56),(-1.65,1.586,2.40),(-1.91,1.586,3.15)],(0,1,0),.002,1,False)
        panel('Shoulder insert_L',[(-2.18,1.597,2.35),(-1.64,1.597,2.81),(-.95,1.597,2.62),(-.25,1.597,1.43),(-.77,1.597,1.37),(-1.96,1.597,1.93)],(0,1,0),.025)
        slot_y('Shoulder service slot_L',-1.37,1.629,2.59,.29,math.radians(-68))
        # A small lamp is mounted on a coherent flat, forward-facing shoulder tab.
        box('Shoulder lamp tab_L',(-1.69,1.30,3.61),(.16,.41,.24),3,.026)
        box('Shoulder lamp_L',(-1.601,1.30,3.61),(.02,.29,.13),4,0)
        # The armor leans outward towards the front. Each broad face stays planar.
        lean=Matrix.Identity(4);lean[1][0]=.14;lean[1][3]=.22
        for o in PARTS[start:]:o.data.transform(lean)
        # Rear wheel fairing, independent of the tall shoulder plate.
        prism('Rear wheel fairing_L',[(-3.03,1.54),(-2.58,1.80),(-1.42,1.50),(-1.20,.95),(-2.11,.64),(-2.92,1.01)],.71,2.19,0,.045)
        panel('Fairing ivory_L',[(-2.83,2.198,1.52),(-2.47,2.198,1.67),(-1.52,2.198,1.43),(-1.36,2.198,1.02),(-2.10,2.198,.79),(-2.75,2.198,1.05)],(0,1,0),.012)
        slot_y('Fairing latch_L',-2.15,2.215,1.41,.34)
    pair(shoulders)
    nose=[(-.38,.80),(-.47,1.78),(.30,2.16),(3.14,1.28),(3.27,.76),(2.98,.64),(.22,.67)]
    prism('Forward hood',nose,-.69,.69,0,.07)
    roof_panel('Hood ivory',[(.49,-.49),(.49,.49),(1.17,.53),(2.54,.40),(2.69,.24),(2.69,-.24),(2.54,-.40),(1.17,-.53)],lambda x:2.178-(x-.30)*.88/2.84,(.31,0,1))
    roof_panel('Hood service latch',[(2.24,-.20),(2.24,.20),(2.30,.20),(2.30,-.20)],lambda x:2.201-(x-.30)*.88/2.84,(.31,0,1),.005,3,False)
    box('Front bumper dark recess',(3.276,0,.95),(.018,.36,.08),3,.01)
    def axles():
        for axle,x,r in [('F',2.21,.96),('R',-2.62,1.04)]:
            cyl('Wheel drive axle_'+axle+'_L',(x,1.90,r),.25,1.50,2,'Y',12)
            cyl('Axle articulated knuckle_'+axle+'_L',(x,2.20,r+.04),.38,.30,2,'Y',12)
            box('Axle orange cover_'+axle+'_L',(x,1.49,r+.14),(1.05,1.24,.55),0,.09)
            box('Axle white collar_'+axle+'_L',(x,1.16,r+.17),(1.12,.27,.62),1,.055)
            light_x('Axle lamp_'+axle+'_L',x+.532,1.89,r+.15,.36,.20)
    pair(axles)
    wheel_pivots={}
    for label,x,r in [('F',2.21,.96),('R',-2.62,1.04)]:
        tag='Wheel_'+label+'L';c=(x,2.73,r);w=.94
        profile=[(-w*.50,r*.66),(-w*.43,r*.90),(-w*.30,r),(w*.30,r),(w*.43,r*.90),(w*.50,r*.68),(w*.52,r*.60),(w*.54,r*.43),(w*.55,r*.40)]
        o=lathe(tag,c,profile,[2,2,2,2,2,1,1,2],sides=16,motion=tag);mirror(o,tag[:-1]+'R')
        wheel_pivots[tag]=list(c);wheel_pivots[tag[:-1]+'R']=[c[0],-c[1],c[2]]
        for dx in (-r*.5,r*.5):
            o=box('Hub index '+tag,(x+dx,2.73+w*.542,r),(.052,.012,.15),3,0,tag);mirror(o,tag[:-1]+'R')
        o=cyl('Hub axle cap '+tag,(x,2.73+w*.56,r),r*.095,.018,3,'Y',8,tag);mirror(o,tag[:-1]+'R')
    # A single machine gun, with a physical lateral pitch axle.
    box('Gun fork foundation',(.15,0,1.84),(1.25,1.90,.18),2,.035)
    o=prism('Fixed gun bearing cradle_L',[(-.25,1.98),(.62,1.98),(.36,2.91),(.15,3.04),(-.06,2.91)],.51,.63,2,.03);mirror(o)
    gun='Gun_Pitch';pivot=Vector((.15,0,2.96))
    prism('Machine gun core',[(-.69,2.73),(-.51,3.39),(-.22,3.51),(.81,3.39),(1.12,2.92),(.91,2.61),(-.45,2.61)],-.43,.43,2,.060,gun)
    cheek=[(-.67,2.73),(-.65,3.13),(-.34,3.52),(.63,3.40),(.98,3.05),(1.07,2.75),(.73,2.69),(.51,3.08),(-.09,3.16),(-.33,2.71)]
    o=prism('Gun cheek armor_L',cheek,.43,.66,0,.05,gun);mirror(o)
    box('Receiver central block',(.90,0,3.025),(.97,.70,.65),2,.085,gun)
    roof_panel('Receiver ivory dorsal',[(-.13,-.38),(-.13,.38),(.57,.38),(.69,.26),(.69,-.26),(.57,-.38)],lambda x:3.528-(x+.22)*.12/1.03,(.1165,0,1),.018,1,True,gun)
    box('Gun indicator housing',(1.392,0,3.13),(.031,.32,.17),3,.02,gun)
    box('Gun indicator lens',(1.413,0,3.13),(.012,.23,.085),4,0,gun)
    for side in (-1,1):
        cyl('Gun pivot rim',(pivot.x,side*.646,pivot.z),.40,.18,2,'Y',16,gun)
        cyl('Gun pivot amber ring',(pivot.x,side*.742,pivot.z),.292,.025,4,'Y',16,gun)
        cyl('Gun pivot cap',(pivot.x,side*.758,pivot.z),.25,.018,2,'Y',16,gun)
        box('Gun ivory cheek',(1.00,side*.52,2.76),(.28,.21,.34),1,.035,gun)
    box('Barrel socket',(1.43,0,2.96),(.32,.52,.45),2,.055,gun)
    box('Barrel collar',(1.64,0,2.96),(.17,.36,.36),5,.030,gun)
    box('Single barrel',(2.51,0,2.96),(1.99,.235,.25),2,.026,gun)
    recessed_muzzle('Muzzle brake',(3.68,0,2.96),.47,.42,.37,gun)
    for side in (-1,1):
        for x in (3.56,3.76):slot_y('Muzzle vent',x,side*.212,2.96,.15,math.pi/2,gun)
        for x in (1.30,1.43):slot_y('Receiver cooling slot',x,side*.266,2.96,.21,math.pi/2,gun)
    return dict(wheel_pivots_m=wheel_pivots,wheel_radius_m=.96,sockets_m={'Rig_GunPitch':list(pivot),'Muzzle_Gun':[3.925,0,2.96]},gun_pitch_limits_degrees=[-15,60])

def build_war_machine():
    swept_body('Lower armored keel',[(-2.60,.72,.92,1.69,.16),(-1.20,.93,.87,1.83,.17),(.90,.89,.82,1.73,.16),(1.88,.54,.80,1.48,.12)],0)
    swept_body('Dark mechanical waist',[(-2.49,.90,1.59,1.85,.08),(-.90,1.22,1.49,1.83,.09),(1.52,.73,1.60,1.88,.07)],2)
    swept_body('Main upper armor',[(-2.68,.97,1.72,3.11,.19),(-1.54,1.30,1.65,3.60,.24),(-.22,1.24,1.61,3.49,.22),(1.22,.76,1.73,2.63,.16),(1.79,.56,1.82,2.32,.13)],0)
    roof_panel('Upper rear ivory',[(-2.48,-.52),(-2.48,.52),(-1.64,.56),(-1.64,-.56)],lambda x:3.125+(x+2.68)*.49/1.14,(-.43,0,1))
    roof_panel('Raised dorsal armor',octagon_xy(-1.55,-.24,.77,.14),lambda x:3.625-(x+1.54)*.11/1.32,(.0833,0,1),.11,0)
    roof_panel('Dorsal ivory hatch',[(-1.41,-.52),(-1.41,.52),(-.49,.55),(-.35,.40),(-.35,-.40),(-.49,-.55)],lambda x:3.753-(x+1.54)*.11/1.32,(.0833,0,1))
    roof_panel('Forehead armor brow',[(-.14,-.73),(-.14,.73),(1.20,.47),(1.24,.22),(1.24,-.22),(1.20,-.47)],lambda x:3.512-(x+.22)*.86/1.44,(.597,0,1),.065,0)
    roof_panel('Forehead ivory',[(-.06,-.62),(-.06,.62),(.99,.47),(1.13,.28),(1.13,-.28),(.99,-.47)],lambda x:3.592-(x+.22)*.86/1.44,(.597,0,1))
    roof_panel('Top latch',[(-.61,-.19),(-.61,.19),(-.54,.19),(-.54,-.19)],lambda x:3.776-(x+1.54)*.11/1.32,(.0833,0,1),.006,3,False)
    for side in (-1,1):
        roof_panel('Deck inset vent',[(x,side*y) for x,y in [(-.66,.62),(-.66,.71),(-.43,.70),(-.48,.61)]],lambda x:3.748-(x+1.54)*.11/1.32,(.0833,0,1),.005,3,False)
    light_x('Central targeting lamp',1.32,0,2.72,.43,.12)
    box('Front air inlet',(1.80,0,2.05),(.035,.77,.21),3,.032)
    swept_body('Forward lower prow',[(.95,.77,.66,1.80,.15),(1.65,.77,.53,1.86,.15),(3.01,.45,.48,1.13,.11),(3.21,.41,.53,1.00,.10)],0)
    roof_panel('Prow ivory',[(1.73,-.50),(1.73,.50),(2.84,.29),(2.97,.19),(2.97,-.19),(2.84,-.29)],lambda x:1.878-(x-1.65)*.73/1.36,(.537,0,1))
    box('Prow inset dark mouth',(3.225,0,.775),(.022,.54,.27),6,.035)
    roof_panel('Prow latch',[(2.59,-.19),(2.59,.19),(2.66,.19),(2.66,-.19)],lambda x:1.901-(x-1.65)*.73/1.36,(.537,0,1),.006,3,False)
    box('Rear engine vent',(-2.702,0,2.10),(.045,1.09,1.05),2,.09)
    for z in (1.85,2.10,2.35):box('Rear vent slot',(-2.73,0,z),(.014,.60,.073),3,.01)
    # Stable, broadly spaced feet and simple articulated-looking support beams.
    def leg_side():
        for label,x in [('F',1.99),('R',-2.24)]:
            inner_x=.80 if label=='F' else -1.30
            beam('Diagonal upper suspension_'+label+'_L',(inner_x,1.06,1.42),(x,2.05,.98),.59,.54)
            box('Suspension beam_'+label+'_L',(x,2.15,1.02),(.63,.77,.46),2,.065)
            cyl('Suspension bearing_'+label+'_L',(inner_x,1.18,1.42),.34,.35,2,'Y',12)
            cyl('Bearing end_'+label+'_L',(inner_x,1.369,1.42),.22,.028,3,'Y',12)
            p0,p1=Vector((inner_x,1.06,1.42)),Vector((x,2.05,.98));c=p0.lerp(p1,.61);d=(p1-p0).normalized()
            beam('Suspension ivory cuff_'+label+'_L',c-d*.12,c+d*.12,.68,.62,1)
            box('Hover knuckle_'+label+'_L',(x,2.21,.90),(.83,.57,.62),0,.065)
            box('Knuckle ivory cuff_'+label+'_L',(x,2.01,.97),(.88,.18,.66),1,.04)
            light_x('Knuckle lamp_'+label+'_L',x+.423,2.23,.92,.23,.20)
            c=(x,2.92,.37)
            profile=[(-.33,1.01),(-.20,1.27),(.15,1.27),(.32,1.14),(.34,.93),(.44,.84),(.48,.61),(.53,.56),(.60,.50),(.61,.43)]
            o=lathe('Hover disc_'+label+'_L',c,profile,[2,0,0,0,2,2,1,1,2],axis='Z',sides=16)
            # Ivory sectors are authored as broad angular panels, with no random scratches.
            for p in o.data.polygons:
                c0=p.center-Vector(c)
                if .13<c0.z<.34 and abs(c0.x)>.68:p.material_index=1
            for angle in (0,math.pi):armor_sector('Disc formed ivory cover_'+label+'_L',c,angle)
            # A single regular slot on the front edge of each foot.
            box('Disc intake_'+label+'_L',(x+1.302,2.92,.38),(.013,.32,.072),3,0)
    pair(leg_side)
    # Two symmetrical side assemblies, each with two horizontal gun barrels.
    def cannon_side():
        prism('Side cannon shoulder_L',[(-2.10,2.35),(-2.08,3.17),(-1.68,3.40),(-.76,3.23),(-.33,2.68),(-.70,2.28)],1.24,2.00,0,.07)
        box('Shoulder ivory cuff_L',(-1.32,1.63,2.85),(.30,.83,1.03),1,.055)
        prism('Twin cannon orange housing_L',[(-1.20,2.10),(-1.30,2.87),(-.90,3.10),(.46,3.02),(1.02,2.70),(.82,2.10)],1.38,2.37,0,.085)
        box('Twin cannon receiver_L',(.61,1.88,2.65),(1.46,.88,.67),2,.075)
        box('Twin cannon dorsal block_L',(.29,1.88,3.02),(.53,.60,.14),2,.035)
        roof_panel('Cannon ivory top band_L',[(-.52,1.49),(-.52,2.28),(-.17,2.28),(-.17,1.49)],lambda x:3.114-(x+.90)*.08/1.36,(.059,0,1),.014,1,False)
        box('Cannon side lamp housing_L',(-.44,2.392,2.69),(.48,.055,.28),3,.035)
        box('Cannon side lamp_L',(-.44,2.425,2.69),(.34,.018,.17),4,0)
        for y in (1.62,2.14):
            box('Twin barrel ivory collar_L',(1.13,y,2.66),(.24,.46,.58),1,.042)
            box('Twin barrel_L',(2.10,y,2.66),(1.89,.30,.32),2,.032)
            recessed_muzzle('Twin muzzle_L',(3.12,y,2.66),.45,.39,.43)
            box('Twin barrel rear sleeve_L',(1.56,y,2.66),(.20,.34,.37),5,.022)
            for x in (2.24,2.68):slot_y('Twin barrel vent_L',x,y+.154,2.66,.24)
        # A long pale armor cheek follows the hull instead of floating beside it.
        o=prism('Hull pale cheek_L',[(-.65,3.17),(-.21,3.21),(.95,2.50),(1.05,2.03),(.65,1.97),(-.04,2.40)],1.22,1.46,1,.035)
        lean=Matrix.Identity(4);lean[1][0]=-.31;o.data.transform(lean)
    pair(cannon_side)
    sockets={'Muzzle_Cannon_L':[3.36,1.88,2.66],'Muzzle_Cannon_R':[3.36,-1.88,2.66]}
    # Two separate parallel launch pods: zero yaw/roll, exactly 45 degrees pitch.
    axis=Vector((math.sqrt(.5),0,math.sqrt(.5)))
    for side in (-1,1):
        center=Vector((-1.98,side*1.35,4.30));before=len(PARTS)
        box('Missile pod shell',(0,0,0),(2.05,1.11,1.17),0,.065)
        box('Pod rear cap',(-1.04,0,0),(.035,.83,.88),6,.048)
        box('Pod ivory rim',(.93,0,0),(.27,1.135,1.195),1,.062)
        box('Pod dark socket face',(1.076,0,0),(.021,.91,.96),3,.041)
        for y in (-.247,.247):
            for z in (-.259,.259):
                cyl('Launch tube',(1.108,y,z),.205,.06,2,'X',12)
                cyl('Launch tube bore',(1.145,y,z),.151,.014,3,'X',12)
                cyl('Missile tip',(1.158,y,z),.111,.027,4,'X',12)
        box('Pod mount armor',(-.60,0,-.59),(.82,.74,.22),2,.04)
        for y in (-.568,.568):
            box('Pod hinge side plate',(-.62,y,-.10),(.62,.075,.63),2,.065)
            box('Pod hinge inset',(-.62,y*1.08,-.10),(.42,.015,.42),5,.040)
        for y in (-.565,.565):
            box('Pod access recess',(-.19,y,0),(.12,.012,.40),6,.011)
        xf=Matrix.Translation(center)@Matrix.Rotation(-math.pi/4,4,'Y')
        for o in PARTS[before:]:o.data.transform(xf);o['fixed_pitch_degrees']=45.0
        box('Pod fixed support',(-2.16,side*1.35,3.55),(.73,.73,.61),2,.060)
        for z in (3.36,3.55,3.73):box('Pod cradle reinforcement',(-2.16,side*1.35,z),(.80,.79,.082),2,.016)
        sockets['Muzzle_Missile_'+('L' if side>0 else 'R')]=list(center+axis*1.20)
    return dict(sockets_m=sockets,missile_pod_pitch_degrees=45,wheel_pivots_m={},wheel_radius_m=0)

def evaluated_stats(parts):
    dg=bpy.context.evaluated_depsgraph_get();vs=[];triangles=0
    for o in parts:
        e=o.evaluated_get(dg);me=e.to_mesh();triangles+=sum(len(p.vertices)-2 for p in me.polygons);vs.extend([o.matrix_world@v.co for v in me.vertices]);e.to_mesh_clear()
    lo=Vector([min(v[i] for v in vs) for i in range(3)]);hi=Vector([max(v[i] for v in vs) for i in range(3)])
    from mathutils.kdtree import KDTree
    kd=KDTree(len(vs))
    for i,v in enumerate(vs):kd.insert(v,i)
    kd.balance();err=max(kd.find(Vector((v.x,-v.y,v.z)))[2] for v in vs)
    return lo,hi,triangles,err

def render(unit,view='three_quarter',clay=False):
    s=bpy.context.scene
    for o in list(s.objects):
        if o.type in {'LIGHT','CAMERA'}:bpy.data.objects.remove(o,do_unlink=True)
    s.render.engine='BLENDER_EEVEE';s.render.resolution_x=1500;s.render.resolution_y=1200;s.render.resolution_percentage=100
    s.view_settings.view_transform='Standard';s.view_settings.look='None'
    if not s.world:s.world=bpy.data.worlds.new('Neutral art review')
    s.world.use_nodes=True;bg=next(n for n in s.world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs[0].default_value=(.38,.44,.50,1);bg.inputs[1].default_value=.65
    lo,hi,_,_=evaluated_stats(PARTS);center=(lo+hi)*.5;span=max(hi-lo)
    direction={'three_quarter':(1.25,-1.6,1.12),'front':(1,0,0),'side':(0,-1,0),'back':(-1,0,.12),'top':(0,0,1)}[view]
    ca=bpy.data.cameras.new('Review camera');cam=bpy.data.objects.new('Review camera',ca);s.collection.objects.link(cam);cam.location=center+Vector(direction)*span;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();ca.type='ORTHO';ca.ortho_scale=span*(1.60 if view=='three_quarter' else 1.42);ca.clip_end=10000;s.camera=cam
    for name,loc,power in [('Key',(0.8,-1.5,2.6),2.25),('Fill',(-2,1,1.6),1.1)]:
        ld=bpy.data.lights.new(name,'SUN');ob=bpy.data.objects.new(name,ld);s.collection.objects.link(ob);ob.rotation_euler=Vector(loc).to_track_quat('Z','Y').to_euler();ld.energy=power;ld.use_shadow=name=='Key';ld.angle=math.radians(10)
    s.view_layers[0].material_override=material(0,True) if clay else None
    s.render.image_settings.file_format='PNG';s.render.filepath=str(OUT/(unit+('_Clay' if clay else '')+'_'+view+'.png'));bpy.ops.render.render(write_still=True)
    s.view_layers[0].material_override=None

def main(unit):
    global PARTS
    bpy.ops.wm.read_factory_settings(use_empty=True);PARTS=[];s=bpy.context.scene;s.name=unit+'_Handbuilt_Review'
    s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
    report=build_sweeper() if unit=='Sweeper' else build_war_machine()
    lo,hi,tris,err=evaluated_stats(PARTS);target=15.10524 if unit=='Sweeper' else 57.61493
    scale=target/max(hi.x-lo.x,hi.y-lo.y);offset=Vector((-(lo.x+hi.x)/2,0,-lo.z))
    for o in PARTS:
        o.data.transform(Matrix.Scale(scale,4)@Matrix.Translation(offset))
        for mod in o.modifiers:
            if mod.type=='BEVEL':mod.width*=scale
    for group in ('sockets_m','wheel_pivots_m'):report[group]={k:list((Vector(v)+offset)*scale) for k,v in report[group].items()}
    report['wheel_radius_m']*=scale
    lo,hi,tris,err=evaluated_stats(PARTS)
    report.update(unit=unit,display_name='扫荡者' if unit=='Sweeper' else '战争机器',method='Authored Blender geometry from approved concept and three views. No Tripo geometry or texture.',references=[str(ROOT/'Concepts'/(unit+'_'+v+'.png')) for v in ('Concept_v1','Front','Left','Back')],palette=PALETTE,bounds_m=[list(lo),list(hi)],triangles=tris,symmetry_error_m=err,source_scale=scale,source_offset=list(offset),parts=len(PARTS))
    assert err<.0001,err
    for view in ('three_quarter','front','side','back','top'):render(unit,view)
    render(unit,'three_quarter',True)
    render(unit,'three_quarter')
    for o in PARTS:o.select_set(False)
    s['production_method']=report['method'];s['display_name']=report['display_name']
    (OUT/(unit+'_handbuilt_report.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(unit+'_Handbuilt_Editable.blend')))
    print('HANDBUILT_READY',json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':
    unit=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    for u in ('Sweeper','WarMachine') if unit=='all' else (unit,):main(u)

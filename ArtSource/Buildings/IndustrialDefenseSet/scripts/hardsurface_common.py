"""Deterministic hard-surface construction: planar profiles and narrow chamfers.
Reference: BP_ResourceProcessingFactory / native RPF source construction.
No generated geometry or noisy bitmap surface relief is used.
"""
import bpy,bmesh,math,json,sys
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).parent))
from blender_common import activate,setup_studio,render,describe,pack_save,bounds,aim

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'delivery_hardsurface'
WORK=ROOT/'hardsurface_work'
OUT.mkdir(exist_ok=True);WORK.mkdir(exist_ok=True)

COLORS={
 'Steel':((.075,.105,.13),.55,.48),
 'Dark':((.018,.027,.034),.42,.56),
 'Edge':((.26,.31,.34),.64,.42),
 'Teal':((.016,.135,.15),.24,.53),
 'Red':((.235,.044,.038),.22,.57),
 'Ochre':((.54,.29,.065),.18,.51),
 'Ivory':((.46,.51,.48),.18,.51),
 'Cyan':((.045,.62,.70),.08,.38),
 'Seal':((.022,.025,.028),.05,.70),
}

def make_materials():
    mats={}
    for role,(color,metal,rough) in COLORS.items():
        mat=bpy.data.materials.new('M_HS_'+role);mat.use_nodes=True
        bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        bs.inputs['Base Color'].default_value=(*color,1)
        bs.inputs['Metallic'].default_value=metal;bs.inputs['Roughness'].default_value=rough
        if role=='Cyan':
            bs.inputs['Emission Color'].default_value=(*color,1)
            bs.inputs['Emission Strength'].default_value=1.20
        mat.diffuse_color=(*color,1);mats[role]=mat
    return mats

def chamfer_rect(cx,cy,w,h,c):
    a=w/2;b=h/2
    return [(cx-a+c,cy-b),(cx+a-c,cy-b),(cx+a,cy-b+c),(cx+a,cy+b-c),
            (cx+a-c,cy+b),(cx-a+c,cy+b),(cx-a,cy+b-c),(cx-a,cy-b+c)]

def circle(cx,cy,r,n=32,angle=0):
    return [(cx+r*math.cos(angle+i*math.tau/n),cy+r*math.sin(angle+i*math.tau/n)) for i in range(n)]

class Builder:
    def __init__(self,name,mats):
        self.name=name;self.materials=mats;self.parts=[];self.catalog=[]
        self.scene=bpy.data.scenes.new(name);bpy.context.window.scene=self.scene
        self.scene.unit_settings.system='METRIC';self.scene.unit_settings.scale_length=1
        self.collection=bpy.data.collections.new('CONSTRUCTION_'+name)
        self.scene.collection.children.link(self.collection)
        self.transform=Matrix.Identity(4);self.group='static'
    def mesh(self,label,verts,faces,role='Steel',bevel=.025,kind='Planar extrusion'):
        vs=[self.transform @ Vector(v) for v in verts]
        mesh=bpy.data.meshes.new(label);mesh.from_pydata(vs,[],faces);mesh.update()
        bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
        ob=bpy.data.objects.new(label,mesh);self.collection.objects.link(ob)
        mesh.materials.append(self.materials[role])
        for p in mesh.polygons:p.use_smooth=False
        ob['ConstructionType']=kind;ob['RigidPart']=self.group;ob['SurfaceRole']=role
        ob['BevelWidthMeters']=bevel
        if bevel:
            mod=ob.modifiers.new('Consistent manufactured chamfer','BEVEL')
            mod.width=bevel;mod.segments=1;mod.limit_method='ANGLE';mod.angle_limit=math.radians(25)
            mod.use_clamp_overlap=True;mod.harden_normals=True
            mod=ob.modifiers.new('Weighted planar normals','WEIGHTED_NORMAL');mod.keep_sharp=True;mod.weight=40
        self.parts.append(ob)
        self.catalog.append({'name':ob.name,'group':self.group,'primitive':kind,'surface':role,'chamfer_m':bevel})
        return ob
    def box(self,label,loc,size,role='Steel',bevel=.025):
        x,y,z=loc;a,b,c=[v/2 for v in size]
        vs=[(x+i*a,y+j*b,z+k*c) for i,j,k in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        fs=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]
        return self.mesh(label,vs,fs,role,bevel,'Planar box')
    def prism(self,label,poly,lo,hi,axis='Z',role='Steel',bevel=.025):
        def co(p,t):return (p[0],p[1],t) if axis=='Z' else (p[0],t,p[1]) if axis=='Y' else (t,p[0],p[1])
        n=len(poly);vs=[co(p,t) for t in (lo,hi) for p in poly]
        fs=[tuple(range(n-1,-1,-1)),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        return self.mesh(label,vs,fs,role,bevel,'Straight profile extrusion')
    def cylinder(self,label,a,b,r,role='Steel',n=32,bevel=.02):
        a,b=Vector(a),Vector(b);axis=(b-a).normalized()
        u=axis.cross(Vector((0,0,1)))
        if u.length<.01:u=axis.cross(Vector((0,1,0)))
        u.normalize();v=axis.cross(u)
        vs=[p+r*(math.cos(i*math.tau/n)*u+math.sin(i*math.tau/n)*v) for p in (a,b) for i in range(n)]
        fs=[tuple(range(n-1,-1,-1)),tuple(range(n,n*2))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        return self.mesh(label,vs,fs,role,bevel,'Regular '+str(n)+'-sided cylinder')
    def cylz(self,label,center,r,z0,z1,role='Steel',n=32,bevel=.02):
        return self.cylinder(label,(*center,z0),(*center,z1),r,role,n,bevel)
    def ring(self,label,outer,inner,z0,z1,role='Steel',bevel=.015):
        n=len(outer);assert len(inner)==n
        vs=[(x,y,z) for z in (z0,z1) for loop in (outer,inner) for x,y in loop]
        fs=[]
        for i in range(n):
            j=(i+1)%n
            fs += [(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                   (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)]
        return self.mesh(label,vs,fs,role,bevel,'Concentric machined ring')
    def beam(self,label,a,b,width,role='Steel',bevel=.012,depth=None):
        a,b=Vector(a),Vector(b);axis=(b-a).normalized()
        u=axis.cross(Vector((0,0,1)))
        if u.length<.01:u=axis.cross(Vector((0,1,0)))
        u.normalize();v=axis.cross(u);h=width if depth is None else depth
        vs=[p+su*u*width/2+sv*v*h/2 for p in (a,b) for su,sv in [(-1,-1),(1,-1),(1,1),(-1,1)]]
        return self.mesh(label,vs,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],role,bevel,'Straight rectangular beam')
    def triangle(self,label,cx,cy,z,w,h,role='Ochre'):
        return self.prism(label,[(cx-w/2,cy-h/2),(cx+w/2,cy-h/2),(cx,cy+h/2)],z,z+.007,'Z',role,0)

def apply_parts(parts):
    for ob in parts:
        activate([ob])
        for mod in list(ob.modifiers):bpy.ops.object.modifier_apply(modifier=mod.name)

def join_parts(parts,name):
    activate(parts);bpy.ops.object.join()
    ob=bpy.context.object;ob.name=name
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    return ob

def uv_unwrap(objects):
    activate(objects)
    for ob in objects:
        if not ob.data.uv_layers:ob.data.uv_layers.new(name='UVMap')
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.003,area_weight=0,correct_aspect=True,scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')

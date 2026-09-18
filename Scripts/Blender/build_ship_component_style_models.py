"""Rule-geometry reconstruction of the three user-approved Ship references.

All design coordinates below are meters in the original UE mesh axes. Geometry
is authored from planar sections, repeated components and circular machined parts.
The original triangles are used only to measure Thor's mounting/cell layout.
Run with a factory-startup background Blender. No UE assets are edited/exported.
"""
import bpy
import bmesh
import collections
import importlib.util
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917'
OUT = ROOT / 'Production/v1'
PREVIEW = ROOT / 'Previews/v1/Geometry'
OUT.mkdir(parents=True, exist_ok=True)
PREVIEW.mkdir(parents=True, exist_ok=True)
SNAPSHOT = json.loads((ROOT / 'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
APPROVAL = json.loads((ROOT / 'approval_A_20260917.json').read_text(encoding='utf-8'))
assert APPROVAL['decision'] == 'approved'
PALETTE = {'Pearl': 'DDE6E6', 'Slate': '344B5D', 'Navy': '1E3041', 'Steel': '637C89', 'Ink': '172B3B',
           'Ochre': 'B98535', 'Cyan': '2F91A4', 'Brick': 'A45447'}
PARTS = []
CUTTERS = []
MATS = {}
KEY = ''
SOURCE_COLLECTION = None
CUTTER_COLLECTION = None


def point(p): return Vector((p[0], -p[1], p[2]))


def linear_hex(value):
    rgb = [int(value[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4 for c in rgb) + (1,)


def select(objects, active=None):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects: obj.select_set(True)
    bpy.context.view_layer.objects.active = active or objects[0]


def make_mesh(name, verts, faces, material, bone='Root', bevel=0, smooth=False, cutter=False):
    mesh = bpy.data.meshes.new(name + '_Mesh')
    mesh.from_pydata([point(p) for p in verts], [], faces)
    mesh.update()
    bm = bmesh.new(); bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh); bm.free()
    obj = bpy.data.objects.new(name, mesh)
    (CUTTER_COLLECTION if cutter else SOURCE_COLLECTION).objects.link(obj)
    mesh.materials.append(MATS[material])
    for polygon in mesh.polygons: polygon.use_smooth = smooth
    obj['rigid_bone'] = bone
    obj['design_reference'] = APPROVAL['parts'][KEY]['version']
    obj['authoring'] = 'Planar/circular rule geometry; editable source'
    if cutter:
        obj.display_type = 'WIRE'; obj.hide_render = True
        CUTTERS.append(obj)
    else:
        PARTS.append(obj)
        if bevel:
            mod = obj.modifiers.new('Narrow_Manufactured_Bevel', 'BEVEL')
            mod.width = bevel; mod.segments = 2; mod.limit_method = 'ANGLE'
            mod.angle_limit = math.radians(32); mod.harden_normals = True
            normal = obj.modifiers.new('Planar_Weighted_Normals', 'WEIGHTED_NORMAL')
            normal.keep_sharp = True; normal.weight = 50
    return obj


def prism_z(name, xy, z0, z1, mat='Pearl', bone='Root', bevel=.03):
    n = len(xy)
    verts = [(x, y, z) for z in (z0, z1) for x, y in xy]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, n * 2))]
    faces += [(i, (i + 1) % n, (i + 1) % n + n, i + n) for i in range(n)]
    return make_mesh(name, verts, faces, mat, bone, bevel)


def prism_x(name, yz, x0, x1, mat='Pearl', bone='Root', bevel=.03):
    n = len(yz)
    verts = [(x, y, z) for x in (x0, x1) for y, z in yz]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, n * 2))]
    faces += [(i, (i + 1) % n, (i + 1) % n + n, i + n) for i in range(n)]
    return make_mesh(name, verts, faces, mat, bone, bevel)


def rect_xy(cx, cy, w, d, chamfer=0):
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - d / 2, cy + d / 2
    c = min(chamfer, w * .2, d * .2)
    if c <= 1e-8:
        return [(x0,y0),(x1,y0),(x1,y1),(x0,y1)]
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c),
            (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]


def box(name, center, size, mat='Pearl', bone='Root', bevel=.02, chamfer=0):
    x, y, z = center; w, d, h = size
    return prism_z(name, rect_xy(x, y, w, d, chamfer), z - h / 2, z + h / 2, mat, bone, bevel)


def loft_y(name, sections, mat='Slate', bone='BarrelPitch', bevel=.02):
    # Each section is (UE y, a cyclic list of (x,z) coordinates).
    n = len(sections[0][1]); verts = [(x, y, z) for y, ring in sections for x, z in ring]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range((len(sections) - 1) * n, len(sections) * n))]
    for j in range(len(sections) - 1):
        faces += [(j*n+i, j*n+(i+1)%n, (j+1)*n+(i+1)%n, (j+1)*n+i) for i in range(n)]
    return make_mesh(name, verts, faces, mat, bone, bevel)


def rect_xz(cx, cz, hw, hh, corner=.15):
    c = min(corner, hw * .3, hh * .3)
    return [(cx-hw+c, cz-hh), (cx+hw-c, cz-hh), (cx+hw, cz-hh+c), (cx+hw, cz+hh-c),
            (cx+hw-c, cz+hh), (cx-hw+c, cz+hh), (cx-hw, cz+hh-c), (cx-hw, cz-hh+c)]


def cylinder(name, center, radius, depth, axis='Y', mat='Steel', bone='Root', sides=48, bevel=.015, cutter=False):
    cx, cy, cz = center
    verts = []
    for end in (-depth / 2, depth / 2):
        for i in range(sides):
            a = math.tau * i / sides; c, s = radius * math.cos(a), radius * math.sin(a)
            v = (cx+end, cy+c, cz+s) if axis == 'X' else ((cx+c, cy+end, cz+s) if axis == 'Y' else (cx+c, cy+s, cz+end))
            verts.append(v)
    faces = [tuple(range(sides-1, -1, -1)), tuple(range(sides, sides*2))]
    faces += [(i, (i+1)%sides, (i+1)%sides+sides, i+sides) for i in range(sides)]
    obj=make_mesh(name, verts, faces, mat, bone, bevel, True, cutter)
    obj.data.polygons[0].use_smooth=False;obj.data.polygons[1].use_smooth=False
    return obj


def tube(name, center, outer, inner, depth, axis='Y', mat='Steel', bone='BarrelPitch', sides=48):
    cx, cy, cz = center; verts = []
    for radius, offset in ((outer, -depth/2), (outer, depth/2), (inner, depth/2), (inner, -depth/2)):
        for i in range(sides):
            a = math.tau * i / sides; c, s = radius*math.cos(a), radius*math.sin(a)
            verts.append((cx+offset,cy+c,cz+s) if axis=='X' else ((cx+c,cy+offset,cz+s) if axis=='Y' else (cx+c,cy+s,cz+offset)))
    faces = [(j*sides+i, j*sides+(i+1)%sides, ((j+1)%4)*sides+(i+1)%sides, ((j+1)%4)*sides+i) for j in range(4) for i in range(sides)]
    obj = make_mesh(name, verts, faces, mat, bone, 0, True)
    obj.data.materials.append(MATS['Ink'])
    for p in obj.data.polygons:
        if 2*sides <= p.index < 3*sides: p.material_index = 1
        if sides <= p.index < 2*sides or p.index >= 3*sides: p.use_smooth = False
    return obj


def subtract(obj, cutter):
    mod = obj.modifiers.new('Machined_Bore_' + cutter.name, 'BOOLEAN')
    mod.operation = 'DIFFERENCE'; mod.solver = 'EXACT'; mod.object = cutter
    # Keep machining before bevel/normal modifiers.
    idx = list(obj.modifiers).index(mod)
    obj.modifiers.move(idx, 0)


def hollow_rect_barrel(name, x, z, tip):
    sections = [(14.2, rect_xz(x,z,1.50,.70)), (18.0,rect_xz(x,z,.82,.56)),
                (22.5,rect_xz(x,z,.68,.48)), (tip,rect_xz(x,z,.68,.48))]
    obj = loft_y(name, sections, 'Slate', 'BarrelPitch', .045)
    cut = box(name+'_Cutter', (x,(tip+13.8)/2,z), (.88,tip-13.8+1,.57), 'Ink', 'Root', 0)
    PARTS.remove(cut); SOURCE_COLLECTION.objects.unlink(cut); CUTTER_COLLECTION.objects.link(cut)
    cut.hide_render=True;cut.display_type='WIRE';CUTTERS.append(cut)
    subtract(obj,cut)
    box(name+'_Bore_Stop',(x,14.22,z),(.88,.10,.57),'Ink','BarrelPitch',.01)
    return obj


def twin():
    p = SNAPSHOT['parts'][KEY]; pitch = [v/100 for v in p['bones'][1]['local']['location']]
    # Rear mounting bridge, floor and longitudinal load paths leave both sweep channels open.
    box('Twin_Rear_Mount_Bridge',(0,-14.8,-3.66),(18.05,5.6,.8782208),'Navy',bevel=.07,chamfer=.45)
    box('Twin_Rear_CrossMember',(0,-16.4,.2),(15.0,2.6,5.5),'Slate',bevel=.11,chamfer=.6)
    center_profile=[(-17.4,-2.8),(9.2,-2.8),(11.0,-1.5),(6.0,2.7),(-16.7,2.7)]
    prism_x('Twin_Central_Spine',center_profile,-1.40,1.40,'Pearl',bevel=.06)
    for y in (-8.2,-11.3,-14.4):
        prism_x('Twin_Center_Service_Rib',[(y-1.1,2.72),(y+1.1,2.72),(y+.7,3.83),(y-.7,3.83)],-1.25,1.25,'Slate',bevel=.05)
    for side, socket in zip((-1,1), p['sockets']):
        tag = 'L' if side < 0 else 'R'
        x,tip,z=[v/100 for v in socket['mesh_space']['location']]
        rear=[(-20.402744,.24),(-16.1,.24),(-3.8,2.54),(-3.8,4.269),(-17.1,4.269),(-20.402744,3.95)]
        prism_x('Twin_'+tag+'_Rear_Rail',rear,side*2.20,side*4.65,'Pearl',bevel=.055)
        fin=[(-20.0,3.90),(-17.05,3.90),(-17.05,5.97345398),(-19.15,5.60)]
        prism_x('Twin_'+tag+'_Rear_Stabilizer',fin,side*2.87,side*3.14,'Slate',bevel=.018)
        side_profile=[(-17.6,-3.25),(9.6,-3.25),(9.1,-2.00),(5.8,1.5),(-2.6,3.1),(-5.8,3.7),(-14.7,3.7),(-17.6,2.40)]
        loadrail=prism_x('Twin_'+tag+'_Outer_LoadRail',side_profile,side*7.0,side*9.35,'Navy',bevel=.07)
        top_line=[(-16.8,2.80),(-14.5,3.92),(-5.9,3.92),(-2.55,3.32),(5.7,1.73),(9.2,-1.77)]
        top_skin=top_line+[(y,z-.20) for y,z in reversed(top_line)]
        prism_x('Twin_'+tag+'_Layered_Upper_Cheek',top_skin,side*7.02,side*9.4,'Pearl',bevel=.034)
        back_plate=[(-17.1,-2.75),(-3.8,-2.75),(-3.8,2.70),(-6.1,3.55),(-14.3,3.55),(-17.1,2.35)]
        rearplate=prism_x('Twin_'+tag+'_Rear_Armor',back_plate,side*9.33,side*10.02,'Pearl',bevel=.065)
        front_plate=[(-2.5,-2.9),(9.55,-2.9),(9.1,-1.9),(5.85,1.55),(-2.5,2.85)]
        frontplate=prism_x('Twin_'+tag+'_Front_Armor',front_plate,side*8.6,side*9.95,'Pearl',bevel=.065)
        accent=[(-1.2,-2.45),(6.70,-2.45),(7.05,-1.87),(4.90,1.05),(-1.2,2.17)]
        accentplate=prism_x('Twin_'+tag+'_Ochre_SidePanel',accent,side*9.96,side*10.0838794,'Ochre',bevel=.025)
        joint_cut=cylinder('CUT_Twin_'+tag+'_Joint_Clearance',(side*8.5,pitch[1],pitch[2]),1.52,4.2,'X','Ink','Root',64,0,True)
        for joint_part in (loadrail,frontplate,accentplate):subtract(joint_part,joint_cut)
        prism_x('Twin_'+tag+'_Rear_ColorCap',[(-17.0,-2.7),(-15.1,-2.7),(-15.1,2.65),(-17,2.1)],side*10.03,side*10.07,'Ochre',bevel=.015)
        for i in range(5):
            y=-13.9+i*1.25
            cut=box('CUT_Twin_'+tag+'_Cooling_%02d'%i,(side*9.9,y,1.62),(1.3,.74,2.48),'Ink',bevel=0)
            PARTS.remove(cut);SOURCE_COLLECTION.objects.unlink(cut);CUTTER_COLLECTION.objects.link(cut);CUTTERS.append(cut);cut.hide_render=True
            subtract(rearplate,cut)
            prism_x('Twin_'+tag+'_Cooling_Recess_%02d'%i,[(y-.37,.30),(y+.37,.30),(y+.37,2.85),(y-.37,2.85)],side*9.38,side*9.40,'Navy',bevel=.018)
            box('Twin_'+tag+'_Cooling_Crown_%02d'%i,(side*9.25,y,3.69),(1.28,.42,.50),'Pearl',bevel=.045)
        # Fixed annular bearing and a smaller rotating axle leave a genuine joint clearance.
        tube('Twin_'+tag+'_Fixed_Pitch_Race',(side*8.27,pitch[1],pitch[2]),1.43,1.05,.50,'X','Slate','Root')
        cylinder('Twin_'+tag+'_Pitch_Axle',(side*7.3,pitch[1],pitch[2]),.94,2.50,'X','Steel','BarrelPitch',48,.04)
        cylinder('Twin_'+tag+'_Bearing_Cap',(side*9.57,pitch[1],pitch[2]),1.00,.15,'X','Navy','Root',48,.025)
        sections=[(-2.4,rect_xz(x,-.27,1.9,1.8,.3)),(3.8,rect_xz(x,-.27,2.05,2.10,.35)),
                  (11.8,rect_xz(x,-.65,1.85,1.35,.25)),(15.65,rect_xz(x,-.885,1.3,.8,.18))]
        loft_y('Twin_'+tag+'_Moving_Receiver',sections,'Slate','BarrelPitch',.055)
        top=[(-2.2,1.54),(3.9,1.89),(10.0,1.10),(14.3,.12),(14.3,.40),(10.0,1.38),(3.9,2.17),(-2.2,1.82)]
        prism_x('Twin_'+tag+'_Receiver_Armor',top,x-1.92,x+1.92,'Pearl','BarrelPitch',.05)
        box('Twin_'+tag+'_Ochre_Receiver_Cover',(x,3.2,2.18),(2.65,5.7,.33),'Ochre','BarrelPitch',.04,chamfer=.32)
        for dx in (-1.15,1.15):
            box('Twin_'+tag+'_Receiver_Stiffener',(x+dx,3.3,2.40),(.26,4.8,.20),'Ochre','BarrelPitch',.02)
        hollow_rect_barrel('Twin_'+tag+'_Hollow_Rectangular_Barrel',x,z,tip)
        box('Twin_'+tag+'_Barrel_Top_Guide',(x,15.0,.01),(1.35,2.2,.23),'Pearl','BarrelPitch',.045,chamfer=.13)


def ciws():
    p=SNAPSHOT['parts'][KEY]; pitch=[v/100 for v in p['bones'][1]['local']['location']]
    box('CIWS_Mount_Sole',(0,-.40,-.85316),(3.04,4.60,.1063194),'Navy',bevel=.014,chamfer=.14)
    box('CIWS_Rear_Frame',(0,-1.66,-.04),(2.80,1.72,1.70),'Slate',bevel=.025,chamfer=.20)
    box('CIWS_Rear_Lower_Bumper',(0,-2.58,-.25),(2.80,.267565,.50),'Navy',bevel=.015,chamfer=.05)
    box('CIWS_Rear_Canopy',(0,-1.56,.81),(2.15,1.75,.20),'Pearl',bevel=.025,chamfer=.12)
    profile=[(-2.50,-.84),(1.91,-.84),(1.87,-.61),(.37,.90),(-.82,.985),(-1.43,.89),(-2.5,.20)]
    for side in (-1,1):
        tag='L' if side<0 else 'R'
        prism_x('CIWS_'+tag+'_Structural_Cheek',profile,side*.87,side*1.55,'Navy',bevel=.025)
        front=[(-.86,.88),(.28,.80),(1.84,-.66),(1.80,-.80),(1.05,-.80),(-1.10,.47)]
        prism_x('CIWS_'+tag+'_Sloping_Pearl_Armor',front,side*.94,side*1.555,'Pearl',bevel=.026)
        top=[(-1.44,.86),(-.83,.99),(.38,.93),(.42,.81),(-.76,.84),(-1.44,.71)]
        prism_x('CIWS_'+tag+'_Upper_Shoulder',top,side*.94,side*1.54,'Pearl',bevel=.018)
        accent=[(-1.20,.65),(-.98,.80),(.25,.72),(.61,.34),(.46,.15),(-.99,.36)]
        prism_x('CIWS_'+tag+'_Cyan_Flank',accent,side*1.557,side*1.60052094,'Cyan',bevel=.012)
        box('CIWS_'+tag+'_Rear_Heat_Shroud',(side*1.37,-1.85,.20),(.45,1.12,.72),'Pearl',bevel=.025,chamfer=.04)
        for i in range(4):
            y=-2.20+i*.23
            prism_x('CIWS_'+tag+'_Four_Slot_Vent_%02d'%i,[(y-.045,-.02),(y+.045,-.02),(y+.045,.47),(y-.045,.47)],side*1.596,side*1.600,'Navy',bevel=.008)
        panel=[(-1.34,-.66),(-1.34,-.11),(-.22,-.10),(.19,-.54),(.12,-.66)]
        prism_x('CIWS_'+tag+'_Lower_Service_Cover',panel,side*1.552,side*1.590,'Slate',bevel=.008)
        for y,z in ((-1.2,-.55),(-1.2,-.22),(-.18,-.52)):
            cylinder('CIWS_'+tag+'_Captive_Fastener',(side*1.596,y,z),.018,.012,'X','Steel','Root',12,0)
        fin=[(-1.9118,.895),(-1.381,.895),(-1.381,1.44417862),(-1.78,1.33)]
        prism_x('CIWS_'+tag+'_Rear_Fin',fin,side*1.027,side*1.102,'Pearl',bevel=.009)
        tube('CIWS_'+tag+'_Fixed_Pitch_Race',(side*.90,pitch[1],pitch[2]),.38,.265,.11,'X','Slate','Root')
        cylinder('CIWS_'+tag+'_Pitch_Axle',(side*.76,pitch[1],pitch[2]),.235,.34,'X','Steel','BarrelPitch',48,.009)
    # Rounded gun shroud: semicircular top and a flat, chamfered lower tray.
    def shroud_section(y, scale=1):
        ring=[(-.65*scale,-.255),(.65*scale,-.255),(.73*scale,-.12),(.73*scale,.19)]
        ring += [(.73*scale*math.cos(a),.19+.46*scale*math.sin(a)) for a in [math.pi*i/16 for i in range(1,17)]]
        ring += [(-.73*scale,-.12)]
        return y,ring
    shroud=loft_y('CIWS_Rounded_Moving_Shroud',[shroud_section(.12,.96),shroud_section(.55,1),shroud_section(1.46,.88)],'Cyan','BarrelPitch',.009)
    for poly in shroud.data.polygons:
        if poly.index>=2 and 3<=(poly.index-2)%21<=18:poly.use_smooth=True
    tube('CIWS_Front_Machined_Collar',(0,1.51,.21),.335,.265,.18,'Y','Slate','BarrelPitch')
    cylinder('CIWS_Dark_Receiver_End',(0,1.44,.21),.31,.03,'Y','Navy','BarrelPitch',48,0)
    for i,s in enumerate(p['sockets'],1):
        x,tip,z=[v/100 for v in s['mesh_space']['location']]
        tube('CIWS_Barrel_%02d'%i,(x,(1.46+tip)/2,z),.1015,.0635,tip-1.46,'Y','Steel','BarrelPitch',48)
        tube('CIWS_Muzzle_Rim_%02d'%i,(x,tip-.055,z),.1135,.0635,.110,'Y','Slate','BarrelPitch',48)
    # Three-hole clamp plates, retaining live boolean cutters in the editable source.
    clamp_section=[(-.28,.07),(-.18,-.055),(.18,-.055),(.28,.07),(.17,.445),(-.17,.445)]
    for j,y in enumerate((3.38,3.82)):
        obj=loft_y('CIWS_Triangular_Cluster_Clamp_%d'%j,[(y-.055,clamp_section),(y+.055,clamp_section)],'Slate','BarrelPitch',.010)
        for i,s in enumerate(p['sockets']):
            x,_,z=[v/100 for v in s['mesh_space']['location']]
            cut=cylinder('CUT_CIWS_Clamp_%d_%d'%(j,i),(x,y,z),.104,.25,'Y','Ink','Root',48,0,True)
            subtract(obj,cut)


def thor_top_measurements(data, seed):
    points=data['geometry']['points']; comp=next(c for c in data['components'] if c['seed']==seed)
    ids=set(comp['ids']); z=max(points[i][2] for i in ids)
    triangles=[t for t in data['geometry']['triangles'] if set(t)<=ids and all(abs(points[i][2]-z)<.01 for i in t)]
    edges=collections.Counter(tuple(sorted((a,b))) for t in triangles for a,b in zip(t,t[1:]+t[:1]))
    adj=collections.defaultdict(set)
    for (a,b),count in edges.items():
        if count==1: adj[a].add(b);adj[b].add(a)
    seen=set(); loops=[]
    for start in adj:
        if start in seen:continue
        loop=[];current=start;previous=None
        while current not in seen:
            seen.add(current);loop.append(current)
            candidates=adj[current]-({previous} if previous is not None else set())
            nxt=next(iter(candidates));previous,current=current,nxt
        loops.append([(points[i][0]/100,points[i][1]/100) for i in loop])
    outer=next(loop for loop in loops if len(loop)==8)
    holes=[]
    for loop in loops:
        if len(loop)!=16:continue
        holes.append(tuple((min(p[j] for p in loop)+max(p[j] for p in loop))/2 for j in (0,1)))
    assert len(holes)==3
    return outer,holes,z/100


def thor():
    data=json.loads((ROOT/'Source/Thor_MissilePod_original_geometry.json').read_text(encoding='utf-8'))
    core_seeds=(866,0,641,450,215)
    flank_seeds=(1165,1081,1155,1145,396)
    foot_seeds=((1037,1059),(1101,1123),(812,834),(406,428),(171,193))
    measured=[]
    for cell,(seed,flank_seed,feet) in enumerate(zip(core_seeds,flank_seeds,foot_seeds),1):
        outer,holes,top=thor_top_measurements(data,seed)
        center=tuple(sum(p[j] for p in outer)/len(outer) for j in (0,1))
        def scaled(amount): return [(center[0]+(x-center[0])*amount,center[1]+(y-center[1])*amount) for x,y in outer]
        body=prism_z('Thor_Cell%02d_Canister'%cell,scaled(1.085),-2.97,1.87,'Slate',bevel=.055)
        lower=prism_z('Thor_Cell%02d_Lower_Chassis'%cell,scaled(1.087),-2.975, -2.57,'Navy',bevel=.035)
        collar=prism_z('Thor_Cell%02d_Pearl_Armor_Collar'%cell,scaled(1.093),1.65,2.72,'Pearl',bevel=.07)
        lid=prism_z('Thor_Cell%02d_Brick_Lid'%cell,scaled(.991),2.69,top,'Brick',bevel=.038)
        # Three broad planar armor faces over the canister, with deliberate dark
        # corner gaps; the approved hero is not an entirely bare blue canister.
        wall=scaled(1.086)
        edges=sorted([(math.dist(a,b),a,b) for a,b in zip(wall,wall[1:]+wall[:1])],reverse=True,key=lambda e:e[0])[:3]
        for edge_index,(_,a,b) in enumerate(edges):
            a,b=Vector(a),Vector(b);mid=(a+b)/2;delta=b-a
            normal=Vector((delta.y,-delta.x)).normalized()
            if normal.dot(mid-Vector(center))<0:normal=-normal
            a=mid+(a-mid)*.86;b=mid+(b-mid)*.86
            verts=[]
            for offset in (.035,.17):
                for xy,z in ((a,-2.57),(b,-2.57),(b,1.68),(a,1.68)):
                    q=xy+normal*offset;verts.append((q.x,q.y,z))
            make_mesh('Thor_Cell%02d_Side_Armor_%d'%(cell,edge_index),verts,
                      [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'Pearl','Root',.045)
        for hole,(x,y) in enumerate(holes,1):
            cut=cylinder('CUT_Thor_%02d_%02d_Bore'%(cell,hole),(x,y,.0),1.27,8,'Z','Ink','Root',64,0,True)
            subtract(body,cut);subtract(lower,cut);subtract(collar,cut)
            lidcut=cylinder('CUT_Thor_%02d_%02d_Collar'%(cell,hole),(x,y,2.8),1.583,1.8,'Z','Ink','Root',64,0,True)
            subtract(lid,lidcut)
            # A circular machined mouth and a separate dark bore liner.
            ring=tube('Thor_Cell%02d_Bore%02d_Mouth'%(cell,hole),(x,y,top-.19),1.58,1.25,.36,'Z','Steel','Root',64)
            tube('Thor_Cell%02d_Bore%02d_Liner'%(cell,hole),(x,y,.02),1.26,1.235,5.1,'Z','Navy','Root',64)
            cylinder('Thor_Cell%02d_Bore%02d_Dark_Floor'%(cell,hole),(x,y,-2.54),1.235,.05,'Z','Ink','Root',64,0)
        flank=next(c for c in data['components'] if c['seed']==flank_seed)
        lo,hi=([v/100 for v in flank[k]] for k in ('min','max'))
        side=-1 if (lo[0]+hi[0])/2<center[0] else 1
        cx,cy,cz=[(a+b)/2 for a,b in zip(lo,hi)]; sx,sy,sz=[b-a for a,b in zip(lo,hi)]
        box('Thor_Cell%02d_Outboard_Frame'%cell,(cx-side*.18,cy,cz),(sx-.36,sy,sz),'Navy',bevel=.065,chamfer=.22)
        face=lo[0] if side<0 else hi[0]
        panel=[(cy-sy*.42,-2.68),(cy+sy*.42,-2.68),(cy+sy*.42,1.48),(cy+sy*.27,1.93),(cy-sy*.32,1.93),(cy-sy*.42,1.47)]
        plate=prism_x('Thor_Cell%02d_Outboard_Pearl_Plate'%cell,panel,face-side*.34,face-side*.07,'Pearl',bevel=.045)
        hatch=[(cy-.67,-.85),(cy+.67,-.85),(cy+.67,.73),(cy+.52,.88),(cy-.67,.88)]
        prism_x('Thor_Cell%02d_Service_Hatch'%cell,hatch,face-side*.055,face,'Pearl',bevel=.014)
        for j in range(3):
            y=cy-sy*.35+j*.34
            cut=box('CUT_Thor_%02d_Vent_%d'%(cell,j),(face-side*.15,y,-.7),(1,.20,2.4),'Ink',bevel=0)
            PARTS.remove(cut);SOURCE_COLLECTION.objects.unlink(cut);CUTTER_COLLECTION.objects.link(cut);CUTTERS.append(cut);cut.hide_render=True
            subtract(plate,cut)
            prism_x('Thor_Cell%02d_Cooling_%d'%(cell,j),[(y-.10,-1.9),(y+.10,-1.9),(y+.10,.5),(y-.10,.5)],face-side*.38,face-side*.33,'Slate',bevel=.025)
        for foot_seed in feet:
            foot=next(c for c in data['components'] if c['seed']==foot_seed)
            a,b=([v/100 for v in foot[k]] for k in ('min','max'))
            box('Thor_Cell%02d_Mount_Pad_%d'%(cell,foot_seed),((a[0]+b[0])/2,(a[1]+b[1])/2,-2.745),
                (b[0]-a[0],b[1]-a[1],.50),'Navy',bevel=.045,chamfer=.3)
        measured.append({'cell':cell,'original_core_seed':seed,'top_outline_m':outer,'visible_bore_centers_m':holes})
    (OUT/'Thor_measured_cell_layout.json').write_text(json.dumps(measured,indent=2),encoding='utf-8')


def armature():
    part=SNAPSHOT['parts'][KEY]
    if 'bones' not in part:return None
    arm=bpy.data.objects.new('Armature',bpy.data.armatures.new('SK_Style_'+KEY));bpy.context.scene.collection.objects.link(arm)
    select([arm]);bpy.ops.object.mode_set(mode='EDIT')
    root=arm.data.edit_bones.new('Root');root.head=(0,0,0);root.tail=(1,0,0);root.align_roll(Vector((0,0,1)))
    pitch=point([v/100 for v in part['bones'][1]['local']['location']])
    barrel=arm.data.edit_bones.new('BarrelPitch');barrel.head=pitch;barrel.tail=pitch+Vector((0,-1,0));barrel.parent=root
    barrel.use_connect=False;barrel.align_roll(Vector((0,0,1)))
    bpy.ops.object.mode_set(mode='OBJECT');arm.show_in_front=True;arm.data.display_type='STICK'
    arm['reference_UE_socket_data']='//../../../Source/source_snapshot_v1.json'
    return arm


def bind(obj,arm,bone):
    if not arm:return
    vg=obj.vertex_groups.get(bone) or obj.vertex_groups.new(name=bone)
    vg.add(list(range(len(obj.data.vertices))),1,'REPLACE')
    obj.parent=arm
    mod=obj.modifiers.new('Rigid_Single_Bone_Skin','ARMATURE');mod.object=arm


def socket_markers(arm):
    for s in SNAPSHOT['parts'][KEY]['sockets']:
        marker=bpy.data.objects.new(s['name'],None);bpy.context.scene.collection.objects.link(marker)
        loc=s.get('mesh_space',{}).get('location',s['location'])
        marker.location=point([v/100 for v in loc]);marker.rotation_euler=(0,0,-math.pi/2) if arm else (0,0,0)
        marker.empty_display_type='ARROWS';marker.empty_display_size=.15 if KEY=='CIWS' else .7
        marker['ue_interface']=json.dumps(s);marker.hide_render=True
        if arm:
            bpy.context.view_layer.update()
            world=marker.matrix_world.copy();marker.parent=arm;marker.parent_type='BONE';marker.parent_bone='BarrelPitch'
            bpy.context.view_layer.update();marker.matrix_world=world


def render_geometry(objects, arm):
    scene=bpy.context.scene;scene.render.engine='BLENDER_WORKBENCH'
    scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.display.shading.light='STUDIO';scene.display.shading.studiolight_rotate_z=.6
    scene.display.shading.color_type='MATERIAL';scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
    scene.display.shading.cavity_type='BOTH';scene.display.shading.background_type='WORLD';scene.world.color=(.11,.14,.17)
    scene.view_settings.view_transform='Standard'
    camera=bpy.data.objects.new('ReviewCamera',bpy.data.cameras.new('ReviewCamera'));scene.collection.objects.link(camera);scene.camera=camera
    camera.data.type='ORTHO';camera.data.clip_end=2000
    points=[]
    deps=bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        evaluated=obj.evaluated_get(deps);mesh=evaluated.to_mesh();points.extend([evaluated.matrix_world@v.co for v in mesh.vertices]);evaluated.to_mesh_clear()
    lo=Vector(tuple(min(p[i] for p in points) for i in range(3)));hi=Vector(tuple(max(p[i] for p in points) for i in range(3)))
    center=(lo+hi)/2;span=max(hi-lo)
    for view,direction in [('hero',(1.1,-1.7,1.65)),('right',(1,0,0)),('top',(0,0,1))]:
        camera.location=center+Vector(direction).normalized()*span*3;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
        bpy.context.view_layer.update(); projected=[camera.matrix_world.inverted()@p for p in points]
        offset=Vector(((min(p.x for p in projected)+max(p.x for p in projected))/2,(min(p.y for p in projected)+max(p.y for p in projected))/2,0))
        camera.location+=camera.rotation_euler.to_matrix()@offset
        camera.data.ortho_scale=max(max(p.x for p in projected)-min(p.x for p in projected),(max(p.y for p in projected)-min(p.y for p in projected))*4/3)*1.16
        scene.render.filepath=str(PREVIEW/(KEY+'_geometry_'+view+'.png'));bpy.ops.render.render(write_still=True)
    return {'bounds_blender_m':[list(lo),list(hi)],'dimensions_m':list(hi-lo),'source_parts':len(objects)}


def build(key):
    global PARTS,CUTTERS,MATS,KEY,SOURCE_COLLECTION,CUTTER_COLLECTION
    KEY=key;PARTS=[];CUTTERS=[];MATS={}
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.name=key+'_Source';scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    scene.world=bpy.data.worlds.new('ReviewWorld')
    SOURCE_COLLECTION=bpy.data.collections.new('EDITABLE_'+key);scene.collection.children.link(SOURCE_COLLECTION)
    CUTTER_COLLECTION=bpy.data.collections.new('CONSTRUCTION_Cutters');scene.collection.children.link(CUTTER_COLLECTION)
    for label,color in PALETTE.items():
        mat=bpy.data.materials.new('SC_'+label);mat.diffuse_color=linear_hex(color);mat['base_hex']=color
        mat['roughness']=.58 if label not in ('Steel','Slate') else .46;mat['metallic']=.10 if label in ('Pearl','Ochre','Cyan','Brick') else .65
        MATS[label]=mat
    {'Twin_Barrel_Turret':twin,'CIWS':ciws,'Thor_MissilePod':thor}[key]()
    # Evaluate construction at rest before adding skin. Baked preview geometry is
    # deliberately deferred until the editable geometry has been visually checked.
    arm=armature()
    for obj in PARTS:bind(obj,arm,obj['rigid_bone'])
    for obj in CUTTERS:obj.hide_set(True)
    socket_markers(arm)
    report=render_geometry(PARTS,arm)
    report.update({'key':key,'A_reference_version':APPROVAL['parts'][key]['version'],'A_reference_sha256':APPROVAL['parts'][key]['sha256'],
                   'part_names':[o.name for o in PARTS],'cutters':len(CUTTERS),'bone_count':2 if arm else 0,
                   'sockets':[s['name'] for s in SNAPSHOT['parts'][key]['sockets']],'status':'editable_geometry_review; not B approved'})
    scene['review_stage']='A approved; geometry under production; B pending'
    if arm:select([arm])
    else:select([PARTS[0]])
    target=OUT/(key+'_EditableGeometry.blend')
    bpy.ops.wm.save_as_mainfile(filepath=str(target))
    (OUT/(key+'_geometry_manifest.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('SHIP_STYLE_GEOMETRY_READY',key,json.dumps({k:report[k] for k in ('dimensions_m','source_parts','cutters','bone_count')}),flush=True)


if __name__=='__main__':
    for key in ('Twin_Barrel_Turret','CIWS','Thor_MissilePod'):build(key)
    print('SHIP_STYLE_ALL_EDITABLE_GEOMETRY_COMPLETE',flush=True)

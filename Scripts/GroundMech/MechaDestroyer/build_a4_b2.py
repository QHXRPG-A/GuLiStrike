"""A4 B2: rebuild solid armor volumes after the user's rejected B1 comparison.

Keeps B1 on disk. The approved two source drawings remain the authority.
"""
from pathlib import Path
import json, shutil

HERE=Path(__file__).parent
base=(HERE/'build_a4_blender.py').read_text(encoding='utf8')
base=base.replace("if __name__=='__main__':",'if False:')
base=base.replace('Production_A4_B_v1','Production_A4_B_v2').replace('GuLi.GroundMech.ApprovedA4.B1','GuLi.GroundMech.ApprovedA4.B2')
base=base.replace('GMA4_','GMA4B2_').replace('GroundMech_A4_','GroundMech_A4B2_').replace('Editable_B_v1.blend','Editable_B_v2.blend')
base=base.replace("(sign*1.53,.02,3.94)","(sign*1.47,.04,4.02)")
base=base.replace("(sign*2.00,-.59,2.97)","(sign*1.72,-.66,3.045)")
base=base.replace("(sign*1.03,.03,2.25)","(sign*.90,.03,2.40)")
base=base.replace("(sign*1.04,-.57,1.64)","(sign*1.00,-.50,1.62)")
base=base.replace("(sign*1.15,.05,.62)","(sign*1.15,.38,.62)")
base=base.replace("Vector((side*1.16,.05,0))","Vector((side*1.16,.38,0))")
base=base.replace('v.co+=normal*.005','v.co+=normal*.009')
base=base.replace("axis=Vector((0,-.996,-.089)).normalized()","axis=Vector(((-.40 if b.name.endswith('_R') else .40),-.91,-.18)).normalized()")
base=base.replace('up=Vector((0,axis.z,-axis.y))','up=Vector((0,axis.z,-axis.y)).normalized()')
exec(compile(base,str(HERE/'build_a4_blender.py'),'exec'),globals())

OUT.mkdir(parents=True,exist_ok=True)
(OUT/'References').mkdir(exist_ok=True)
previous=OUT.parent/'Production_A4_B_v1'
for f in ['source_rig_contract.json','approval_A.json']:
    if not (OUT/f).exists():shutil.copy2(previous/f,OUT/f)
for f in ['Approved_A4_Concept.png','Approved_A4_ThreeViews.png']:
    if not (OUT/'References'/f).exists():shutil.copy2(previous/'References'/f,OUT/'References'/f)

BASE_MATERIAL=material
def material(name,role,rig=None):
    m=BASE_MATERIAL(name,role,rig)
    if role not in ['Ink','Amber','Outline']:
        ramp=next(n for n in m.node_tree.nodes if n.type=='VALTORGB')
        tints={
            'TeamPrimary':[(.12,.20,.24),(.38,.88,1.00),(.86,1.30,1.35)],
            'TeamSecondary':[(.42,.50,.56),(.90,1.00,1.12),(1.60,1.75,1.90)],
            'Mechanism':[(.60,.65,.75),(1.70,1.65,1.90),(2.70,2.60,2.95)],
            'Cream':[(.75,.70,.61),(1.04,1.02,.93),(1.21,1.18,1.08)],
            'Sand':[(.54,.53,.48),(.96,.95,.90),(1.24,1.22,1.14)],
        }[role]
        red_tints=[(.42,.38,.43),(.96,.88,.94),(1.38,1.34,1.37)]
        for band,(e,p,rgb) in enumerate(zip(ramp.color_ramp.elements,[0,.38,.89],tints)):
            e.position=p;e.color=(*rgb,1)
            if role=='TeamPrimary':
                for ch in range(3):
                    d=e.driver_add('color',ch).driver
                    v=d.variables.new();v.name='team';v.type='SINGLE_PROP';v.targets[0].id=rig;v.targets[0].data_path='["TeamIndex"]'
                    d.expression='%.8f+(%.8f)*min(1,max(0,team))'%(rgb[ch],red_tints[band][ch]-rgb[ch])
        m['tone_factors']=[list(x) for x in tints]
        # Occlusion enters before the quantizer, retaining exactly three bands.
        n,l=m.node_tree.nodes,m.node_tree.links
        src=ramp.inputs[0].links[0].from_socket;l.remove(ramp.inputs[0].links[0])
        ao=n.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.075;ao.only_local=False
        mul=n.new('ShaderNodeMath');mul.operation='MULTIPLY'
        l.new(src,mul.inputs[0]);l.new(ao.outputs['AO'],mul.inputs[1]);l.new(mul.outputs[0],ramp.inputs[0])
    return m

BASE_LATHE=lathe
def lathe(name,center,axis,profile,roles,bone='cockpit',sides=16,group='Torso',phase=0):
    o=BASE_LATHE(name,center,axis,profile,roles,bone,sides,group,phase)
    if sides>=24:
        for p in o.data.polygons[:-2]:p.use_smooth=True
    return o

def solid(name,pixels,yfront,yback,role='TeamPrimary',bone='cockpit',group='Torso',inset=.10,bevel=.008):
    """Solid eight/few sided armor with a real broad perimeter chamfer."""
    ring=[Vector(xz(*p)) for p in pixels]
    c=sum(ring,Vector((0,0)))/len(ring)
    xf=lambda z:yfront(z) if callable(yfront) else yfront
    xb=lambda z:yback(z) if callable(yback) else yback
    verts=[]
    for level in range(4):
        scale=(1-inset) if level in [0,3] else 1
        for v in ring:
            q=c+(v-c)*scale;x,z=q
            front=xf(z);back=xb(z)
            y=front if level==0 else front+.075 if level==1 else back-.055 if level==2 else back
            verts.append((x,y,z))
    n=len(ring);faces=[tuple(reversed(range(n)))]
    for j in range(3):
        for i in range(n):faces.append((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i))
    faces.append(tuple(range(3*n,4*n)))
    return mesh(name,verts,faces,role,bone,bevel,group)

def mirrored(pix,sign):return pix if sign<0 else mirror_front(pix)

SIDE_SCALE=4.88/489.0
def side_point(u,v):return ((u-222)*SIDE_SCALE,(511-v)*SIDE_SCALE)

# Seat the tapered bridge tail inside the compact integrated heel.
FOOT_BRIDGE_SECTIONS=[(-1.05,-.42,.42,.16,.39,.08),
                      (-.57,-.43,.43,.11,.50,.09),
                      (-.10,-.29,.29,.10,.43,.065),
                      (.28,-.26,.26,.09,.24,.055)]

def build_foot_bridge(label,sign):
    o=foot_wedge('Foot_BlueBridge_'+label,sign,FOOT_BRIDGE_SECTIONS,'TeamPrimary','Foot_'+label)
    o['assembly_revision']='integrated_heel_20261023'
    o['heel_overlap_local_y_m']=.26
    return o

def build_integrated_heel(label,sign):
    # One low, chamfered wedge per foot; no pair of protruding cuboids.
    sections=[(.02,-.29,.29,0,.31,.055),
              (.15,-.37,.37,0,.29,.065),
              (.37,-.34,.34,0,.22,.055),
              (.48,-.25,.25,0,.16,.045)]
    o=foot_wedge('Foot_IntegratedHeel_'+label,sign,sections,'Mechanism','Foot_'+label)
    o['assembly_revision']='integrated_heel_20261023'
    o['assembly']='Single tapered heel seated below ankle and overlapping the instep bridge'
    return o

def build_toe_pair(label,sign):
    """Two short tapered toes, with sloped tips and flush chamfered insets."""
    parts={}
    for i,x in enumerate([-.30,.30]):
        name='Foot_SeparateToe_%s_%s'%(label,i)
        sections=[(-1.55,x-.16,x+.16,0,.15,.02),
                  (-1.40,x-.25,x+.25,0,.23,.045),
                  (-1.06,x-.265,x+.265,0,.36,.065),
                  (-.86,x-.235,x+.235,.04,.38,.06),
                  (-.65,x-.17,x+.17,.09,.34,.04)]
        o=foot_wedge(name,sign,sections,'Mechanism','FootEnd_'+label)
        # The leading cap leans backward; it is not a vertical cuboid end.
        offset=Matrix.Rotation(math.radians(18*sign),3,'Z')@Vector((0,.07,0))
        for j in (2,3):o.data.vertices[j].co+=offset
        o.data.update();o['assembly_revision']='tapered_toes_20261023';parts[name]=o
        name='Foot_ToeCream_%s_%s'%(label,i)
        inset=[(x-.105,-1.34),(x+.105,-1.34),(x+.15,-1.30),(x+.15,-1.14),
               (x+.125,-1.10),(x-.125,-1.10),(x-.15,-1.14),(x-.15,-1.30)]
        points=[foot_transform((u,y,.23+(y+1.40)*(.13/.34)+.008),sign) for u,y in inset]
        o=panel(name,points,.016,'Sand','FootEnd_'+label,normal=(0,0,1),ink=False,group='Legs')
        o['assembly_revision']='tapered_toes_20261023';parts[name]=o
    return parts

def side_armor(name,pixels,sign,outer_x,depth,role,bone,group='Legs',bevel=.035):
    pts=[(sign*(outer_x(z) if callable(outer_x) else outer_x),y,z) for y,z in [side_point(u,v) for u,v in pixels]]
    return prism(name,pts,(-sign*depth,0,0),role,bone,bevel,group)

def front_loft(name,pixels,sign,depth_rows,role,bone,group,cut=.045):
    """Front outline with deliberate depth changes at broad armor facets.

    Every ring is planar. The front can incline at chosen levels without a
    warped ngon, and the bevel band is a real eight-corner cross section.
    """
    poly=[xz(*p) for p in mirrored(pixels,sign)]
    z0=min(p[1] for p in poly)+.003;z1=max(p[1] for p in poly)-.003
    rows=sorted(depth_rows)
    def depth_at(z):
        for a,b in zip(rows,rows[1:]):
            if a[0]<=z<=b[0]:
                t=(z-a[0])/(b[0]-a[0]);return (a[1]*(1-t)+b[1]*t,a[2]*(1-t)+b[2]*t)
        return rows[0][1:] if z<rows[0][0] else rows[-1][1:]
    zs=sorted(set([z0,z1]+[p[1] for p in poly if z0+.015<p[1]<z1-.015]+[r[0] for r in rows if z0+.015<r[0]<z1-.015]))
    verts=[]
    for z in zs:
        x0,x1=span_at(poly,z);yf,yb=depth_at(z)
        cx=min(cut,(x1-x0)*.18);cy=min(cut,(yb-yf)*.18)
        verts.extend([(x0+cx,yf,z),(x1-cx,yf,z),(x1,yf+cy,z),(x1,yb-cy,z),(x1-cx,yb,z),(x0+cx,yb,z),(x0,yb-cy,z),(x0,yf+cy,z)])
    faces=[tuple(reversed(range(8)))]
    for j in range(len(zs)-1):
        faces.extend((j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i) for i in range(8))
    faces.append(tuple(range(len(verts)-8,len(verts))))
    return mesh(name,verts,faces,role,bone,.004,group)

def span_at(poly,z):
    zmin=min(p[1] for p in poly);zmax=max(p[1] for p in poly)
    z=max(zmin+.001,min(zmax-.001,z));xs=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        if min(a[1],b[1])<=z<=max(a[1],b[1]) and abs(b[1]-a[1])>1e-7:
            xs.append(a[0]+(b[0]-a[0])*(z-a[1])/(b[1]-a[1]))
    assert len(xs)>=2,(poly,z,xs)
    return min(xs),max(xs)

def profile_volume(name,front_pixels,side_pixels,sign,role,bone,group='Legs'):
    """Intersect the front width and side depth profiles at each height."""
    fp=[xz(*p) for p in mirrored(front_pixels,sign)]
    sp=[side_point(*p) for p in side_pixels]
    z0=min(p[1] for p in sp)+.003;z1=max(p[1] for p in sp)-.003
    # Interior rows make the intended broad sloped faces explicit.
    heights=sorted(set([z0,z1]+[p[1] for p in fp+sp if z0+.015<p[1]<z1-.015]))
    verts=[]
    for z in heights:
        x0,x1=span_at(fp,z);y0,y1=span_at(sp,z)
        cutx=min(.07,(x1-x0)*.16);cuty=min(.065,(y1-y0)*.16)
        ring=[(x0+cutx,y0),(x1-cutx,y0),(x1,y0+cuty),(x1,y1-cuty),(x1-cutx,y1),(x0+cutx,y1),(x0,y1-cuty),(x0,y0+cuty)]
        verts.extend((x,y,z) for x,y in ring)
    faces=[tuple(reversed(range(8)))]
    for j in range(len(heights)-1):
        for i in range(8):faces.append((j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i))
    faces.append(tuple(range(len(verts)-8,len(verts))))
    return mesh(name,verts,faces,role,bone,.006,group)

def plate(name,pix,front,depth=.07,role='Cream',bone='cockpit',group='Torso',bevel=.015):
    pts=[(x,front(x,z) if callable(front) else front,z) for x,z in [xz(*p) for p in pix]]
    return prism(name,pts,(0,depth,0),role,bone,bevel,group)

def raised_chest_armor(name,pixels):
    """Separate solid armor, with an exposed chamfer and a fitted deep back.

    The rear ring penetrates the continuous chest mounting bed. The light
    face sits 116 mm in front of that bed, rather than tracing an ink polygon
    onto a coplanar cream surface.
    """
    poly=[Vector(xz(*p)) for p in pixels];c=sum(poly,Vector((0,0)))/len(poly)
    verts=[]
    for scale,offset in [(1,.035),(1,-.065),(.925,-.145)]:
        for p in poly:
            x,z=c+(p-c)*scale;verts.append((x,chest_front(x,z)+offset,z))
    n=len(poly);faces=[tuple(reversed(range(n)))]
    for ring in range(2):
        faces.extend((ring*n+i,ring*n+(i+1)%n,(ring+1)*n+(i+1)%n,(ring+1)*n+i) for i in range(n))
    faces.append(tuple(range(2*n,3*n)))
    o=mesh(name,verts,faces,'Cream','cockpit',.007,'Torso')
    o.data.materials.append(MATS['Sand'])
    for p in o.data.polygons:
        if 1<=p.index<=n:p.material_index=1
    o['assembly']='Independent bolted armor on continuous chest; rear seated, front raised'
    o['nominal_depth_m']=.18;o['face_proud_of_mount_m']=.117
    return o

def chest_front(x,z):
    return (-.91+(z-3.64)*.72 if z>=3.64 else -.91+(3.64-z)*.63)+abs(x)*.055

def build_back_armor_revision():
    """Back-view A4 profiles, with a narrow side wall outside the blue covers."""
    parts={}
    def keep(name,obj):
        obj['assembly_revision']='back_assembly_20261023'
        parts[name]=obj
        return obj
    # Central saddle stays below the cooling fins. The old crest covered them.
    keep('Back_UpperSpineCrest',box('Back_UpperSpineCrest',(0,.44,4.18),(.86,.20,.12),'Mechanism',bevel=.018))
    # Source drawing is 1536x1024, back-view centre 1217.5, common floor 464.
    back_profiles={
        'Back_UpperPanel':([(1183,92),(1252,92),(1252,132),(1246,140),(1189,140),(1183,132)],.83,.18,.026),
        'Back_MiddlePanel':([(1189,139),(1246,139),(1244,165),(1238,172),(1197,172),(1191,165)],.82,.20,.024),
        'Back_LowerPanel':([(1197,171),(1238,171),(1236,192),(1230,198),(1205,198),(1199,192)],lambda z:.56+(z-2.984)*.66,.22,.024),
    }
    for name,(profile,y,depth,bevel) in back_profiles.items():
        points=[((u-1217.5)*S,y((464-v)*S) if callable(y) else y,(464-v)*S) for u,v in profile]
        keep(name,prism(name,points,(0,-depth,0),'Mechanism',bevel=bevel))
    for sign,label in [(-1,'R'),(1,'L')]:
        name='Back_DeepMechanismShell_'+label
        keep(name,side_armor(name,[(276,72),(317,88),(323,131),(334,153),(331,176),(311,194),(283,202),(259,176),(252,133)],sign,1.07,.105,'Mechanism','cockpit','Torso',.018))
        name='Back_PodSupport_'+label
        keep(name,side_armor(name,[(229,52),(272,52),(289,73),(294,103),(254,107),(229,87)],sign,.70,.22,'Mechanism','cockpit','Torso',.022))
        name='Back_BlueShoulderLobe_'+label
        profile=[(1142,75),(1178,75),(1181,84),(1181,130),(1172,143),(1142,141),(1137,125),(1137,87)]
        points=[]
        for u,v in profile:
            x=sign*(1217.5-u)*S;z=(464-v)*S
            points.append((x,.68+.19*(4.364-z),z))
        keep(name,prism(name,points,(0,-.35,0),'TeamPrimary',bevel=.027))
    return parts

def build_top_pod_rear_faces():
    """The approved rear view has concentric rings and an amber core too."""
    parts={}
    for sign,label in [(-1,'R'),(1,'L')]:
        axis=Vector((sign*.13,-.985,-.11)).normalized()
        c=Vector((sign*.66,.05,4.59737))-axis*.323
        name='TopPod_RearConcentricRims_'+label
        parts[name]=lathe(name,c,-axis,[(0,.203),(.020,.197),(.031,.164),(.044,.151),(.057,.114)],['Mechanism','TeamPrimary','Mechanism','TeamSecondary','Ink'],sides=32)
        name='TopPod_RearAmberLens_'+label
        parts[name]=cylinder(name,c-axis*.061,-axis,.085,.014,'Amber',sides=32)
        for part in parts.values():part['assembly_revision']='back_assembly_20261023'
    return parts

def build_torso():
    # A true sectioned volume follows both slopes; a single nonplanar ngon did not.
    verts=[]
    levels=[(2.97,.26,.30),(3.23,.40,.47),(3.64,1.01,.59),(3.86,1.20,.60),(4.16,1.02,.48),(4.28,.78,.40)]
    for z,w,yback in levels:
        ring=[(-w,chest_front(-w,z)+.08),(-w*.84,chest_front(-w*.84,z)+.015),(0,chest_front(0,z)+.015),(w*.84,chest_front(w*.84,z)+.015),(w,chest_front(w,z)+.08),(w,yback-.08),(w*.78,yback),(-w*.78,yback),(-w,yback-.08)]
        verts.extend((x,y,z) for x,y in ring)
    faces=[tuple(reversed(range(9)))]
    for row in range(len(levels)-1):
        for i in range(9):faces.append((row*9+i,row*9+(i+1)%9,(row+1)*9+(i+1)%9,(row+1)*9+i))
    faces.append(tuple(range(len(verts)-9,len(verts))))
    mesh('Chest_ContinuousSectionedShell',verts,faces,'TeamSecondary',bevel=.012)
    upper=[(308,87),(350,84),(362,89),(380,89),(380,142),(358,142),(312,139),(281,121),(282,112)]
    for sign,label in [(-1,'R'),(1,'L')]:
        plate('Chest_ContinuousCreamFrame_'+label,mirrored(upper,sign),lambda x,z:chest_front(x,z)-.028,.09,'Cream',bevel=.013)
    # Two independent armor parts. Their side walls and broad chamfers are
    # visible geometry, and their back faces remain seated in the chest shell.
    L=[(308,95),(339,94),(351,98),(364,122),(350,133),(311,132),(291,117)]
    for sign,label in [(-1,'R'),(1,'L')]:
        p=mirrored(L,sign)
        c=(sum(x for x,y in p)/len(p),sum(y for x,y in p)/len(p))
        gasket=[(c[0]+(x-c[0])*1.018,c[1]+(y-c[1])*1.018) for x,y in p]
        plate('Chest_RecessedMountGasket_'+label,gasket,lambda x,z:chest_front(x,z)-.040,.08,'Mechanism',bevel=.004)
        raised_chest_armor('Chest_IndependentArmor_'+label,p)
    plate('Chest_TopIntake',[(369,96),(391,96),(386,120),(374,120)],lambda x,z:chest_front(x,z)-.050,.04,'Ink',bevel=.006)
    plate('Chest_IntakeDivider',[(378,97),(382,97),(382,118),(378,118)],lambda x,z:chest_front(x,z)-.066,.04,'Mechanism',bevel=.004)
    # Lower cream keel has a projected top chamfer and a raised planar face.
    plate('Chest_KeelDarkBorder',[(364,137),(396,137),(403,147),(395,185),(387,198),(374,198),(365,185),(357,147)],lambda x,z:chest_front(x,z)-.04,.10,'Mechanism',bevel=.015)
    keel=[(366,139),(394,139),(399,146),(392,178),(368,178),(361,146)]
    plate('Chest_KeelCream',keel,lambda x,z:chest_front(x,z)-.17,.15,'Cream',bevel=.022)
    for sign,label in [(-1,'R'),(1,'L')]:
        p=mirrored([(314,139),(356,139),(362,161),(357,176),(339,161)],sign)
        plate('Chest_LowerCream_'+label,p,lambda x,z:chest_front(x,z)-.041,.075,'Cream',bevel=.014)
        vent=mirrored([(319,143),(349,143),(341,158)],sign)
        plate('Chest_LowerVent_'+label,vent,lambda x,z:chest_front(x,z)-.060,.04,'Ink',bevel=.004)
        # Real narrow open lower frame follows the angled torso side.
        rail=mirrored([(283,126),(293,131),(317,158),(361,193),(360,201),(350,197),(307,165)],sign)
        plate('Chest_IntegratedLowerEdge_'+label,rail,lambda x,z:chest_front(x,z)-.018,.105,'TeamPrimary',bevel=.012)
        beam('Chest_RearLowerRail_'+label,(sign*1.02,.35,3.50),(sign*.31,.21,2.97),.115,.14,'Mechanism',bevel=.012)
        beam('Chest_SideBrace_'+label,(sign*1.10,.19,3.77),(sign*.75,-.30,3.15),.13,.14,'Mechanism',bevel=.015)
    # A tapered three-piece back, with broad overlapping blue shoulder-back covers.
    body=[(-.74,.58,4.18),(.74,.58,4.18),(.86,.70,3.82),(.59,.74,3.32),(.31,.37,3.02),(-.31,.37,3.02),(-.59,.74,3.32),(-.86,.70,3.82)]
    prism('Back_TaperedCore',body,(0,-.39,0),'Mechanism',bevel=.06)
    build_back_armor_revision()
    for sign,label in [(-1,'R'),(1,'L')]:
        side_armor('Back_DeepSideLobe_'+label,[(269,123),(310,146),(313,177),(284,194),(255,177),(252,150)],sign,1.095,.36,'TeamPrimary','cockpit','Torso',.035)
        side_armor('Back_LowerAngledLobe_'+label,[(257,181),(281,195),(259,219),(240,205)],sign,.91,.26,'TeamSecondary','cockpit','Torso',.025)
    box('Cooling_Base',(0,.12,4.23),(.92,.71,.14),'Mechanism',bevel=.035)
    for i in range(4):
        x=(i-1.5)*.178
        p=[(x-.058,-.29,4.24),(x-.058,-.22,4.59),(x-.058,.30,4.59),(x-.058,.47,4.40),(x-.058,.47,4.25)]
        prism('Cooling_Blade_%02d'%i,p,(.116,0,0),'Sand',bevel=.019)
    for sign,label in [(-1,'R'),(1,'L')]:
        c=Vector((sign*.66,.05,4.59737));pod_axis=Vector((sign*.13,-.985,-.11)).normalized()
        box('TopPod_Mount_'+label,(sign*.66,.12,4.28),(.30,.41,.27),'Mechanism',bevel=.035)
        lathe('TopPod_ThickArmor_'+label,c,pod_axis,[(-.33,.205),(-.22,.26),(.16,.28),(.215,.24)],['TeamSecondary','TeamPrimary','TeamPrimary','TeamSecondary'],sides=12)
        lathe('TopPod_ConcentricRims_'+label,c+pod_axis*.218,pod_axis,[(0,.238),(.028,.227),(.045,.190),(.068,.175),(.081,.137),(.093,.12)],['Mechanism','TeamPrimary','Mechanism','TeamSecondary','Ink','Ink'],sides=32)
        cylinder('TopPod_AmberLens_'+label,c+pod_axis*.322,pod_axis,.095,.018,'Amber',sides=32)
    build_top_pod_rear_faces()
    cylinder('Waist_YawBearing',(0,.04,2.86),(0,0,1),.29,.20,'Mechanism','MiddleJoint',24,'Pelvis')
    box('Waist_UpperNeck',(0,.03,2.69),(.48,.48,.27),'Mechanism','MiddleJoint',.045,'Pelvis')
    front_loft('Pelvis_CompactBlock',[(351,238),(409,238),(414,253),(412,283),(405,290),(355,290),(348,283),(346,253)],-1,[(1.95,-.36,.28),(2.06,-.50,.34),(2.33,-.50,.34),(2.54,-.25,.28)],'Mechanism','MiddleJoint','Pelvis',.045)
    cylinder('Pelvis_RecessedAxle',(0,.03,2.40),(1,0,0),.235,1.38,'Mechanism','MiddleJoint',24,'Pelvis')
    for sign,label in [(-1,'R'),(1,'L')]:
        c=Vector((sign*.65,.50,3.05));d=Vector((0,.19,-.98)).normalized()
        lathe('Jet_Housing_'+label,c,d,[(-.11,.14),(-.045,.18),(.09,.18),(.14,.15),(.145,.11),(.11,.11)],['Mechanism','Mechanism','Sand','Mechanism','Ink','Ink'],sides=24,group='Sockets')
        lathe('Jet_AmberInnerLip_'+label,c,d,[(.142,.145),(.153,.115),(.155,.105)],['Amber','Amber','Amber'],sides=32,group='Sockets')
        cylinder('Jet_Lens_'+label,c+d*.13,d,.10,.02,'Amber',sides=24,group='Sockets')
        SOCKETS.append({'name':'Jet_Outlet_'+label,'bone':'cockpit','position':list(c+d*.16),'direction':list(d)})

def build_arms():
    shoulder=[(240,61),(270,61),(283,75),(282,111),(273,130),(243,130),(228,117),(228,82)]
    upper=[(239,127),(260,137),(237,172),(219,188),(200,180),(208,163)]
    for sign,label in [(-1,'R'),(1,'L')]:
        arm='GunArm_'+label;gun='Gun_'+label;group='Arms_'+label
        c=Vector((sign*1.47,.04,4.02))
        cylinder('Shoulder_Bearing_'+label,c,(sign,0,0),.36,.40,'Mechanism',arm,32,group)
        # One continuous housing: its octagonal side profile is also the
        # depth profile of the front shoulder, without a second floating pad.
        shoulder_object=side_armor('Shoulder_DeepArmor_'+label,[(186,63),(244,64),(270,83),(275,117),(258,137),(222,145),(190,133),(172,109),(172,84)],sign,1.72,.62,'TeamPrimary',arm,group,.075)
        shoulder_object.data.materials.append(MATS['TeamSecondary'])
        for face in shoulder_object.data.polygons:
            if face.index==1 or face.center.y>.26:face.material_index=1
        pivot=Vector((sign*1.73,.035,4.01));shoulder_axis=Vector((sign*.99,-.12,0)).normalized()
        lathe('Shoulder_StackedJoint_'+label,pivot,shoulder_axis,[(-.06,.35),(.025,.38),(.08,.38),(.12,.325),(.15,.315),(.19,.26),(.205,.215),(.225,.205)],['TeamSecondary','Sand','Sand','Mechanism','Mechanism','TeamSecondary','Ink','Ink'],arm,32,group)
        cylinder('Shoulder_AmberHub_'+label,pivot+shoulder_axis*.24,shoulder_axis,.10,.02,'Amber',arm,32,group)
        a=Vector((sign*1.47,.05,3.77));elbow=Vector((sign*1.72,-.46,2.99))
        beam('UpperArm_InternalLink_'+label,a,elbow,.22,.25,'Mechanism',arm,.025,group)
        armfront=lambda z:-.32-(3.77-z)*.46
        solid('UpperArm_ForwardArmor_'+label,mirrored(upper,sign),armfront,.12,'TeamPrimary',arm,group,.16,.009)
        stripe=mirrored([(236,137),(244,143),(222,172),(213,169)],sign)
        plate('UpperArm_CreamInset_'+label,stripe,lambda x,z:armfront(z)-.02,.05,'Sand',arm,group,.009)
        rear_top=Vector((sign*1.20,.38,3.72));rear_low=Vector((sign*1.42,.21,2.67))
        beam('UpperArm_BackRail_'+label,rear_top,rear_low,.13,.15,'TeamSecondary',arm,.015,group)
        beam('UpperArm_InnerRail_'+label,rear_low,(sign*1.90,-.57,2.49),.12,.14,'TeamSecondary',arm,.012,group)
        cylinder('Elbow_DarkAxle_'+label,elbow,(sign,0,0),.245,.60,'Mechanism',arm,24,group)
        d=Vector((sign*.40,-.91,-.18)).normalized();q=d.to_track_quat('Z','Y');cannon=elbow+Vector((0,-.20,.055))
        chamfer_loft('Cannon_Volume_'+label,cannon,d,[(-.19,.58,.57,.09),(.05,.74,.64,.11),(.67,.69,.60,.105),(.80,.55,.46,.075)],'TeamPrimary',arm,group)
        for side,word in [(-1,'A'),(1,'B')]:
            p=[(side*.373,-.20,.10),(side*.350,-.20,.59),(side*.350,.18,.59),(side*.373,.19,.10)]
            panel('Cannon_CreamSide_'+label+word,[tuple(cannon+q@Vector(v)) for v in p],.023,'Cream',arm,normal=q@Vector((side,0,0)),group=group)
        # A visible open trapezoidal cage below the main barrel housing.
        for side in [-1,1]:
            pa=cannon+q@Vector((side*.26,-.44,-.12));pb=cannon+q@Vector((side*.25,-.32,.64))
            beam('Cannon_OpenLowerRail_%s_%s'%(label,side),pa,pb,.105,.115,'TeamSecondary',arm,.012,group)
            beam('Cannon_CageRear_%s_%s'%(label,side),pa,cannon+q@Vector((side*.27,-.16,-.12)),.11,.11,'TeamPrimary',arm,.012,group)
        beam('Cannon_CageFront_'+label,cannon+q@Vector((-.25,-.32,.64)),cannon+q@Vector((.25,-.32,.64)),.11,.11,'TeamPrimary',arm,.012,group)
        # Side elbow cuff overlaps the cannon rear and exposes a concentric pivot.
        side_armor('Elbow_SandCuff_'+label,[(155,202),(181,211),(208,228),(207,254),(190,269),(167,264),(146,246)],sign,2.07,.20,'Sand',arm,group,.027)
        ey,ez=side_point(165,255);cuff=Vector((sign*2.085,ey,ez))
        lathe('Elbow_OuterRings_'+label,cuff,(sign,0,0),[(0,.205),(.025,.205),(.055,.157),(.066,.125)],['Mechanism','Mechanism','TeamSecondary','Mechanism'],arm,28,group)
        side_plate=[(194,149),(221,157),(210,198),(183,190)]
        mid=(sum(p[0] for p in side_plate)/4,sum(p[1] for p in side_plate)/4)
        frame=[(mid[0]+(u-mid[0])*1.14,mid[1]+(v-mid[1])*1.07) for u,v in side_plate]
        side_armor('UpperArm_SideMount_'+label,frame,sign,lambda z:1.765+.72*(3.60-z),.25,'TeamSecondary',arm,group,.018)
        side_armor('UpperArm_SideCream_'+label,side_plate,sign,lambda z:1.78+.72*(3.60-z),.20,'Cream',arm,group,.012)
        mb=cannon+d*.81
        lathe('Cannon_MuzzleCollars_'+label,mb,d,[(0,.255),(.075,.255),(.10,.227),(.22,.227),(.25,.22)],['Mechanism','TeamSecondary','Mechanism','Mechanism','Mechanism'],gun,32,group)
        muzzle=mb+d*.28
        tube('Cannon_OpenBore_'+label,muzzle,d,.22,.15,.13,'Mechanism',gun,group,32)
        cylinder('Cannon_VisibleAmberCore_'+label,muzzle+d*.004,d,.083,.012,'Amber',gun,32,group)
        SOCKETS.append({'name':'Muzzle' if label=='R' else 'Muzzle_L','bone':gun,'position':list(muzzle+d*.066),'direction':list(d)})

def build_legs():
    hip=[(287,225),(318,226),(329,238),(326,252),(310,265),(284,263),(276,253),(278,238)]
    thigh=[(268,266),(313,269),(321,284),(316,309),(306,326),(278,326),(258,309),(256,285)]
    shin=[(269,349),(299,351),(299,369),(281,389),(261,382),(255,369)]
    for sign,label in [(-1,'R'),(1,'L')]:
        up='UpperLeg_'+label;low='Leg_'+label;foot='Foot_'+label;toe='FootEnd_'+label
        hp=Vector((sign*.90,.03,2.40));kn=Vector((sign*1.00,-.50,1.62));an=Vector((sign*1.15,.38,.62))
        cylinder('Hip_Bearing_'+label,hp,(sign,0,0),.26,.58,'Mechanism',up,24,'Legs')
        front_loft('Hip_FullArmor_'+label,hip,sign,[(2.17,-.57,.30),(2.29,-.63,.39),(2.51,-.40,.38),(2.68,-.26,.29)],'TeamPrimary',up,'Legs',.045)
        side_armor('Hip_OuterSand_'+label,[(245,244),(261,251),(270,274),(264,293),(250,304),(242,294),(251,273)],sign,1.41,.17,'Sand',up,bevel=.025)
        side_armor('Hip_BlueSideFacet_'+label,[(211,249),(239,253),(252,279),(237,303),(211,311),(191,296),(188,273)],sign,1.27,.28,'TeamPrimary',up,bevel=.035)
        beam('Thigh_HiddenCore_'+label,hp,kn,.24,.24,'Mechanism',up,.025,'Legs')
        profile_volume('Thigh_CompleteArmor_'+label,thigh,[(175,273),(196,289),(191,309),(154,347),(128,353),(110,326),(130,303)],sign,'TeamPrimary',up)
        # Back guard is inside the thick shell, leaving the blue back planes visible.
        back_thigh=[(295+(x-288.5)*.65,z) for x,z in thigh]
        front_loft('Thigh_BackGuard_'+label,back_thigh,sign,[(1.56,-.62,-.40),(1.85,-.50,-.22),(2.05,-.32,-.04),(2.23,-.18,.10)],'TeamPrimary',up,'Legs',.035)
        cylinder('Knee_Axle_'+label,kn,(sign,0,0),.195,.48,'Mechanism',low,32,'Legs')
        cylinder('Knee_OuterPivotShaft_'+label,kn+Vector((sign*.34,0,0)),(sign,0,0),.13,.22,'Mechanism',low,24,'Legs')
        lathe('Knee_OuterPivot_'+label,kn+Vector((sign*.42,0,0)),(sign,0,0),[(0,.20),(.025,.20),(.045,.165),(.06,.165)],['Ink','Mechanism','Mechanism','Mechanism'],low,32,'Legs')
        side_armor('Knee_OuterSandArmor_'+label,[(211,333),(231,340),(246,359),(247,380),(227,391),(204,379),(191,355)],sign,1.42,.15,'Sand',low,bevel=.035)
        side_armor('Knee_CapMount_'+label,[(189,333),(211,333),(222,355),(212,379),(190,373),(177,353)],sign,1.31,.35,'Mechanism',low,bevel=.02)
        capcenter=kn+Vector((-sign*.27,.57,-.18))
        beam('Knee_InnerCapMount_'+label,kn+Vector((0,.10,-.14)),capcenter,.18,.20,'Mechanism',low,.018,'Legs')
        lathe('Knee_InnerBlueArmor_'+label,kn+Vector((-sign*.27,.57,-.18)),(-sign*.75,-.66,0),[(-.04,.18),(.04,.22),(.13,.20)],['TeamSecondary','TeamPrimary','TeamPrimary'],low,6,'Legs',math.pi/6)
        beam('Shin_HiddenCore_'+label,kn,an,.235,.24,'Mechanism',low,.025,'Legs')
        profile_volume('Shin_ForwardArmor_'+label,[(272,337),(301,343),(304,361),(286,390),(264,384),(257,369)],[(153,366),(181,370),(213,391),(207,417),(176,413),(146,384)],sign,'TeamPrimary',low)
        # A broad rear calf shell closes the former exposed stick silhouette.
        back_calf=[(292+(x-280.5)*.80,z) for x,z in [(272,337),(301,343),(304,361),(286,390),(264,384),(257,369)]]
        front_loft('Calf_BackArmor_'+label,back_calf,sign,[(.84,.02,.30),(1.13,-.16,.14),(1.44,-.35,-.10)],'TeamPrimary',low,'Legs',.028)
        side_armor('Shin_SandSideInsert_'+label,[(219,399),(237,409),(246,432),(235,450),(214,444),(207,423)],sign,1.40,.17,'Sand',low,bevel=.028)
        cylinder('Ankle_DarkBearing_'+label,an,(sign,0,0),.20,.46,'Mechanism',foot,24,'Legs')
        side_armor('Ankle_RearGuard_'+label,[(244,432),(262,428),(282,458),(278,477),(252,484),(233,464)],sign,1.48,.47,'Mechanism',foot,bevel=.035)
        profile_volume('Ankle_LargeBlueBoot_'+label,[(248,388),(279,389),(291,405),(286,422),(238,422),(235,409)],[(188,423),(211,425),(220,449),(207,466),(148,465),(159,445)],sign,'TeamPrimary',foot)
        beam('Foot_InstepLink_'+label,an,(sign*1.26,-.30,.57),.30,.26,'Mechanism',foot,.025,'Legs')
        build_toe_pair(label,sign)
        build_foot_bridge(label,sign)
        build_integrated_heel(label,sign)

BASE_CONTROLS=add_controls_and_sockets
def add_controls_and_sockets():
    BASE_CONTROLS()
    for side,sign,prop in [('R',-1,'RightRecoil'),('L',1,'LeftRecoil')]:
        b=RIG.pose.bones['Gun_'+side]
        local=b.bone.matrix_local.to_3x3().inverted()@(-Vector((sign*.40,-.91,-.18)).normalized())
        for i in range(3):
            b.driver_add('location',i).driver.expression='recoil*%.12f'%local[i]

BASE_SCENE=create_scene
def create_scene():
    s=BASE_SCENE()
    bg=next(n for n in s.world.node_tree.nodes if n.type=='BACKGROUND')
    bg.inputs[0].default_value=(1,1,1,1);bg.inputs[1].default_value=1
    return s

BASE_OUTLINES=outlines
def outlines():
    shells=BASE_OUTLINES()
    for obj in shells:
        obj.visible_shadow=False;obj.visible_diffuse=False;obj.visible_glossy=False
        obj.visible_transmission=False;obj.visible_volume_scatter=False
    return shells

BASE_REVIEWS=configure_reviews
def configure_reviews(red):
    scenes=BASE_REVIEWS(red)
    chest=review_scene('GroundMech_A4B2_04_ChestAssembly',1800,1200)
    instance(chest,BLUE,'Chest_Assembly_Blue',(0,0,0))
    camera(chest,'Chest_Assembly_Camera',(7,-14,6.3),(0,-.12,3.64),4.7)
    chest.render.filepath=str(OUT/'Previews/04_ChestAssembly.png')
    heels=review_scene('GroundMech_A4B2_05_HeelAssembly',1800,1200)
    instance(heels,BLUE,'Heel_Assembly_Blue',(0,0,0))
    camera(heels,'Heel_Assembly_Camera',(6,11,5),(0,.15,.65),4.8)
    heels.render.filepath=str(OUT/'Previews/05_HeelAssembly.png')
    toes=review_scene('GroundMech_A4B2_06_ToeAssembly',1800,1200)
    instance(toes,BLUE,'Toe_Assembly_Blue',(0,0,0))
    camera(toes,'Toe_Assembly_Camera',(6,-11,5),(0,-.30,.55),4.8)
    toes.render.filepath=str(OUT/'Previews/06_ToeAssembly.png')
    return (*scenes,chest,heels,toes)

BASE_AUDIT=audit_geometry
def audit_geometry(shells,redrig):
    report=BASE_AUDIT(shells,redrig)
    report['approval_B']='changes_requested'
    report['output_visual_QA']='Revision in progress after user difference reports; not a fidelity pass or user approval'
    report['chest_panel_assembly']={
        'parts':[PREFIX+'Chest_IndependentArmor_R',PREFIX+'Chest_IndependentArmor_L'],
        'separate_closed_solids':True,
        'nominal_depth_m':.18,
        'face_proud_of_mount_m':.117,
        'back_overlaps_mount_m':.063,
        'seated_on_continuous_support':True,
        'gaskets':'Below solid parts; not painted contour lines'
    }
    return report

exec(compile((HERE/'a4_b2_annotation_revision.py').read_text(encoding='utf8'),
             str(HERE/'a4_b2_annotation_revision.py'),'exec'),globals())
exec(compile((HERE/'a4_b2_gun_detail_revision.py').read_text(encoding='utf8'),
             str(HERE/'a4_b2_gun_detail_revision.py'),'exec'),globals())
BASE_LEGS_BEFORE_ANNOTATIONS=build_legs

def build_legs():
    BASE_LEGS_BEFORE_ANNOTATIONS()
    install_annotation_revision_in_full_build()
    install_gun_detail_revision_in_full_build()

BASE_REVIEWS_BEFORE_ANNOTATIONS=configure_reviews

def configure_reviews(red):
    return (*BASE_REVIEWS_BEFORE_ANNOTATIONS(red),*configure_annotation_review_scenes())

if __name__=='__main__':
    result=main()
    print('A4_B2_BUILD_RESULT '+json.dumps(result))

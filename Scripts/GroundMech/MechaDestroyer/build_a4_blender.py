"""Author the approved A4 mech from editable hard-surface profiles.

Reference pixels are source-design measurements, never output-image tests.
Original package mesh, armature and other Blender scenes remain untouched.
"""
from pathlib import Path
import bpy, bmesh, math, json, hashlib
from mathutils import Vector, Matrix, Quaternion

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1'
OWNER = 'GuLi.GroundMech.ApprovedA4.B1'
PREFIX = 'GMA4_'
S = 4.88 / 435.0
CX, FLOOR = 380.0, 464.0
PARTS = []
PALETTE = {'Cream':'FEE4D9','Sand':'D5C09C','Mechanism':'2C3735','Ink':'1A182F',
           'Amber':'EE9D58','BluePrimary':'6AA4BE','BlueSecondary':'274E61',
           'RedPrimary':'A34053','RedSecondary':'662249'}

def linear(h):
    values=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values)+(1,)

def activate(o):
    if bpy.context.object and bpy.context.object.mode!='OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active=o

def xz(x,z): return ((x-CX)*S, (FLOOR-z)*S)
def front_depth(z):
    # Two deliberate planar facets, meeting at the broad chest ridge.
    return -.80 + (.60*(z-3.50) if z>=3.50 else .68*(3.50-z))

def create_scene():
    name='GroundMech_A4_Construction'
    # Rebuild only this generator's owned data; the original reference scenes survive.
    for obj in list(bpy.data.objects):
        if obj.get('author')==OWNER:bpy.data.objects.remove(obj,do_unlink=True)
    for col in list(bpy.data.collections):
        if col.name.startswith(PREFIX):bpy.data.collections.remove(col)
    old=bpy.data.scenes.get(name)
    if old:
        assert old.get('author')==OWNER, 'Unowned scene collision'
        for obj in list(old.objects):
            if obj.get('author')==OWNER: bpy.data.objects.remove(obj,do_unlink=True)
        scene=old
    else: scene=bpy.data.scenes.new(name)
    scene['author']=OWNER
    bpy.context.window.scene=scene
    scene.unit_settings.system='METRIC'
    scene.unit_settings.scale_length=1
    scene.render.engine='CYCLES'
    scene.cycles.samples=32
    scene.cycles.use_denoising=True
    scene.render.resolution_x=1800
    scene.render.resolution_y=1600
    scene.render.resolution_percentage=100
    scene.view_settings.view_transform='Standard'
    scene.view_settings.look='None'
    scene.world=bpy.data.worlds.new(PREFIX+'World')
    scene.world.use_nodes=True
    bg=next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND')
    bg.inputs[0].default_value=(.94,.925,.89,1)
    bg.inputs[1].default_value=.4
    return scene

def collection(name, parent=None):
    c=bpy.data.collections.new(PREFIX+name)
    (parent or SCENE.collection).children.link(c)
    return c

def make_rig():
    contract=json.loads((OUT/'source_rig_contract.json').read_text(encoding='utf8'))
    data=bpy.data.armatures.new(PREFIX+'Skeleton')
    rig=bpy.data.objects.new(PREFIX+'Rig_BLUE',data)
    RIGCOL.objects.link(rig)
    rig['author']=OWNER
    rig['TeamIndex']=0.0
    rig['AimYaw']=0.0;rig['AimPitch']=0.0
    rig['RightRecoil']=0.0;rig['LeftRecoil']=0.0
    rig.id_properties_ui('TeamIndex').update(min=0,max=1,description='0 Blue / 1 Red. Team-color regions only.')
    for prop in ['AimYaw','AimPitch']: rig.id_properties_ui(prop).update(min=-60,max=60)
    for prop in ['RightRecoil','LeftRecoil']:rig.id_properties_ui(prop).update(min=0,max=.12)
    activate(rig);bpy.ops.object.mode_set(mode='EDIT')
    root=data.edit_bones.new('root');root.head=(0,0,0);root.tail=(0,.12,0)
    positions={'MiddleJoint':(0,.06,2.20),'cockpit':(0,.04,2.67)}
    for sign,side in [(-1,'R'),(1,'L')]:
        positions.update({
            'GunArm_'+side:(sign*1.53,.02,3.94),
            'Gun_'+side:(sign*2.00,-.59,2.97),
            'UpperLeg_'+side:(sign*1.03,.03,2.25),
            'Leg_'+side:(sign*1.04,-.57,1.64),
            'Foot_'+side:(sign*1.15,.05,.62),
            'FootEnd_'+side:(sign*1.27,-.49,.19),
            'IK_Foot_'+side:(sign*1.15,.05,.62),
        })
    for spec in contract['bones']:
        b=data.edit_bones.new(spec['name'])
        # Matrix assignment requires a nonzero initial bone length.
        b.head=(0,0,0);b.tail=(0,.32,0)
        m=Matrix(spec['matrix_local']);m.translation=Vector(positions[b.name])
        if b.name in ['Gun_R','Gun_L']:
            # Source Gun_X spins the barrel around local X. Align that axis with
            # the approved cannon bore; rectangular armor belongs to GunArm_X.
            axis=Vector((0,-.996,-.089)).normalized()
            up=Vector((0,axis.z,-axis.y))
            rotation=Matrix((axis,up,axis.cross(up))).transposed().to_4x4()
            rotation.translation=m.translation;m=rotation
        b.matrix=m
        b.length=.20 if b.name.startswith('IK_') else .32
        b.use_connect=False
    for spec in contract['bones']:data.edit_bones[spec['name']].parent=data.edit_bones[spec['parent']]
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.show_in_front=True;data.display_type='STICK'
    rig['reference_pose']='Approved A_v4; source names/hierarchy retained. Gun X axes follow actual bores. Rest-translation and gun-basis retarget required.'
    rig['source_contract']='source_rig_contract.json'
    return rig

def material(name, role, rig=None):
    m=bpy.data.materials.new(PREFIX+name)
    m['author']=OWNER;m['color_role']=role;m.use_nodes=True
    n,l=m.node_tree.nodes,m.node_tree.links;n.clear()
    out=n.new('ShaderNodeOutputMaterial');out.location=(620,0)
    em=n.new('ShaderNodeEmission');em.location=(400,0);l.new(em.outputs[0],out.inputs['Surface'])
    if role=='Outline':
        geo=n.new('ShaderNodeNewGeometry');trans=n.new('ShaderNodeBsdfTransparent');mix=n.new('ShaderNodeMixShader')
        em.inputs[0].default_value=linear(PALETTE['Ink'])
        l.new(geo.outputs['Backfacing'],mix.inputs[0]);l.new(em.outputs[0],mix.inputs[1]);l.new(trans.outputs[0],mix.inputs[2]);l.new(mix.outputs[0],out.inputs[0])
        m.diffuse_color=linear(PALETTE['Ink']);m.use_backface_culling=True
        return m
    base=n.new('ShaderNodeRGB');base.location=(-580,40)
    if role.startswith('Team'):
        primary=role=='TeamPrimary'
        blue=linear(PALETTE['BluePrimary' if primary else 'BlueSecondary'])
        red=linear(PALETTE['RedPrimary' if primary else 'RedSecondary'])
        sw=n.new('ShaderNodeMixRGB');sw.blend_type='MIX';sw.location=(-380,60)
        sw.inputs[1].default_value=blue;sw.inputs[2].default_value=red
        fc=sw.inputs[0].driver_add('default_value');d=fc.driver;d.type='SCRIPTED'
        var=d.variables.new();var.name='team';var.type='SINGLE_PROP';var.targets[0].id=rig;var.targets[0].data_path='["TeamIndex"]';d.expression='min(1,max(0,team))'
        color_socket=sw.outputs[0];m.diffuse_color=blue
    else:
        base.outputs[0].default_value=linear(PALETTE[role]);color_socket=base.outputs[0];m.diffuse_color=linear(PALETTE[role])
    if role in ['Ink','Amber']:
        l.new(color_socket,em.inputs['Color']);em.inputs['Strength'].default_value=1.05 if role=='Amber' else 1
        return m
    geo=n.new('ShaderNodeNewGeometry');geo.location=(-620,-220)
    dot=n.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.location=(-420,-220)
    dot.inputs[1].default_value=Vector((-.40,-.62,.68)).normalized();l.new(geo.outputs['Normal'],dot.inputs[0])
    remap=n.new('ShaderNodeMath');remap.operation='MULTIPLY_ADD';remap.inputs[1].default_value=.5;remap.inputs[2].default_value=.5;l.new(dot.outputs['Value'],remap.inputs[0])
    ramp=n.new('ShaderNodeValToRGB');ramp.label='THREE TONES — constant bands';ramp.location=(-100,-180)
    ramp.color_ramp.interpolation='CONSTANT'
    ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for i,(pos,value) in enumerate([(0,.44),(.42,.73),(.73,1.0)]):
        e=ramp.color_ramp.elements[0] if i==0 else ramp.color_ramp.elements.new(pos);e.position=pos;e.color=(value,value,value,1)
    l.new(remap.outputs[0],ramp.inputs[0])
    mul=n.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1;mul.location=(190,0)
    l.new(color_socket,mul.inputs[1]);l.new(ramp.outputs[0],mul.inputs[2]);l.new(mul.outputs[0],em.inputs[0])
    m['tone_factors']=[.44,.73,1.0]
    return m

def mesh(name, vertices, faces, role='TeamPrimary', bone='cockpit', bevel=.012, group='Torso'):
    data=bpy.data.meshes.new(PREFIX+name);data.from_pydata(vertices,[],faces);data.update()
    bm=bmesh.new();bm.from_mesh(data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(data);bm.free()
    o=bpy.data.objects.new(PREFIX+name,data);GROUPS[group].objects.link(o)
    o['author']=OWNER;o['rigid_bone']=bone;o['color_role']=role;o['design']='Approved_A4'
    data.materials.append(MATS[role])
    if bevel>0:
        b=o.modifiers.new('Editable narrow mechanical bevel','BEVEL');b.width=bevel;b.segments=1;b.affect='EDGES';b.use_clamp_overlap=True
        w=o.modifiers.new('Planar weighted normals','WEIGHTED_NORMAL');w.keep_sharp=True;w.weight=50
    o.parent=RIG
    g=o.vertex_groups.new(name=bone);g.add(list(range(len(data.vertices))),1.0,'REPLACE')
    a=o.modifiers.new('Rigid one-bone skin','ARMATURE');a.object=RIG;a.use_vertex_groups=True
    PARTS.append(o)
    return o

def prism(name, profile, back, role='TeamPrimary', bone='cockpit', bevel=.012, group='Torso'):
    # profile points are front-surface 3D coordinates; back is their extrusion vector.
    v=[Vector(p) for p in profile];dv=Vector(back);count=len(v)
    verts=[tuple(p) for p in v]+[tuple(p+dv) for p in v]
    faces=[tuple(reversed(range(count))),tuple(range(count,2*count))]
    faces += [(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
    return mesh(name,verts,faces,role,bone,bevel,group)

def panel(name, points, depth=.055, role='Cream', bone='cockpit', normal=(0,-1,0), ink=True, group='Torso'):
    points=[Vector(p) for p in points];normal=Vector(normal).normalized()
    center=sum(points,Vector())/len(points)
    if ink:
        rim=[center+(p-center)*1.035-normal*.006 for p in points]
        prism(name+'_Seam',rim,-normal*.025,'Ink',bone,.004,group)
    return prism(name,points,-normal*depth,role,bone,min(.008,depth*.15),group)

def front_panel(name,pixels,depth_fn=front_depth,role='Cream',bone='cockpit',group='Torso',offset=0):
    points=[]
    for px,py in pixels:
        x,z=xz(px,py);points.append((x,depth_fn(z)+offset,z))
    return panel(name,points,role=role,bone=bone,group=group)

def box(name,center,dim,role='Mechanism',bone='cockpit',bevel=.025,group='Torso',rotation=None):
    c=Vector(center);dx,dy,dz=(float(v)/2 for v in dim)
    local=[Vector(p) for p in [(-dx,-dy,-dz),(dx,-dy,-dz),(dx,dy,-dz),(-dx,dy,-dz),(-dx,-dy,dz),(dx,-dy,dz),(dx,dy,dz),(-dx,dy,dz)]]
    mat=rotation.to_matrix() if isinstance(rotation,Quaternion) else rotation or Matrix.Identity(3)
    return mesh(name,[tuple(c+mat@p) for p in local],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],role,bone,bevel,group)

def beam(name,a,b,width,depth,role='Mechanism',bone='cockpit',bevel=.015,group='Torso'):
    a,b=Vector(a),Vector(b);direction=b-a
    return box(name,(a+b)/2,(width,depth,direction.length),role,bone,bevel,group,direction.to_track_quat('Z','Y'))

def lathe(name,center,axis,profile,roles,bone='cockpit',sides=16,group='Torso',phase=0):
    axis=Vector(axis).normalized();q=axis.to_track_quat('Z','Y');c=Vector(center)
    verts=[]
    for distance,radius in profile:
        for i in range(sides):
            a=2*math.pi*i/sides+phase
            verts.append(tuple(c+q@Vector((math.cos(a)*radius,math.sin(a)*radius,distance))))
    faces=[];matinds=[]
    distinct=list(dict.fromkeys(roles))
    for j in range(len(profile)-1):
        for i in range(sides):
            k=(i+1)%sides;faces.append((j*sides+i,j*sides+k,(j+1)*sides+k,(j+1)*sides+i));matinds.append(distinct.index(roles[min(j,len(roles)-1)]))
    faces.extend([tuple(reversed(range(sides))),tuple((len(profile)-1)*sides+i for i in range(sides))]);matinds.extend([0,distinct.index(roles[-1])])
    o=mesh(name,verts,faces,distinct[0],bone,0,group)
    o.data.materials.clear()
    for role in distinct:o.data.materials.append(MATS[role])
    for face,index in zip(o.data.polygons,matinds):face.material_index=index
    o['color_roles']=distinct
    return o

def cylinder(name,center,axis,radius,length,role='Mechanism',bone='cockpit',sides=16,group='Torso'):
    return lathe(name,center,axis,[(-length/2,radius),(length/2,radius)],[role,role],bone,sides,group)

def tube(name,center,axis,r_outer,r_inner,length,role='Mechanism',bone='cockpit',group='Torso',sides=20):
    return lathe(name,center,axis,[(-length/2,r_outer),(length/2,r_outer),(length/2,r_inner),(-length/2+.04,r_inner)],
                 [role,role,'Ink','Ink'],bone,sides,group)

def mirror_front(pixels):return [(2*CX-x,y) for x,y in reversed(pixels)]

def foot_transform(point,side):
    yaw=math.radians(18*side)
    return Vector((side*1.16,.05,0))+Matrix.Rotation(yaw,3,'Z')@Vector(point)

def foot_wedge(name,side,local_sections,role,bone):
    verts=[]
    # each section: y, x_left, x_right, z_bottom, z_top, upper_side_inset
    for y,x0,x1,z0,z1,inset in local_sections:
        if z0<=.03:z0=0.0
        for p in [(x0,y,z0),(x1,y,z0),(x1-inset,y,z1),(x0+inset,y,z1)]:verts.append(tuple(foot_transform(p,side)))
    faces=[(3,2,1,0)]
    for j in range(len(local_sections)-1):
        a=j*4;b=(j+1)*4
        for i in range(4):faces.append((a+i,a+(i+1)%4,b+(i+1)%4,b+i))
    faces.append(tuple(range(len(verts)-4,len(verts))))
    return mesh(name,verts,faces,role,bone,.009,'Legs')

def chamfer_loft(name,center,axis,sections,role,bone,group):
    q=Vector(axis).normalized().to_track_quat('Z','Y');c=Vector(center)
    vertices=[]
    for along,width,height,cut in sections:
        w,h=width/2,height/2
        ring=[(-w+cut,-h),(w-cut,-h),(w,-h+cut),(w,h-cut),(w-cut,h),(-w+cut,h),(-w,h-cut),(-w,-h+cut)]
        vertices += [tuple(c+q@Vector((x,y,along))) for x,y in ring]
    faces=[tuple(reversed(range(8)))]
    for r in range(len(sections)-1):
        for i in range(8):faces.append((r*8+i,r*8+(i+1)%8,(r+1)*8+(i+1)%8,(r+1)*8+i))
    faces.append(tuple(range(len(vertices)-8,len(vertices))))
    return mesh(name,vertices,faces,role,bone,.009,group)

def build_torso():
    body_pixels=[(295,97),(465,97),(490,123),(482,158),(407,218),(353,218),(278,158),(270,123)]
    body=[(x,front_depth(z)+.045,z) for x,z in [xz(*p) for p in body_pixels]]
    prism('Chest_MechanismCore',body,(0,.76,0),'Mechanism','cockpit',.035)
    # The two uninterrupted cream chest facets are traced from the front drawing.
    left=[(291,112),(315,102),(351,105),(367,129),(349,149),(303,149),(279,132)]
    front_panel('Chest_Armor_R',left,offset=-.022)
    front_panel('Chest_Armor_L',mirror_front(left),offset=-.022)
    crest=[(351,98),(409,98),(414,110),(396,137),(364,137),(346,110)]
    front_panel('Chest_CentralCreamBridge',crest,offset=-.012)
    for sign,label in [(-1,'R'),(1,'L')]:
        px=[(369,110),(378,111),(378,133),(373,130)] if sign<0 else [(382,111),(391,110),(387,130),(382,133)]
        front_panel('Chest_TwinIntake_'+label,px,role='Ink',offset=-.082)
    keel=[(369,150),(391,150),(400,163),(392,199),(392,210),(369,210),(369,199),(360,163)]
    front_panel('Chest_CreamKeel',keel,depth_fn=lambda z:-.83+.68*(3.50-z),offset=-.035)
    lower_plate=[(304,152),(351,151),(361,163),(347,184),(334,176)]
    front_panel('Chest_LowerCream_R',lower_plate,offset=-.015)
    front_panel('Chest_LowerCream_L',mirror_front(lower_plate),offset=-.015)
    intake=[(316,157),(347,157),(340,173)]
    front_panel('Chest_LowerIntake_R',intake,role='Ink',offset=-.085)
    front_panel('Chest_LowerIntake_L',mirror_front(intake),role='Ink',offset=-.085)
    # Open lower triangular framework; gaps remain actual empty space.
    rail=[(267,129),(280,140),(302,169),(351,211),(361,218),(350,228),(339,218),(291,175),(278,158)]
    front_panel('Torso_OuterFrame_R',rail,role='TeamPrimary',offset=.015)
    front_panel('Torso_OuterFrame_L',mirror_front(rail),role='TeamPrimary',offset=.015)
    for sign,label in [(-1,'R'),(1,'L')]:
        beam('Torso_OpenSideUpper_'+label,(sign*1.14,.12,3.68),(sign*.49,.24,2.81),.16,.15,'Mechanism')
        beam('Torso_OpenSideLower_'+label,(sign*1.11,.20,3.25),(sign*.36,.18,2.73),.15,.14,'TeamSecondary')
        beam('Torso_UpperBrace_'+label,(sign*.65,.17,3.97),(sign*1.37,.04,3.82),.22,.23,'Mechanism')
    # Rear facets match the broad, low-noise A4 rear silhouette.
    back_outline=[(-.77,.61,4.04),(.77,.61,4.04),(.92,.62,3.82),(.63,.72,3.23),(.30,.34,2.82),(-.30,.34,2.82),(-.63,.72,3.23),(-.92,.62,3.82)]
    prism('Back_CoreArmor',back_outline,(0,-.30,0),'Mechanism','cockpit',.025)
    box('Back_UnderPlateSupport',(0,.64,3.63),(1.14,.30,.64),'Mechanism',bevel=.03)
    panel('Back_UpperCenter',[(-.61,.79,3.94),(.61,.79,3.94),(.61,.83,3.61),(-.61,.83,3.61)],.08,'Mechanism',normal=(0,1,0))
    panel('Back_LowerCenter',[(-.60,.81,3.55),(.60,.81,3.55),(.39,.61,3.12),(-.39,.61,3.12)],.07,'Mechanism',normal=(0,1,0))
    for sign,label in [(-1,'R'),(1,'L')]:
        panel('Back_TeamShoulderPanel_'+label,[(sign*.72,.65,4.05),(sign*1.12,.49,4.05),(sign*1.16,.55,3.61),(sign*.79,.72,3.56)],.10,'TeamPrimary',normal=(0,1,0))
        panel('Back_LateralArmor_'+label,[(sign*.83,.63,3.58),(sign*1.10,.43,3.65),(sign*.89,.57,3.20),(sign*.57,.64,3.01)],.08,'TeamSecondary',normal=(0,1,0))
    # Four clean broad cream cooling blades, as in the supplied hero image.
    box('Cooling_Base',(0,.20,4.10),(.80,.70,.16),'Mechanism',bevel=.025)
    for i in range(4):
        x=(i-1.5)*.18
        p=[(x-.054,-.22,4.10),(x-.054,-.21,4.42),(x-.054,.26,4.43),(x-.054,.52,4.22),(x-.054,.52,4.10)]
        prism('Cooling_Blade_%02d'%i,p,(.108,0,0),'Cream','cockpit',.014)
    for sign,label in [(-1,'R'),(1,'L')]:
        center=(sign*.68,.10,4.57)
        box('TopFixture_Base_'+label,(sign*.68,.20,4.20),(.35,.48,.34),'Mechanism',bevel=.025)
        lathe('TopFixture_Housing_'+label,center,(0,-1,0),[(-.26,.20),(-.20,.29),(.12,.31),(.19,.265)],['TeamSecondary','TeamPrimary','TeamPrimary','TeamPrimary'],sides=12)
        lathe('TopFixture_Rings_'+label,(sign*.68,-.11,4.57),(0,-1,0),[(0,.255),(.04,.243),(.065,.187),(.075,.174),(.105,.156)],['Mechanism','Sand','Mechanism','Ink','Ink'],sides=24)
        cylinder('TopFixture_AmberLens_'+label,(sign*.68,-.22,4.57),(0,-1,0),.110,.026,'Amber',sides=24)
    # Neck and compact pelvis are independent rigid groups.
    cylinder('Waist_YawBearing',(0,.04,2.66),(0,0,1),.35,.19,'Mechanism','MiddleJoint',20,'Pelvis')
    box('Waist_UpperNeck',(0,.02,2.57),(.46,.45,.29),'Mechanism','MiddleJoint',.035,'Pelvis')
    pelvis=[(-.42,-.45,2.42),(.42,-.45,2.42),(.49,-.42,2.29),(.41,-.44,1.99),(-.41,-.44,1.99),(-.49,-.42,2.29)]
    prism('Pelvis_Main',pelvis,(0,.73,0),'Mechanism','MiddleJoint',.045,'Pelvis')
    panel('Pelvis_FrontPlate',[(-.36,-.509,2.37),(.36,-.509,2.37),(.39,-.517,2.23),(.27,-.532,2.10),(-.27,-.532,2.10),(-.39,-.517,2.23)],.035,'Mechanism','MiddleJoint',group='Pelvis')
    cylinder('Pelvis_CrossAxle',(0,.03,2.25),(1,0,0),.25,1.80,'Mechanism','MiddleJoint',20,'Pelvis')
    for sign,label in [(-1,'R'),(1,'L')]:
        center=Vector((sign*.61,.40,2.81));direction=Vector((0,.16,-.987)).normalized()
        lathe('Jet_OutletHousing_'+label,center,direction,[(-.16,.16),(-.10,.22),(.12,.22),(.17,.19),(.18,.14),(.06,.14)],['Mechanism','TeamSecondary','Mechanism','Sand','Ink','Ink'],sides=20,group='Sockets')
        cylinder('Jet_OutletAmber_'+label,center+direction*.075,direction,.133,.015,'Amber',group='Sockets',sides=20)
        SOCKETS.append({'name':'Jet_Outlet_'+label,'bone':'cockpit','position':list(center+direction*.18),'direction':list(direction)})

def build_arms():
    shoulder_pixels=[(236,62),(268,62),(282,78),(279,116),(265,138),(237,132),(225,116),(224,80)]
    for sign,label in [(-1,'R'),(1,'L')]:
        arm='GunArm_'+label;gun='Gun_'+label
        group='Arms_'+label
        center=Vector((sign*1.53,.02,3.94))
        cylinder('Shoulder_Core_'+label,center,(sign,0,0),.395,.66,'Mechanism',arm,20,group)
        pixels=shoulder_pixels if sign<0 else mirror_front(shoulder_pixels)
        front_panel('Shoulder_BlueArmor_'+label,pixels,depth_fn=lambda z:-.38,role='TeamPrimary',bone=arm,group=group)
        # A deep chamfered shoulder cover, not a flat card.
        pp=[(x,-.32,z) for x,z in [xz(*p) for p in pixels]]
        prism('Shoulder_ArmorDepth_'+label,pp,(0,.73,0),'TeamPrimary',arm,.018,group)
        pivot=Vector((sign*1.76,.025,3.98))
        lathe('Shoulder_OuterJoint_'+label,pivot,(sign,0,0),[(-.06,.38),(.02,.43),(.07,.42),(.10,.355),(.13,.335),(.16,.27),(.18,.235)],['TeamPrimary','TeamPrimary','Cream','Cream','Mechanism','Ink','Ink'],arm,24,group)
        cylinder('Shoulder_AmberHub_'+label,pivot+Vector((sign*.192,0,0)),(sign,0,0),.105,.018,'Amber',arm,24,group)
        a=Vector((sign*1.56,.03,3.72));b=Vector((sign*2.00,-.51,3.02))
        beam('UpperArm_Core_'+label,a,b,.30,.33,'Mechanism',arm,.020,group)
        # Two separated structural rails around the forearm gap.
        beam('UpperArm_OuterRail_'+label,a+Vector((sign*.19,.07,0)),b+Vector((sign*.21,.09,-.02)),.16,.15,'TeamPrimary',arm,.012,group)
        beam('UpperArm_InnerRail_'+label,a+Vector((-sign*.14,.09,0)),b+Vector((-sign*.12,.10,.01)),.105,.13,'TeamSecondary',arm,.009,group)
        # Long cream insertion follows the arm, leaving the articulated end clear.
        direction=b-a;q=direction.to_track_quat('Z','Y');c=(a+b)/2+Vector((0,-.18,0))
        box('UpperArm_CreamInsert_'+label,c,(.17,.045,direction.length*.57),'Sand',arm,.01,group,q)
        elbow=Vector((sign*2.00,-.58,2.93))
        cylinder('Elbow_Axle_'+label,elbow,(1,0,0),.24,.57,'Mechanism',arm,20,group)
        cylinder('Elbow_Hub_'+label,elbow+Vector((sign*.30,0,0)),(sign,0,0),.17,.07,'TeamSecondary',arm,16,group)
        direction=Vector((0,-.996,-.089)).normalized()
        q=direction.to_track_quat('Z','Y')
        cannon=elbow+Vector((0,-.01,.04))
        chamfer_loft('Cannon_Box_'+label,cannon,direction,[(-.18,.62,.52,.09),(.04,.76,.57,.08),(.68,.67,.49,.08),(.80,.50,.42,.065)],'TeamPrimary',arm,group)
        # Identical large cream strips on both broad side faces.
        for edge,word in [(-1,'Inner'),(1,'Outer')]:
            x=sign*edge*.382
            local=[(x,-.16,.08),(x,-.15,.58),(x,.15,.58),(x,.18,.17)]
            pts=[tuple(cannon+q@Vector(p)) for p in local]
            panel('Cannon_CreamStrip_'+label+'_'+word,pts,.024,'Cream',arm,normal=q@Vector((sign*edge,0,0)),group=group)
        # The slim triangular lower cage is genuine separated geometry.
        low_a=elbow+Vector((0,.10,-.28));low_b=cannon+direction*.69+Vector((0,0,-.33))
        for edge in [-1,1]:
            off=Vector((edge*.22,0,0))
            beam('Cannon_LowerRail_%s_%s'%(label,edge),low_a+off,low_b+off,.095,.10,'TeamSecondary',arm,.008,group)
        beam('Cannon_UnderFront_'+label,low_b+Vector((-.22,0,0)),low_b+Vector((.22,0,0)),.105,.10,'TeamPrimary',arm,.009,group)
        muzzlebase=cannon+direction*.82
        lathe('Cannon_MuzzleCollars_'+label,muzzlebase,direction,[(0,.205),(.10,.205),(.135,.175),(.24,.175),(.28,.145)],['Mechanism','Mechanism','TeamSecondary','Mechanism','Mechanism'],gun,24,group)
        muzzle=muzzlebase+direction*.30
        tube('Cannon_OpenBore_'+label,muzzle,direction,.145,.105,.14,'Mechanism',gun,group,24)
        cylinder('Cannon_BoreAmber_'+label,muzzle-direction*.059,direction,.072,.005,'Amber',gun,24,group)
        SOCKETS.append({'name':'Muzzle' if label=='R' else 'Muzzle_L','bone':gun,'position':list(muzzle+direction*.071),'direction':list(direction)})

def build_legs():
    hip=[(282,234),(310,228),(333,243),(328,267),(306,280),(279,273),(267,258)]
    thigh=[(274,274),(308,276),(326,295),(315,321),(278,319),(258,299)]
    shin=[(278,338),(309,339),(313,360),(297,382),(270,375)]
    thigh_y=lambda z:.03-(2.25-z)*(.60/.61)-.23
    shin_y=lambda z:.05-(z-.62)*(.62/1.02)-.22
    for sign,label in [(-1,'R'),(1,'L')]:
        upper='UpperLeg_'+label;lower='Leg_'+label;foot='Foot_'+label;toe='FootEnd_'+label
        hp=Vector((sign*1.03,.03,2.25));kn=Vector((sign*1.04,-.57,1.64));an=Vector((sign*1.15,.05,.62))
        cylinder('Hip_DarkBearing_'+label,hp,(1,0,0),.35,.68,'Mechanism',upper,16,'Legs')
        lathe('Hip_OuterCap_'+label,hp+Vector((sign*.37,0,0)),(sign,0,0),[(-.04,.285),(.02,.32),(.12,.30),(.15,.245)],['Mechanism','Sand','Sand','Sand'],upper,8,'Legs',math.pi/8)
        # Hip shell and knee-side plates retain the A4 octagonal language.
        pix=hip if sign<0 else mirror_front(hip)
        pts=[(x,-.30,z) for x,z in [xz(*p) for p in pix]]
        prism('Hip_ArmorDepth_'+label,pts,(0,.40,0),'TeamPrimary',upper,.016,'Legs')
        front_panel('Hip_ArmorPlate_'+label,pix,lambda z:-.32,'TeamPrimary',upper,'Legs')
        beam('Thigh_StructuralLink_'+label,hp,kn,.40,.39,'Mechanism',upper,.02,'Legs')
        pix=thigh if sign<0 else mirror_front(thigh)
        pts=[(x,thigh_y(z)+.04,z) for x,z in [xz(*p) for p in pix]]
        prism('Thigh_ArmorDepth_'+label,pts,(0,.38,0),'TeamPrimary',upper,.017,'Legs')
        front_panel('Thigh_ArmorPlate_'+label,pix,thigh_y,'TeamPrimary',upper,'Legs',-.018)
        # A single rear plate, no dense machinery or extra pistons.
        beam('Thigh_BackGuard_'+label,hp+Vector((0,.24,-.12)),kn+Vector((0,.23,.13)),.34,.13,'Mechanism',upper,.025,'Legs')
        cylinder('Knee_Axle_'+label,kn,(1,0,0),.235,.66,'Mechanism',lower,20,'Legs')
        lathe('Knee_OuterCap_'+label,kn+Vector((sign*.37,.10,-.08)),(sign,0,0),[(-.035,.245),(.03,.275),(.13,.26),(.16,.22)],['Mechanism','Sand','Sand','Sand'],lower,6,'Legs',math.pi/6)
        cylinder('Knee_InnerDarkCap_'+label,kn-Vector((sign*.34,0,0)),(sign,0,0),.165,.035,'Mechanism',lower,16,'Legs')
        beam('Shin_StructuralLink_'+label,kn,an,.34,.35,'Mechanism',lower,.018,'Legs')
        pix=shin if sign<0 else mirror_front(shin)
        pts=[(x,shin_y(z)+.04,z) for x,z in [xz(*p) for p in pix]]
        prism('Shin_BlueArmorDepth_'+label,pts,(0,.33,0),'TeamPrimary',lower,.013,'Legs')
        front_panel('Shin_BlueArmorPlate_'+label,pix,shin_y,'TeamPrimary',lower,'Legs',-.018)
        beam('Shin_BackEdge_'+label,kn+Vector((0,.14,-.12)),an+Vector((0,.15,.17)),.22,.09,'TeamSecondary',lower,.009,'Legs')
        # Existing angular ankle housing and lower sand side insert.
        lathe('Ankle_OuterCap_'+label,an+Vector((sign*.28,0,0)),(sign,0,0),[(-.04,.23),(.02,.245),(.12,.22)],['Mechanism','Sand','Sand'],foot,6,'Legs',math.pi/6)
        cylinder('Ankle_DarkAxle_'+label,an,(1,0,0),.22,.56,'Mechanism',foot,16,'Legs')
        boot=[(-.37,-.32,.21),(.37,-.32,.21),(.39,-.34,.47),(.23,-.10,.76),(-.23,-.10,.76),(-.39,-.34,.47)]
        prism('Ankle_BlueBoot_'+label,[foot_transform(p,sign) for p in boot],(0,.40,0),'TeamPrimary',foot,.018,'Legs')
        # Two forward toes and compact rear heel, with controlled 18-degree splay.
        for i,x in enumerate([-.30,.30]):
            foot_wedge('Foot_Toe_%s_%s'%(label,i),sign,[(-1.21,x-.275,x+.275,.028,.25,.08),(-.79,x-.28,x+.28,.028,.40,.07),(-.37,x-.23,x+.23,.08,.42,.07)],'Mechanism',toe)
            p=[(x-.145,-1.02,.324),(x+.145,-1.02,.324),(x+.14,-.73,.422),(x-.14,-.73,.422)]
            panel('Foot_ToeCream_%s_%s'%(label,i),[foot_transform(v,sign) for v in p],.018,'Cream',toe,normal=(0,0,1),ink=False,group='Legs')
        foot_wedge('Foot_BlueBridge_'+label,sign,[(-.70,-.43,.43,.20,.44,.08),(-.28,-.45,.45,.18,.58,.12),(.06,-.30,.30,.13,.52,.07)],'TeamPrimary',foot)
        for i,x in enumerate([-.23,.23]):
            foot_wedge('Foot_Heel_%s_%s'%(label,i),sign,[(.20,x-.24,x+.24,.025,.34,.06),(.66,x-.23,x+.23,.025,.25,.08)],'Mechanism',foot)
        # Broad rear calf guard is visible in the approved back view.
        beam('Calf_BackGuard_'+label,kn+Vector((0,.26,-.18)),an+Vector((0,.23,.19)),.31,.15,'Mechanism',lower,.024,'Legs')

def add_controls_and_sockets():
    for prop,index in [('AimPitch',0),('AimYaw',1)]:
        bone=RIG.pose.bones['cockpit'];bone.rotation_mode='XYZ'
        d=bone.driver_add('rotation_euler',index).driver
        v=d.variables.new();v.name='angle';v.type='SINGLE_PROP';v.targets[0].id=RIG;v.targets[0].data_path='["%s"]'%prop
        d.expression='angle*pi/180'
    for side,prop in [('R','RightRecoil'),('L','LeftRecoil')]:
        bone=RIG.pose.bones['Gun_'+side]
        local_direction=bone.bone.matrix_local.to_3x3().inverted()@Vector((0,.996,.089))
        for axis in range(3):
            d=bone.driver_add('location',axis).driver
            v=d.variables.new();v.name='recoil';v.type='SINGLE_PROP';v.targets[0].id=RIG;v.targets[0].data_path='["%s"]'%prop
            d.expression='recoil*%.12f'%local_direction[axis]
    for spec in SOCKETS:
        e=bpy.data.objects.new(PREFIX+spec['name'],None);GROUPS['Sockets'].objects.link(e)
        e['author']=OWNER;e['socket_name']=spec['name'];e['bone_name']=spec['bone'];e['forward_axis']='+X'
        e.empty_display_type='ARROWS';e.empty_display_size=.12
        e.parent=RIG;e.parent_type='BONE';e.parent_bone=spec['bone']
        bpy.context.view_layer.update()
        world=Vector(spec['direction']).to_track_quat('X','Z').to_matrix().to_4x4();world.translation=Vector(spec['position'])
        e.matrix_world=world;e.hide_render=True
        spec['bone_local_matrix']=[list(r) for r in RIG.data.bones[spec['bone']].matrix_local.inverted()@world]

def add_uvs():
    bpy.ops.object.select_all(action='DESELECT')
    for o in PARTS:o.select_set(True)
    bpy.context.view_layer.objects.active=PARTS[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.03,area_weight=0,correct_aspect=True,scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')

def outlines():
    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
    result=[]
    for o in PARTS:
        if o.get('color_role') in ['Ink','Amber']:continue
        evaluated=o.evaluated_get(deps)
        data=bpy.data.meshes.new_from_object(evaluated,depsgraph=deps,preserve_all_data_layers=True)
        data.update()
        # A single real backface shell, with a restrained 5 mm silhouette offset.
        normals=[v.normal.copy() for v in data.vertices]
        for v,normal in zip(data.vertices,normals):v.co+=normal*.005
        bm=bmesh.new();bm.from_mesh(data);bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(data);bm.free()
        data.materials.clear();data.materials.append(MATS['Outline'])
        for p in data.polygons:p.material_index=0
        shell=bpy.data.objects.new(o.name+'_OUTLINE',data);INKCOL.objects.link(shell)
        shell['author']=OWNER;shell['source_part']=o.name;shell['rigid_bone']=o['rigid_bone'];shell['outline_only']=True
        shell.parent=RIG
        # Evaluated mesh retains deform indices, create the same one-bone group.
        vg=shell.vertex_groups.new(name=o['rigid_bone']);vg.add(list(range(len(data.vertices))),1,'REPLACE')
        a=shell.modifiers.new('Rigid shell skin','ARMATURE');a.object=RIG
        result.append(shell)
    return result

def copy_red_team():
    red=bpy.data.collections.new(PREFIX+'TEAM_RED')
    rig=RIG.copy();rig.data=RIG.data.copy();rig.name=PREFIX+'Rig_RED';red.objects.link(rig)
    rig['TeamIndex']=1.0
    new_materials={role:material('Red_'+role,role,rig) for role in ['TeamPrimary','TeamSecondary']}
    mapping={RIG:rig}
    for o in list(BLUE.all_objects):
        if o==RIG:continue
        copy=o.copy();copy.name=o.name+'_RED';red.objects.link(copy);mapping[o]=copy
        if o.type=='MESH':
            for slot in copy.material_slots:
                role=slot.material.get('color_role') if slot.material else None
                if role in new_materials:slot.link='OBJECT';slot.material=new_materials[role]
            for mod in copy.modifiers:
                if mod.type=='ARMATURE':mod.object=rig
        if copy.animation_data:
            for f in copy.animation_data.drivers:
                for v in f.driver.variables:
                    for t in v.targets:
                        if t.id==RIG:t.id=rig
    for original,copy in mapping.items():
        if original.parent in mapping:copy.parent=mapping[original.parent]
    if rig.animation_data:
        for f in rig.animation_data.drivers:
            for v in f.driver.variables:
                for t in v.targets:
                    if t.id==RIG:t.id=rig
    return red,rig

def camera(scene,name,position,target,scale):
    data=bpy.data.cameras.new(PREFIX+name);data.type='ORTHO';data.ortho_scale=scale;data.lens=55
    obj=bpy.data.objects.new(PREFIX+name,data);scene.collection.objects.link(obj);obj['author']=OWNER
    obj.location=position;obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler();scene.camera=obj
    return obj

def instance(scene,col,name,position,angle=0):
    obj=bpy.data.objects.new(PREFIX+name,None);obj.instance_type='COLLECTION';obj.instance_collection=col
    obj.location=position;obj.rotation_euler.z=angle;obj['author']=OWNER;scene.collection.objects.link(obj)
    return obj

def text_label(scene,name,value,position,size=.22):
    c=bpy.data.curves.new(PREFIX+name,'FONT');c.body=value;c.size=size;c.align_x='CENTER'
    o=bpy.data.objects.new(PREFIX+name,c);scene.collection.objects.link(o);o['author']=OWNER
    o.location=position;o.rotation_euler.x=math.pi/2;c.materials.append(MATS['Ink'])
    return o

def review_scene(name,x=2000,y=1400):
    old=bpy.data.scenes.get(name)
    if old:
        assert old.get('author')==OWNER
        bpy.data.scenes.remove(old)
    scene=bpy.data.scenes.new(name);scene['author']=OWNER
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
    scene.render.resolution_x=x;scene.render.resolution_y=y;scene.render.resolution_percentage=100
    scene.world=SCENE.world;scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
    scene.render.image_settings.file_format='PNG'
    return scene

def reference_objects():
    refs=collection('Approved_Reference_Images')
    for filename,name,anchor,rotation,position in [
        ('Approved_A4_ThreeViews.png','Front_Reference',(380/1536,1-464/1024),(math.pi/2,0,0),(0,1.2,0)),
        ('Approved_A4_ThreeViews.png','Side_Reference',(805/1536,1-464/1024),(math.pi/2,0,math.pi/2),(2.5,0,0)),
    ]:
        image=bpy.data.images.load(str(OUT/'References'/filename),check_existing=True);image.pack();image.use_fake_user=True
        e=bpy.data.objects.new(PREFIX+name,None);refs.objects.link(e);e.empty_display_type='IMAGE';e.data=image
        e['author']=OWNER;e['reference_only']=True;e.empty_display_size=1536*S
        e.empty_image_offset=(-anchor[0],-anchor[1]);e.rotation_euler=rotation;e.location=position;e.color[3]=.35
        e.empty_image_depth='BACK';e.show_in_front=False;e.hide_render=True;e.hide_set(True)
    hero=bpy.data.images.load(str(OUT/'References/Approved_A4_Concept.png'),check_existing=True);hero.pack();hero.use_fake_user=True

def configure_reviews(red):
    hero=review_scene('GroundMech_A4_01_TeamReview',2200,1250)
    instance(hero,BLUE,'Blue_Hero',(-2.85,0,0));instance(hero,red,'Red_Hero',(2.85,0,0))
    camera(hero,'Hero_Camera',(9,-24,8.0),(0,0,2.38),12.2)
    hero.render.filepath=str(OUT/'Previews/01_BlueRed_Hero.png')
    ortho=review_scene('GroundMech_A4_02_ThreeViews',2400,1600)
    for team,col,z in [('BLUE',BLUE,3.0),('RED',red,-3.0)]:
        for i,(label,angle) in enumerate([('FRONT',0),('SIDE',-math.pi/2),('BACK',math.pi)]):
            x=(i-1)*5.4
            instance(ortho,col,team+'_'+label,(x,0,z),angle)
            text_label(ortho,team+'_'+label+'_Label',team+' / '+label,(x,-3.1,z-.40),.22)
    camera(ortho,'ThreeView_Camera',(0,-32,2.44),(0,0,2.44),18.8)
    ortho.render.filepath=str(OUT/'Previews/02_BlueRed_ThreeViews.png')
    single=review_scene('GroundMech_A4_03_SingleInspect',1500,1500)
    instance(single,BLUE,'Single_Blue',(0,0,0));camera(single,'Single_Camera',(7,-14,6.2),(0,0,2.4),6.3)
    single.render.filepath=str(OUT/'Previews/03_Blue_Inspect.png')
    camera(SCENE,'Editable_Camera',(8,-16,7),(0,0,2.4),6.4)
    return hero,ortho,single

def audit_geometry(shells,redrig):
    bpy.context.window.scene=SCENE;bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
    report={'owner':OWNER,'reference':'A_v4','approval_A':'approved','approval_B':'pending','source_mesh_reused':False,'authorship':'Editable authored polygon profiles, section lofts and regular rings',
        'body_parts':len(PARTS),'outline_parts':len(shells),'bones':len(RIG.data.bones),'body_triangles':0,'outline_triangles':0,'body_vertices':0,'errors':[],
        'source_rig_names_and_hierarchy_preserved':True,'source_orientation_exceptions':['Gun_R','Gun_L'],'source_translation_retarget_required':True,'sockets':SOCKETS,'parts':[],'output_visual_QA':'Not performed; user visual review remains separate'}
    lo=Vector((1e9,1e9,1e9));hi=-lo
    for o in PARTS:
        ev=o.evaluated_get(deps);me=ev.to_mesh();me.calc_loop_triangles()
        tris=len(me.loop_triangles);report['body_triangles']+=tris;report['body_vertices']+=len(me.vertices)
        bm=bmesh.new();bm.from_mesh(me);nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
        badarea=sum(t.area<1e-10 for t in me.loop_triangles)
        if nonmanifold or badarea:report['errors'].append({'object':o.name,'nonmanifold_edges':nonmanifold,'degenerate_triangles':badarea})
        for v in me.vertices:
            p=ev.matrix_world@v.co
            for i in range(3):lo[i]=min(lo[i],p[i]);hi[i]=max(hi[i],p[i])
        report['parts'].append({'name':o.name,'bone':o['rigid_bone'],'role':o['color_role'],'triangles':tris,'uv_layers':len(me.uv_layers)})
        ev.to_mesh_clear()
    for o in shells:o.data.calc_loop_triangles();report['outline_triangles']+=len(o.data.loop_triangles)
    report['bounds_m']={'min':list(lo),'max':list(hi),'dimensions':list(hi-lo)}
    report['mecha_source_triangles']=436588
    report['body_reduction_fraction']=1-report['body_triangles']/436588
    report['team_controls']={'blue':RIG.name,'red':redrig.name,'property':'TeamIndex','blue_value':0,'red_value':1}
    expected=json.loads((OUT/'source_rig_contract.json').read_text())
    for entry in expected['bones']:
        b=RIG.data.bones[entry['name']]
        parent=b.parent.name if b.parent else None
        if parent!=entry['parent']:report['errors'].append({'bone':b.name,'parent_mismatch':parent})
        delta=max(abs(b.matrix_local[i][j]-entry['matrix_local'][i][j]) for i in range(3) for j in range(3))
        if delta>1e-5 and b.name not in report['source_orientation_exceptions']:report['errors'].append({'bone':b.name,'unexpected_orientation_delta':delta})
    target={'bones':[{'name':b.name,'parent':b.parent.name if b.parent else None,'matrix_local':[list(r) for r in b.matrix_local]} for b in RIG.data.bones],
            'source_contract':'source_rig_contract.json','gun_bone_semantics':'Barrel spin/recoil only. GunArm owns rectangular cannon housing.'}
    (OUT/'target_rig_contract.json').write_text(json.dumps(target,indent=2),encoding='utf8')
    report['success']=not report['errors']
    (OUT/'geometry_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report

def main():
    global SCENE,BLUE,RIGCOL,GROUPS,RIG,MATS,INKCOL,SOCKETS
    (OUT/'Previews').mkdir(parents=True,exist_ok=True)
    SCENE=create_scene()
    BLUE=collection('TEAM_BLUE')
    RIGCOL=collection('Rig',BLUE)
    GROUPS={name:collection(name,BLUE) for name in ['Torso','Pelvis','Arms_R','Arms_L','Legs','Sockets']}
    INKCOL=collection('Silhouette_Only',BLUE)
    RIG=make_rig()
    MATS={role:material('Blue_'+role if role.startswith('Team') else role,role,RIG) for role in ['TeamPrimary','TeamSecondary','Cream','Sand','Mechanism','Ink','Amber','Outline']}
    SOCKETS=[]
    build_torso();build_arms();build_legs();add_controls_and_sockets();add_uvs()
    shell_objects=outlines()
    red,redrig=copy_red_team()
    reference_objects()
    reviews=configure_reviews(red)
    report=audit_geometry(shell_objects,redrig)
    # Save the actual editable scene, packed approved references and review scenes.
    bpy.context.window.scene=reviews[0]
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.type='MATERIAL'
            area.spaces.active.overlay.show_overlays=False
    filepath=OUT/'GroundMech_A4_Editable_B_v1.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(filepath))
    report['blend']=str(filepath)
    (OUT/'geometry_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return {'success':report['success'],'blend':str(filepath),'parts':report['body_parts'],'triangles':report['body_triangles'],'outline_triangles':report['outline_triangles'],'errors':report['errors'],'bounds_m':report['bounds_m'],'review_scenes':[s.name for s in reviews]}

if __name__=='__main__':
    result=main()



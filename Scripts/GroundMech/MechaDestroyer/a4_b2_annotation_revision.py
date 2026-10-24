"""Authored parts for the six annotated user corrections of 2026-10-23.

Executed in build_a4_b2's geometry namespace. The same definitions are used by
the full generator and the live, non-destructive revision application.
"""

ANNOTATION_REVISION='weapon_leg_assembly_20261023'
GUN_REAR=-.19
GUN_OLD_FRONT=1.155
GUN_LENGTH_FACTOR=1.5
GUN_EXTENSION=(GUN_OLD_FRONT-GUN_REAR)*(GUN_LENGTH_FACTOR-1)


def fitted_side_guard(name,pixels,sign,outer_x,depth,role,bone,group='Legs',face_scale=.87):
    """Closed, broad-chamfered guard with a seated back, not a thin flat tab."""
    poly=[Vector(side_point(*p)) for p in pixels]
    center=sum(poly,Vector((0,0)))/len(poly)
    verts=[]
    for scale,inward in [(1,depth),(1,.055),(face_scale,0)]:
        for p in poly:
            y,z=center+(p-center)*scale
            x=outer_x(z) if callable(outer_x) else outer_x
            verts.append((sign*(x-inward),y,z))
    n=len(poly);faces=[tuple(reversed(range(n)))]
    for j in range(2):
        faces.extend((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i) for i in range(n))
    faces.append(tuple(range(2*n,3*n)))
    return mesh(name,verts,faces,role,bone,.006,group)


def build_annotation_revision():
    start=len(PARTS)
    socket_updates=[]
    for sign,label in [(-1,'R'),(1,'L')]:
        arm='GunArm_'+label;gun='Gun_'+label;group='Arms_'+label
        elbow=Vector((sign*1.72,-.46,2.99))
        cannon=elbow+Vector((0,-.20,.055))
        d=Vector((sign*.40,-.91,-.18)).normalized();q=d.to_track_quat('Z','Y')
        local=lambda p:cannon+q@Vector(p)
        end=.80+GUN_EXTENSION

        # A recessed coupling carries the independently readable weapon housing.
        # Only the round barrel is attached to the source barrel-spin bone.
        lathe('Cannon_IndependentMount_'+label,cannon,d,
              [(GUN_REAR,.205),(-.15,.225),(-.055,.225),(-.035,.19),
               (.025,.19),(.045,.255),(.13,.255),(.16,.245)],
              ['Mechanism','Mechanism','Mechanism','Ink','Mechanism','Mechanism','Mechanism','Mechanism'],arm,24,group)
        chamfer_loft('Cannon_Volume_'+label,cannon,d,
                     [(.12,.58,.57,.09),(.23,.74,.64,.11),
                      (.67+GUN_EXTENSION,.69,.60,.105),(end+.025,.55,.46,.075)],
                     'TeamPrimary',arm,group)

        # Side plaques are clipped eight-corner parts, seated on the actual
        # sloping side plane of the long housing, not arbitrary rectangle decals.
        for side,word in [(-1,'A'),(1,'B')]:
            poly=[(.35,-.105),(.43,-.175),(1.20,-.155),(1.29,-.09),
                  (1.29,.095),(1.21,.175),(.45,.185),(.35,.12)]
            def side_x(t):return .37-(.025*(t-.23)/(.67+GUN_EXTENSION-.23))
            points=[local((side*(side_x(t)+.008),v,t)) for t,v in poly]
            normal=q@Vector((side,0,.025/(.67+GUN_EXTENSION-.23)))
            panel('Cannon_CreamSide_'+label+word,points,.030,'Cream',arm,normal=normal,group=group)

        # The lower gun cradle follows its underside closely. It is not the
        # arm's large rear loop, and remains an independently named subassembly.
        for side in [-1,1]:
            pa=local((side*.245,-.365,.25));pb=local((side*.235,-.315,end-.11))
            beam('Cannon_OpenLowerRail_%s_%s'%(label,side),pa,pb,.085,.085,'TeamSecondary',arm,.009,group)
            beam('Cannon_CageRear_%s_%s'%(label,side),pa,local((side*.245,-.20,.25)),.085,.085,'TeamPrimary',arm,.009,group)
        beam('Cannon_CageFront_'+label,local((-.235,-.315,end-.11)),local((.235,-.315,end-.11)),.085,.085,'TeamPrimary',arm,.009,group)

        # Collapse the old projecting frame toward the elbow and upper-arm link.
        rear_top=(sign*1.38,.22,3.68);rear_low=(sign*1.61,-.21,2.94)
        beam('UpperArm_BackRail_'+label,rear_top,rear_low,.085,.10,'TeamSecondary',arm,.010,group)
        beam('UpperArm_InnerRail_'+label,rear_low,(sign*1.78,-.60,2.91),.085,.10,'TeamSecondary',arm,.010,group)

        # Fit the cream insert to the arm silhouette and replace its old deep,
        # outward-leaning rectangular slab with a tapered, clipped armor plaque.
        outer=lambda z:1.74+.51*(3.60-z)
        side_plate=[(197,151),(212,155),(219,162),(214,181),
                    (207,193),(189,188),(186,182),(192,159)]
        center=Vector((sum(p[0] for p in side_plate)/len(side_plate),sum(p[1] for p in side_plate)/len(side_plate)))
        frame=[tuple(center+(Vector(p)-center)*1.075) for p in side_plate]
        side_armor('UpperArm_SideMount_'+label,frame,sign,lambda z:outer(z)-.012,.17,'TeamSecondary',arm,group,.010)
        side_armor('UpperArm_SideCream_'+label,side_plate,sign,lambda z:outer(z)+.012,.045,'Cream',arm,group,.010)
        armfront=lambda z:-.32-(3.77-z)*.46
        stripe=mirrored([(236,137),(243,143),(239,151),(224,172),(215,171),(214,166)],sign)
        plate('UpperArm_CreamInset_'+label,stripe,lambda x,z:armfront(z)-.02,.05,'Sand',arm,group,.008)
        # The cuff terminates behind the casing; the recessed coupling is exposed.
        fitted_side_guard('Elbow_SandCuff_'+label,
            [(163,204),(181,211),(208,228),(207,254),(190,269),(167,264),(158,246)],
            sign,2.055,.22,'Sand',arm,group,.90)

        mb=cannon+d*(.81+GUN_EXTENSION)
        lathe('Cannon_MuzzleCollars_'+label,mb,d,
              [(0,.255),(.075,.255),(.10,.227),(.22,.227),(.25,.22)],
              ['Mechanism','TeamSecondary','Mechanism','Mechanism','Mechanism'],gun,32,group)
        muzzle=mb+d*.28
        tube('Cannon_OpenBore_'+label,muzzle,d,.22,.15,.13,'Mechanism',gun,group,32)
        cylinder('Cannon_VisibleAmberCore_'+label,muzzle+d*.004,d,.083,.012,'Amber',gun,32,group)
        socket_updates.append({'name':'Muzzle' if label=='R' else 'Muzzle_L','bone':gun,
                               'position':list(muzzle+d*.066),'direction':list(d)})

        up='UpperLeg_'+label;low='Leg_'+label;foot='Foot_'+label
        # The side drawing supplies the silhouettes; thickness and rearward
        # tilt wrap them around the mechanism instead of leaving thin sticks.
        fitted_side_guard('Hip_OuterSand_'+label,
            [(245,244),(259,250),(268,273),(263,292),(251,302),(242,291),(249,272)],
            sign,lambda z:1.36-.12*(z-2.4),.22,'Sand',up,face_scale=.83)
        fitted_side_guard('Knee_OuterSandArmor_'+label,
            [(210,333),(230,337),(246,356),(247,378),(229,388),(203,376),(191,354)],
            sign,lambda z:1.31+.10*(1.62-z),.23,'Sand',low,face_scale=.86)
        side_armor('Knee_CapMount_'+label,
            [(189,333),(211,333),(222,355),(212,379),(190,373),(177,353)],
            sign,1.25,.24,'Mechanism',low,bevel=.020)
        # Inner kneecap: compact six-corner armor on a short seated mounting pad.
        box('Knee_InnerCapMount_'+label,(sign*.89,-.26,1.45),(.16,.24,.25),'Mechanism',low,.025,'Legs')
        yz=[(-.39,1.62),(-.17,1.65),(-.06,1.54),(-.12,1.30),(-.31,1.29),(-.45,1.42)]
        inner_pix=[(y/SIDE_SCALE+222,511-z/SIDE_SCALE) for y,z in yz]
        fitted_side_guard('Knee_InnerBlueArmor_'+label,inner_pix,-sign,-.76,.16,'TeamPrimary',low,face_scale=.86)
        fitted_side_guard('Shin_SandSideInsert_'+label,
            [(215,399),(235,409),(246,430),(234,448),(213,439),(206,422)],
            sign,lambda z:1.34+.11*(1.00-z),.22,'Sand',low,face_scale=.85)

        # Foot pieces remain mechanically continuous, but the ankle cover is
        # separated from the lower armor ledge by a recessed dark structure.
        foot_wedge('Foot_RecessedChassis_'+label,sign,
            [(-1.04,-.28,.28,.10,.255,.045),(-.66,-.30,.30,.08,.26,.05),
             (-.30,-.23,.23,.10,.43,.045),(.04,-.21,.21,.12,.50,.04),(.26,-.19,.19,.10,.31,.035)],
            'Mechanism',foot)
        foot_wedge('Foot_BlueBridge_'+label,sign,
            [(-.99,-.31,.31,.20,.30,.055),(-.62,-.36,.36,.19,.37,.06),
             (-.20,-.28,.28,.18,.32,.05),(.10,-.21,.21,.14,.22,.04)],'TeamPrimary',foot)
        boot=foot_wedge('Ankle_LargeBlueBoot_'+label,sign,
            [(-1.02,-.235,.235,.44,.56,.045),(-.82,-.35,.35,.43,.65,.065),
             (-.48,-.31,.31,.46,.88,.065),(-.24,-.23,.23,.49,.83,.05)],'TeamPrimary',foot)
        boot['assembly']='Separate upper boot armor above a recessed mechanism; no fused blue sole'
        fitted_side_guard('Ankle_RearGuard_'+label,
            [(244,434),(259,434),(278,457),(273,474),(253,474),(235,462)],
            sign,1.43,.35,'Mechanism',foot,face_scale=.84)

    replacements={o.name[len(PREFIX):].split('.')[0]:o for o in PARTS[start:]}
    for suffix,o in replacements.items():
        o['assembly_revision']=ANNOTATION_REVISION
        if suffix.startswith('Cannon_'):
            o['assembly']='Weapon_Module_'+('R' if '_R' in suffix else 'L')
            o['weapon_length_factor']=GUN_LENGTH_FACTOR
            o['weapon_length_anchor_m']=GUN_REAR
    SOCKETS[:]=[s for s in SOCKETS if s['name'] not in ('Muzzle','Muzzle_L')]+socket_updates
    return replacements


def install_annotation_revision_in_full_build():
    """Replace only these authored parts before UVs, outlines and team cloning."""
    original={o.name:o for o in PARTS}
    revised=build_annotation_revision()
    for suffix,obj in revised.items():
        name=PREFIX+suffix;old=original.get(name)
        if old is not None:
            PARTS.remove(old)
            bpy.data.objects.remove(old,do_unlink=True)
        obj.name=name
    return revised


def configure_annotation_review_scenes():
    """Actual model close-ups for the user's review of the annotated changes."""
    hero=bpy.data.scenes['GroundMech_A4B2_01_TeamReview']
    hero.camera.data.ortho_scale=13.2
    bpy.data.objects[PREFIX+'Blue_Hero'].location.x=-3.10
    bpy.data.objects[PREFIX+'Red_Hero'].location.x=3.10
    for label in ('R','L'):
        for team in ('BLUE','RED'):
            suffix='' if team=='BLUE' else '_RED'
            parent=bpy.data.collections[PREFIX+'Arms_'+label] if team=='BLUE' else bpy.data.collections[PREFIX+'TEAM_RED']
            name=PREFIX+'Weapon_Module_'+label+suffix
            col=bpy.data.collections.get(name) or bpy.data.collections.new(name)
            if col.name not in parent.children:parent.children.link(col)
            for obj in list(parent.all_objects):
                if obj.type!='MESH' or not obj.name.startswith(PREFIX+'Cannon_') or obj.get('assembly')!='Weapon_Module_'+label:continue
                if obj.name not in col.objects:col.objects.link(obj)
                for old in list(obj.users_collection):
                    if old!=col:old.objects.unlink(obj)
    gun=review_scene('GroundMech_A4B2_07_WeaponAssembly',1800,1400)
    instance(gun,BLUE,'Weapon_Assembly_Blue',(0,0,0))
    camera(gun,'Weapon_Assembly_Camera',(-10,-13,7),(-1.92,-1.10,3.40),3.8)
    gun.render.filepath=str(OUT/'Previews/07_WeaponAssembly.png')
    legs=review_scene('GroundMech_A4B2_08_LegAssembly',1800,1600)
    instance(legs,BLUE,'Leg_Assembly_Blue',(0,0,0))
    camera(legs,'Leg_Assembly_Camera',(6,12,5),(0,.05,1.40),4.6)
    legs.render.filepath=str(OUT/'Previews/08_LegAssembly.png')
    return gun,legs

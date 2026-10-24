"""Reference-led weapon detail, preserving the user's 1.5x axial length.

Executed inside build_a4_b2's geometry namespace; no scene mutation until called.
"""
GUN_DETAIL_REVISION='gun_reference_detail_20261023'


def gun_front_armor_ring(name,center,axis,bone,group):
    """Faceted blue octagonal bezel surrounding the circular barrel socket."""
    q=Vector(axis).to_track_quat('Z','Y');c=Vector(center);count=32
    verts=[]
    # The chamfered outer front is larger than the rear seating ring. The
    # circular throat is recessed into the housing and does not cap the bore.
    for along,width,height,cut,radius in [
        (1.445,.58,.56,.075,None),(1.515,.64,.61,.09,None),
        (1.515,0,0,0,.242),(1.445,0,0,0,.242)]:
        for i in range(count):
            a=2*math.pi*i/count;dx=math.cos(a);dy=math.sin(a)
            if radius is None:
                w=width/2;h=height/2
                r=min(w/max(abs(dx),1e-8),h/max(abs(dy),1e-8),
                      (w+h-cut)/(abs(dx)+abs(dy)))
            else:r=radius
            verts.append(tuple(c+q@Vector((r*dx,r*dy,along))))
    faces=[]
    for row in range(4):
        nxt=(row+1)%4
        for i in range(count):faces.append((row*count+i,row*count+(i+1)%count,nxt*count+(i+1)%count,nxt*count+i))
    obj=mesh(name,verts,faces,'TeamPrimary',bone,.003,group)
    obj.data.materials.append(MATS['Mechanism'])
    for face in obj.data.polygons:
        if count*2<=face.index<count*3:face.material_index=1
    return obj


def build_gun_detail_revision():
    start=len(PARTS)
    for sign,label in [(-1,'R'),(1,'L')]:
        arm='GunArm_'+label;gun='Gun_'+label;group='Arms_'+label
        c=Vector((sign*1.72,-.66,3.045))
        d=Vector((sign*.40,-.91,-.18)).normalized();q=d.to_track_quat('Z','Y')
        local=lambda p:c+q@Vector(p)
        end=.80+GUN_EXTENSION
        # Restore a broad front armor lip around the barrel, as in A_v4.
        # Total weapon length and width remain inside the previous envelope.
        chamfer_loft('Cannon_Volume_'+label,c,d,
            [(.12,.58,.57,.09),(.23,.74,.64,.11),
             (.67+GUN_EXTENSION,.69,.60,.105),(end+.025,.62,.58,.075)],
            'TeamPrimary',arm,group)
        gun_front_armor_ring('Cannon_FrontArmorCollar_'+label,c,d,arm,group)

        # A_v4's raised rear top cover and small mechanical latch. Both are
        # seated solids with visible chamfers; they are not painted lines.
        top=[(-.12,.18),(.12,.18),(.22,.28),(.22,.60),(.16,.71),(-.16,.71),(-.22,.60),(-.22,.28)]
        top_y=lambda t:.367-.02*(t-.23)/(.67+GUN_EXTENSION-.23)
        points=[local((x,top_y(t),t)) for x,t in top]
        cover=prism('Cannon_TopReceiver_'+label,points,q@Vector((0,-.080,0)),'TeamPrimary',arm,.014,group)
        cover['reference_feature']='Raised rear top armor from approved A_v4 concept'
        box('Cannon_TopRearLatch_'+label,local((0,.345,.165)),(.14,.10,.20),'Mechanism',arm,.017,group,q)

        # The cream marking becomes a shorter front trapezoid inside a raised
        # blue side armor part, matching the reference hierarchy of large forms.
        for side,word in [(-1,'A'),(1,'B')]:
            surface=lambda t:.37-.025*(t-.23)/(.67+GUN_EXTENSION-.23)
            normal=q@Vector((side,0,.025/(.67+GUN_EXTENSION-.23)))
            cheek=[(.76,-.11),(.83,-.205),(1.21,-.185),(1.32,-.10),
                   (1.32,.10),(1.24,.195),(.83,.20),(.76,.12)]
            points=[local((side*(surface(t)+.030),v,t)) for t,v in cheek]
            prism('Cannon_SideCheek_'+label+word,points,-normal.normalized()*.070,'TeamPrimary',arm,.009,group)
            inset=[(.86,-.095),(.90,-.155),(1.17,-.14),(1.25,-.075),
                   (1.25,.085),(1.19,.157),(.91,.16),(.855,.105)]
            points=[local((side*(surface(t)+.052),v,t)) for t,v in inset]
            panel('Cannon_CreamSide_'+label+word,points,.032,'Cream',arm,normal=normal,group=group)

        # A real open trapezoidal lower frame, with broad angled side rails.
        # Rear arm braces are unchanged from the user's previous tuck-in request.
        rail=[(.23,-.36),(.35,-.41),(1.23,-.34),(1.36,-.28),
              (1.40,-.33),(1.28,-.43),(.34,-.49),(.23,-.44)]
        for side in (-1,1):
            points=[local((side*.29,v,t)) for t,v in rail]
            obj=prism('Cannon_OpenLowerRail_%s_%s'%(label,side),points,q@Vector((-side*.09,0,0)),
                      'TeamPrimary',arm,.009,group)
            obj.data.materials.append(MATS['TeamSecondary'])
            for p in obj.data.polygons:
                if p.index!=0:p.material_index=1

        # Three real axial steps and two recessed necks in the dark muzzle.
        # Original barrel axis, visible diameter and final muzzle plane retained.
        mb=c+d*(.81+GUN_EXTENSION)
        lathe('Cannon_MuzzleCollars_'+label,mb,d,
            [(0,.238),(.016,.255),(.064,.255),(.080,.238),(.088,.216),
             (.110,.216),(.122,.236),(.180,.236),(.196,.218),(.230,.218),(.250,.220)],
            ['Mechanism','Mechanism','Mechanism','Mechanism','Ink','Mechanism',
             'Mechanism','Mechanism','Mechanism','Mechanism','Mechanism'],gun,32,group)
        muzzle=mb+d*.28
        lathe('Cannon_OpenBore_'+label,muzzle,d,
            [(-.065,.218),(.012,.218),(.022,.220),(.050,.220),
             (.065,.205),(.065,.150),(.048,.140),(-.042,.140)],
            ['Mechanism','Mechanism','Mechanism','Mechanism','Mechanism','Ink','Ink','Ink'],gun,32,group)
        # The amber core is seated in the recess floor rather than floating
        # ahead of it. This is visible geometry only; the firing socket is fixed.
        cylinder('Cannon_VisibleAmberCore_'+label,muzzle-d*.041,d,.083,.012,'Amber',gun,32,group)

    result={o.name[len(PREFIX):].split('.')[0]:o for o in PARTS[start:]}
    for suffix,obj in result.items():
        obj['assembly_revision']=GUN_DETAIL_REVISION
        obj['assembly']='Weapon_Module_'+('R' if '_R' in suffix else 'L')
        obj['weapon_length_factor']=1.5
        obj['reference_basis']='Approved_A4_Concept.png and Approved_UpperBody_Detail.png; user gun-length amendment preserved'
    return result


def install_gun_detail_revision_in_full_build():
    original={o.name:o for o in PARTS}
    revised=build_gun_detail_revision()
    for suffix,obj in revised.items():
        name=PREFIX+suffix;old=original.get(name)
        if old is not None:
            PARTS.remove(old);bpy.data.objects.remove(old,do_unlink=True)
        obj.name=name
    return revised

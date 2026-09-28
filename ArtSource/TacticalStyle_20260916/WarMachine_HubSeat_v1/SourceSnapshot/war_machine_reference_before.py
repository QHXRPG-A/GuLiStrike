"""War Machine's approved front/side/parallel-back/hero design in authored parts.

No generated model is read. Coordinates are X-forward, Y-left, Z-up.  The
front and hero govern the horizontal paired guns; the corrected back governs
parallel missile pods. All opposite parts are mirrored from one construction.
"""
import math
from mathutils import Vector,Matrix

def build(a):
    def frame_x(name,center,length,width,height,color=0,throat=3,opening=.73):
        c=Vector(center)
        def ring(x,w,h,cut):
            return [tuple(c+Vector((x,y,z))) for y,z in [(-w+cut,-h),(w-cut,-h),(w,-h+cut),(w,h-cut),(w-cut,h),(-w+cut,h),(-w,h-cut),(-w,-h+cut)]]
        w,h=width/2,height/2;cut=min(w,h)*.22
        v=ring(-length/2,w*.92,h*.92,cut)+ring(length*.25,w,h,cut)+ring(length/2,w*.93,h*.93,cut)+ring(length/2,w*opening,h*opening,cut*.75)+ring(-length*.28,w*opening*.97,h*opening*.97,cut*.75)
        f=[];ci=[]
        for j in range(4):
            for i in range(8):f.append((j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i));ci.append(color if j<3 else throat)
        f.extend([tuple(reversed(range(8))),tuple(32+i for i in range(8))]);ci.extend([color,throat])
        o=a.mesh(name,v,f,color)
        for p,col in zip(o.data.polygons,ci):p.material_index=col
        return o

    def folded_plate(name,rows,color=1):
        # Each row has two authored corners. Faceted strips follow the hull
        # instead of putting a planar sticker above several angled surfaces.
        v=[tuple(p) for row in rows for p in row];n=len(v)
        v += [(x,y-.065,z) for x,y,z in v]
        f=[]
        for k in range(len(rows)-1):
            i=k*2;f.extend([(i,i+1,i+3,i+2),(i+n,i+2+n,i+3+n,i+1+n)])
        boundary=[0]+list(range(2,n,2))+[n-1]+list(range(n-3,0,-2))
        for i,j in zip(boundary,boundary[1:]+boundary[:1]):f.append((i,j,j+n,i+n))
        return a.mesh(name,v,f,color)

    def hover_cover(name,center,angle):
        # Broad ivory armor drops over the outer rim, with chamfered corners.
        rows=[(.365,1.085,35),(.285,1.225,33),(.135,1.330,29),(-.105,1.329,25),(-.145,1.302,22)]
        v=[];steps=4
        for z,r,half in rows:
            for k in range(steps+1):
                theta=angle+math.radians(-half+2*half*k/steps)
                v.append(tuple(Vector(center)+Vector(((r+.07)*math.cos(theta),(r+.07)*math.sin(theta),z+.018))))
        n=len(v);v += [tuple(Vector(p)-Vector((math.cos(math.atan2(p[1]-center[1],p[0]-center[0]))*.024,math.sin(math.atan2(p[1]-center[1],p[0]-center[0]))*.024,.006))) for p in v]
        f=[];w=steps+1
        for r in range(len(rows)-1):
            for k in range(steps):
                i=r*w+k;f.extend([(i,i+1,i+w+1,i+w),(i+n,i+w+n,i+w+1+n,i+1+n)])
        perimeter=list(range(w))+[r*w+steps for r in range(1,len(rows))]+list(range(n-2,n-w-1,-1))+[r*w for r in range(len(rows)-2,0,-1)]
        for i,j in zip(perimeter,perimeter[1:]+perimeter[:1]):f.append((i,j,j+n,i+n))
        return a.mesh(name,v,f,1)

    def armor_top(name,xy,z_at_x,normal,thickness=.10):
        # Wide painted armor has a formed edge, not the silhouette of a card.
        normal=Vector(normal).normalized()
        bottom=[Vector((x,y,z_at_x(x))) for x,y in xy]
        center=sum(bottom,Vector())/len(bottom)
        n=len(bottom)
        top=[center+(p-center)*.95+normal*thickness for p in bottom]
        verts=[tuple(p) for p in bottom+top]
        faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
        faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
        a.panel(name+' seam',[tuple(center+(p-center)*1.008-normal*.008) for p in bottom],normal,.018,3,False)
        return a.mesh(name,verts,faces,1)

    # Layered central mass: thick belly, dark waist, sloping main shell.
    a.swept_body('WM belly armor',[(-2.52,.82,.66,1.88,.18),(-1.35,1.17,.62,1.95,.19),(.52,1.19,.64,1.85,.18),(1.54,.73,.69,1.66,.15)],0)
    a.swept_body('WM mechanical waist',[(-2.60,.97,1.64,1.99,.11),(-1.15,1.28,1.58,2.01,.10),(1.36,.76,1.62,1.94,.10)],2)
    a.swept_body('WM main armor',[(-2.67,1.03,1.89,3.13,.20),(-1.72,1.35,1.81,3.52,.25),(-.60,1.35,1.78,3.48,.22),(.47,1.06,1.81,3.06,.20),(1.525,.58,1.89,2.35,.15)],0)
    a.roof_panel('WM rear ivory saddle',a.octagon_xy(-2.52,-1.83,.61,.08),lambda x:3.155+(x+2.67)*.39/.95,(-.41,0,1),.035,1,False)
    a.box('WM rear saddle vent',(-2.26,0,3.355),(.34,.51,.075),2,.045)
    a.swept_body('WM raised dorsal crest',[(-1.87,.67,3.38,3.66,.12),(-1.55,.84,3.33,3.79,.13),(-.52,.74,3.26,3.60,.12),(-.26,.66,3.24,3.43,.08)],0)
    armor_top('WM dorsal ivory lid',[(-1.60,-.52),(-1.60,.52),(-1.43,.59),(-.53,.51),(-.40,.34),(-.40,-.34),(-.53,-.51),(-1.43,-.59)],lambda x:3.805-(x+1.55)*.19/1.03,(.184,0,1),.08)
    a.roof_panel('WM dorsal latch',[(-.70,-.21),(-.70,.21),(-.63,.21),(-.63,-.21)],lambda x:3.895-(x+1.55)*.19/1.03,(.184,0,1),.007,3,False)
    a.swept_body('WM brow orange surround',[(-.22,.78,2.98,3.43,.10),(.54,.73,2.77,3.05,.11),(1.29,.48,2.47,2.63,.09)],0)
    armor_top('WM forehead ivory',[(-.17,-.64),(-.17,.64),(.15,.70),(.87,.56),(1.18,.33),(1.18,-.33),(.87,-.56),(.15,-.70)],lambda x:3.465-(x+.22)*.80/1.51,(.53,0,1),.10)
    a.light_x('WM central amber optic',1.337,0,2.656,.47,.13)
    frame_x('WM recessed chin intake',(1.675,0,2.106),.34,1.04,.43,0,2,.70)
    for side in (-1,1):
        a.prism('WM crest inset vent',[(-.77,3.40),(-.62,3.57),(-.38,3.43),(-.43,3.34)],side*.785,side*.818,3,.012)

    def cheeks():
        # Orange shoulder strips and ivory side plates are structural armor.
        a.prism('WM broad shoulder armor_L',[(-2.14,2.76),(-2.05,3.35),(-1.53,3.56),(-.68,3.47),(.25,2.85),(.10,2.52),(-1.38,2.52)],1.04,1.21,0,.07)
        cheek=a.prism('WM formed ivory cheek_L',[(-.95,3.34),(-.48,3.40),(.88,2.48),(1.11,2.08),(.90,1.88),(.57,1.93),(-.17,2.61),(-.95,3.08)],1.16,1.56,1,.055)
        lean=Matrix.Identity(4);lean[1][0]=-.13;lean[1][3]=.02;cheek.data.transform(lean)
        a.box('WM rear ivory clasp_L',(-2.72,.91,2.35),(.27,.35,.71),1,.065)
    a.pair(cheeks)

    # Lower forward armored prow, with a physical recessed mouth.
    a.swept_body('WM lower forward prow',[(.95,.87,.57,1.92,.15),(1.55,.88,.54,1.96,.14),(2.68,.58,.48,1.17,.12)],0)
    armor_top('WM prow ivory',[ (1.51,-.64),(1.51,.64),(1.77,.70),(2.73,.48),(2.82,.28),(2.82,-.28),(2.73,-.48),(1.77,-.70)],lambda x:1.980-(x-1.55)*.79/1.34,(.589,0,1),.09)
    a.roof_panel('WM prow hatch latch',[(2.39,-.24),(2.39,.24),(2.47,.24),(2.47,-.24)],lambda x:2.088-(x-1.55)*.79/1.34,(.589,0,1),.008,3,False)
    frame_x('WM prow opening',(2.955,0,.879),.50,1.23,.90,0,6,.72)
    for side in (-1,1):
        a.prism('WM prow ivory corner',[(2.70,.53),(2.80,1.22),(3.02,1.15),(3.09,.52)],side*.49,side*.57,1,.025)
    # Rear: framed vent and three orderly horizontal recesses.
    a.box('WM rear engine frame',(-2.663,0,1.95),(.23,1.12,1.46),3,.12)
    a.box('WM rear engine cover',(-2.80,0,1.95),(.09,.95,1.25),2,.105)
    for z in (1.62,1.95,2.28):a.box('WM rear vent recess',(-2.852,0,z),(.014,.54,.095),3,.02)

    # Four identical armored suspension ends and broad hover discs.
    def legs():
        for label,x in [('F',2.75),('R',-2.82)]:
            inner=.63 if label=='F' else -1.60
            a.beam('WM suspension diagonal_'+label+'_L',(inner,1.02,1.27),(x,2.12,.78),.70,.65,2)
            a.cyl('WM suspension round joint_'+label+'_L',(inner,1.40,1.15),.42,.48,2,'Y',12)
            a.cyl('WM suspension hub rim_'+label+'_L',(inner,1.70,1.15),.29,.045,3,'Y',12)
            a.cyl('WM suspension hub_'+label+'_L',(inner,1.73,1.15),.225,.035,2,'Y',12)
            a.prism('WM suspension ivory arch_'+label+'_L',[(inner-.49,.70),(inner-.49,1.37),(inner-.30,1.65),(inner+.08,1.70),(inner+.40,1.50),(inner+.47,1.13),(inner+.30,1.13),(inner+.24,1.38),(inner+.05,1.49),(inner-.22,1.45),(inner-.32,1.29),(inner-.32,.70)],1.35,1.72,1,.035)
            a.box('WM suspension toe core_'+label+'_L',(x,2.16,.87),(.81,1.02,.57),2,.085)
            a.box('WM suspension toe armor_'+label+'_L',(x+.12,2.20,.87),(.65,.50,.65),0,.075)
            for y in (1.84,2.49):a.box('WM suspension toe ivory cuff_'+label+'_L',(x,y,.89),(.86,.20,.63),1,.058)
            a.light_x('WM toe amber optic_'+label+'_L',x+.453,2.20,.91,.27,.21)
            c=(x,3.02,.34);disc_start=len(a.PARTS)
            profile=[(-.29,1.10),(-.20,1.27),(.13,1.31),(.285,1.23),(.355,1.09),(.34,1.01),(.41,.86),(.435,.64),(.50,.59),(.52,.47)]
            a.lathe('WM hover disc_'+label+'_L',c,profile,[2,0,0,0,2,2,2,1,1],axis='Z',sides=20)
            a.cyl('WM hover hub cap_'+label+'_L',(x,3.02,.866),.46,.021,2,'Z',20)
            for angle in (0,math.pi/2,math.pi,3*math.pi/2):
                hover_cover('WM hover ivory cover_'+label+'_L',c,angle)
                slit=a.box('WM hover face slit_'+label+'_L',(1.405,0,.012),(.015,.37,.071),3,.012)
                slit.data.transform(Matrix.Translation(Vector(c))@Matrix.Rotation(angle,4,'Z'))
            for angle in (math.pi/4,3*math.pi/4,5*math.pi/4,7*math.pi/4):
                before=len(a.PARTS)
                a.box('WM hover structural joint_'+label+'_L',(1.280,0,.005),(.13,.16,.35),2,.018)
                xf=Matrix.Translation(Vector(c))@Matrix.Rotation(angle,4,'Z')
                for ob in a.PARTS[before:]:ob.data.transform(xf)
            # The drawings use broad, weighty landing discs, with four armor
            # wraps visible from both front and side. Scale around each hub.
            for ob in a.PARTS[disc_start:]:
                for v in ob.data.vertices:
                    v.co.x=x+(v.co.x-x)*1.10
                    v.co.y=3.02+(v.co.y-3.02)*1.10
                    v.co.z=.05+(v.co.z-.05)*1.20
    a.pair(legs)
    def flank():
        a.prism('WM lower flank armored lobe_L',[(-1.02,.70),(-1.20,1.03),(-1.12,1.63),(-.65,1.81),(.15,1.74),(.42,1.44),(.21,.76)],1.00,1.35,0,.08)
        a.prism('WM rear flank armored lobe_L',[(-2.50,.81),(-2.61,1.20),(-2.42,1.76),(-1.91,1.78),(-1.70,1.50),(-1.91,.88)],.84,1.17,0,.07)
    a.pair(flank)

    # Two side-by-side guns per side: a blue receiver nested in orange armor.
    def cannons():
        start=len(a.PARTS)
        a.prism('WM cannon rear shoulder_L',[(-2.36,2.28),(-2.39,3.04),(-2.06,3.25),(-.95,3.12),(-.57,2.74),(-.73,2.22)],1.38,2.33,0,.10)
        a.box('WM cannon shoulder ivory strap_L',(-2.24,1.88,2.75),(.49,1.01,1.06),1,.08)
        a.prism('WM cannon underside ivory seat_L',[(-1.10,2.02),(-.90,1.89),(.60,1.89),(.90,2.11),(.65,2.17),(-.80,2.14)],1.40,2.63,1,.05)
        a.prism('WM twin cannon receiver_L',[(-1.12,2.27),(-1.17,2.79),(-.84,3.03),(.47,2.97),(1.10,2.69),(.98,2.17),(-.43,2.08)],1.38,2.61,2,.085)
        a.prism('WM cannon orange cage_L',[(-1.40,2.10),(-1.64,2.28),(-1.64,2.75),(-1.28,3.02),(-.33,3.08),(.28,2.89),(.21,2.68),(.40,2.51),(.27,2.21),(-.13,2.08),(-.90,2.07)],2.45,2.77,0,.065)
        a.box('WM cannon side lamp gasket_L',(-.92,2.796,2.58),(.59,.053,.32),3,.043)
        a.box('WM cannon side amber panel_L',(-.92,2.828,2.58),(.43,.017,.20),4,.018)
        a.box('WM cannon side receiver latch_L',(.52,2.75,2.68),(.75,.19,.42),2,.045)
        a.box('WM cannon small amber optic_L',(.52,2.853,2.68),(.26,.011,.115),4,.013)
        for x,w in [(-.56,.72),(-.12,.80),(.29,.70)]:a.box('WM receiver upper ridge_L',(x,2.00,3.035),(.36,w,.15),2,.052)
        for y in (1.70,2.34):
            a.box('WM ivory barrel collar_L',(1.00,y,2.56),(.32,.54,.70),1,.073)
            a.box('WM twin gun barrel_L',(1.73,y,2.56),(1.48,.35,.39),2,.042)
            a.recessed_muzzle('WM twin gun muzzle_L',(2.51,y,2.56),.53,.42,.48)
            a.box('WM gun barrel sleeve_L',(1.40,y,2.56),(.22,.39,.46),2,.033)
            for x in (1.83,2.22):a.slot_y('WM gun cooling slot_L',x,y+.178,2.56,.22)
        a.roof_panel('WM cannon ivory rear top_L',[(-1.25,1.43),(-1.25,2.31),(-.86,2.31),(-.86,1.43)],lambda x:3.024,(0,0,1),.027,1,False)
        for ob in a.PARTS[start:]:ob.data.transform(Matrix.Translation(Vector((0,.30,0))))
    a.pair(cannons)
    sockets={'Muzzle_Cannon_L':[2.775,2.32,2.56],'Muzzle_Cannon_R':[2.775,-2.32,2.56]}

    # Two independent fixed launchers. The rear view has zero outward splay.
    axis=Vector((math.sqrt(.5),0,math.sqrt(.5)))
    for side in (-1,1):
        center=Vector((-2.02,side*1.48,4.18));before=len(a.PARTS)
        a.swept_body('WM missile pod armored shell',[(-1.11,.61,-.66,.66,.10),(-.85,.69,-.72,.72,.12),(.76,.70,-.73,.73,.12),(.93,.65,-.67,.67,.10)],0)
        frame_x('WM missile pod ivory rim',(1.00,0,0),.26,1.43,1.51,1,2,.84)
        a.box('WM missile tube backplate',(.953,0,0),(.024,1.15,1.22),3,.04)
        a.box('WM rim upper wrap',(.79,0,.741),(.36,1.18,.025),1,0)
        for y in (-.298,.298):
            for z in (-.314,.314):
                a.lathe('WM launch tube socket',(.982,y,z),[(-.09,.215),(0,.249),(.065,.234),(.068,.172),(-.025,.163)],[2,2,2,3],axis='X',sides=12)
                tip=a.lathe('WM rounded missile tip',(.970,y,z),[(-.012,.147),(.049,.140),(.101,.108),(.132,.048),(.138,.004)],[4,4,4,4],axis='X',sides=12)
                for p in tip.data.polygons:p.use_smooth=True
        a.box('WM launcher rear panel seam',(-1.124,0,-.045),(.023,1.07,1.21),6,.08)
        a.box('WM launcher rear access hatch',(-1.143,0,-.07),(.023,.94,1.05),0,.075)
        a.box('WM launcher rear latch',(-1.160,0,.08),(.012,.10,.25),6,.012)
        for y in (-.704,.704):
            a.box('WM launcher hinge socket',(-.66,y,-.27),(.91,.16,.95),2,.115)
            a.box('WM launcher hinge inset',(-.66,y*1.11,-.25),(.65,.06,.69),2,.078)
            a.box('WM launcher side service recess',(.10,y,-.035),(.10,.015,.42),6,.016)
        a.box('WM launcher underside shoe',(-.64,0,-.742),(.78,.98,.20),2,.06)
        xf=Matrix.Translation(center)@Matrix.Rotation(-math.pi/4,4,'Y')
        for ob in a.PARTS[before:]:ob.data.transform(xf);ob['fixed_pitch_degrees']=45.
        a.box('WM launcher foot block',(-2.22,side*1.48,3.30),(.82,.86,.42),2,.09)
        a.box('WM launcher upper cradle',(-2.20,side*1.48,3.54),(.92,.91,.25),2,.06)
        for y in (-.28,0,.28):a.box('WM launcher cradle rib',(-2.35,side*1.48+y,3.55),(.71,.135,.30),2,.033)
        sockets['Muzzle_Missile_'+('L' if side>0 else 'R')]=list(center+axis*1.17)
    return dict(sockets_m=sockets,missile_pod_pitch_degrees=45,missile_pod_outward_splay_degrees=0,wheel_pivots_m={},wheel_radius_m=0,design_revision='Approved four-view reconstruction: layered hull, four armored hover discs, nested side twin cannons, parallel 45-degree missile pods')

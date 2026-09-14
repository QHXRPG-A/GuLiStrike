"""V3: rebuild three assets from clean hard-surface construction.
Keeps the reference silhouettes/color roles and the cannon's three-bone interface.
"""
import bpy,sys,math,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from hardsurface_common import *
import finish_assets as finish

def build_cannon(mats):
    b=Builder('HeavyDefenseCannon',mats);b.group='root'
    b.prism('Octagonal foundation',circle(0,0,1.94,8,math.pi/8),0,.36,role='Dark',bevel=.045)
    b.prism('Foundation upper armor',circle(0,0,1.79,8,math.pi/8),.36,.50,role='Steel',bevel=.025)
    for i,deg in enumerate([53,127,233,307]):
        b.transform=Matrix.Rotation(math.radians(deg),4,'Z')
        b.prism('Foot %d structural casting'%i,chamfer_rect(2.39,0,1.94,1.05,.14),.02,.40,role='Steel',bevel=.035)
        b.prism('Foot %d flat anchor plate'%i,chamfer_rect(2.70,0,.91,.79,.10),.41,.53,role='Edge',bevel=.018)
        b.prism('Foot %d inset anchor pad'%i,chamfer_rect(2.70,0,.65,.56,.06),.535,.55,role='Dark',bevel=.006)
        b.triangle('Foot %d regular caution triangle'%i,2.70,0,.552,.33,.31)
        for y in [-.35,.35]:b.cylz('Foot captive fastener',(2.28,y),.062,.40,.455,'Dark',6,.004)
        b.box('Foot leading bumper',(3.32,0,.27),(.20,.76,.29),'Dark',.025)
    b.transform=Matrix.Identity(4)
    b.ring('Fixed bearing housing',circle(0,0,1.69,48),circle(0,0,1.50,48),.51,.67,'Edge',.015)
    b.ring('Bearing identification band',circle(0,0,1.696,48),circle(0,0,1.68,48),.58,.615,'Ochre',.004)
    for y in [-1.80,1.80]:b.box('Foundation rectangular safety plate',(0,y,.32),(.72,.04,.095),'Ochre',.004)

    b.group='base_yaw'
    b.cylz('Rotating turntable',(0,0),1.485,.535,.82,'Steel',48,.022)
    b.cylz('Turntable top plate',(0,0),1.37,.825,.89,'Edge',48,.010)
    b.cylz('Central pivot housing',(0,0),.68,.89,1.13,'Dark',24,.025)
    yoke=[(-.58,.86),(.58,.86),(.58,1.14),(.38,1.38),(.35,2.28),(.23,2.40),(-.23,2.40),(-.35,2.28),(-.38,1.38),(-.58,1.14)]
    for side in [-1,1]:
        y=side*1.14
        b.prism('Rigid yaw yoke',yoke,y-.19,y+.19,'Y','Steel',.035)
        b.box('Yoke vertical stiffener',(0,side*1.342,1.52),(.18,.052,.70),'Edge',.012)
        b.cylinder('Pitch bearing cap',(0,side*1.34,2.12),(0,side*1.45,2.12),.295,'Edge',24,.016)
        b.cylinder('Pitch bearing center',(0,side*1.454,2.12),(0,side*1.48,2.12),.137,'Dark',6,.005)
        for x in [-.82,.82]:b.cylz('Turntable captive fastener',(x,side*.62),.057,.89,.93,'Dark',6,.004)

    b.group='barrel_pitch'
    b.cylinder('Pitch axle',(0,-1.32,2.12),(0,1.32,2.12),.202,'Dark',24,.012)
    b.prism('Elevating cradle',[(-.56,1.94),(.56,1.94),(.69,2.21),(.90,2.61),(-1.10,2.61),(-.68,2.30)],-.87,.87,'Y','Dark',.035)
    b.prism('Breech central chassis',[(-2.38,2.60),(.96,2.60),(1.36,2.92),(1.08,3.10),(-2.38,3.10)],-.78,.78,'Y','Steel',.035)
    b.box('Breech deck inset',(-.75,0,3.118),(2.50,1.36,.055),'Dark',.012)
    for x in [-1.66,-.65,.36]:b.box('Breech deck access plate',(x,0,3.157),(.90,1.13,.042),'Steel',.010)

    housing=[(-2.51,2.68),(1.79,2.68),(2.39,2.95),(2.39,3.55),(1.78,3.93),(-1.26,3.93),(-1.67,4.21),(-2.51,4.21)]
    lower=[(-2.51,2.65),(1.82,2.65),(2.39,2.87),(5.40,2.87),(5.40,3.14),(2.30,3.14),(1.75,2.94),(-2.51,2.94)]
    upper=[(1.77,3.50),(2.36,3.50),(2.64,3.34),(5.40,3.34),(5.40,3.62),(2.56,3.62),(2.27,3.78),(1.77,3.78)]
    amber=[(-2.51,3.83),(-1.60,3.83),(-1.21,3.57),(.62,3.57),(.62,3.925),(-1.26,3.925),(-1.67,4.205),(-2.51,4.205)]
    divider=[(-2.43,3.89),(-1.58,3.89),(-1.20,3.635),(.52,3.635),(.52,3.675),(-1.21,3.675),(-1.60,3.93),(-2.43,3.93)]
    for side in [-1,1]:
        y=side*1.10
        b.prism('Main angular rail housing',housing,y-.265,y+.265,'Y','Steel',.028)
        b.prism('Lower straight acceleration rail',lower,y-.235,y+.235,'Y','Dark',.023)
        b.prism('Upper straight acceleration rail',upper,y-.21,y+.21,'Y','Steel',.018)
        b.prism('Rear solid ochre armor',amber,side*1.378-.025,side*1.378+.025,'Y','Ochre',.009)
        b.prism('Single straight armor separator',divider,side*1.409-.006,side*1.409+.006,'Y','Dark',.001)
        b.prism('Forward ochre armor insert',[(.83,3.80),(1.81,3.80),(1.98,3.94),(.83,3.94)],side*1.372-.017,side*1.372+.017,'Y','Ochre',.008)
        b.box('Long guide rail lower',(3.80,y,3.165),(3.14,.32,.04),'Edge',.006)
        b.box('Long guide rail upper',(3.82,y,3.32),(3.09,.27,.035),'Edge',.005)
        for x in [2.72,3.34,3.96,4.58]:
            b.box('Regular rail bridge lug',(x,side*1.235,3.245),(.13,.16,.20),'Steel',.009)
        b.box('Muzzle teal housing',(5.18,y,3.48),(.45,.49,.30),'Teal',.025)
        b.box('Muzzle aperture backing',(5.414,y,3.25),(.025,.355,.48),'Dark',.006)
        b.box('Muzzle left cheek',(5.33,y-.218,3.23),(.19,.086,.54),'Edge',.011)
        b.box('Muzzle right cheek',(5.33,y+.218,3.23),(.19,.086,.54),'Edge',.011)
        b.box('Muzzle lower sill',(5.34,y,2.96),(.19,.50,.095),'Steel',.010)
        b.box('Muzzle short rectangular lamp',(5.18,side*1.355,3.45),(.29,.018,.046),'Cyan',.004)
        # Deliberate broad side plates with a single inset, not noisy embossing.
        b.prism('Rear removable access panel',chamfer_rect(-1.43,3.26,1.60,.37,.065),side*1.373-.018,side*1.373+.018,'Y','Dark',.010)
        for x in [-1.95,-1.43,-.91]:b.box('Rear cassette rib',(x,side*1.407,3.26),(.047,.022,.22),'Edge',.006)
        for x in [-1.88,-1.65,-1.42,-1.19,-.96]:
            b.box('Equally spaced heat sink',(x,side*.802,3.58),(.072,.07,.48),'Edge',.010)
        b.cylinder('Recoil sleeve',(-.24,side*.62,2.88),(.90,side*.62,2.88),.11,'Dark',16,.011)
        b.cylinder('Recoil slide',(.90,side*.62,2.88),(1.63,side*.62,2.88),.059,'Edge',16,.007)
    for x in [-.44,.71]:
        for side in [-1,1]:b.box('Cross brace foot',(x,side*.69,3.28),(.22,.20,.39),'Steel',.024)
        b.beam('Straight breech cross brace',(x,-.70,3.49),(x,.70,3.49),.17,'Edge',.018)
    return b

def y_outline(r,w,a=.30,c=.10):
    points=[]
    for theta in [math.pi,math.pi+2*math.pi/3,math.pi+4*math.pi/3]:
        for x,y in [(a,-w),(r-c,-w),(r,-w+c),(r,w-c),(r-c,w),(a,w)]:
            points.append((x*math.cos(theta)-y*math.sin(theta),x*math.sin(theta)+y*math.cos(theta)))
    return points

def build_shield(mats):
    b=Builder('ShieldGenerator',mats)
    b.prism('Y foundation lower sole',y_outline(1.76,.57,.39,.15),0,.20,role='Dark',bevel=.030)
    b.prism('Y foundation ivory casting',y_outline(1.72,.54,.37,.13),.20,.43,role='Ivory',bevel=.028)
    b.prism('Y foundation teal deck',y_outline(1.66,.49,.35,.12),.43,.56,role='Teal',bevel=.020)
    b.cylz('Central lower service boss',(0,0),.47,.555,.65,'Edge',6,.018)
    b.cylz('Central service insert',(0,0),.34,.651,.68,'Dark',6,.010)
    b.cylz('Inner structural core',(0,0),.51,.60,4.72,'Dark',6,.020)
    lower=[(.23,.86),(.96,.86),(.96,1.12),(1.31,1.45),(1.31,3.115),(.23,3.115)]
    upper=[(.23,3.37),(1.31,3.37),(1.31,4.19),(1.43,4.43),(1.43,4.70),(.23,4.70)]
    for i,theta in enumerate([math.pi,math.pi/3,-math.pi/3]):
        b.transform=Matrix.Rotation(theta,4,'Z')
        b.prism('Pillar base plinth',chamfer_rect(.85,0,1.00,.83,.10),.56,.79,role='Ivory',bevel=.025)
        b.prism('Pillar base top ring',chamfer_rect(.85,0,.98,.78,.10),.79,.86,role='Teal',bevel=.012)
        b.prism('Lower planar armor body',lower,-.385,.385,'Y','Ivory',.025)
        b.prism('Waist joint dark spacer',chamfer_rect(.78,0,1.05,.68,.075),3.115,3.37,role='Dark',bevel=.010)
        b.box('Waist cyan status strip',(1.309,0,3.235),(.018,.26,.031),'Cyan',.002)
        b.prism('Upper planar armor body',upper,-.385,.385,'Y','Ivory',.027)
        b.box('Upper red identification stripe',(1.331,0,3.55),(.027,.65,.12),'Red',.006)
        b.prism('Upper projecting teal cover',chamfer_rect(1.35,0,.20,.63,.055),3.73,3.96,role='Teal',bevel=.018)
        for t in [-.25,.25]:
            b.box('Straight lower teal stiffener',(1.341,t,2.12),(.052,.10,1.12),'Teal',.012)
            b.box('Rectangular red identification block',(1.340,t,2.87),(.047,.13,.22),'Red',.008)
        b.box('Upper end face access inset',(1.450,0,4.45),(.018,.52,.29),'Dark',.008)
        b.box('Upper end face armor plate',(1.464,0,4.45),(.030,.44,.23),'Ivory',.008)
        for side in [-1,1]:
            # Flat rectangles on flat armor planes: no UV distortion or dot matrices.
            b.box('Recessed rectangular instrument panel',(.82,side*.396,2.70),(.43,.023,.56),'Dark',.006)
            for z,w in [(2.79,.27),(2.63,.20)]:b.box('Instrument screen rectangular readout',(.82,side*.410,z),(w,.013,.030),'Cyan',.002)
            b.box('Lower service recess',(.84,side*.395,1.79),(.54,.019,.60),'Steel',.009)
            b.box('Lower service hatch',(.84,side*.408,1.79),(.46,.025,.53),'Ivory',.008)
            b.box('Hatch latch',(.96,side*.426,1.78),(.035,.018,.12),'Dark',.002)
            b.box('Upper side plain panel',(.82,side*.403,4.23),(.66,.035,.45),'Ivory',.014)
        b.prism('Base exposed flat pad',chamfer_rect(1.39,0,.39,.62,.05),.566,.60,role='Edge',bevel=.007)
        for t in [-.17,0,.17]:b.box('Base regular cooling slot',(1.44,t,.609),(.20,.060,.018),'Dark',.003)
        b.triangle('Base regular caution triangle',1.06,0,.867,.16,.14,'Ochre')
    b.transform=Matrix.Identity(4)
    b.prism('Y crown teal casting',y_outline(1.57,.43,.30,.10),4.70,4.84,role='Teal',bevel=.023)
    b.ring('Crown continuous narrow rim',y_outline(1.57,.43,.30,.10),y_outline(1.51,.365,.25,.085),4.84,4.895,'Edge',.010)
    b.prism('Crown inner flat deck',y_outline(1.50,.36,.245,.082),4.835,4.856,role='Dark',bevel=.005)
    for theta in [math.pi,math.pi/3,-math.pi/3]:
        b.transform=Matrix.Rotation(theta,4,'Z')
        b.prism('Crown shield emitter block',chamfer_rect(1.02,0,.65,.55,.085),4.86,4.975,role='Ivory',bevel=.013)
        b.prism('Crown shield emitter top',chamfer_rect(1.02,0,.44,.37,.06),4.976,5.0,role='Teal',bevel=.004)
        b.box('Crown emitter short status bar',(.64,0,4.88),(.12,.19,.025),'Cyan',.003)
    b.transform=Matrix.Identity(4)
    return b

def build_refinery(mats):
    b=Builder('RedOreRefinery',mats)
    outline=[(5.82,2.90),(3.80,4.70),(-3.80,4.70),(-5.82,2.90),(-5.82,-2.90),(-3.80,-4.70),(3.80,-4.70),(5.82,-2.90)]
    inner=[(x*.79,y*.78) for x,y in outline]
    sole=[(x*1.025,y*1.025) for x,y in outline]
    b.ring('Octagonal foundation sole',sole,inner,0,.24,'Dark',.045)
    b.ring('Octagonal platform structural ring',outline,inner,.24,1.27,'Steel',.040)
    b.ring('Inner platform rim',[(x*1.025,y*1.025) for x,y in inner],inner,1.27,1.46,'Edge',.015)
    for i,(p,q) in enumerate(zip(outline,outline[1:]+outline[:1])):
        pi,qi=inner[i],inner[(i+1)%8]
        def lerp(a,c,t):return (a[0]+(c[0]-a[0])*t,a[1]+(c[1]-a[1])*t)
        poly=[lerp(p,q,.012),lerp(p,q,.988),lerp(pi,qi,.988),lerp(pi,qi,.012)]
        b.prism('Teal deck segment %02d'%i,poly,1.275,1.435,role='Teal',bevel=.025)
        # Evenly spaced outer casing plates follow the straight octagon edges.
        P,Q=Vector((*p,0)),Vector((*q,0));direction=Q-P
        count=max(2,round(direction.length/1.15))
        outward=Vector((direction.y,-direction.x,0)).normalized()
        for j in range(count):
            a=P+direction*((j+.12)/count)+outward*.035;a.z=.68
            c=P+direction*((j+.88)/count)+outward*.035;c.z=.68
            b.beam('Regular perimeter armor cassette',a,c,.12,'Dark',.018,depth=.70)
            a.z=1.04;c.z=1.04
            b.beam('Cassette upper steel edge',a,c,.13,'Edge',.009,depth=.055)
    b.prism('Central equipment platform',chamfer_rect(0,1.31,5.45,4.20,.22),.34,.64,role='Dark',bevel=.035)
    b.box('Rear platform tie',(0,3.42,.63),(5.20,1.0,.32),'Steel',.025)
    for x in [-2.70,-1.35,1.35,2.70]:
        b.box('Front structural buttress',(x,-4.84,.71),(.35,.41,1.0),'Steel',.035)
        b.box('Buttress upper block',(x,-5.01,1.12),(.36,.39,.22),'Edge',.026)
    b.box('Front service access border',(0,-4.88,.71),(.68,.25,1.00),'Dark',.032)
    b.box('Front access ochre plate',(0,-5.014,.72),(.39,.028,.74),'Ochre',.010)

    for side in [-1,1]:
        x=side*1.27
        ramp=[(-3.74,1.42),(-.56,.91),(-.56,.66),(-3.74,1.14)]
        b.prism('Straight inward conveyor frame',ramp,x-.47,x+.47,'X','Steel',.025)
        belt=[(-3.68,1.423),(-.62,.932),(-.62,.901),(-3.68,1.39)]
        b.prism('Conveyor flat belt',belt,x-.32,x+.32,'X','Dark',.004)
        for dx in [-.42,.42]:
            b.beam('Straight conveyor side guide',(x+dx,-3.72,1.49),(x+dx,-.59,.988),.07,'Teal',.009)
        for k in range(8):
            y=-3.48+k*.37;z=1.423-(y+3.68)*.491/3.06+.012
            b.box('Equally spaced conveyor tread',(x,y,z),(.57,.12,.027),'Edge',.003)
        # A single raised, planar direction triangle lies on the same inclined plane.
        poly=[(x-.16,-3.10),(x+.16,-3.10),(x,-2.78)]
        top=[(xx,yy,1.423-(yy+3.68)*.491/3.06+.033) for xx,yy in poly]
        b.mesh('Conveyor direction triangle',top+[(xx,yy,zz+.007) for xx,yy,zz in top],
               [(0,2,1),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)],'Ochre',0,'Planar isosceles triangle')

    # Straight guardrail runs with matching posts, following five regular perimeter edges.
    rail=[(-5.51,.45),(-5.51,2.72),(-3.64,4.40),(3.64,4.40),(5.51,2.72),(5.51,.45)]
    post_points=[]
    for i,(p,q) in enumerate(zip(rail,rail[1:])):
        P,Q=Vector((*p,2.44)),Vector((*q,2.44))
        b.beam('Straight ochre guardrail',P,Q,.075,'Ochre',.009)
        n=max(1,math.ceil((Q-P).length/1.90))
        for j in range(n):
            v=P+(Q-P)*j/n;post_points.append((v.x,v.y))
        # Straight rear containment plates under the safety rail.
        direction=Q-P
        for j in range(n):
            a=P+direction*((j+.08)/n);c=P+direction*((j+.92)/n);a.z=c.z=1.80
            b.beam('Rear containment plate',a,c,.07,'Dark',.015,depth=.60)
    post_points.append(rail[-1])
    for x,y in post_points:
        b.box('Vertical guardrail post',(x,y,1.94),(.075,.075,1.00),'Steel',.010)
        b.box('Guardrail post ochre cap',(x,y,2.39),(.081,.081,.15),'Ochre',.008)
        b.box('Guardrail mounting block',(x,y,1.48),(.17,.17,.085),'Edge',.009)

    # Primary processing vessel: exact coaxial shells and clean circular lids.
    c=(-.50,.48)
    b.cylz('Primary vessel base flange',c,.98,.65,.87,'Dark',32,.025)
    b.cylz('Primary vessel teal lower drum',c,.82,.87,1.95,'Teal',32,.030)
    b.cylz('Primary lower steel band',c,.875,1.04,1.15,'Edge',32,.013)
    b.cylz('Primary upper steel band',c,.90,1.86,2.04,'Edge',32,.016)
    b.cylz('Primary upper pressure vessel',c,.91,2.04,2.91,'Dark',32,.025)
    b.cylz('Primary oxide red lid',c,.97,2.91,3.19,'Red',32,.032)
    b.cylz('Primary lid socket',c,.24,3.19,3.25,'Edge',12,.012)
    b.cylz('Primary lid center',c,.17,3.25,3.275,'Dark',12,.006)
    for i in range(6):
        a=i*math.tau/6
        b.cylz('Primary lid captive bolt',(c[0]+.74*math.cos(a),c[1]+.74*math.sin(a)),.042,3.19,3.223,'Edge',6,.003)
    for side in [-1,1]:
        b.box('Primary flat inspection window',(c[0]+side*.844,c[1],1.54),(.05,.49,.40),'Dark',.012)
        b.box('Primary short readout',(c[0]+side*.874,c[1],1.55),(.02,.25,.04),'Cyan',.003)

    c=(.66,2.38)
    b.prism('Rear processing support housing',chamfer_rect(*c,2.24,1.91,.18),.65,2.57,role='Dark',bevel=.035)
    b.cylz('Rear processing upper drum',c,1.16,2.57,3.13,'Teal',32,.024)
    b.cylz('Rear processing red lid',c,1.21,3.13,3.35,'Red',32,.025)
    b.cylz('Rear processing teal lid inset',c,1.015,3.351,3.40,'Teal',32,.012)
    for x in [-.10,.62,1.34]:
        b.box('Rear cabinet red service panel',(x,3.351,1.53),(.60,.055,1.38),'Red',.018)
        b.box('Rear cabinet latch',(x+.20,3.388,1.51),(.055,.032,.19),'Edge',.007)
    chimney=(1.16,2.34)
    b.cylz('Chimney mounting flange',chimney,.31,3.40,3.53,'Edge',24,.015)
    b.cylz('Straight chimney',chimney,.195,3.53,4.89,'Steel',24,.020)
    b.cylz('Chimney red crown',chimney,.43,4.89,5.01,'Red',32,.018)
    b.cylz('Chimney teal cap',chimney,.34,5.012,5.04,'Teal',32,.006)
    b.cylz('Antenna foot',(.13,1.89),.145,3.40,3.58,'Edge',16,.009)
    b.cylz('Straight antenna',(.13,1.89),.085,3.58,5.60,'Steel',16,.010)
    b.cylz('Antenna red sleeve',(.13,1.89),.094,4.99,5.29,'Red',16,.005)

    for side in [-1,1]:
        c=(side*2.28,1.35)
        b.cylz('Side processing vessel foot',c,.42,.64,.85,'Dark',16,.016)
        b.cylz('Side processing red cylinder',c,.34,.85,1.96,'Red',16,.019)
        b.cylz('Side processing top assembly',c,.365,1.96,2.52,'Dark',16,.018)
        b.cylz('Side processing collar',c,.40,2.29,2.40,'Red',16,.013)
        b.cylz('Side processing cyan status band',c,.369,2.14,2.174,'Cyan',16,.003)
        b.cylz('Side processing cap',c,.34,2.52,2.60,'Edge',16,.012)
        # Each perimeter beacon is a clean stem with a regular, concentric cap.
        for j,(x,y,r,z) in enumerate([(side*4.58,-1.86,.42,2.95),(side*5.13,-.62,.53,3.14)]):
            b.prism('Beacon foot',chamfer_rect(x,y,.43,.43,.08),1.44,1.58,role='Dark',bevel=.018)
            b.cylz('Beacon straight stem',(x,y),.14,1.58,z-.11,'Steel',16,.015)
            b.cylz('Beacon red stem sleeve',(x,y),.151,1.87,2.16,'Red',16,.005)
            b.cylz('Beacon dark lower collar',(x,y),r,z-.14,z,'Dark',32,.018)
            b.cylz('Beacon oxide red cap',(x,y),r+.018,z,z+.10,'Red',32,.013)
            b.cylz('Beacon teal top face',(x,y),r-.055,z+.101,z+.137,'Teal',32,.007)
            b.box('Beacon rectangular indicator',(x,y,z+.144),(.11,.055,.013),'Cyan',.003)
        # Low side housings have regular ribs like the supplied RPF reference.
        b.prism('Auxiliary square cabinet',chamfer_rect(side*1.62,1.02,.88,1.03,.10),.64,1.87,role='Steel',bevel=.024)
        b.box('Auxiliary cabinet red lid',(side*1.62,1.02,1.91),(.79,.94,.08),'Red',.013)
        b.box('Auxiliary flat instrument recess',(side*1.62,.489,1.37),(.60,.045,.53),'Dark',.012)
        for z,w in [(1.47,.37),(1.31,.27)]:b.box('Auxiliary straight readout',(side*1.62,.458,z),(w,.018,.036),'Cyan',.003)
    return b

def draft():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats=make_materials();builders=[]
    for build in [build_refinery,build_shield,build_cannon]:
        b=build(mats);builders.append(b)
        camera,center,size,stage=setup_studio(b.scene,b.parts,res=(1600,1250),samples=40)
        b.scene['StyleReference']='/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory'
        b.scene['ArtDirection']='Flat armor planes, straight rails, uniform narrow chamfers, symmetric repeatable construction.'
        render(b.scene,WORK/(b.name+'_Construction.png'))
        (OUT/'Construction').mkdir(exist_ok=True)
        pack_save(b.scene,OUT/'Construction'/(b.name+'_EditableParts.blend'))
        (OUT/'Construction'/(b.name+'_PartCatalog.json')).write_text(json.dumps(b.catalog,indent=2),encoding='utf-8')
        print('HARD_SURFACE_CONSTRUCTION '+b.name+' '+str(len(b.parts)),flush=True)
    # Save named parts with live bevel modifiers before preparing merged exports.
    bpy.context.window.scene=builders[-1].scene
    bpy.ops.wm.save_as_mainfile(filepath=str(WORK/'ThreeModels_Construction.blend'),compress=True)
    print('HARD_SURFACE_CONSTRUCTION_COMPLETE',flush=True)

if __name__=='__main__':draft()

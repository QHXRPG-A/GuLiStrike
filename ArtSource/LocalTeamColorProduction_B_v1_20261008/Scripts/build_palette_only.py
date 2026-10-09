"""Independent color candidates. Never edit source meshes, rigs, clips or UE assets."""
import bpy, json, math, hashlib, sys, re
import numpy as np
from pathlib import Path
from collections import defaultdict, Counter
from mathutils import Vector, Matrix
from mathutils.kdtree import KDTree
R=Path('D:/UE5.7/test1');O=R/'ArtSource/LocalTeamColorProduction_B_v1_20261008'
INS=json.loads((O/'Reports/source-inspection.json').read_text(encoding='utf8'))
CONFIGS=R/'ArtSource/LocalTeamColorReference_A_v3_20261008/Configs'
PALETTE={'Cream':'#FEE4D9','Sand':'#D5C09C','Gray':'#2C3735','BlueLight':'#6AA4BE','BlueDark':'#274E61','RedLight':'#A34053','RedDark':'#662249','Amber':'#EE9D58','Cyan':'#0D9099'}
ROLES=['Cream','Sand','Gray','TeamLight','TeamDark','Amber','Cyan','TeamLightLamp']
LIGHT=Vector((.35,-.55,.76)).normalized()
SSF_KEYS=['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']
SELECTED=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
ALL_KEYS=['DefaultSoldier','WM01','BiZhiMao','ShieldGenerator','ManualOutpost','MissileTurret','SentryTurret','ResourceFactory']+['SSF_'+k for k in SSF_KEYS]

def linear(hex):
    rgb=np.array([int(hex[i:i+2],16)/255 for i in (1,3,5)],np.float32)
    return np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
def color(role,team):
    if role in ('TeamLight','TeamLightLamp'):role=team+'Light'
    if role=='TeamDark':role=team+'Dark'
    return (*linear(PALETTE[role]),1.)
def signature(ob):
    h=hashlib.sha256();m=ob.data
    for data,prop,width,dtype in [(m.vertices,'co',3,np.float32),(m.edges,'vertices',2,np.int32),(m.loops,'vertex_index',1,np.int32),(m.polygons,'loop_start',1,np.int32),(m.polygons,'loop_total',1,np.int32),(m.corner_normals,'vector',3,np.float32),(m.polygons,'use_smooth',1,np.bool_)]:
        a=np.empty(len(data)*width,dtype);data.foreach_get(prop,a);h.update(a.tobytes())
    for uv in m.uv_layers:
        a=np.empty(len(uv.data)*2,np.float32);uv.data.foreach_get('uv',a);h.update(uv.name.encode());h.update(a.tobytes())
    h.update(json.dumps([[(ob.vertex_groups[g.group].name,g.weight) for g in v.groups] for v in m.vertices],separators=(',',':')).encode())
    h.update(np.array(ob.matrix_basis,np.float32).tobytes());h.update(np.array(ob.matrix_parent_inverse,np.float32).tobytes())
    h.update(json.dumps([(m.name,m.type) for m in ob.modifiers]).encode())
    return h.hexdigest()
def bonesig(ob):
    return hashlib.sha256(json.dumps([(b.name,b.parent.name if b.parent else '',[list(r) for r in b.matrix_local]) for b in ob.data.bones],separators=(',',':')).encode()).hexdigest()
def action_sig(a):
    curves=list(a.fcurves) if hasattr(a,'fcurves') else []
    for layer in a.layers:
        for strip in layer.strips:
            if hasattr(strip,'channelbags'):
                for bag in strip.channelbags:curves.extend(bag.fcurves)
    rows=[(f.data_path,f.array_index,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation) for k in f.keyframe_points]) for f in curves]
    return hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
def connected(me):
    parent=list(range(len(me.vertices)))
    def root(a):
        while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
        return a
    for e in me.edges:
        a,b=map(root,e.vertices)
        if a!=b:parent[b]=a
    g=defaultdict(list)
    for p in me.polygons:g[root(p.vertices[0])].append(p.index)
    return dict(enumerate(g.values()))
def describe(ob,faces):
    me=ob.data;ids={v for i in faces for v in me.polygons[i].vertices}
    co=np.array([ob.matrix_world@me.vertices[v].co for v in ids]);lo=co.min(0);hi=co.max(0)
    return {'lo':lo,'hi':hi,'c':(lo+hi)/2,'d':hi-lo,'faces':len(faces),'area':sum(me.polygons[i].area for i in faces)}
def set_role(face_roles,faces,role):
    for f in faces:face_roles[f]=role
def nearest_parts(ob,helpers):
    points=[];names=[];dg=bpy.context.evaluated_depsgraph_get()
    for helper in helpers:
        idx=len(names);names.append(helper.name)
        ev=helper.evaluated_get(dg);me=ev.to_mesh()
        me.calc_loop_triangles()
        points.extend((helper.matrix_world@tri.center,idx) for tri in me.loop_triangles)
        ev.to_mesh_clear()
    tree=KDTree(len(points))
    for n,(p,idx) in enumerate(points):tree.insert(p,idx)
    tree.balance()
    groups=defaultdict(list)
    for p in ob.data.polygons:
        q=ob.matrix_world@p.center;_,idx,dist=tree.find(q);groups[names[idx]].append(p.index)
    return groups

def whole_component_names(ob,groups):
    owners={f:n for n,faces in groups.items() for f in faces};result=defaultdict(list)
    for faces in connected(ob.data).values():
        name=Counter(owners[f] for f in faces).most_common(1)[0][0]
        result[name].extend(faces)
    return result

def legacy_samples(ob,key):
    paths={'DefaultSoldier':'ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/Textures/T_RSGMech_BaseColor.png',
           'WM01':'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/ReviewData/After/T_WarMachine_BaseColor.png'}
    image=bpy.data.images.load(str(R/paths[key]),check_existing=True);w,h=image.size
    pixels=np.empty(w*h*4,np.float32);image.pixels.foreach_get(pixels);pixels=pixels.reshape(h,w,4)
    uv=ob.data.uv_layers.get('UVmap_0') or ob.data.uv_layers[0]
    result=[]
    for p in ob.data.polygons:
        q=np.array([uv.data[i].uv[:] for i in p.loop_indices]).mean(0)
        x,y=np.clip((q*np.array([w,h])).astype(int),[0,0],[w-1,h-1]);result.append(pixels[y,x,:3])
    return np.array(result)

def sf_role(key,ident,desc,legacy):
    # Whole source mechanical components; explicit existing panel IDs override
    # the default fixed armor. The three-tone ramp is independent of these hues.
    if key=='CloningCenter' and ident==32:return 'Gray'
    primary={'AirBase':[141,143],'CloningCenter':[11,23,12,24,30,31,33,34],
             'CommandCenter':[3,36,38,57,64], 'MilitaryFactory':[0,27,28,29,36,67,68,69,70],
             'Reactor':[10,24,38], 'StrategyCenter':[8,9,10,11]}
    secondary={'AirBase':[145,9,66,165,166],'CloningCenter':[0,1,8,9,32],
               'CommandCenter':[1,4,0,60,61,67,68],'MilitaryFactory':[35,37,30],
               'Reactor':[49,11,25,39],'StrategyCenter':[173,51,54]}
    if ident in primary[key]:return 'TeamLight'
    if ident in secondary[key]:return 'TeamDark'
    cream={'AirBase':[], 'CloningCenter':[17,48],
           'CommandCenter':[49,50,51,52,53,54,85,86,87,96,97,98,100,111,116,117,120,121,122,123],
           'MilitaryFactory':[], 'Reactor':[5,13,20,27,34,41,48],
           'StrategyCenter':[56,57,144,147,153,156,1,2,4,0,3]}
    if ident in cream[key]:return 'Cream'
    if key=='CommandCenter' and ident in (19,92):return 'Gray'
    if key=='MilitaryFactory' and ident==71:return 'Gray'
    if key=='CloningCenter' and ident==32:return 'Gray'
    if key=='StrategyCenter' and ident in (55,6):return 'Gray'
    if key=='AirBase' and ident in (79,80,144,153,161,140):return 'Gray'
    if key=='AirBase' and ident in (5,51,53,58,44,142,101):return 'Sand'
    if key=='Reactor' and desc['c'][2]>3.8:return 'Gray'
    if legacy=='Frame':return 'Gray'
    if legacy=='Accent':return 'Cream'
    if legacy=='Equipment':return 'Gray'
    return 'Sand'

def roles_for(ob,key,helper_groups=None):
    me=ob.data;roles=['Gray']*len(me.polygons);groups={};desc_records=[]
    if key.startswith('SSF_'):
        k=key[4:];groups=defaultdict(list)
        for p in me.polygons:groups[me.attributes['SSF_ComponentID'].data[p.index].value].append(p.index)
        refs=json.loads((R/'ArtSource/Buildings/SSFStyle_20261005/References_A_v7/render_manifest.json').read_text(encoding='utf8'))
        theme=next(a['theme'] for a in refs['assets'] if a['key']==k)
        anchors=np.array([linear(theme[r]) for r in ('Primary','Equipment','Accent','Frame')])
        attr=me.color_attributes['SSF_PaletteLinear']
        for ident,faces in groups.items():
            d=describe(ob,faces);old=np.array(attr.data[me.polygons[faces[0]].loop_start].color[:3]);legacy=('Primary','Equipment','Accent','Frame')[((anchors-old)**2).sum(1).argmin()]
            role=sf_role(k,ident,d,legacy);set_role(roles,faces,role)
            desc_records.append({'source_component':ident,'legacy_role':legacy,'role':role,'center':d['c'].tolist(),'dimensions':d['d'].tolist(),'faces':len(faces)})
        if k=='AirBase':
            for ident in (3,5,51,56,53,58,44,48):
                for f in groups.get(ident,[]):
                    q=ob.matrix_world@me.polygons[f].center
                    roles[f]='TeamLight' if q.x*q.y>0 and abs(q.x)>.15 else 'Sand'
            for p in me.polygons:
                q=ob.matrix_world@p.center
                if q.z>5.7 and abs(q.x)<.8 and abs(q.y)<1.4 and p.normal.z>.4:roles[p.index]='TeamDark'
        if k=='CommandCenter':
            for f in groups.get(92,[]):
                if (ob.matrix_world@me.polygons[f].center).z>9.7:roles[f]='Sand'
            for ident in (57,64):
                for f in groups.get(ident,[]):
                    if (ob.matrix_world@me.polygons[f].center).z<.7:roles[f]='Cream'
        if k=='CloningCenter':
            for ident in (10,48):
                for f in groups.get(ident,[]):
                    q=ob.matrix_world@me.polygons[f].center
                    roles[f]='Cream' if q.z>7.6 else 'Sand'
            for p in me.polygons:
                q=ob.matrix_world@p.center
                if q.z>8.7 and abs(q.x)<.8 and abs(q.y-.6)<.9:roles[p.index]='TeamDark'
        if k=='MilitaryFactory':
            # Existing entrance polygons, not new cut lines or geometry.
            for p in me.polygons:
                q=ob.matrix_world@p.center
                if abs(q.x)<2.05 and q.y<-3.7 and .25<q.z<4.8 and abs(p.normal.y)>.3:roles[p.index]='Cream'
        if k=='Reactor':
            for ident in (10,24,38):
                for f in groups.get(ident,[]):
                    q=ob.matrix_world@me.polygons[f].center
                    if q.z<1.55:roles[f]='Gray'
                    elif abs(q.x)<.45 and abs(q.y)>1.15:roles[f]='Sand'
            for ident in (7,21,35):set_role(roles,groups.get(ident,[]),'Cream')
        if k=='StrategyCenter':
            for ident,faces in groups.items():
                d=describe(ob,faces)
                if d['c'][2]>7.7 and max(d['d'][:2])<.65:set_role(roles,faces,'Gray')
        # Original translucent logos are display sections. Only their colors
        # change; the original alpha and display geometry are retained.
        return roles,desc_records
    if helper_groups:
        groups=helper_groups
        for name,faces in groups.items():
            d=describe(ob,faces);n=name.lower();role='Sand'
            if key=='ManualOutpost':
                index=int(re.search(r'GS_OP_(\d+)',name).group(1))
                role='TeamLight' if index==1 else 'TeamDark' if index in (13,14) else 'Cream' if index in (5,15,16,17,18,19,20) else 'Gray' if index==21 else 'Sand'
            elif key=='ShieldGenerator':
                if any(t in n for t in ('dark','insert','seam','groove','slot','rib','vent')):role='Gray'
                elif any(t in n for t in ('emitter lens','emitter glass','emissive','indicator','light lens')):role='TeamLightLamp'
                elif any(t in n for t in ('teal deck','teal','upper inspection','upper service','upper access')):role='TeamLight'
                elif any(t in n for t in ('foundation','lower foot','lower service','cap rim','ivory')):role='Cream'
                if 'foundation teal deck' in n:role='TeamDark'
                if 'crown teal casting' in n:role='Cream'
                if 'crown shield emitter block' in n:role='Cream'
                if 'crown shield emitter top' in n:role='TeamLightLamp'
                if 'crown emitter short status bar' in n:role='TeamDark'
                if 'upper end face armor plate' in n or 'upper side plain panel' in n:role='TeamLight'
                if 'upper planar armor body' in n:role='TeamLight'
                if any(t in n for t in ('upper end face armor plate','upper end face access inset','upper side plain panel')):role='TeamDark'
                if 'inner structural core' in n or 'pillar base top ring' in n:role='Gray'
                if 'crown continuous narrow rim' in n:role='Cream'
                if 'upper red identification stripe' in n or 'straight lower teal stiffener' in n:role='TeamDark'
            elif key=='ResourceFactory':
                if any(t in n for t in ('vent','bolt','cable','rib','recess','channel','cross tie','stiffener','slot','rail','hinge','rigid core','structural roof','wheel','pipe','fan','louver','groove','designation','legend')):role='Gray'
                elif any(t in n for t in ('service light','running light')):role='Amber'
                elif any(t in n for t in ('central roof','upper roof hatch','forward armor','shoulder armor cassette','nose armor')):role='TeamLight'
                elif 'cabinet' in n:role='TeamDark' if any(t in n for t in ('fascia','band','top','cover')) else 'Sand'
                elif 'equipment lid' in n:role='TeamDark'
                elif 'equipment fascia' in n:role='Sand'
                elif 'equipment access inset' in n:role='Sand'
                elif 'status console' in n:role='TeamDark'
                elif 'green readout' in n:role='Cyan'
                elif any(t in n for t in ('roof armored tile','door layered','door inset face','door interior','front lintel','armor spine')):role='Cream'
                elif any(t in n for t in ('door upper service','hazard stripe')):role='TeamDark'
                if 'armor spine' in n:role='TeamLight'
            set_role(roles,faces,role)
            desc_records.append({'source_component':name,'role':role,'center':d['c'].tolist(),'dimensions':d['d'].tolist(),'faces':len(faces)})
        return roles,desc_records
    groups=connected(me)
    for ident,faces in groups.items():
        d=describe(ob,faces);x,y,z=d['c'];dx,dy,dz=d['d'];role='Gray'
        if key=='DefaultSoldier':
            if z>1.65 and x<.6 and dx>.7 and dy>.45 and dz>.25:role='Sand'
            if z>1.6 and abs(y)>.4 and .2<x<1.8 and dx>.25 and dy>.3 and dz>.25:role='TeamLight'
            if .35<z<1.7 and max(abs(x-.27),abs(y))>1.1 and dz>.45 and d['area']>200:role='Cream'
            if .35<z<1.7 and max(abs(x-.27),abs(y))>1.1 and dz>.45 and dx<.6 and dy<.6:role='TeamDark'
            if x>1.5 and z>1.5 and dx>.5 and dy>.7:role='TeamDark'
        elif key=='WM01':
            if 10<z<30 and abs(y)<9 and dx>8 and dy>5:role='Sand'
            if 15<z<29 and 9<abs(y)<20 and dx>5 and dy>2:role='TeamLight'
            if z>30 and abs(y)>5 and dx>6 and dz>8:role='TeamLight'
            if z<5 and dx>12 and dy>12:
                role='Sand'
                # Paint the existing disc skirt faces; its top remains sand.
                for f in faces:roles[f]='TeamDark' if me.polygons[f].normal.z<.45 else 'Sand'
                desc_records.append({'source_component':ident,'role':'disc sand top / team-dark skirt','center':d['c'].tolist(),'dimensions':d['d'].tolist(),'faces':len(faces)});continue
            if x>8 and 10<z<22 and dy>5 and dz>4:role='Cream'
            if z>31 and dz<2:role='Cream'
        elif key=='BiZhiMao':
            attr=me.color_attributes.get('GuLi_PaletteLinear');old=np.array(attr.data[me.polygons[faces[0]].loop_start].color[:3])
            # Preserve the existing gray joints, barrels and mechanism partition.
            if old.max()<.07:role='Gray'
            else:
                role='Sand'
                if max(abs(x),abs(y))>3 and z<6.7:role='Cream' if z>1.1 else 'TeamDark'
                if z>7.0 and dy>2.5:role='TeamLight' if dx<1.6 else 'Sand'
                if old[0]>old[1]*1.8:role='TeamLight' if y>-13 and z>6.7 else 'TeamDark'
                if z>8 and y<-14:role='TeamDark'
                if abs(x)>2.2 and 5.5<z<8:role='TeamLight'
        elif key=='MissileTurret':
            role='Sand'
            if abs(x)>6 and z>16 and dz>5:role='TeamLight'
            if abs(x)>6 and z>26 and dx<4:role='TeamDark'
            if 3.2<z<5.5 and dx>12:role='TeamDark'
            if z>13 and abs(x)<6:role='Sand' if dz>4 else 'Gray'
            mat=me.materials[me.polygons[faces[0]].material_index].name.lower()
            if any(t in mat for t in ('wire','bars','pipe','vent','electronic','blackmetal','aroundmissile','lensaround','solar')):role='Gray'
            if mat in ('lens','lens2','lens3'):role='Amber'
            if z>12 and y<-7 and dz<4:role='Gray'
            if ident==5:role='Sand'
            if ident in (37,109,110):role='TeamDark'
        elif key=='SentryTurret':
            role={0:'TeamLight',1:'TeamLight',2:'Cream',3:'Cream',6:'Sand',8:'Gray',10:'Gray',4:'Gray',5:'Gray',7:'Gray',9:'Gray',11:'Gray',12:'Gray'}.get(ident,'Gray')
            if ident==2:
                for f in faces:
                    p=me.polygons[f];c=ob.matrix_world@p.center
                    roles[f]='TeamLight' if abs(p.normal.x)>.5 else 'TeamDark' if c.y>9 else 'Cream'
                desc_records.append({'source_component':ident,'role':'side armor team-light / rear cover team-dark / front fixed cream','center':d['c'].tolist(),'dimensions':d['d'].tolist(),'faces':len(faces)});continue
            if ident==6:
                for f in faces:
                    p=me.polygons[f];c=ob.matrix_world@p.center
                    roles[f]='TeamDark' if c.z>3 and abs(p.normal.z)<.6 else 'Sand'
                desc_records.append({'source_component':ident,'role':'fixed sand base / existing upper turntable band team-dark','center':d['c'].tolist(),'dimensions':d['d'].tolist(),'faces':len(faces)});continue
        set_role(roles,faces,role)
        desc_records.append({'source_component':ident,'role':role,'center':d['c'].tolist(),'dimensions':d['d'].tolist(),'faces':len(faces)})
    # The original baked outline surfaces remain dark and keep their slot indices.
    for p in me.polygons:
        if 'contour' in me.materials[p.material_index].name.lower():roles[p.index]='Gray'
        if key=='BiZhiMao' and roles[p.index]=='TeamLight':
            q=ob.matrix_world@p.center
            if q.y<-3 and q.z>7 and p.normal.z>.6:roles[p.index]='Sand'
    if key in ('DefaultSoldier','WM01'):
        samples=legacy_samples(ob,key)
        if key=='DefaultSoldier':
            semantic=json.loads((O/'Reports/original-semantics.json').read_text(encoding='utf8'))
            entries=[m for m in semantic[key]['materials'] if m['name'].startswith('Reference_')]
            lin=np.array([m['diffuse'][:3] for m in entries]);srgb=np.where(lin<=.0031308,lin*12.92,1.055*np.power(lin,1/2.4)-.055)
            def fit(p):return np.min(((samples[:,None,:]-p[None,:,:])**2).sum(2),1).mean()
            anchors=lin if fit(lin)<fit(srgb) else srgb
            owners=((samples[:,None,:]-anchors[None,:,:])**2).sum(2).argmin(1)
            for p in me.polygons:
                if 'contour' in me.materials[p.material_index].name.lower():continue
                n=entries[owners[p.index]]['name'];c=ob.matrix_world@p.center
                if n in ('Reference_Chassis','Reference_Steel','Reference_Ink'):roles[p.index]='Gray'
                elif n=='Reference_WarmWhite':roles[p.index]='Sand' if c.z>1.7 else 'Cream'
                elif n in ('Reference_Coral','Reference_SkyBlue'):roles[p.index]='TeamLight' if c.z>2.0 else 'TeamDark'
                elif n=='Reference_Amber':roles[p.index]='Amber'
                else:roles[p.index]='Sand'
            for ident,faces in groups.items():
                d=describe(ob,faces);x,y,z=d['c'];dx,dy,dz=d['d']
                if x<-.9 and z>2.3 and dy>1.8 and dx>1 and dz<.8:set_role(roles,faces,'TeamDark')
                if abs(y)<.1 and -.9<x<0 and z>2.2 and .8<dx<2.5 and .6<dy<1.1:set_role(roles,faces,'Sand')
                if abs(y)<.1 and .5<x<1.7 and z>1.9 and .7<dx<1.5 and .8<dy<1.3:set_role(roles,faces,'Sand')
        else:
            for p in me.polygons:
                if 'contour' in me.materials[p.material_index].name.lower():continue
                rgb=samples[p.index];c=ob.matrix_world@p.center
                # Original dark texture regions are gun barrels and mechanism
                # faces even when they share a connected armor component.
                if rgb.max()<.35:roles[p.index]='Gray'
                if c.z<6:
                    rad=min(math.hypot(c.x-x,c.y-y) for x,y in ((14.67,16.31),(14.67,-16.31),(-17.54,19.02),(-17.54,-19.02)))
                    if rad>7 and abs(p.normal.z)<.65:roles[p.index]='TeamDark'
                    elif 4.5<rad<9.4 and p.normal.z>.65:roles[p.index]='Sand'
            for ident,faces in groups.items():
                d=describe(ob,faces);x,y,z=d['c'];dx,dy,dz=d['d']
                if z>30 and max(dx,dy,dz)<4:set_role(roles,faces,'Gray')
                if x>18 and dx>3 and dy<3 and dz<3:set_role(roles,faces,'Gray')
    return roles,desc_records

def paint(ob,roles,team):
    me=ob.data;attr=me.color_attributes.get('SSF_PaletteLinear') or me.color_attributes.get('GuLi_PaletteLinear') or me.color_attributes.get('Bv1_PaintLinear')
    if attr is None:attr=me.color_attributes.new(name='Bv1_PaintLinear',type='FLOAT_COLOR',domain='CORNER')
    arr=np.zeros((len(me.loops),4),np.float32);em=np.zeros_like(arr);em[:,3]=1
    region=me.attributes.get('Bv1_ColorRegion') or me.attributes.new(name='Bv1_ColorRegion',type='INT',domain='FACE')
    area=Counter()
    for p in me.polygons:
        r=roles[p.index];arr[list(p.loop_indices)]=color(r,team);region.data[p.index].value=ROLES.index(r);area[r]+=p.area
        if r in ('TeamLightLamp','Amber','Cyan'):em[list(p.loop_indices)]=color(r,team)
    attr.data.foreach_set('color',arr.ravel())
    lamp=me.color_attributes.new(name='Bv1_LampLinear',type='FLOAT_COLOR',domain='CORNER');lamp.data.foreach_set('color',em.ravel())
    me.update();return attr.name,{r:round(a/sum(area.values()),5) for r,a in area.items()}

def material(src,attr,team,key):
    m=src.copy();m.name=f'Bv1_{team}_{key}_{src.name}';m.use_nodes=True;nt=m.node_tree;ns=nt.nodes;ls=nt.links
    m['color_only_candidate']=True;m['tone_factors']=[.4,.72,1.];m['tone_thresholds']=[.38,.68];m['fixed_art_light']=list(LIGHT)
    if 'outline' in src.name.lower() or 'contour' in src.name.lower():
        ns.clear();out=ns.new('ShaderNodeOutputMaterial');e=ns.new('ShaderNodeEmission');e.inputs[0].default_value=color('Gray',team)
        transparent=ns.new('ShaderNodeBsdfTransparent');g=ns.new('ShaderNodeNewGeometry');mix=ns.new('ShaderNodeMixShader')
        ls.new(g.outputs['Backfacing'],mix.inputs[0]);ls.new(e.outputs[0],mix.inputs[1]);ls.new(transparent.outputs[0],mix.inputs[2]);ls.new(mix.outputs[0],out.inputs['Surface'])
        m.diffuse_color=color('Gray',team);m.use_backface_culling=True;return m
    # Existing SSF / BiZhiMao structural line masks and widths stay intact.
    vc=next((n for n in ns if n.type=='VERTEX_COLOR' and n.layer_name in ('SSF_PaletteLinear','GuLi_PaletteLinear')),None)
    if vc:
        vc.layer_name=attr
        for n in ns:
            if n.type=='VALTORGB' and len(n.color_ramp.elements)==3:
                n.color_ramp.interpolation='CONSTANT'
                for e,pos,tone in zip(n.color_ramp.elements,(0,.38,.68),(.4,.72,1.)):e.position=pos;e.color=(tone,tone,tone,1)
            if n.type=='VECT_MATH' and n.operation=='DISTANCE':n.inputs[1].default_value=(-10,-10,-10)
            if n.name=='SourcePanelLineStrength' and n.type=='MATH':n.inputs[1].default_value=.12
        em=next((n for n in ns if n.type=='EMISSION'),None)
        if em and em.inputs['Color'].is_linked:
            ink=em.inputs['Color'].links[0].from_node
            if ink.type=='MIX_RGB' and not ink.inputs[2].is_linked:ink.inputs[2].default_value=color('Gray',team)
        m.diffuse_color=color('TeamLight',team);return m
    # Uniform fixed-light three-tone paint. Source nodes remain archived in the
    # candidate material; the geometry, UVs and original normal data do not change.
    for n in ns:n.location.x-=1000
    out=next(n for n in ns if n.type=='OUTPUT_MATERIAL')
    for l in list(out.inputs['Surface'].links):ls.remove(l)
    v=ns.new('ShaderNodeVertexColor');v.layer_name=attr;v.label='固定色 / 两档队色分区';v.location=(-700,300)
    geom=ns.new('ShaderNodeNewGeometry');geom.location=(-700,0)
    dot=ns.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=LIGHT;dot.location=(-480,0);ls.new(geom.outputs['Normal'],dot.inputs[0])
    ramp=ns.new('ShaderNodeValToRGB');ramp.name='Bv1_ThreeTone';ramp.label='三档明暗 0.40 / 0.72 / 1.00';ramp.location=(-260,0);ramp.color_ramp.interpolation='CONSTANT'
    ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1]);ramp.color_ramp.elements[0].color=(.4,.4,.4,1)
    for pos,tone in ((.38,.72),(.68,1.)):
        e=ramp.color_ramp.elements.new(pos);e.color=(tone,tone,tone,1)
    ls.new(dot.outputs['Value'],ramp.inputs[0])
    mul=ns.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1;mul.location=(0,200);ls.new(v.outputs[0],mul.inputs[1]);ls.new(ramp.outputs[0],mul.inputs[2])
    lamp=ns.new('ShaderNodeVertexColor');lamp.layer_name='Bv1_LampLinear';lamp.location=(-250,-230)
    add=ns.new('ShaderNodeMixRGB');add.blend_type='ADD';add.inputs[0].default_value=.15;add.location=(230,200);ls.new(mul.outputs[0],add.inputs[1]);ls.new(lamp.outputs[0],add.inputs[2])
    em=ns.new('ShaderNodeEmission');em.location=(450,200);ls.new(add.outputs[0],em.inputs[0]);ls.new(em.outputs[0],out.inputs['Surface']);out.location=(650,200)
    m.diffuse_color=color('TeamLight',team);return m

def scene_settings(s):
    s.render.engine='CYCLES';s.cycles.samples=8;s.cycles.use_denoising=False;s.cycles.max_bounces=1
    s.render.resolution_x=1100;s.render.resolution_y=1000;s.render.resolution_percentage=100
    s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA';s.render.film_transparent=False
    s.world=bpy.data.worlds.new(s.name+'_Studio');s.world.color=(.62,.64,.66);s.world.use_nodes=True
    s.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.62,.64,.66,1)
    s.view_settings.view_transform='Standard';s.view_settings.look='None';s.view_settings.exposure=0;s.view_settings.gamma=1
    s.unit_settings.system='METRIC';s.unit_settings.scale_length=1;s.render.fps=30;s.frame_set(1)
    # Freestyle draws on the existing surfaces. It never adds outline shells,
    # displaces vertices, triangulates meshes, or edits any source topology.
    s.render.use_freestyle=True;settings=s.view_layers[0].freestyle_settings;settings.crease_angle=math.radians(110)
    lines=settings.linesets[0] if settings.linesets else settings.linesets.new('主要轮廓与结构线')
    if lines.linestyle is None:lines.linestyle=bpy.data.linestyles.new('Bv1_主要轮廓与结构线')
    lines.select_silhouette=True;lines.select_border=True;lines.select_crease=True;lines.select_edge_mark=False
    lines.linestyle.color=linear('#2C3735');lines.linestyle.thickness=1.0

def fit(s,obs,direction):
    bpy.context.window.scene=s;bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
    pts=[o.evaluated_get(dg).matrix_world@Vector(c) for o in obs if o.type=='MESH' and not o.hide_render for c in o.evaluated_get(dg).bound_box]
    lo=Vector([min(p[i] for p in pts) for i in range(3)]);hi=Vector([max(p[i] for p in pts) for i in range(3)]);center=(lo+hi)/2
    camera=bpy.data.objects.new(s.name+'_Camera',bpy.data.cameras.new(s.name+'_Camera'));s.collection.objects.link(camera);s.camera=camera
    camera.data.type='ORTHO';camera.location=center+Vector(direction).normalized()*max(hi-lo)*4;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
    inv=camera.rotation_euler.to_matrix().transposed();relative=[inv@(p-center) for p in pts];width=max(p.x for p in relative)-min(p.x for p in relative);height=max(p.y for p in relative)-min(p.y for p in relative)
    camera.data.ortho_scale=max(height,width/(s.render.resolution_x/s.render.resolution_y))*1.20;camera.data.clip_end=max(hi-lo)*20
    return {'lo':list(lo),'hi':list(hi),'center':list(center),'ortho':camera.data.ortho_scale}

def main(key):
    ssf=key.startswith('SSF_');short=key[4:] if ssf else key
    source=R/'ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/SSF_Production_B_v1.blend' if ssf else Path(INS[key]['path']) if key in INS else O/'References'/(key+'_Original.blend')
    bpy.ops.wm.open_mainfile(filepath=str(source),load_ui=False)
    bpy.context.scene.frame_set(1);bpy.context.view_layer.update()
    if ssf:names=[short+f'_LOD{i}_Body' for i in range(3)]+[short+f'_LOD{i}_Outline' for i in (0,1)]
    else:names={'DefaultSoldier':[f'SM_Pioneer_VAT_LOD{i}' for i in range(3)],'WM01':[f'SM_WarMachine_Rigid_LOD{i}' for i in range(3)],'BiZhiMao':['ControlRigMech_LOD%d_Body'%i for i in range(3)]+['ControlRigMech_B_v4_LOD%d_Outline'%i for i in (0,1)],'ShieldGenerator':['SM_ShieldGenerator'],'ManualOutpost':['SM_OutpostMonument_v01'],'ResourceFactory':['SM_RPF_Body','SK_RPF_Door'],'MissileTurret':['missile_turret'],'SentryTurret':['Stylized_Turrets_A_a']}[key]
    originals=[bpy.data.objects[n] for n in names if n in bpy.data.objects]
    assert originals and len(originals)==len(names),(key,names)
    palette_cache={};region_records={}
    for ob in originals:
        if 'Outline' in ob.name:continue
        helpers=None
        if key=='ManualOutpost':helpers=[o for o in bpy.data.objects if o.type=='MESH' and o.name.startswith('GS_OP_')]
        if key=='ResourceFactory':helpers=list(bpy.data.collections['GS_RPF_02_EditableDoor' if ob.name=='SK_RPF_Door' else 'GS_RPF_01_EditableBody'].objects)
        if key=='ShieldGenerator':
            shield_parts=R/'ArtSource/Buildings/IndustrialDefenseSet/delivery_hardsurface/Construction/ShieldGenerator_EditableParts.blend'
            with bpy.data.libraries.load(str(shield_parts),link=False) as (src,dst):dst.objects=list(src.objects)
            helpers=[o for o in dst.objects if o and o.type=='MESH']
            # Helper surfaces are read-only references and are not delivered as
            # substituted geometry; the original merged mesh stays exact.
            c=bpy.data.collections.new('Bv1_READONLY_HELPERS');bpy.context.scene.collection.children.link(c)
            for h in helpers:c.objects.link(h)
        groups=nearest_parts(ob,helpers) if helpers else None
        if groups and key in ('ManualOutpost','ShieldGenerator'):groups=whole_component_names(ob,groups)
        palette_cache[ob.name],region_records[ob.name]=roles_for(ob,key,groups)
    roots=set(originals)
    for o in originals:
        parent=o.parent
        while parent:roots.add(parent);parent=parent.parent
        for m in o.modifiers:
            if m.type=='ARMATURE' and m.object:roots.add(m.object)
    root_names=[o.name for o in roots];before={o.name:signature(o) for o in originals};bone_before={o.name:bonesig(o) for o in roots if o.type=='ARMATURE'}
    actions_before={a.name:action_sig(a) for a in bpy.data.actions}
    # Load just the actual render LODs and their unchanged parents into a clean
    # new review file; dependencies (rigs, clips, masks) are appended by Blender.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    with bpy.data.libraries.load(str(source),link=False) as (src,dst):dst.objects=list(root_names);dst.actions=list(actions_before)
    loaded={o.name:o for o in dst.objects if o}
    for n,h in before.items():assert signature(loaded[n])==h,(key,n,'append changed geometry')
    report={'key':key,'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'B_approval':'pending','scope':'palette and authorized consistent cel/three-tone/line rendering only','topology_modified':False,'vertices_modified':False,'UVs_modified':False,'rig_animation_LODs_modified':False,'UE_modified':False,'teams':[],'original_animation_signatures':actions_before}
    for team in ('Blue','Red'):
        s=bpy.data.scenes.new(f'{team}_{key}');scene_settings(s);bpy.context.window.scene=s
        c=bpy.data.collections.new(f'Bv1_{team}_{key}_LOD0');s.collection.children.link(c)
        copies={}
        for n in root_names:
            src=loaded[n];o=src.copy()
            if src.data:o.data=src.data.copy()
            o.name=f'{team}_{key}_{n}';o['original_object_name']=n;c.objects.link(o);copies[n]=o
        for n,o in copies.items():
            src=loaded[n]
            if src.parent:o.parent=copies[src.parent.name]
            o.matrix_basis=src.matrix_basis.copy();o.matrix_parent_inverse=src.matrix_parent_inverse.copy()
            for m in o.modifiers:
                if m.type=='ARMATURE' and m.object:m.object=copies[m.object.name]
        rec={'team':team,'scene':s.name,'collection':c.name,'objects':[],'region_mapping':region_records}
        mats={}
        for n,o in copies.items():
            if o.type=='ARMATURE':assert bonesig(o)==bone_before[n];rec.setdefault('rigs',[]).append({'name':o.name,'source':n,'bones':len(o.data.bones),'signature':bone_before[n]});continue
            if o.type!='MESH':continue
            lod=int(re.search(r'LOD([012])',n).group(1)) if re.search(r'LOD([012])',n) else 0
            o.hide_render=lod!=0;o.hide_viewport=False;o.hide_set(False)
            if lod:
                c.objects.unlink(o);lc=bpy.data.collections.get(f'Bv1_{team}_{key}_LOD{lod}')
                if not lc:lc=bpy.data.collections.new(f'Bv1_{team}_{key}_LOD{lod}');s.collection.children.link(lc)
                lc.objects.link(o);o.hide_render=True
            if n in palette_cache:
                attr,proportion=paint(o,palette_cache[n],team)
                for i,mat in enumerate(list(o.data.materials)):
                    if mat.name not in mats:mats[mat.name]=material(mat,attr,team,key)
                    o.data.materials[i]=mats[mat.name]
            else:
                proportion={}
                for i,mat in enumerate(list(o.data.materials)):
                    if mat.name not in mats:mats[mat.name]=material(mat,'Bv1_PaintLinear',team,key)
                    o.data.materials[i]=mats[mat.name]
            assert signature(o)==before[n],(key,n,'geometry changed')
            rec['objects'].append({'name':o.name,'source':n,'LOD':lod,'signature':before[n],'color_area_fractions':proportion,'vertices':len(o.data.vertices),'polygons':len(o.data.polygons),'UVs':[uv.name for uv in o.data.uv_layers]})
        direction=(-1,-1,.85) if key=='BiZhiMao' else (1,-1,.85)
        if key in ('SentryTurret','MissileTurret'):direction=(1,-1,.9)
        rec['view']=fit(s,list(c.objects),direction);s['stage']='B review pending';s['key']=key;s['team']=team
        for lc in s.collection.children:
            if lc!=c:
                layer=s.view_layers[0].layer_collection.children.get(lc.name)
                if layer:layer.exclude=True
        report['teams'].append(rec)
    for name,h in actions_before.items():
        assert action_sig(bpy.data.actions[name])==h,(key,name,'animation changed')
        bpy.data.actions[name].use_fake_user=True
    bpy.context.window.scene=bpy.data.scenes['Blue_'+key]
    for s in list(bpy.data.scenes):
        if s.name not in ('Blue_'+key,'Red_'+key):bpy.data.scenes.remove(s)
    for o in list(loaded.values()):bpy.data.objects.remove(o,do_unlink=True)
    text=bpy.data.texts.new('READ_ME_只改配色_B待审核');text.write('仅修改配色与授权的三渲二、三档明暗、适量渲染线稿。\n原几何、UV、法线、骨架、动画、LOD 完全保留。\nBlue / Red 场景分别审核；LOD1/2 仅在独立集合内隐藏。\n本文件为 B 候选，未更新 UE 正式资源。\n')
    try:bpy.ops.file.pack_all()
    except RuntimeError:pass
    bpy.ops.wm.save_as_mainfile(filepath=str(O/'Models'/(key+'_PaletteOnly_B_v1.blend')))
    (O/'Reports'/(key+'_production.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('PALETTE_B_READY',key,'Blue/Red','GEOMETRY_AND_ANIMATION_IDENTICAL',flush=True)

if __name__=='__main__':
    for key in SELECTED or ALL_KEYS:main(key)

import bpy,json,numpy as np
from pathlib import Path
from collections import defaultdict,Counter
R=Path('D:/UE5.7/test1');O=R/'ArtSource/LocalTeamColorProduction_B_v1_20261008'
sources=json.loads((O/'Reports/source-inspection.json').read_text(encoding='utf8'))
report={}
def component_faces(me):
    parent=list(range(len(me.vertices)))
    def root(a):
        while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
        return a
    for e in me.edges:
        a,b=map(root,e.vertices)
        if a!=b:parent[b]=a
    groups=defaultdict(list)
    for p in me.polygons:groups[root(p.vertices[0])].append(p.index)
    return list(groups.values())
def rows(ob,groups):
    me=ob.data;result=[]
    attr=me.color_attributes.get('SSF_PaletteLinear') or me.color_attributes.get('GuLi_PaletteLinear')
    for ident,faces in groups.items():
        ps=[me.polygons[i] for i in faces];ids=list({v for p in ps for v in p.vertices})
        co=np.array([ob.matrix_world@me.vertices[i].co for i in ids]);lo=co.min(0);hi=co.max(0)
        colors=Counter()
        if attr:
            for p in ps:
                c=tuple(round(x,3) for x in attr.data[p.loop_start].color[:3]);colors[c]+=p.area
        result.append({'id':ident,'faces':len(faces),'area':round(sum(p.area for p in ps),3),'lo':lo.round(3).tolist(),'hi':hi.round(3).tolist(),'center':((lo+hi)/2).round(3).tolist(),'dims':(hi-lo).round(3).tolist(),'colors':[(list(c),round(a,2)) for c,a in colors.most_common(5)]})
    return sorted(result,key=lambda x:-x['area'])
for key in ('SSF','DefaultSoldier','WM01','BiZhiMao','ShieldGenerator','ManualOutpost','ResourceFactory'):
    bpy.ops.wm.open_mainfile(filepath=sources[key]['path'],load_ui=False)
    if key=='SSF':
        for name in ['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']:
            ob=bpy.data.objects['Blue_'+name+'_LOD0_Body'];g=defaultdict(list)
            for p in ob.data.polygons:g[ob.data.attributes['SSF_ComponentID'].data[p.index].value].append(p.index)
            report[name]=rows(ob,g)
    elif key in ('ManualOutpost','ResourceFactory'):
        prefix='GS_OP_' if key=='ManualOutpost' else 'RPF_'
        report[key]=[{'name':ob.name,'dims':[round(x,3) for x in ob.dimensions], 'center':[round(x,3) for x in ob.matrix_world.translation], 'materials':[m.name if m else None for m in ob.data.materials]} for ob in bpy.data.objects if ob.type=='MESH' and (ob.name.startswith(prefix) or ob.name.startswith(('SM_RPF','SK_RPF')))]
    else:
        name={'DefaultSoldier':'SM_Pioneer_VAT_LOD0','WM01':'SM_WarMachine_Rigid_LOD0','BiZhiMao':'ControlRigMech_LOD0_Body','ShieldGenerator':'SM_ShieldGenerator'}[key]
        ob=bpy.data.objects[name];g=component_faces(ob.data)
        report[key]=rows(ob,dict(enumerate(g)))
    print('REGIONS',key,flush=True)
for key in ('MissileTurret','SentryTurret'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(O/'References'/(key+'_Original_RenderLODs.fbx')))
    obs=[o for o in bpy.data.objects if o.type=='MESH']
    report[key]={'objects':[]}
    for ob in obs:
        g=component_faces(ob.data)
        report[key]['objects'].append({'name':ob.name,'dims':list(ob.dimensions),'vertices':len(ob.data.vertices),'polygons':len(ob.data.polygons),'materials':[m.name if m else None for m in ob.data.materials],'parts':rows(ob,dict(enumerate(g)))})
    bpy.ops.wm.save_as_mainfile(filepath=str(O/'References'/(key+'_Original.blend')))
(O/'Reports/part-regions.json').write_text(json.dumps(report,indent=2),encoding='utf8')

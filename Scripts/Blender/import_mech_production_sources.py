import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v1'
snapshot=json.loads((ROOT/'Source/Production_v1/source_snapshot.json').read_text(encoding='utf-8'))
assert snapshot['success'],snapshot['errors']
scene=bpy.data.scenes['Mechs_Production_v1'];bpy.context.window.scene=scene
source=bpy.data.collections['00_SOURCE_READONLY'];source.hide_viewport=False
working=bpy.data.collections['10_WORKING_MODELS']
report={'objects':{},'materials':{},'assemblies':snapshot['assemblies'],'engine_basis':'UE source FBX; actual imported mesh and skeleton; not generated substitute'}
def bounds(obs):
    pts=[o.matrix_world@Vector(v) for o in obs if o.type=='MESH' for v in o.bound_box]
    return {'min':[min(p[i] for p in pts) for i in range(3)],'max':[max(p[i] for p in pts) for i in range(3)]}
for key,data in snapshot['meshes'].items():
    sc=bpy.data.collections.new('SRC_'+key);source.children.link(sc)
    wc=bpy.data.collections.new('WORK_'+key);working.children.link(wc)
    before=set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT/data['fbx']['path']),use_anim=False,ignore_leaf_bones=False,automatic_bone_orientation=False)
    obs=[o for o in bpy.data.objects if o not in before]
    mapping={}
    for o in obs:
        original=o.name;o.name='SRC_'+key+'__'+original
        for c in list(o.users_collection):c.objects.unlink(o)
        sc.objects.link(o)
        o['source_asset']=data['path'];o['source_key']=key
        n=o.copy()
        if o.data:n.data=o.data.copy()
        n.name='WORK_'+key+'__'+original
        wc.objects.link(n);mapping[o]=n
    for o,n in mapping.items():
        if o.parent in mapping:n.parent=mapping[o.parent]
        for mod in n.modifiers:
            if mod.type=='ARMATURE' and mod.object in mapping:mod.object=mapping[mod.object]
        if n.type=='MESH':
            for idx,slot in enumerate(n.material_slots):
                if slot.material:
                    mat=slot.material.copy();mat.name='WORK_'+key+'__'+mat.name
                    n.data.materials[idx]=mat
    bpy.context.view_layer.update()
    rec={'source':[o.name for o in obs],'working':[o.name for o in mapping.values()],'source_asset':data['path'],'bounds':bounds(obs),'meshes':[]}
    for o in obs:
        if o.type=='MESH':
            o.data.calc_loop_triangles()
            rec['meshes'].append({'name':o.name,'verts':len(o.data.vertices),'triangles':len(o.data.loop_triangles),'polygons':len(o.data.polygons),'uvs':[u.name for u in o.data.uv_layers],'materials':[s.material.name if s.material else None for s in o.material_slots],'groups':[g.name for g in o.vertex_groups],'matrix':[list(r) for r in o.matrix_world]})
        elif o.type=='ARMATURE':rec['bones']=[{'name':b.name,'parent':b.parent.name if b.parent else None,'head':list(b.head_local),'tail':list(b.tail_local)} for b in o.data.bones]
    report['objects'][key]=rec
    (OUT/'import_report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
source.hide_viewport=True;source.hide_render=True
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_Style_SourceBased_v1.blend'),check_existing=False)
result={'imported':{k:{'triangles':sum(m['triangles'] for m in v['meshes']),'bounds':v['bounds'],'bones':len(v.get('bones',[]))} for k,v in report['objects'].items()},'saved':bpy.data.filepath}

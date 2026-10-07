"""Editable source meshes and deterministic source/candidate geometry readback."""
import bpy,hashlib,json,sys
from pathlib import Path
from mathutils import Vector,Matrix,Euler
import math
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,unit_dir
from common import require_unapproved_candidate
require_unapproved_candidate()
data=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
before=json.loads((ART/'Reports/formal_before.json').read_text(encoding='utf8'))
report=[]

def import_fbx(path):
    old=set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path),use_custom_normals=True,use_anim=False)
    return [obj for obj in bpy.data.objects if obj not in old]

def lod_index(obj):
    current=obj
    while current:
        # UE FBX exporter uses *_LOD0, *_LOD1... nested beneath the LODGroup.
        import re
        found=re.search(r'[_ ]LOD(\d+)',current.name,re.I)
        if found:return int(found[1])
        current=current.parent
    return 0

def signature(objects):
    rows=[]
    for obj in objects:
        if obj.type!='MESH':continue
        mesh=obj.data;mesh.calc_loop_triangles()
        triangles=[]
        for triangle in mesh.loop_triangles:
            corners=[]
            for loop in triangle.loops:
                vertex=mesh.vertices[mesh.loops[loop].vertex_index]
                values=list(vertex.co)
                for layer in mesh.uv_layers:values.extend(layer.data[loop].uv)
                for attr in mesh.color_attributes:
                    if attr.domain=='CORNER':values.extend(attr.data[loop].color)
                corners.append(tuple(round(value,6) for value in values))
            triangles.append(min(tuple(corners[i:]+corners[:i]) for i in range(3)))
        rows.extend(sorted(triangles))
    return hashlib.sha256(json.dumps(sorted(rows),separators=(',',':')).encode()).hexdigest()

def count(objects):return sum(len(obj.data.loop_triangles) for obj in objects if obj.type=='MESH')

for unit in data['units']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    containers=[];mesh_rows=[];meshes={}
    for item in unit['meshes']:
        objects=import_fbx(item['fbx'])
        groups=[[obj for obj in objects if obj.type=='MESH' and lod_index(obj)==lod] for lod in range(3)]
        assert all(groups),(item['fbx'],[(o.name,o.type) for o in objects])
        for objects_in_lod in groups:
            for obj in objects_in_lod:obj.data.calc_loop_triangles()
        triangles=[count(group) for group in groups]
        native_counts=item['triangles']
        if item['type']=='SkeletalMesh':
            originals=import_fbx(unit_dir(unit['name'])/'Sources'/(Path(item['fbx']).stem.replace('_Candidate','_Original')+'.fbx'))
        else:
            source=next(row for row in before['units'] if row['name']==unit['name'])
            original=next(row for row in source['meshes'] if row['asset']==item['source'])
            originals=import_fbx(original['fbx'])
        original0=[obj for obj in originals if obj.type=='MESH' and lod_index(obj)==0]
        source_sig=signature(original0);candidate_sig=signature(groups[0])
        if source_sig!=candidate_sig:
            print('DETAILS',unit['name'],item['source'],[(len(o.data.vertices),len(o.data.polygons),len(o.data.uv_layers),len(o.data.color_attributes)) for o in original0],[(len(o.data.vertices),len(o.data.polygons),len(o.data.uv_layers),len(o.data.color_attributes)) for o in groups[0]],flush=True)
            a,b=original0[0].data,groups[0][0].data
            print('DIFF',max((v.co-w.co).length for v,w in zip(a.vertices,b.vertices)),all(list(p.vertices)==list(q.vertices) for p,q in zip(a.polygons,b.polygons)),[max((u.uv-v.uv).length for u,v in zip(x.data,y.data)) for x,y in zip(a.uv_layers,b.uv_layers)],flush=True)
        assert source_sig==candidate_sig,(unit['name'],item['source'],'LOD0 mismatch')
        for obj in originals:bpy.data.objects.remove(obj,do_unlink=True)
        key=item['source'];meshes[key]=groups
        mesh_rows.append(dict(source=key,lods=[dict(index=i,triangles=triangles[i],mesh_names=[o.name for o in group],
            uv_channels=[len(o.data.uv_layers) for o in group],materials=[len(o.data.materials) for o in group]) for i,group in enumerate(groups)],
            lod0_signature=candidate_sig,source_lod0_signature=source_sig,lod0_preserved=True,
            native_triangles=native_counts,fbx_triangles=triangles,
            export_triangle_delta=[native_counts[i]-triangles[i] for i in range(3)] if native_counts else None,
            armatures=len([o for o in objects if o.type=='ARMATURE'])))
    # Clone original component transforms into independent, editable LOD collections.
    for lod in range(3):
        collection=bpy.data.collections.new(f'{unit["name"]}_LOD{lod}')
        scene.collection.children.link(collection);containers.append(collection)
        root=bpy.data.objects.new(f'{unit["name"]}_LOD{lod}_Presentation',None);collection.objects.link(root)
        root.scale=(unit['presentation_scale'],)*3
        rigs={}
        for index,component in enumerate(unit['components'] or [dict(name=unit['name'],mesh=unit['meshes'][0]['source'],
                location=[0,0,0],rotation=[0,0,0],scale=[1,1,1])]):
            groups=meshes[component['mesh']]
            for prototype in groups[lod]:
                obj=prototype.copy();obj.data=prototype.data.copy();collection.objects.link(obj)
                obj.name=f'LOD{lod}_{component["name"]}_{index}'
                # UE FBX roundtrip converts X,Y,Z cm into Blender X,-Y,Z metres.
                location=Vector((component['location'][0]/100,-component['location'][1]/100,component['location'][2]/100))
                pitch,yaw,roll=map(math.radians,component['rotation'])
                matrix=Matrix.Translation(location)@Euler((roll,-pitch,-yaw),'XYZ').to_matrix().to_4x4()
                matrix=matrix@Matrix.Diagonal(Vector((*component['scale'],1)))
                obj.parent=root;obj.matrix_basis=matrix@prototype.matrix_world
                for modifier in obj.modifiers:
                    if modifier.type=='ARMATURE' and modifier.object:
                        original_rig=modifier.object
                        if original_rig.name not in rigs:
                            rig=original_rig.copy();rig.data=original_rig.data.copy();collection.objects.link(rig)
                            rig.parent=root;rig.matrix_basis=matrix@original_rig.matrix_world
                            rigs[original_rig.name]=rig
                        modifier.object=rigs[original_rig.name]
                obj.hide_render=lod!=0;obj.hide_set(lod!=0)
        collection['lod_index']=lod
    # Keep armatures for the original vehicle skin and hide imported prototype meshes.
    for obj in list(scene.objects):
        if not obj.name.startswith(('LOD0_','LOD1_','LOD2_')) and obj.type=='MESH':
            obj.hide_render=True;obj.hide_set(True)
    aggregate=[]
    for lod,collection in enumerate(containers):
        triangles=count([o for o in collection.objects if o.type=='MESH'])
        aggregate.append(triangles)
    scene['LODCount']=3;scene['Approval']='B pending';scene['FormalAssetsChanged']=False
    path=unit_dir(unit['name'])/(unit['name']+'_3Tier.blend')
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    row=dict(name=unit['name'],id=unit['id'],display_name=unit['display_name'],blend=str(path),
        triangles=aggregate,meshes=mesh_rows,lod0_preserved=True,approval='pending')
    (unit_dir(unit['name'])/'Reports/blender_candidate.json').write_text(json.dumps(row,ensure_ascii=False,indent=2),encoding='utf8')
    report.append(row);print('CANDIDATE',unit['name'],aggregate,flush=True)
(ART/'Reports/blender_candidates.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')

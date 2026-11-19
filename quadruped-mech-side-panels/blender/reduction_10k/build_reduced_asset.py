"""Build the 10k deliverable, preserving source objects, materials and clip timing."""
import bpy
import bmesh
import hashlib
import json
import math
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parent
SOURCE=Path(bpy.data.filepath)
OUTPUT=SOURCE.with_name('quadruped-mech-vertex-animation-10k.blend')
PLAN=json.loads((ROOT/'optimized-components.json').read_text(encoding='utf-8'))
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==PLAN['source_sha256']
SCENES=list(bpy.data.scenes)
STATIC=next(s for s in SCENES if s.name.startswith('00'))
bpy.context.window.scene=STATIC
bpy.context.view_layer.update()
PARTS={}
for p in PLAN['parts']:
    PARTS.setdefault(p['object'],[]).append(p)
ORIGINAL={o.name:o.data for o in STATIC.objects if o.name in PARTS}
REST_OBJECTS={o.name:o for o in STATIC.objects if o.name in PARTS}
OLD_MESHES=set()
REPORT={'source':str(SOURCE),'output':str(OUTPUT),'source_sha256':PLAN['source_sha256'],'components_preserved':len(PLAN['parts']),'mirrored_component_pairs':len(PLAN['symmetry_pairs']),'body_objects':len(PARTS),'scenes':{},'source_animation_formula_max_sample_error_m':0.0,'materials_unchanged':True,'visual_acceptance':'待用户验收；未渲染、未截图、未由助手读图检查。','source_file_unchanged':False,'source_surface_sample_max_error_m':max(p['selected']['max_error'] for p in PLAN['parts'])}

# Import only the source animation definitions, never its scene-creation section.
animation_path=SOURCE.parent/'create_vertex_animations.py'
animation_code=animation_path.read_text(encoding='utf-8').split('\nbase_scene = bpy.context.scene',1)[0]
ANIM={'__file__':str(animation_path),'__name__':'reduction_animation_reference'}
exec(compile(animation_code,str(animation_path),'exec'),ANIM)
REPORT['animation_source_sha256']=hashlib.sha256(animation_path.read_bytes()).hexdigest()

def barycentric(point,a,b,c):
    v0,v1,v2=b-a,c-a,point-a
    d00,d01,d11=v0.dot(v0),v0.dot(v1),v1.dot(v1)
    den=d00*d11-d01*d01
    if abs(den)<1e-20:
        return (1.,0.,0.)
    u=(d11*v2.dot(v0)-d01*v2.dot(v1))/den
    v=(d00*v2.dot(v1)-d01*v2.dot(v0))/den
    return (1.-u-v,u,v)

def classify(name,part):
    leg=name.split(' · ')[0]
    if leg not in ANIM['LEGS']:
        return None,None,None
    obj=REST_OBJECTS[name]
    points=[obj.matrix_world @ ORIGINAL[name].vertices[i].co for i in part['source_vertex_ids']]
    center=sum(points,Vector())/len(points)
    data=ANIM['LEGS'][leg]
    group=ANIM['classify_component'](center,leg)
    dimensions=[max(p[a] for p in points)-min(p[a] for p in points) for a in range(3)]
    if max(dimensions)<0.09 and center.z>1.46 and (center-data['hip']).length<0.40:
        group='hip'
    elif max(dimensions)<0.09 and (center-data['knee']).length<0.40:
        group='lower'
    lo,hi=min(p.z for p in points),max(p.z for p in points)
    span=None
    if group in {'upper','lower'} and lo<data['knee'].z-0.08 and hi>data['knee'].z+0.08:
        span='knee'
    elif group=='lower' and lo<data['ankle'].z+0.45 and hi>data['ankle'].z+0.35:
        span='ankle'
    elif group=='upper' and lo<data['hip'].z-0.05 and hi>data['hip'].z-0.30:
        span='hip'
    return leg,group,span

def transforms(clip,frame,leg):
    if clip=='Walk':
        body,_,phase=ANIM['walk_pose'](frame)
    else:
        body,_,phase,_=ANIM['mining_pose'](frame)
    if leg is None:
        return {'body':body}
    data=ANIM['LEGS'][leg]
    hip=body @ data['hip']
    if clip=='Walk':
        ankle,alignment=ANIM['walk_foot'](leg,phase)
    else:
        ankle,alignment=data['ankle'].copy(),0.0
    knee=ANIM['solve_leg'](leg,hip,ankle)
    lower=ANIM['segment_matrix'](data['knee'],data['ankle'],knee,ankle)
    foot=ANIM['translation'](ankle-data['ankle'])
    if alignment>0:
        lower_rotation=lower.to_quaternion()
        rotation=lower_rotation.copy()
        rotation.identity()
        rotation=rotation.slerp(lower_rotation,alignment)
        foot=ANIM['translation'](ankle) @ rotation.to_matrix().to_4x4() @ ANIM['translation'](-data['ankle'])
    return {'body':body,'hip':body,'upper':ANIM['segment_matrix'](data['hip'],data['knee'],hip,knee),'lower':lower,'foot':foot}

def transform_points(points,matrix):
    m=np.asarray(matrix,dtype=np.float64)
    return points @ m[:3,:3].T + m[:3,3]

def pose_points(points,leg,group,span,matrices):
    if leg is None:
        return transform_points(points,matrices['body'])
    if span is None:
        return transform_points(points,matrices[group])
    data=ANIM['LEGS'][leg]
    if span=='knee':
        t=(points[:,2]-(data['knee'].z-0.30))/0.60
        a,b='upper','lower'
    elif span=='ankle':
        t=(points[:,2]-data['ankle'].z)/0.50
        a,b='lower','foot'
    else:
        t=(data['hip'].z-points[:,2])/0.45
        a,b='upper','hip'
    t=np.clip(t,0.,1.)
    weights=(t*t*(3.-2.*t))[:,None]
    return transform_points(points,matrices[a])*weights+transform_points(points,matrices[b])*(1.-weights)

LOW_MESHES={}
MOTION={}
for name,parts in PARTS.items():
    src=ORIGINAL[name]
    src.calc_loop_triangles()
    obj=REST_OBJECTS[name]
    verts=[]
    faces=[]
    component_ids=[]
    all_normals=[]
    all_uv={uv.name:[] for uv in src.uv_layers}
    motion=[]
    source_coords=[v.co.copy() for v in src.vertices]
    for part in parts:
        offset=len(verts)
        pts=[Vector(v) for v in part['selected']['coords']]
        local_faces=part['selected']['faces']
        verts.extend(pts)
        faces.extend(tuple(offset+i for i in f) for f in local_faces)
        component_ids.extend([part['component']]*len(pts))
        poly_ids=set(part['source_polygon_ids'])
        triangles=[t for t in src.loop_triangles if t.polygon_index in poly_ids]
        bvh=BVHTree.FromPolygons(source_coords,[tuple(t.vertices) for t in triangles],all_triangles=True)
        for f in local_faces:
            center=sum((pts[i] for i in f),Vector())/3
            face_normal=(pts[f[1]]-pts[f[0]]).cross(pts[f[2]]-pts[f[0]]).normalized()
            for index in f:
                # Sampling slightly inside the face chooses the correct side at hard corners.
                query=pts[index]*0.85+center*0.15
                nearest,_,ti,_=bvh.find_nearest(query)
                triangle=triangles[ti]
                a,b,c=(source_coords[i] for i in triangle.vertices)
                weights=barycentric(nearest,a,b,c)
                normal=sum((src.corner_normals[li].vector*w for li,w in zip(triangle.loops,weights)),Vector())
                if normal.length<1e-8 or normal.dot(face_normal)<=0.0:
                    normal=face_normal
                all_normals.append(tuple(normal.normalized()))
                for layer in src.uv_layers:
                    uv=sum((layer.data[li].uv*w for li,w in zip(triangle.loops,weights)),Vector((0.,0.)))
                    all_uv[layer.name].append(tuple(uv))
        leg,group,span=classify(name,part)
        world=np.array([obj.matrix_world @ v for v in pts],dtype=np.float64)
        source_ids=part['source_vertex_ids']
        sample_ids=sorted(set(source_ids[i] for i in (0,len(source_ids)//4,len(source_ids)//2,3*len(source_ids)//4,len(source_ids)-1)))
        samples=np.array([obj.matrix_world @ src.vertices[i].co for i in sample_ids],dtype=np.float64)
        motion.append({'start':offset,'end':offset+len(pts),'leg':leg,'group':group,'span':span,'world':world,'sample_ids':sample_ids,'samples':samples,'component':part['component']})
    mesh=bpy.data.meshes.new(src.name+'_10k')
    mesh.from_pydata(verts,[],faces)
    for material in src.materials:
        mesh.materials.append(material)
    for polygon in mesh.polygons:
        polygon.use_smooth=True
    for name_uv,values in all_uv.items():
        uv=mesh.uv_layers.new(name=name_uv)
        uv.data.foreach_set('uv',np.asarray(values,dtype=np.float32).ravel())
    if src.uv_layers:
        mesh.uv_layers.active_index=src.uv_layers.active_index
        for uv in src.uv_layers:
            mesh.uv_layers[uv.name].active_render=uv.active_render
    attr=mesh.attributes.new('source_component','INT','POINT')
    attr.data.foreach_set('value',component_ids)
    mesh.normals_split_custom_set(all_normals)
    mesh.update()
    LOW_MESHES[obj.name]=mesh
    MOTION[obj.name]=motion
    print('MESH_READY',obj.name,len(mesh.polygons),flush=True)

# First replace animated bodies, while every high-resolution source key remains available.
for scene in SCENES:
    if scene==STATIC:
        continue
    bpy.context.window.scene=scene
    bpy.context.view_layer.update()
    clip='Walk' if scene.name.startswith('01') else 'Mining'
    objects=[o for o in scene.objects if o.type=='MESH' and not o.get('is_lineart') and o.get('asset_source_name') in LOW_MESHES]
    for obj in objects:
        name=obj['asset_source_name']
        original=obj.data
        original_keys=original.shape_keys
        assert original_keys and not original_keys.use_relative
        mesh=LOW_MESHES[name].copy()
        mesh.name=original.name+'_10k'
        obj.data=mesh
        inverse=obj.matrix_world.inverted()
        object_max=0.
        for frame,old_key in enumerate(original_keys.key_blocks,start=1):
            positions=np.empty((len(mesh.vertices),3),dtype=np.float32)
            for part in MOTION[name]:
                matrices=transforms(clip,frame,part['leg'])
                new_world=pose_points(part['world'],part['leg'],part['group'],part['span'],matrices)
                positions[part['start']:part['end']]=transform_points(new_world,inverse)
                expected_world=pose_points(part['samples'],part['leg'],part['group'],part['span'],matrices)
                actual=np.array([obj.matrix_world @ old_key.data[i].co for i in part['sample_ids']],dtype=np.float64)
                error=float(np.max(np.linalg.norm(expected_world-actual,axis=1)))
                object_max=max(error,object_max)
            key=obj.shape_key_add(name=old_key.name,from_mix=False)
            key.data.foreach_set('co',positions.ravel())
            key.interpolation=old_key.interpolation
            key.mute=old_key.mute
            assert abs(key.frame-old_key.frame)<1e-6
        REPORT['source_animation_formula_max_sample_error_m']=max(REPORT['source_animation_formula_max_sample_error_m'],object_max)
        assert object_max<0.00002,(obj.name,object_max)
        keys=mesh.shape_keys
        keys.use_relative=False
        keys.eval_time=original_keys.eval_time
        if original_keys.animation_data and original_keys.animation_data.action:
            keys.animation_data_create()
            keys.animation_data.action=original_keys.animation_data.action
            if original_keys.animation_data.action_slot:
                keys.animation_data.action_slot=original_keys.animation_data.action_slot
        OLD_MESHES.add(original)
        obj['减面版本']='10k / 2026-11-13'
        obj['原本体三角面']=sum(len(p.vertices)-2 for p in original.polygons)
        obj['本体三角面']=len(mesh.polygons)
    print('ANIMATION_REBUILT',scene.name,flush=True)

for name,mesh in LOW_MESHES.items():
    obj=REST_OBJECTS[name]
    OLD_MESHES.add(obj.data)
    obj.data=mesh
    obj['减面版本']='10k / 2026-11-13'
    obj['本体三角面']=len(mesh.polygons)

# Solidify's first face block is the coincident original surface; only its expanded,
# reversed second block contributes the intended inverted-hull outline.
group=bpy.data.node_groups.new('10k · 仅保留反向外壳','GeometryNodeTree')
group.interface.new_socket(name='Geometry',in_out='INPUT',socket_type='NodeSocketGeometry')
group.interface.new_socket(name='Geometry',in_out='OUTPUT',socket_type='NodeSocketGeometry')
inp=group.nodes.new('NodeGroupInput')
out=group.nodes.new('NodeGroupOutput')
index=group.nodes.new('GeometryNodeInputIndex')
compare=group.nodes.new('FunctionNodeCompare')
compare.data_type='INT'
compare.operation='LESS_THAN'
delete=group.nodes.new('GeometryNodeDeleteGeometry')
delete.domain='FACE'
delete.mode='ALL'
group.links.new(inp.outputs['Geometry'],delete.inputs['Geometry'])
group.links.new(index.outputs['Index'],compare.inputs['A'])
group.links.new(compare.outputs['Result'],delete.inputs['Selection'])
group.links.new(delete.outputs['Geometry'],out.inputs['Geometry'])
outline_groups={}

def outline_safety_weights(mesh,thickness):
    # Bound displacement by the smallest incident triangle altitude over the entire clip.
    # Edge-length clamps alone miss very thin, acute triangles at bent joints.
    triangles=np.asarray([tuple(p.vertices) for p in mesh.polygons],dtype=np.int32)
    caps=np.full(len(mesh.vertices),thickness,dtype=np.float64)
    states=mesh.shape_keys.key_blocks if mesh.shape_keys else [None]
    for key in states:
        data=key.data if key else mesh.vertices
        coords=np.empty(len(data)*3,dtype=np.float32)
        data.foreach_get('co',coords)
        points=coords.reshape(-1,3).astype(np.float64)[triangles]
        a=points[:,1]-points[:,0]
        b=points[:,2]-points[:,0]
        longest=np.maximum(np.maximum(np.linalg.norm(a,axis=1),np.linalg.norm(b,axis=1)),np.linalg.norm(points[:,2]-points[:,1],axis=1))
        height=np.linalg.norm(np.cross(a,b),axis=1)/np.maximum(longest,1e-20)
        for corner in range(3):
            np.minimum.at(caps,triangles[:,corner],height*0.20)
    return np.clip(caps/max(thickness,1e-20),0.,1.)

for scene in SCENES:
    bpy.context.window.scene=scene
    for obj in scene.objects:
        if not obj.get('is_lineart'):
            continue
        assert obj.parent and obj.parent.type=='MESH'
        outline_material=obj.material_slots[0].material
        OLD_MESHES.add(obj.data)
        obj.data=obj.parent.data
        obj.material_slots[0].link='OBJECT'
        obj.material_slots[0].material=outline_material
        solidify=next(m for m in obj.modifiers if m.type=='SOLIDIFY')
        assert solidify.offset==1 and solidify.use_flip_normals and not solidify.use_rim
        # Even-offset compensation on aggressively reduced acute triangles can spike.
        # Keep the source thickness, with local edge/angle clamping for a stable hull.
        solidify.use_even_offset=False
        solidify.use_quality_normals=False
        solidify.thickness_clamp=0.25
        solidify.use_thickness_angle_clamp=True
        weights=outline_safety_weights(obj.data,solidify.thickness)
        group_name='Outline_Safe_Thickness'
        body_group=obj.parent.vertex_groups.get(group_name) or obj.parent.vertex_groups.new(name=group_name)
        weight_group=obj.vertex_groups.get(group_name) or obj.vertex_groups.new(name=group_name)
        assert body_group.index==weight_group.index
        for vi,weight in enumerate(weights):
            weight_group.add([vi],float(weight),'REPLACE')
        solidify.vertex_group=group_name
        solidify.thickness_vertex_group=0.
        modifier=obj.modifiers.new('去除重叠内层 · 保留单层描边','NODES')
        count=len(obj.data.polygons)
        if count not in outline_groups:
            local_group=group.copy()
            local_group.name=f'10k · 单层描边 · {count} 面'
            local_compare=next(n for n in local_group.nodes if n.bl_idname=='FunctionNodeCompare')
            local_compare.inputs['B'].default_value=count
            outline_groups[count]=local_group
        modifier.node_group=outline_groups[count]
        obj['描边实现']='与本体共享顶点动画；Solidify 反向外壳，删除重叠原表面'
    scene.frame_set(scene.frame_current)
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get()
    counts={'body':0,'outline':0,'laser':0,'studio':0}
    for obj in scene.objects:
        if obj.type!='MESH':
            continue
        category='outline' if obj.get('is_lineart') else 'studio' if 'Studio ground' in obj.name else 'laser' if 'Laser' in obj.name else 'body'
        ev=obj.evaluated_get(dg)
        mesh=ev.to_mesh()
        mesh.calc_loop_triangles()
        counts[category]+=len(mesh.loop_triangles)
        if category=='outline':
            assert len(mesh.loop_triangles)==len(obj.parent.data.polygons),(obj.name,len(mesh.loop_triangles),len(obj.parent.data.polygons))
        ev.to_mesh_clear()
    counts['asset_total']=counts['body']+counts['outline']+counts['laser']
    counts['scene_total']=sum(counts[k] for k in ('body','outline','laser','studio'))
    assert counts['scene_total']<=10000,counts
    REPORT['scenes'][scene.name]={'triangles':counts,'frames':[scene.frame_start,scene.frame_end],'fps':scene.render.fps}
    scene['减面统计']=json.dumps(counts,ensure_ascii=False)
    print('SCENE_COUNTS',scene.name,counts,flush=True)

for mesh in OLD_MESHES:
    if mesh.users==0:
        bpy.data.meshes.remove(mesh)
notes=bpy.data.texts.get('减面交付说明') or bpy.data.texts.new('减面交付说明')
notes.write('''四足机甲 10k 三角面候选 / 2026-11-13

原件保留。当前文件本体 4,940 三角面，单层反向描边 4,928 三角面，模型合计 9,868。
采矿激光 120 三角面；包含演示地面 12 三角面，最复杂的采矿场景总计 10,000。
保留全部 326 个连通机械分件、原材质节点与配色、三档明暗、Freestyle 内线、侧板变换。
静态 / 行进 / 采矿三个场景；行进 1–29 帧、采矿 1–73 帧，均为 24 fps。
减面后重新按源动作公式采样绝对形态键；本体和描边共用形态键与时序。
描边的 Solidify 后有 Geometry Nodes 修改器，负责删除重复内层；导出时应求值修改器。

为达到原总面数约 2.64% 的预算，细小倒角与圆曲面细分有损简化。
几何采样偏差和结构检查见同目录 reduction_10k/reduction-report.json 与 validation.json。
未进行助手读图测试，外观一致性由用户在 Blender 中验收；未导入 UE。
''')
walk=next(s for s in SCENES if s.name.startswith('01'))
bpy.context.window.scene=walk
walk.frame_set(1)
bpy.context.view_layer.update()
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT),check_existing=False,compress=True)
REPORT['source_file_unchanged']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()==PLAN['source_sha256']
assert REPORT['source_file_unchanged']
REPORT['output_sha256']=hashlib.sha256(OUTPUT.read_bytes()).hexdigest()
REPORT['output_bytes']=OUTPUT.stat().st_size
(ROOT/'reduction-report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print('REDUCTION_COMPLETE',str(OUTPUT),REPORT['output_bytes'],flush=True)

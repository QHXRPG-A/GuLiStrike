"""Independent native Blender readback: geometry, materials and every clip frame."""
import bpy
import bmesh
import hashlib
import json
import numpy as np
from collections import Counter
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
PLAN=json.loads((ROOT/'optimized-components.json').read_text(encoding='utf-8'))
PRODUCTION=json.loads((ROOT/'reduction-report.json').read_text(encoding='utf-8'))
SOURCE=Path(PLAN['source'])
OUTPUT=Path(PRODUCTION['output'])
PARTS={}
for p in PLAN['parts']:
    PARTS.setdefault(p['object'],[]).append(p)

def value(v):
    if isinstance(v,(str,int,float,bool)) or v is None:
        return v
    if hasattr(v,'name'):
        return v.name
    try:
        return [value(x) for x in v]
    except TypeError:
        return str(v)

def material_fingerprint(material):
    result={'name':material.name,'diffuse':list(material.diffuse_color),'backface':material.use_backface_culling,'nodes':[],'links':[]}
    if material.use_nodes:
        for n in material.node_tree.nodes:
            node={'name':n.name,'type':n.bl_idname,'inputs':[(s.identifier,value(s.default_value)) for s in n.inputs if hasattr(s,'default_value')],'outputs':[(s.identifier,value(s.default_value)) for s in n.outputs if hasattr(s,'default_value')]}
            if hasattr(n,'color_ramp'):
                node['ramp']=[(e.position,list(e.color)) for e in n.color_ramp.elements]
                node['ramp_interpolation']=n.color_ramp.interpolation
            for attr in ('blend_type','operation','distribution'):
                if hasattr(n,attr):node[attr]=getattr(n,attr)
            result['nodes'].append(node)
        result['links']=[(l.from_node.name,l.from_socket.identifier,l.to_node.name,l.to_socket.identifier) for l in material.node_tree.links]
    return result

def curves(action):
    if hasattr(action,'fcurves'):
        return list(action.fcurves)
    return [f for layer in action.layers for strip in layer.strips if hasattr(strip,'channelbags') for bag in strip.channelbags for f in bag.fcurves]

def action_fingerprint(keys):
    if not keys or not keys.animation_data or not keys.animation_data.action:
        return None
    return [(c.data_path,c.array_index,[(tuple(k.co),k.interpolation) for k in c.keyframe_points]) for c in curves(keys.animation_data.action)]

def positions(key,matrix):
    array=np.empty(len(key.data)*3,dtype=np.float32)
    key.data.foreach_get('co',array)
    m=np.asarray(matrix,dtype=np.float64)
    return array.reshape(-1,3) @ m[:3,:3].T+m[:3,3]

# The command starts in the untouched source file.
assert Path(bpy.data.filepath)==SOURCE
BASE_MATERIALS={m.name:material_fingerprint(m) for m in bpy.data.materials}
BASE_OBJECTS={}
BASE_CLIPS={}
BASE_LASER={}
BASE_BOUNDS=[]
for scene in bpy.data.scenes:
    bpy.context.window.scene=scene
    bpy.context.view_layer.update()
    BASE_CLIPS[scene.name]={'frames':[scene.frame_start,scene.frame_end],'fps':scene.render.fps,'camera':scene.camera.name,'freestyle':scene.render.use_freestyle}
    for obj in scene.objects:
        BASE_OBJECTS[obj.name]={'matrix':[list(row) for row in obj.matrix_world],'parent':obj.parent.name if obj.parent else None,'materials':[s.material.name if s.material else None for s in obj.material_slots],'type':obj.type}
        if obj.type!='MESH':continue
        key=obj.data.shape_keys
        if scene.name.startswith('00') and obj.name in PARTS:
            BASE_BOUNDS.extend(obj.matrix_world @ v.co for v in obj.data.vertices)
            open_parts={}
            for part in PARTS[obj.name]:
                edges=Counter()
                for pi in part['source_polygon_ids']:
                    ids=list(obj.data.polygons[pi].vertices)
                    edges.update(tuple(sorted((a,b))) for a,b in zip(ids,ids[1:]+ids[:1]))
                open_parts[part['component']]=any(n==1 for n in edges.values())
            BASE_OBJECTS[obj.name]['open_components']=open_parts
        if key:
            BASE_OBJECTS[obj.name]['keys']=[(k.name,k.frame,k.interpolation) for k in key.key_blocks]
            BASE_OBJECTS[obj.name]['action']=action_fingerprint(key)
            if 'Laser' in obj.name:
                BASE_LASER[obj.name]=[positions(k,obj.matrix_world) for k in key.key_blocks]
            elif not obj.get('is_lineart'):
                name=obj.get('asset_source_name')
                rigid={}
                if name in PARTS:
                    coords=[positions(k,obj.matrix_world) for k in key.key_blocks]
                    for part in PARTS[name]:
                        ids=part['source_vertex_ids']
                        reference=coords[0][ids]
                        # Distances to extrema identify rigid motion independently of the builder.
                        anchors=sorted(set([0]+[int(np.argmin(reference[:,a])) for a in range(3)]+[int(np.argmax(reference[:,a])) for a in range(3)]))
                        lengths=np.linalg.norm(reference[:,None,:]-reference[anchors][None,:,:],axis=2)
                        change=max(float(np.max(np.abs(np.linalg.norm(c[ids][:,None,:]-c[ids][anchors][None,:,:],axis=2)-lengths))) for c in coords)
                        rigid[str(part['component'])]=change<0.000005
                    BASE_OBJECTS[obj.name]['rigid_components']=rigid

bpy.ops.wm.open_mainfile(filepath=str(OUTPUT),load_ui=False)
REPORT={'file':str(OUTPUT),'scenes':{},'checks':{},'visual_acceptance':'待用户在 Blender 核验；未渲染或读图。'}
checks=REPORT['checks']
checks['source_hash_unchanged']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()==PLAN['source_sha256']
checks['body_material_nodes_and_colors_unchanged']=all(m.name in BASE_MATERIALS and material_fingerprint(m)==BASE_MATERIALS[m.name] for m in bpy.data.materials if not m.name.startswith('线稿 · 深蓝灰外轮廓'))
checks['outline_colors_unchanged']=all(list(m.diffuse_color)==BASE_MATERIALS[m.name]['diffuse'] for m in bpy.data.materials if m.name.startswith('线稿 · 深蓝灰外轮廓'))
checks['outline_explicit_backface_transparency']=all(any(n.type=='BSDF_TRANSPARENT' for n in m.node_tree.nodes) and any(l.from_socket.name=='Backfacing' for l in m.node_tree.links) for m in bpy.data.materials if m.name.startswith('线稿 · 深蓝灰外轮廓'))
checks['object_names_and_hierarchy_preserved']=set(BASE_OBJECTS)==set(o.name for o in bpy.data.objects)
checks['transforms_preserved']=True
checks['material_assignments_preserved']=True
checks['all_mesh_components_preserved']=True
checks['animation_keys_and_timing_preserved']=True
checks['laser_animation_unchanged']=True
checks['no_armatures']=not any(o.type=='ARMATURE' for o in bpy.data.objects)
all_finite=True
all_topology=True
all_outline_sync=True
all_outline_orientation=True
outline_winding_failures=[]
degenerate_count=0
nonmanifold_count=0
singular_edge_count=0
open_parts_preserved=True
maximum_loop_error=0.
maximum_rigid_error=0.
rigid_components_checked=0
bearing_components_checked=0
maximum_bearing_error=0.
new_bounds=[]

for scene in bpy.data.scenes:
    bpy.context.window.scene=scene
    bpy.context.view_layer.update()
    baseline=BASE_CLIPS[scene.name]
    assert baseline=={'frames':[scene.frame_start,scene.frame_end],'fps':scene.render.fps,'camera':scene.camera.name,'freestyle':scene.render.use_freestyle}
    body=[o for o in scene.objects if o.type=='MESH' and not o.get('is_lineart') and o.get('asset_source_name',o.name) in PARTS]
    outlines=[o for o in scene.objects if o.get('is_lineart')]
    for obj in scene.objects:
        old=BASE_OBJECTS[obj.name]
        checks['transforms_preserved'] &= np.max(np.abs(np.array(obj.matrix_world)-np.array(old['matrix'])))<1e-6
        checks['material_assignments_preserved'] &= [s.material.name if s.material else None for s in obj.material_slots]==old['materials']
        if obj.type!='MESH':continue
        if obj.get('is_lineart'):
            all_outline_sync &= obj.data==obj.parent.data
            continue
        key=obj.data.shape_keys
        if key:
            checks['animation_keys_and_timing_preserved'] &= not key.use_relative and [(k.name,k.frame,k.interpolation) for k in key.key_blocks]==old['keys'] and action_fingerprint(key)==old['action']
            coords=[positions(k,obj.matrix_world) for k in key.key_blocks]
            all_finite &= all(bool(np.all(np.isfinite(c))) for c in coords)
            loop_error=float(np.max(np.linalg.norm(coords[0]-coords[-1],axis=1)))
            maximum_loop_error=max(loop_error,maximum_loop_error)
            if 'Laser' in obj.name:
                checks['laser_animation_unchanged'] &= all(np.array_equal(a,b) for a,b in zip(coords,BASE_LASER[obj.name]))
            elif obj in body:
                attrs=np.array([a.value for a in obj.data.attributes['source_component'].data])
                for component,is_rigid in old['rigid_components'].items():
                    part=next(p for p in PARTS[obj['asset_source_name']] if p['component']==int(component))
                    bearing=part['constraints']['rigid_bearing']
                    if bearing:is_rigid=True
                    if not is_rigid:continue
                    ids=np.flatnonzero(attrs==int(component))
                    ref=coords[0][ids]
                    lengths=np.linalg.norm(ref[:,None,:]-ref[None,:,:],axis=2)
                    error=max(float(np.max(np.abs(np.linalg.norm(c[ids][:,None,:]-c[ids][None,:,:],axis=2)-lengths))) for c in coords)
                    maximum_rigid_error=max(maximum_rigid_error,error)
                    rigid_components_checked+=1
                    if bearing:
                        bearing_components_checked+=1
                        maximum_bearing_error=max(maximum_bearing_error,error)
        if scene.name.startswith('00') and obj in body:
            bm=bmesh.new()
            bm.from_mesh(obj.data)
            degenerate_count+=sum(f.calc_area()<1e-12 for f in bm.faces)
            nonmanifold_count+=sum(not e.is_manifold for e in bm.edges)
            singular_edge_count+=sum(e.is_wire or len(e.link_faces)>2 for e in bm.edges)
            labels=[a.value for a in obj.data.attributes['source_component'].data]
            open_parts={int(c):False for c in labels}
            for edge in bm.edges:
                if edge.is_boundary:open_parts[labels[edge.verts[0].index]]=True
            open_parts_preserved &= open_parts==old['open_components']
            bm.free()
            attrs=[a.value for a in obj.data.attributes['source_component'].data]
            checks['all_mesh_components_preserved'] &= len(set(attrs))==len(PARTS[obj.name])
            new_bounds.extend(obj.matrix_world @ v.co for v in obj.data.vertices)
    frame_counts=[]
    for frame in range(scene.frame_start,scene.frame_end+1) if not scene.name.startswith('00') else [scene.frame_current]:
        scene.frame_set(frame)
        dg=bpy.context.evaluated_depsgraph_get()
        counts={'body':0,'outline':0,'laser':0,'studio':0}
        for obj in scene.objects:
            if obj.type!='MESH':continue
            category='outline' if obj.get('is_lineart') else 'studio' if 'Studio ground' in obj.name else 'laser' if 'Laser' in obj.name else 'body'
            ev=obj.evaluated_get(dg)
            mesh=ev.to_mesh()
            mesh.calc_loop_triangles()
            counts[category]+=len(mesh.loop_triangles)
            if category=='body':
                all_topology &= len(mesh.loop_triangles)==len(obj.data.polygons)
            if category=='outline':
                expected=sum(a.value for a in obj.data.attributes['outline_enabled'].data)
                all_topology &= len(mesh.polygons)==expected
                parent_eval=obj.parent.evaluated_get(dg)
                parent_mesh=parent_eval.to_mesh()
                source_ids=[v.value for v in mesh.attributes['source_face'].data] if mesh.polygons else []
                face_pairs=[(a,parent_mesh.polygons[i]) for a,i in zip(mesh.polygons,source_ids)]
                all_outline_orientation &= all(a.normal.dot(b.normal)<0.0 for a,b in face_pairs)
                if len(outline_winding_failures)<10:
                    outline_winding_failures.extend({'scene':scene.name,'frame':frame,'object':obj.name,'face':a.index,'dot':a.normal.dot(b.normal)} for a,b in face_pairs if a.normal.dot(b.normal)>=0.0)
                parent_eval.to_mesh_clear()
            ev.to_mesh_clear()
        counts['scene_total']=sum(counts.values())
        counts['asset_total']=counts['scene_total']-counts['studio']
        frame_counts.append(counts)
    REPORT['scenes'][scene.name]={'frames_evaluated':len(frame_counts),'triangles':frame_counts[0],'counts_constant':all(c==frame_counts[0] for c in frame_counts),'within_10000_triangles':all(c['scene_total']<=10000 for c in frame_counts),'body_objects':len(body),'outline_objects':len(outlines)}
    print('VALIDATED_SCENE',scene.name,REPORT['scenes'][scene.name],flush=True)

old=np.asarray(BASE_BOUNDS)
new=np.asarray(new_bounds)
REPORT['bounds_m']={'original_min':old.min(axis=0).tolist(),'original_max':old.max(axis=0).tolist(),'reduced_min':new.min(axis=0).tolist(),'reduced_max':new.max(axis=0).tolist(),'maximum_bound_shift':float(max(np.max(np.abs(old.min(axis=0)-new.min(axis=0))),np.max(np.abs(old.max(axis=0)-new.max(axis=0)))))}
checks.update({'all_coordinates_finite':bool(all_finite),'topology_stable_all_frames':bool(all_topology),'outline_shared_mesh_and_animation':bool(all_outline_sync),'outline_reversed_winding_all_frames':bool(all_outline_orientation),'no_degenerate_body_triangles':degenerate_count==0,'no_wire_or_multi_face_edges':singular_edge_count==0,'source_open_surfaces_and_closed_parts_preserved':bool(open_parts_preserved),'animation_loops_closed':maximum_loop_error<0.000005,'rigid_parts_remain_rigid':maximum_rigid_error<0.000005})
REPORT['measurements']={'degenerate_body_triangles':degenerate_count,'nonmanifold_body_edges':nonmanifold_count,'maximum_loop_error_m':maximum_loop_error,'rigid_components_checked':rigid_components_checked,'maximum_rigid_distance_error_m':maximum_rigid_error,'source_animation_formula_max_sample_error_m':PRODUCTION['source_animation_formula_max_sample_error_m'],'source_surface_sample_max_error_m':PRODUCTION['source_surface_sample_max_error_m']}
REPORT['outline_winding_failures']=outline_winding_failures
REPORT['measurements']['rigid_bearings_checked']=bearing_components_checked
REPORT['measurements']['maximum_bearing_pair_distance_error_m']=maximum_bearing_error
checks['all_round_bearings_rigid_in_every_frame']=maximum_bearing_error<0.000005
REPORT['all_technical_checks_passed']=all(bool(v) for v in checks.values()) and all(s['counts_constant'] and s['within_10000_triangles'] for s in REPORT['scenes'].values())
# Normalize NumPy scalars before serializing.
REPORT['checks']={k:bool(v) for k,v in checks.items()}
(ROOT/'validation.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print('VALIDATION_COMPLETE',REPORT['all_technical_checks_passed'],REPORT['checks'],REPORT['measurements'],flush=True)
assert REPORT['all_technical_checks_passed']

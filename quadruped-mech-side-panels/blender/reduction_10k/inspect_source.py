import bpy
import bmesh
import json
from collections import Counter
from pathlib import Path

OUT = Path(__file__).resolve().parent

def components(mesh):
    parent = list(range(len(mesh.vertices)))
    def root(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for e in mesh.edges:
        a,b = map(root, e.vertices)
        parent[b] = a
    return Counter(root(v.index) for v in mesh.vertices)

result = {'file': bpy.data.filepath, 'version': bpy.app.version_string, 'scenes': []}
for scene in bpy.data.scenes:
    bpy.context.window.scene = scene
    dg = bpy.context.evaluated_depsgraph_get()
    record = {'name': scene.name, 'frames': [scene.frame_start, scene.frame_end], 'fps': scene.render.fps, 'objects': [], 'triangles': dict(body=0, outline=0, other=0)}
    for obj in scene.objects:
        if obj.type != 'MESH':
            continue
        mesh = obj.data
        ev = obj.evaluated_get(dg)
        em = ev.to_mesh()
        em.calc_loop_triangles()
        group = 'other' if 'Studio ground' in obj.name or 'Laser' in obj.name else 'outline' if obj.get('is_lineart') else 'body'
        record['triangles'][group] += len(em.loop_triangles)
        entry = {'name': obj.name, 'group': group, 'data': mesh.name, 'users': mesh.users, 'vertices': len(mesh.vertices), 'polygons': len(mesh.polygons), 'triangles': len(em.loop_triangles), 'materials': [m.name if m else None for m in mesh.materials], 'keys': len(mesh.shape_keys.key_blocks) if mesh.shape_keys else 0, 'modifiers': [{'name': m.name, 'type': m.type} for m in obj.modifiers], 'uv': [u.name for u in mesh.uv_layers], 'attributes': [(a.name,a.data_type,a.domain) for a in mesh.attributes], 'custom_normals': mesh.has_custom_normals, 'dimensions': list(obj.dimensions), 'source': obj.get('asset_source_name'), 'properties': {k: str(v)[:100] for k,v in obj.items()}}
        if group == 'body' and record['name'].startswith('00'):
            comps=components(mesh)
            entry['components'] = len(comps)
            entry['component_sizes'] = sorted(comps.values(),reverse=True)
            bm = bmesh.new()
            bm.from_mesh(mesh)
            entry['boundary_edges'] = sum(e.is_boundary for e in bm.edges)
            entry['nonmanifold_edges'] = sum(not e.is_manifold for e in bm.edges)
            bmesh.ops.dissolve_limit(bm, angle_limit=0.001, verts=list(bm.verts), edges=list(bm.edges), use_dissolve_boundaries=False, delimit={'MATERIAL','SEAM','SHARP','UV','NORMAL'})
            entry['planar_dissolve_triangles'] = sum(len(f.verts)-2 for f in bm.faces)
            entry['planar_dissolve_polygons'] = len(bm.faces)
            bm.free()
        record['objects'].append(entry)
        ev.to_mesh_clear()
    result['scenes'].append(record)
result['images'] = [{'name':i.name,'path':i.filepath,'packed':bool(i.packed_file)} for i in bpy.data.images]
result['texts'] = [t.name for t in bpy.data.texts]
result['decimate_props'] = list(bpy.types.DecimateModifier.bl_rna.properties.keys())
result['solidify_props'] = list(bpy.types.SolidifyModifier.bl_rna.properties.keys())
(OUT/'source-inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
for s in result['scenes']:
    print(s['name'], s['triangles'], 'mesh_objects',len(s['objects']),flush=True)
    if s['name'].startswith('00'):
        for o in s['objects']:
            if o['group']=='body':
                print(o['name'],o['triangles'],o.get('components'),o.get('planar_dissolve_triangles'),flush=True)
print('INSPECTION_COMPLETE',flush=True)

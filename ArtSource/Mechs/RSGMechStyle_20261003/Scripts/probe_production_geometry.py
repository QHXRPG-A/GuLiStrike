"""Inspect exact source components and planar cleanup before production budgets."""
import bpy
import bmesh
import json
import math
from collections import Counter
from pathlib import Path
from mathutils import Vector

root = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
parts = json.loads((root/'Baseline/source_connected_parts.json').read_text(encoding='utf-8'))
records = []
for part in parts:
    source = bpy.data.objects[part['object']]
    ids = part['vertex_ids']
    remap = {vi: i for i, vi in enumerate(ids)}
    mesh = bpy.data.meshes.new('Probe')
    mesh.from_pydata([source.matrix_world @ source.data.vertices[vi].co for vi in ids], [],
                     [[remap[vi] for vi in source.data.polygons[pi].vertices] for pi in part['polygon_ids']])
    for face, pi in zip(mesh.polygons, part['polygon_ids']):
        face.material_index = source.data.polygons[pi].material_index
    mesh.calc_loop_triangles()
    original = len(mesh.loop_triangles)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.000005)
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(.15), use_dissolve_boundaries=False,
                           verts=list(bm.verts), edges=list(bm.edges), delimit={'MATERIAL'})
    bm.to_mesh(mesh)
    bm.free()
    mesh.calc_loop_triangles()
    records.append({'component': part['component'], 'bone': part['dominant_bone'],
                    'original_triangles': original, 'cleaned_triangles': len(mesh.loop_triangles),
                    'cleaned_vertices': len(mesh.vertices), 'faces': len(mesh.polygons),
                    'dimensions_m': part['dimensions_m'], 'center_m': part['center_m']})
    bpy.data.meshes.remove(mesh)
rig = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
probe = {'parts': records, 'source_triangles': sum(r['original_triangles'] for r in records),
         'cleaned_triangles': sum(r['cleaned_triangles'] for r in records),
         'source_rig_matrix': [list(r) for r in rig.matrix_world],
         'source_bones': [{'name': b.name, 'parent': b.parent.name if b.parent else None,
                           'matrix_local': [list(r) for r in b.matrix_local], 'length': b.length}
                          for b in rig.data.bones],
         'dominant_bones': dict(Counter(r['bone'] for r in records))}
(root/'Production_B_v1/geometry_probe.json').write_text(json.dumps(probe, indent=2), encoding='utf-8')
print(json.dumps({'parts': len(records), 'source_triangles': probe['source_triangles'],
                  'planar_cleaned_triangles': probe['cleaned_triangles'],
                  'largest_parts': sorted(records, key=lambda r: r['cleaned_triangles'], reverse=True)[:12]}, ensure_ascii=False), flush=True)

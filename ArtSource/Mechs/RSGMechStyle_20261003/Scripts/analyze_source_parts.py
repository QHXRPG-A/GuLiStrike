"""Read-only connected-part analysis for Stage A color and movement notes."""
import bpy
import json
from collections import Counter
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
parts = []
for obj in [o for o in bpy.context.scene.objects if o.type == 'MESH']:
    mesh = obj.data
    parents = list(range(len(mesh.vertices)))
    def root(i):
        while parents[i] != i:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i
    # UE exports split vertices at UV/normal seams. Weld only the analysis graph.
    coincident = {}
    for v in mesh.vertices:
        key = tuple(round(a, 5) for a in v.co)
        if key in coincident:
            parents[root(v.index)] = root(coincident[key])
        else:
            coincident[key] = v.index
    for e in mesh.edges:
        a, b = (root(i) for i in e.vertices)
        if a != b:
            parents[b] = a
    groups = {}
    for v in mesh.vertices:
        groups.setdefault(root(v.index), []).append(v.index)
    poly_groups = {}
    for p in mesh.polygons:
        poly_groups.setdefault(root(p.vertices[0]), []).append(p.index)
    for component, ids in groups.items():
        points = [obj.matrix_world @ mesh.vertices[i].co for i in ids]
        lo = [min(p[a] for p in points) for a in range(3)]
        hi = [max(p[a] for p in points) for a in range(3)]
        weights = Counter()
        for i in ids:
            for g in mesh.vertices[i].groups:
                weights[obj.vertex_groups[g.group].name] += g.weight
        mats = Counter(int(mesh.polygons[i].material_index) for i in poly_groups.get(component, []))
        parts.append({'object': obj.name, 'component': component, 'vertex_count': len(ids),
                      'polygon_count': len(poly_groups.get(component, [])),
                      'bounds_m': [lo, hi], 'center_m': [(a+b)/2 for a,b in zip(lo,hi)],
                      'dimensions_m': [b-a for a,b in zip(lo,hi)],
                      'dominant_bone': weights.most_common(1)[0][0] if weights else None,
                      'material_indices': dict(mats), 'vertex_ids': ids,
                      'polygon_ids': poly_groups.get(component, [])})
(ROOT / 'Baseline/source_connected_parts.json').write_text(json.dumps(parts), encoding='utf-8')
print('PARTS', len(parts))
for p in parts:
    if p['vertex_count'] > 400:
        print(p['component'], p['vertex_count'], p['dominant_bone'],
              [round(v,2) for v in p['center_m']], [round(v,2) for v in p['dimensions_m']], p['material_indices'])

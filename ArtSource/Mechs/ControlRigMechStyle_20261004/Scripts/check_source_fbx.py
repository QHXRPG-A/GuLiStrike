"""Read exported FBX metadata to account for the original/Blender triangle-count difference."""
import json
from collections import Counter
from pathlib import Path
import numpy as np
from io_scene_fbx import parse_fbx

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
tree, version = parse_fbx.parse(str(ROOT / 'Source/SKM_Mech_Source_LOD0.fbx'))
objects = next(e for e in tree.elems if e.id == b'Objects')
report = {'fbx_version': version, 'meshes': [], 'source_asset_binary_read': False}
for geom in (e for e in objects.elems if e.id == b'Geometry' and e.props[-1] == b'Mesh'):
    indices = next(e.props[0] for e in geom.elems if e.id == b'PolygonVertexIndex')
    coords = np.array(next(e.props[0] for e in geom.elems if e.id == b'Vertices')).reshape((-1,3))
    polys, poly = [], []
    for i in indices:
        poly.append(int(-i-1 if i<0 else i))
        if i<0:
            polys.append(poly)
            poly = []
    repeated = [i for i,p in enumerate(polys) if len(set(p))<3]
    face_counts = Counter(tuple(sorted(p)) for p in polys)
    duplicate_count = sum(n-1 for n in face_counts.values() if n>1)
    zero_area = []
    tris = np.array(polys, dtype=np.int64)
    pts = coords[tris]
    area2 = np.linalg.norm(np.cross(pts[:,1]-pts[:,0], pts[:,2]-pts[:,0]),axis=1)
    zero_area = np.flatnonzero(area2<1e-12).tolist()
    report['meshes'].append({'fbx_polygon_count': len(polys), 'vertices': len(coords),
        'triangle_count_if_all_triangles': len(tris),
        'polygons_with_repeated_vertex_indices': len(repeated),
        'duplicate_faces_by_vertex_indices': duplicate_count,
        'zero_area_polygons': len(zero_area), 'zero_area_polygon_indices': zero_area})
(ROOT / 'Baseline/fbx_count_audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('FBX_COUNT_AUDIT', json.dumps(report), flush=True)

"""Read UE's raw FBX buffers without Blender's duplicate-face cleanup or images."""
import json
from pathlib import Path
import numpy as np
from io_scene_fbx import parse_fbx

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
root, version = parse_fbx.parse(str(OUT / 'UE_RenderMesh_Readback.fbx'))


def child(node, name):
    return next(n for n in node.elems if n.id == name)


def prop(node, name):
    return child(node, name).props[0]


def layer(node, value_key, index_key, channels):
    assert prop(node, b'MappingInformationType') == b'ByPolygonVertex'
    values = np.array(prop(node, value_key)).reshape((-1, channels))
    if prop(node, b'ReferenceInformationType') == b'IndexToDirect':
        values = values[np.array(prop(node, index_key))]
    else: assert prop(node, b'ReferenceInformationType') == b'Direct'
    return values


result = []
geometries = [g for g in child(root, b'Objects').elems if g.id == b'Geometry' and g.props[2] == b'Mesh']
for lod, geo in enumerate(geometries):
    source = json.loads((OUT / f'lod{lod}.json').read_text())
    indices = np.array(prop(geo, b'PolygonVertexIndex'))
    assert (indices.reshape((-1, 3))[:, 2] < 0).all()
    indices[indices < 0] = -indices[indices < 0]-1
    positions = np.array(prop(geo, b'Vertices')).reshape((-1,3))[indices]
    positions[:,1] *= -1  # UE FBX handedness conversion; centimetres unchanged.
    normals = layer(child(geo, b'LayerElementNormal'), b'Normals', b'NormalsIndex', 3)
    normals[:,1] *= -1
    colors = layer(child(geo, b'LayerElementColor'), b'Colors', b'ColorIndex', 4)
    colors[:,:3] = np.where(colors[:,:3] <= .04045, colors[:,:3]/12.92, ((colors[:,:3]+.055)/1.055)**2.4)
    uvs = []
    for node in sorted([e for e in geo.elems if e.id == b'LayerElementUV'], key=lambda n: n.props[0]):
        uv = layer(node, b'UV', b'UVIndex', 2); uv[:,1] = 1-uv[:,1]; uvs.append(uv)
    assert len(uvs) == 3
    pos_ids = np.rint(uvs[0][:,0]).astype(int)
    normal_ids = np.rint(uvs[2][:,1]).astype(int)
    assert (uvs[0][:,1] == lod).all()
    assert len(np.unique(normal_ids)) == len(indices) == len(source['colors'])
    # The atlas retains bake order, while UE-clockwise triangles reorder corners.
    source_corner_by_normal = np.empty(len(source['colors']), dtype=int)
    source_corner_by_normal[np.rint(np.array(source['uvs'][2])[:,1]).astype(int)] = np.arange(len(source['colors']))
    source_corners = source_corner_by_normal[normal_ids]
    errors = dict(position_cm=float(np.max(np.abs(positions-np.array(source['positions'])[pos_ids]))),
                  normal=float(np.max(np.abs(normals-np.array(source['normals'])[source_corners]))),
                  color=float(np.max(np.abs(colors-np.array(source['colors'])[source_corners]))),
                  uv=max(float(np.max(np.abs(uv-np.array(source['uvs'][c])[source_corners]))) for c,uv in enumerate(uvs)))
    assert errors['position_cm'] < .001 and errors['normal'] < .02 and errors['color'] < .0001 and errors['uv'] < .0001, errors
    triangle_positions = positions.reshape((-1,3,3))
    geometric = np.cross(triangle_positions[:,2]-triangle_positions[:,0], triangle_positions[:,1]-triangle_positions[:,0])
    aligned = (geometric * normals.reshape((-1,3,3)).mean(axis=1)).sum(axis=1) > 0
    assert aligned.all(), 'Render triangle winding disagrees with outward vertex normals'
    materials = np.array(prop(child(geo,b'LayerElementMaterial'),b'Materials'))
    seen = set(); lamp_groups = []
    for i in range(len(indices)//3):
        corners = slice(i*3,i*3+3); face_ids = set(normal_ids[corners]//3)
        assert len(face_ids) == 1
        face = int(next(iter(face_ids))); assert face not in seen; seen.add(face)
        expected = source['triangles'][face]
        assert sorted(pos_ids[corners]) == sorted(expected[:3]) and materials[i] == expected[3]
        if source['material_names'][expected[3]] == 'Lamp':
            lamp_groups.append(set(pos_ids[corners]))
            assert np.max(np.abs(colors[corners,3]*255-7)) < .0001
        else: assert (colors[corners,3] == 0).all()
    counts = []
    while lamp_groups:
        group = lamp_groups.pop(); count = 1; changed = True
        while changed:
            changed = False
            for i in range(len(lamp_groups)-1,-1,-1):
                if group.intersection(lamp_groups[i]):
                    group.update(lamp_groups.pop(i)); count += 1; changed = True
        counts.append(count)
    assert sorted(counts) == [12,12,12,12]
    result.append(dict(lod=lod, triangles=len(indices)//3, correspondence_corners=len(normal_ids),
                       max_error=errors, ue_clockwise_triangles=int(aligned.sum()),
                       lamp_triangles_per_leg=sorted(counts), paint_roles=[0,7]))
assert len(result) == 3
report = dict(success=True, source='UE built render buffers exported as FBX; raw layers retain coincident faces',
              lods=result, visual_inspection=False)
(OUT / 'render-mesh-readback.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))

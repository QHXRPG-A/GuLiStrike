"""Background Blender export: existing B geometry/RGB/UV unchanged, encode role/255 in alpha.

Uses the saved B source, never touches the user's dirty interactive Blender session.
The independent output is an interface candidate, not approval or a formal UE import.
"""
import bpy
import hashlib
import json
from collections import Counter
from pathlib import Path

ROOT = Path(r'D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/ModelInterface_B_20261008'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'Masks').mkdir(exist_ok=True)
ROLE = 'Bv1_ColorRegion'
report = {'source': bpy.data.filepath, 'source_sha256': hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),
          'encoding': 'Alpha = round(PaintRole)/255; linear RGB unchanged; original UVs and geometry unchanged',
          'cpd': {'primary': [8,9,10,11], 'secondary': [12,13,14,15], 'enabled':16, 'lamp_strength':17},
          'models': [], 'approval': 'B pending; no formal import', 'errors': []}
for scene in sorted(bpy.data.scenes, key=lambda s:s.name):
    if not scene.name.startswith('Blue_'): continue
    key = scene.name.removeprefix('Blue_')
    entry = {'name': key, 'meshes': []}
    for obj in scene.objects:
        if obj.type != 'MESH' or ROLE not in obj.data.attributes: continue
        mesh = obj.data
        role = mesh.attributes[ROLE]
        if role.domain != 'FACE' or role.data_type != 'INT': raise ValueError(obj.name + ': wrong role attribute')
        colors = mesh.color_attributes.active_color
        if not colors or colors.domain != 'CORNER': raise ValueError(obj.name + ': missing corner palette')
        # Hash only the fields that must not change, independently of preview materials.
        invariant = json.dumps({'positions':[list(v.co) for v in mesh.vertices],
            'faces':[list(p.vertices) for p in mesh.polygons],
            'uv':[[list(d.uv) for d in layer.data] for layer in mesh.uv_layers],
            'rgb':[list(d.color[:3]) for d in colors.data]},separators=(',',':')).encode()
        before = hashlib.sha256(invariant).hexdigest()
        for face in mesh.polygons:
            r = role.data[face.index].value
            if r not in range(8): raise ValueError(obj.name + ': invalid role')
            for index in face.loop_indices:
                c = colors.data[index].color[:]
                colors.data[index].color = (*c[:3], r / 255.0)
        mesh.calc_loop_triangles()
        triangles = []
        triangle_materials = []
        triangle_uvs = []
        triangle_vertices = []
        for triangle in mesh.loop_triangles:
            face = mesh.polygons[triangle.polygon_index]
            material=mesh.materials[face.material_index]
            if material and any(n in material.name.lower() for n in ['outline','contour']): continue
            rgb = colors.data[triangle.loops[0]].color[:3]
            triangles.append([*[float(x) for vi in triangle.vertices for x in mesh.vertices[vi].co],
                              role.data[face.index].value, *rgb])
            triangle_materials.append(face.material_index)
            triangle_vertices.append(list(triangle.vertices))
            triangle_uvs.append([list(mesh.uv_layers[0].data[i].uv) for i in triangle.loops] if mesh.uv_layers else [])
        filename = key + '__' + obj.name.removeprefix('Blue_' + key + '_') + '.json'
        payload = {'model':key,'object':obj.name,'space':'mesh local Blender coordinates; align bounds/axes before assignment',
                   'matrix': [list(row) for row in obj.matrix_world], 'triangles':triangles,
                   'material_names':[m.name if m else '' for m in mesh.materials],
                   'triangle_materials':triangle_materials,'uv0':triangle_uvs,'vertex_ids':triangle_vertices}
        path = OUT / 'Masks' / filename
        path.write_text(json.dumps(payload,separators=(',',':')),encoding='utf-8')
        after = hashlib.sha256(json.dumps({'positions':[list(v.co) for v in mesh.vertices],
            'faces':[list(p.vertices) for p in mesh.polygons],
            'uv':[[list(d.uv) for d in layer.data] for layer in mesh.uv_layers],
            'rgb':[list(d.color[:3]) for d in colors.data]},separators=(',',':')).encode()).hexdigest()
        if before != after: raise ValueError(obj.name + ': geometry/RGB/UV changed')
        entry['meshes'].append({'object':obj.name, 'faces':len(mesh.polygons), 'triangles':len(triangles),
            'roles':dict(Counter(role.data[p.index].value for p in mesh.polygons)), 'mask':str(path.relative_to(OUT)),
            'mask_sha256':hashlib.sha256(path.read_bytes()).hexdigest(), 'geometry_rgb_uv_sha256':before})
    report['models'].append(entry)
# Keep exactly one canonical paint mesh set. Team variants belong to component parameters.
for obj in list(bpy.data.objects):
    if obj.name.startswith('Red_'):
        bpy.data.objects.remove(obj, do_unlink=True)
for scene in list(bpy.data.scenes):
    if scene.name.startswith('Red_'):
        bpy.data.scenes.remove(scene)
file = OUT / 'PaletteOnly_14Models_EncodedAlpha_InterfaceCandidate.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(file), copy=True)
report['blend'] = str(file.relative_to(OUT))
report['blend_sha256'] = hashlib.sha256(file.read_bytes()).hexdigest()
(OUT / 'region-contract.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
result = {'models':len(report['models']),'meshes':sum(len(x['meshes']) for x in report['models']),
          'geometry_rgb_uv_preserved':True,'output':str(OUT),'source_unmodified':True,'formal_import':False}

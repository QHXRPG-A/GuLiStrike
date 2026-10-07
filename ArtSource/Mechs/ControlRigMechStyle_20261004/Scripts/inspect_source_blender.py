"""Inspect exported source LODs and render a gray baseline; no production mesh edits."""
import bpy
import json
from collections import Counter
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
OUT = ROOT / 'Baseline'
OUT.mkdir(parents=True, exist_ok=True)


def import_source(filename):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Source' / filename),
                            use_custom_normals=True, automatic_bone_orientation=False,
                            ignore_leaf_bones=False)
    bpy.context.view_layer.update()


def mesh_info(o):
    o.data.calc_loop_triangles()
    counts = Counter(t.material_index for t in o.data.loop_triangles)
    return {'name': o.name, 'vertices': len(o.data.vertices),
            'triangles': len(o.data.loop_triangles),
            'parent': o.parent.name if o.parent else None,
            'material_slots': [s.name for s in o.material_slots],
            'triangles_per_material': dict(counts),
            'vertex_groups': [g.name for g in o.vertex_groups],
            'matrix_world': [list(row) for row in o.matrix_world]}


import_source('SKM_Mech_Source_AllLODs.fbx')
lod_report = {'purpose': 'Exported original UE LOD geometry, not generated production LODs',
              'objects': [{'name': o.name, 'type': o.type, 'parent': o.parent.name if o.parent else None}
                          for o in bpy.context.scene.objects],
              'meshes': [mesh_info(o) for o in bpy.context.scene.objects if o.type == 'MESH']}
(OUT / 'source_lod_inspection.json').write_text(json.dumps(lod_report, indent=2), encoding='utf-8')
print('SOURCE_LODS', json.dumps(lod_report['meshes']), flush=True)

import_source('SKM_Mech_Source_LOD0.fbx')
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
rigs = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
points = [o.matrix_world @ Vector(v) for o in meshes for v in o.bound_box]
minimum = Vector([min(p[a] for p in points) for a in range(3)])
maximum = Vector([max(p[a] for p in points) for a in range(3)])
center = (minimum + maximum) * .5
dimensions = maximum - minimum
report = {
    'purpose': 'Untouched source geometry for Stage A inspection, not a Blender production result',
    'bounds_m': {'min': list(minimum), 'max': list(maximum), 'dimensions': list(dimensions)},
    'meshes': [mesh_info(o) for o in meshes],
    'armatures': [{'name': o.name, 'scale': list(o.scale),
        'bones': [{'name': b.name, 'parent': b.parent.name if b.parent else None,
                   'head_m': list(o.matrix_world @ b.head_local),
                   'tail_m': list(o.matrix_world @ b.tail_local)} for b in o.data.bones]} for o in rigs],
}
(OUT / 'blender_source_inspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

parts = []
for obj in meshes:
    mesh = obj.data
    parents = list(range(len(mesh.vertices)))
    def root(i):
        while parents[i] != i:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i
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
    groups, poly_groups = {}, {}
    for v in mesh.vertices:
        groups.setdefault(root(v.index), []).append(v.index)
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
                      'bone_weights': dict(weights.most_common()),
                      'material_indices': dict(mats), 'vertex_ids': ids,
                      'polygon_ids': poly_groups.get(component, [])})
(OUT / 'source_connected_parts.json').write_text(json.dumps(parts), encoding='utf-8')
print('SOURCE_BASELINE', json.dumps(report['bounds_m']), 'parts', len(parts), flush=True)

# Store imported source with its original positions, topology, weights and normals.
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ControlRigMech_SourceImported.blend'))
grey = bpy.data.materials.new('SourceInspection_Gray')
grey.diffuse_color = (.45, .45, .45, 1)
grey.use_nodes = True
bsdf = next(n for n in grey.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
bsdf.inputs['Base Color'].default_value = (.45, .45, .45, 1)
bsdf.inputs['Roughness'].default_value = .75
for o in meshes:
    for slot in o.material_slots:
        slot.material = grey
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = scene.render.resolution_y = 1100
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.world = bpy.data.worlds.new('SourceInspectionWorld')
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.82, .82, .82, 1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .6
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
size = max(dimensions)
for name, direction, power in [('Key', (.7, -1, 1.5), 70), ('Fill', (-1, .3, .6), 40)]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy, data.shape, data.size = power * size ** 2, 'DISK', size * 1.3
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = center + Vector(direction) * size * 2
    obj.rotation_euler = (center - obj.location).to_track_quat('-Z', 'Y').to_euler()
camera = bpy.data.objects.new('StageACamera', bpy.data.cameras.new('StageACamera'))
scene.collection.objects.link(camera)
scene.camera = camera
camera.data.type = 'ORTHO'
camera.data.ortho_scale = size * 1.28
camera.data.clip_end = size * 20
views = {'MinusY': (0,-1,0), 'PlusY': (0,1,0), 'MinusX': (-1,0,0),
         'PlusX': (1,0,0), 'Hero': (.85,-1.1,.65)}
for name, direction in views.items():
    camera.location = center + Vector(direction).normalized() * size * 4
    camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(OUT / f'Inspect_{name}.png')
    bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ControlRigMech_SourceInspection.blend'))
print('CONTROLRIG_BASELINE_OK', flush=True)

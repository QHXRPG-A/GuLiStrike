"""Import untouched source geometry into an isolated Stage A inspection scene."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT / 'Baseline'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Source/SK_FPS_Mech_Source_LOD0.fbx'),
                         use_custom_normals=True, automatic_bone_orientation=False,
                         ignore_leaf_bones=False)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
rigs = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
points = [o.matrix_world @ Vector(v) for o in meshes for v in o.bound_box]
minimum = Vector([min(p[a] for p in points) for a in range(3)])
maximum = Vector([max(p[a] for p in points) for a in range(3)])
center = (minimum + maximum) * 0.5
report = {
    'purpose': 'Stage A source geometry inspection only; not remade production geometry',
    'bounds_m': {'min': list(minimum), 'max': list(maximum), 'dimensions': list(maximum - minimum)},
    'meshes': [], 'armatures': [],
}
for o in meshes:
    o.data.calc_loop_triangles()
    report['meshes'].append({'name': o.name, 'vertices': len(o.data.vertices),
                           'triangles': len(o.data.loop_triangles),
                           'material_slots': [s.name for s in o.material_slots],
                           'vertex_groups': [g.name for g in o.vertex_groups]})
for o in rigs:
    report['armatures'].append({'name': o.name, 'scale': list(o.scale),
        'bones': [{'name': b.name, 'parent': b.parent.name if b.parent else None,
                   'head_m': list(o.matrix_world @ b.head_local),
                   'tail_m': list(o.matrix_world @ b.tail_local)} for b in o.data.bones]})
(OUT / 'blender_source_inspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
grey = bpy.data.materials.new('SourceInspection_Gray')
grey.diffuse_color = (0.49, 0.49, 0.49, 1)
grey.use_nodes = True
bsdf = grey.node_tree.nodes.get('Principled BSDF')
bsdf.inputs['Base Color'].default_value = (0.49, 0.49, 0.49, 1)
bsdf.inputs['Roughness'].default_value = 0.72
for o in meshes:
    for slot in o.material_slots:
        slot.material = grey
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = scene.render.resolution_y = 1200
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.world = bpy.data.worlds.new('InspectionWorld')
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.8, 0.8, 0.8, 1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.6
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
for name, direction, power, size in [('Key', (0.6, -0.8, 1.4), 3200, 8),
                                    ('Fill', (-0.9, 0.2, 0.8), 1800, 9)]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy, data.shape, data.size = power, 'DISK', size
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = center + Vector(direction) * 12
    obj.rotation_euler = (center - obj.location).to_track_quat('-Z', 'Y').to_euler()
camera = bpy.data.objects.new('SourceInspectionCamera', bpy.data.cameras.new('SourceInspectionCamera'))
scene.collection.objects.link(camera)
scene.camera = camera
camera.data.type = 'ORTHO'
camera.data.ortho_scale = max(maximum - minimum) * 1.22
views = {'minusY': (0, -1, 0), 'minusX': (-1, 0, 0), 'plusY': (0, 1, 0),
         'hero': (-0.85, -1.1, 0.65)}
for name, direction in views.items():
    camera.location = center + Vector(direction).normalized() * 25
    camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(OUT / f'inspect_{name}.png')
    bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'RSG_SourceInspection.blend'))
print('RSG source inspection complete', report['bounds_m'], flush=True)

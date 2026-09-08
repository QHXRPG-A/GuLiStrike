"""Read an exported WM01 copy; prepare connected rigid parts for UE skin repair.

Run in background Blender. Does not save or change the exported FBX.
"""
import bpy
import json
from pathlib import Path
from mathutils import Vector

OUT = Path('D:/UE5.7/test1/outputs/commander-rigs')
bpy.ops.import_scene.fbx(filepath=str(OUT / 'WM01_reference.fbx'), use_anim=False)
mesh = next(o for o in bpy.context.scene.objects if o.type == 'MESH' and o.name != 'Cube')
weights = json.loads((OUT / 'WM01-weights.json').read_text(encoding='utf-8'))
group_names = {g.index: g.name for g in mesh.vertex_groups}
positions = [(mesh.matrix_world @ v.co) for v in mesh.data.vertices]
positions = [(p.x * 100, -p.y * 100, p.z * 100) for p in positions]
dominant = [group_names[max(v.groups, key=lambda g: g.weight).group] for v in mesh.data.vertices]
differences = sum(dominant[i] != max(w, key=w.get) for i, w in enumerate(weights)) if len(weights) == len(dominant) else -1
parent = list(range(len(positions)))

def find(i):
    while parent[i] != i:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i

for edge in mesh.data.edges:
    a, b = edge.vertices
    if dominant[a] == dominant[b]:
        a, b = find(a), find(b)
        parent[b] = a
parts = {}
for i, name in enumerate(dominant):
    if name == 'Mainbody':
        parts.setdefault(find(i), []).append(i)
report = []
for indices in parts.values():
    points = [positions[i] for i in indices]
    lo = [min(p[a] for p in points) for a in range(3)]
    hi = [max(p[a] for p in points) for a in range(3)]
    report.append({'ids': indices, 'count': len(indices), 'lo': lo, 'hi': hi,
                   'center': [(lo[a] + hi[a]) / 2 for a in range(3)]})
report.sort(key=lambda p: (-p['count'], p['center']))
(OUT / 'wm01-parts.json').write_text(json.dumps(report), encoding='utf-8')
(OUT / 'wm01-positions.json').write_text(json.dumps(positions), encoding='utf-8')
(OUT / 'wm01-export-correspondence.json').write_text(json.dumps({'ue_vertex_count': len(weights), 'fbx_vertex_count': len(dominant), 'dominant_weight_index_mismatches': differences, 'part_count': len(report)}), encoding='utf-8')
print('CORRESPONDENCE', len(weights), len(dominant), differences, 'PARTS', len(report))
print('LARGEST_PARTS', json.dumps([{k: v for k, v in p.items() if k != 'ids'} for p in report[:70]]))

# Neutral, unskinned geometry views for mechanical-part calibration only.
mesh.modifiers.clear()
for obj in bpy.context.scene.objects:
    obj.hide_render = obj != mesh
material = bpy.data.materials.new('RigInspectionGrey')
material.diffuse_color = (0.35, 0.40, 0.48, 1)
mesh.data.materials.clear()
mesh.data.materials.append(material)
for polygon in mesh.data.polygons:
    polygon.material_index = 0
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.studiolight_rotate_z = 0.5
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.background_type = 'WORLD'
scene.world.color = (0.06, 0.07, 0.09)
scene.render.resolution_x = 1280
scene.render.resolution_y = 960
scene.render.resolution_percentage = 100
camera = bpy.data.objects.new('RigInspectionCamera', bpy.data.cameras.new('RigInspectionCamera'))
scene.collection.objects.link(camera)
scene.camera = camera
camera.data.type = 'ORTHO'
camera.data.clip_end = 10000
target = Vector((-3.0, -5.0, 14.0))
for name, direction, extent in [('top',(0,0,1),70), ('side',(0,1,0.3),70), ('front',(1,0,0.25),70), ('body',(1,1,0.9),43)]:
    camera.location = target + Vector(direction).normalized() * 100
    camera.rotation_euler = (target-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale = extent
    scene.render.filepath = str(OUT / ('WM01_geometry_' + name + '.png'))
    bpy.ops.render.render(write_still=True)

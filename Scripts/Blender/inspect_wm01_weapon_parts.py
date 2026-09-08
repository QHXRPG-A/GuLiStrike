"""Render exported source weapon parts for rig assembly calibration (no saves)."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

OUT = Path('D:/UE5.7/test1/outputs/commander-rigs')
scene = bpy.context.scene
for obj in list(scene.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.show_cavity = True
scene.world.color = (0.06, 0.07, 0.09)
scene.render.resolution_x = 1200
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
camera = bpy.data.objects.new('InspectionCamera', bpy.data.cameras.new('InspectionCamera'))
scene.collection.objects.link(camera)
scene.camera = camera
camera.data.type = 'ORTHO'
camera.data.clip_end = 10000
report = {}
for name in ['FrontMinigun', 'MissileLauncher_01', 'BackMinigun_01', 'CannonArm_01']:
    before = set(scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(OUT / (name + '.fbx')), use_anim=False)
    meshes = [o for o in scene.objects if o not in before and o.type == 'MESH'
              and not o.name.startswith(('UCX_', 'UBX_', 'USP_', 'UCP_'))]
    for obj in scene.objects:
        if obj not in before and obj not in meshes:
            obj.hide_render = True
    points = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    lo = Vector(tuple(min(p[a] for p in points) for a in range(3)))
    hi = Vector(tuple(max(p[a] for p in points) for a in range(3)))
    target = (lo + hi) * .5
    extent = max(hi - lo) * 1.5
    camera.location = target + Vector((1, 1, .8)).normalized() * extent * 2
    camera.rotation_euler = (target-camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.ortho_scale = extent
    scene.render.filepath = str(OUT / (name + '_geometry.png'))
    bpy.ops.render.render(write_still=True)
    report[name] = {'blender_min': list(lo), 'blender_max': list(hi), 'vertices': len(points), 'objects': [o.name for o in meshes]}
    for obj in list(scene.objects):
        if obj not in before:
            bpy.data.objects.remove(obj, do_unlink=True)
(OUT / 'weapon-part-geometry.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

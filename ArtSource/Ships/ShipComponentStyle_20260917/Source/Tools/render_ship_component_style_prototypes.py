"""Render only the frozen ORIGINAL meshes for design-A comparison.

This creates no redesigned model and does not touch interactive Blender.
Run in a factory-startup background Blender process.
"""
import json
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917')
OUT = ROOT / 'Source/Previews'
OUT.mkdir(parents=True, exist_ok=True)
KEYS = ('Thor_MissilePod', 'Twin_Barrel_Turret', 'CIWS')

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 1200
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.display.shading.light = 'STUDIO'
scene.display.shading.studiolight_rotate_z = .6
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = 'BOTH'
scene.display.shading.show_object_outline = True
scene.display.shading.background_type = 'WORLD'
scene.world.color = (.11, .14, .17)
scene.view_settings.view_transform = 'Standard'
material = bpy.data.materials.new('Original_Prototype_Gray_Inspection_Only')
material.diffuse_color = (.48, .56, .61, 1)
camera = bpy.data.objects.new('OriginalReferenceCamera', bpy.data.cameras.new('OriginalReferenceCamera'))
scene.collection.objects.link(camera)
scene.camera = camera
camera.data.type = 'ORTHO'
report = {'purpose': 'Existing original geometry visualization only; not new production model', 'units': 'meters', 'parts': {}}
opening_camera = None

for key in KEYS:
    data = json.loads((ROOT / 'Source' / (key + '_original_geometry.json')).read_text(encoding='utf-8'))
    points = [Vector((v[0] * .01, -v[1] * .01, v[2] * .01)) for v in data['geometry']['points']]
    mesh = bpy.data.meshes.new('ORIGINAL_' + key)
    mesh.from_pydata(points, [], data['geometry']['triangles'])
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new('ORIGINAL_' + key, mesh)
    scene.collection.objects.link(obj)
    mesh.materials.append(material)
    obj['source_geometry_sha256'] = data['geometry_sha256']
    obj['status'] = 'ORIGINAL PROTOTYPE ONLY, not redesigned or approved'
    low = Vector(tuple(min(v[i] for v in points) for i in range(3)))
    high = Vector(tuple(max(v[i] for v in points) for i in range(3)))
    center = (low + high) * .5
    span = max(high - low)
    camera.data.clip_start = .01
    camera.data.clip_end = span * 30
    views = {'hero': (1.1, -1.7, 1.65), 'front': (0, -1, 0), 'right': (1, 0, 0), 'rear': (0, 1, 0), 'top': (0, 0, 1)}
    paths = []
    for name, direction in views.items():
        direction = Vector(direction).normalized()
        camera.location = center + direction * span * 3
        camera.rotation_euler = (-direction).to_track_quat('-Z', 'Y').to_euler()
        bpy.context.view_layer.update()
        view_points = [camera.matrix_world.inverted() @ point for point in points]
        width = max(p.x for p in view_points) - min(p.x for p in view_points)
        height = max(p.y for p in view_points) - min(p.y for p in view_points)
        # Orthographic scale is the horizontal size for landscape renders.
        camera.data.ortho_scale = max(width, height * 4 / 3) * 1.16
        if key == 'Thor_MissilePod' and name == 'hero':
            opening_camera = (camera.location.copy(), camera.rotation_euler.copy(), camera.data.ortho_scale, camera.data.clip_end)
        target = OUT / (key + '_original_' + name + '_v1.png')
        scene.render.filepath = str(target)
        bpy.ops.render.render(write_still=True)
        paths.append(str(target.relative_to(ROOT)))
    report['parts'][key] = {'geometry_sha256': data['geometry_sha256'], 'bounds_m': [list(low), list(high)], 'triangles': len(mesh.polygons), 'renders': paths}
    obj.hide_render = True
    obj.hide_set(True)

(OUT / 'prototype_render_manifest_v1.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
opening_object = bpy.data.objects['ORIGINAL_Thor_MissilePod']
opening_object.hide_render = False
opening_object.hide_set(False)
opening_object.select_set(True)
bpy.context.view_layer.objects.active = opening_object
camera.location, camera.rotation_euler, camera.data.ortho_scale, camera.data.clip_end = opening_camera
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == 'VIEW_3D':
            area.spaces.active.region_3d.view_perspective = 'CAMERA'
scene['purpose'] = 'Frozen ORIGINAL prototype inspection only; no new model production'
scene['other_originals'] = 'Unhide ORIGINAL_Twin_Barrel_Turret or ORIGINAL_CIWS in Outliner to inspect individually.'
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / 'Source/OriginalPrototypeInspection_v1.blend'))
print('ORIGINAL_PROTOTYPE_RENDER_COMPLETE')

"""Render untouched deployed legs as input reference for A_v2 (not visual QA)."""
import bpy, math, json
from pathlib import Path
from mathutils import Matrix, Vector

OUT = Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Reference_A_v2')
scene = bpy.data.scenes['ReferenceOnly_LegacyGround_Lower']
bpy.context.window.scene = scene
source = next(o for o in scene.objects if o.type == 'MESH')
bpy.context.view_layer.update()
geo = source.data.copy()
geo.transform(source.matrix_world)
for o in list(scene.objects):
    o.hide_render = True
for name, x, angle in [('FRONT', -3, 0), ('SIDE', 0, math.pi / 2), ('BACK', 3, math.pi)]:
    display = bpy.data.objects.new('LegacyLowerReference_' + name, geo)
    scene.collection.objects.link(display)
    display.location.x = x
    display.rotation_euler.z = angle
    curve = bpy.data.curves.new('LegacyLowerLabel_' + name, 'FONT')
    curve.body = name
    curve.align_x = 'CENTER'
    curve.size = .17
    label = bpy.data.objects.new(curve.name, curve)
    scene.collection.objects.link(label)
    label.location = (x, -1.8, -.27)
    label.rotation_euler.x = math.pi / 2
camdata = bpy.data.cameras.new('LegacyLowerReferenceCamera')
camdata.type = 'ORTHO'
camdata.ortho_scale = 9
cam = bpy.data.objects.new(camdata.name, camdata)
scene.collection.objects.link(cam)
cam.location = (0, -20, 1.2)
cam.rotation_euler = (Vector((0, 0, 1.2)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
scene.camera = cam
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 2400
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
shading = scene.display.shading
shading.light = 'STUDIO'
shading.color_type = 'SINGLE'
shading.single_color = (.55, .6, .65)
shading.show_shadows = True
shading.show_cavity = True
shading.cavity_type = 'BOTH'
shading.background_type = 'WORLD'
scene.world = bpy.data.worlds.new('LegacyLowerReferenceWorld')
scene.world.color = (.9, .9, .9)
scene.view_settings.view_transform = 'Standard'
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.render.filepath = str(OUT / 'OriginalGround_Lower_ThreeViews.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ReferenceSources_A_v2.blend'))
bpy.ops.render.render(write_still=True)
result = {'success': Path(scene.render.filepath).exists(), 'image': scene.render.filepath,
          'purpose': 'Unmodified original source reference; not output evaluation',
          'source_mesh_changed': False}
(OUT / 'legacy_lower_render.json').write_text(json.dumps(result, indent=2), encoding='utf8')

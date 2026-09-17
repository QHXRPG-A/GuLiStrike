"""Build an editable cel-shaded study from the inspected, existing Ship hull.

Run in the connected Blender after export_ship_style_source.py and FBX import.
The original scene/mesh stay intact; the study owns separate data and materials.
"""
import bpy
import bmesh
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipStylizedStudy_20260916')
SOURCE = ROOT / 'Source'
PREVIEW = ROOT / 'Previews'
PREVIEW.mkdir(exist_ok=True)
original = bpy.data.objects['SM_Dreadnought_Hull']
original_scene = bpy.data.scenes['Ship_Original_Reference']
assert 'Ship_Stylized_Study' not in bpy.data.scenes, 'Study already exists; adjust it instead of overwriting.'


def linear_hex(code):
    rgb = [int(code[i:i+2], 16) / 255 for i in (0, 2, 4)]
    return tuple(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb) + (1,)


PALETTE = {
    'Armor_Pearl': 'D5E5E4',
    'Armor_Blue': '337F9A',
    'Structure_Slate': '486679',
    'Recess_Navy': '1C3348',
    'Safety_Amber': 'EAAA4E',
    'Engine_Cyan': '8AF2EB',
}


def cel_material(name, code, stripe=False, glowing=False):
    mat = bpy.data.materials.new('SS_' + name)
    mat.use_nodes = True
    mat.diffuse_color = linear_hex(code)
    mat['study_owner'] = 'ShipStylizedStudy_20260916'
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = nodes.new('ShaderNodeOutputMaterial'); out.location = (690, 30)
    emission = nodes.new('ShaderNodeEmission'); emission.location = (470, 30)
    emission.inputs['Strength'].default_value = 1.0
    links.new(emission.outputs[0], out.inputs['Surface'])
    if glowing:
        emission.inputs['Color'].default_value = linear_hex(code)
        return mat
    diffuse = nodes.new('ShaderNodeBsdfDiffuse'); diffuse.location = (-620, 160)
    diffuse.inputs['Color'].default_value = (1, 1, 1, 1)
    diffuse.inputs['Roughness'].default_value = 0.6
    conversion = nodes.new('ShaderNodeShaderToRGB'); conversion.location = (-400, 160)
    links.new(diffuse.outputs[0], conversion.inputs[0])
    ramp = nodes.new('ShaderNodeValToRGB'); ramp.location = (-210, 160)
    ramp.name = 'Three_Tone_Lighting'; ramp.label = '3 tones - editable cel lighting'
    ramp.color_ramp.interpolation = 'CONSTANT'
    ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for i, (position, value) in enumerate([(0.0, (0.38, 0.47, 0.59, 1)), (0.26, (0.70, 0.78, 0.87, 1)), (0.56, (1, 1, 1, 1))]):
        elem = ramp.color_ramp.elements[0] if i == 0 else ramp.color_ramp.elements.new(position)
        elem.position, elem.color = position, value
    links.new(conversion.outputs['Color'], ramp.inputs[0])
    multiply = nodes.new('ShaderNodeMixRGB'); multiply.location = (240, 50)
    multiply.blend_type = 'MULTIPLY'; multiply.inputs[0].default_value = 1
    multiply.inputs[1].default_value = linear_hex(code)
    links.new(ramp.outputs['Color'], multiply.inputs[2])
    links.new(multiply.outputs[0], emission.inputs['Color'])
    if stripe:
        geo = nodes.new('ShaderNodeNewGeometry'); geo.location = (-870, -240)
        sep = nodes.new('ShaderNodeSeparateXYZ'); sep.location = (-670, -240)
        links.new(geo.outputs['Position'], sep.inputs[0])
        lower = nodes.new('ShaderNodeMath'); lower.operation = 'GREATER_THAN'; lower.location = (-470, -190)
        upper = nodes.new('ShaderNodeMath'); upper.operation = 'LESS_THAN'; upper.location = (-470, -350)
        lower.inputs[1].default_value = 41; upper.inputs[1].default_value = 47
        links.new(sep.outputs['Y'], lower.inputs[0]); links.new(sep.outputs['Y'], upper.inputs[0])
        both = nodes.new('ShaderNodeMath'); both.operation = 'MULTIPLY'; both.location = (-260, -230)
        links.new(lower.outputs[0], both.inputs[0]); links.new(upper.outputs[0], both.inputs[1])
        paint = nodes.new('ShaderNodeMixRGB'); paint.location = (20, -170)
        paint.inputs[1].default_value = linear_hex(code)
        paint.inputs[2].default_value = linear_hex(PALETTE['Safety_Amber'])
        links.new(both.outputs[0], paint.inputs[0]); links.new(paint.outputs[0], multiply.inputs[1])
    return mat


materials = [cel_material(name, code, stripe=name == 'Armor_Blue', glowing=name == 'Engine_Cyan') for name, code in PALETTE.items()]

# Preserve all source vertices except explicitly selected, small interior greebles.
study = bpy.data.scenes.new('Ship_Stylized_Study')
bpy.context.window.scene = study
ship_collection = bpy.data.collections.new('SS_01_Editable_Ship')
study.collection.children.link(ship_collection)
mesh = original.data.copy(); mesh.name = 'SS_Dreadnought_CleanMesh'
ship = bpy.data.objects.new('SS_Dreadnought_Stylized', mesh)
ship_collection.objects.link(ship)
mesh.transform(original.matrix_world)
ship.matrix_world = Matrix.Identity(4)
mesh.materials.clear()
for mat in materials:
    mesh.materials.append(mat)

bm = bmesh.new(); bm.from_mesh(mesh)
bm.normal_update()
seen = set(); islands = []
for vertex in bm.verts:
    if vertex in seen:
        continue
    stack = [vertex]; seen.add(vertex); vertices = []
    while stack:
        current = stack.pop(); vertices.append(current)
        for edge in current.link_edges:
            neighbor = edge.other_vert(current)
            if neighbor not in seen:
                seen.add(neighbor); stack.append(neighbor)
    faces = {face for vert in vertices for face in vert.link_faces}
    low = Vector([min(v.co[i] for v in vertices) for i in range(3)])
    high = Vector([max(v.co[i] for v in vertices) for i in range(3)])
    islands.append((vertices, faces, low, high, sum(f.calc_area() for f in faces)))

removed = []; removed_triangles = 0
for vertices, faces, low, high, area in islands:
    center = (low + high) * 0.5
    x, y, z = center
    dims = high - low
    small_interior = area < 25 and max(dims) < 8 and abs(x) < 85 and -255 < y < 130 and high.z < 27
    if small_interior:
        removed.extend(vertices)
        removed_triangles += sum(len(f.verts) - 2 for f in faces)
        continue
    material = 0
    if area < 450:
        material = 2
    if z < -10:
        material = 3
    if abs(x) > 45 and y > 12 and z > -6 and area > 900:
        material = 1
    if abs(x) < 13 and y < -55 and z > 13 and area > 650:
        material = 1
    if y > 72 and abs(x) < 31:
        material = 3
    if abs(x) > 89 and area > 900:
        material = 4
    if abs(x) < 10 and -271 < y < -220 and z > 14:
        material = 0
    if area < 200 and z > 14 and y < -60:
        material = 1
    for face in faces:
        face.material_index = 3 if face.normal.z < -0.65 and face.calc_center_median().z < -5 else material
        face.smooth = False

bmesh.ops.delete(bm, geom=removed, context='VERTS')
bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(2), use_dissolve_boundaries=False,
                         verts=list(bm.verts), edges=list(bm.edges), delimit={'MATERIAL'})
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
bm.to_mesh(mesh); bm.free(); mesh.update()
for face in mesh.polygons:
    center = face.center
    if face.normal.y > 0.97 and 126.85 < center.y < 127.0 and 14 < abs(center.x) < 31 and face.area > 20:
        face.material_index = 5
ship['source_asset'] = '/Game/Assets/Arma/CombatAvatarFly-01/StaticMeshes/SM_Dreadnought_Hull'
ship['style_notes'] = 'Original major shape retained; interior micro-detail reduced; six colors; EEVEE three-tone material.'
ship['source_triangles'] = sum(len(p.vertices) - 2 for p in original.data.polygons)
ship['study_triangles'] = sum(len(p.vertices) - 2 for p in mesh.polygons)

# Restore an inspectable, texture-based original without changing its mesh.
old_mat = bpy.data.materials.new('SS_Original_Texture_Reference')
old_mat.use_nodes = True
nodes, links = old_mat.node_tree.nodes, old_mat.node_tree.links
principled = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
principled.inputs['Roughness'].default_value = 0.65
for param in ('BaseColorTexture', 'EmissiveTexture', 'MetallicRoughnessTexture', 'NormalTexture'):
    img = bpy.data.images.load(str(SOURCE / (param + '.tga')), check_existing=True)
    node = nodes.new('ShaderNodeTexImage'); node.image = img; node.label = param
    if param == 'BaseColorTexture':
        node.location = (-700, 360); links.new(node.outputs['Color'], principled.inputs['Base Color'])
    elif param == 'EmissiveTexture':
        node.location = (-700, -500); links.new(node.outputs['Color'], principled.inputs['Emission Color'])
        principled.inputs['Emission Strength'].default_value = 0.5
    elif param == 'MetallicRoughnessTexture':
        img.colorspace_settings.name = 'Non-Color'; node.location = (-700, 80)
        separate = nodes.new('ShaderNodeSeparateColor'); separate.location = (-380, 60)
        links.new(node.outputs['Color'], separate.inputs[0])
        links.new(separate.outputs['Green'], principled.inputs['Roughness'])
        links.new(separate.outputs['Blue'], principled.inputs['Metallic'])
    else:
        img.colorspace_settings.name = 'Non-Color'; node.location = (-700, -200)
        normal = nodes.new('ShaderNodeNormalMap'); normal.location = (-350, -180)
        links.new(node.outputs['Color'], normal.inputs['Color']); links.new(normal.outputs['Normal'], principled.inputs['Normal'])
original.data.materials.clear(); original.data.materials.append(old_mat)


def point_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()


def configure_scene(scene):
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 1600; scene.render.resolution_y = 1150
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.film_transparent = False
    scene.unit_settings.system = 'METRIC'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene.world = bpy.data.worlds.new('SS_World_' + scene.name)
    scene.world.use_nodes = True
    bg = next(n for n in scene.world.node_tree.nodes if n.type == 'BACKGROUND')
    bg.inputs['Color'].default_value = (0.36, 0.43, 0.5, 1)
    bg.inputs['Strength'].default_value = 0.3


for scene in (study, original_scene):
    configure_scene(scene)

stage = bpy.data.collections.new('SS_02_Studio')
study.collection.children.link(stage); original_scene.collection.children.link(stage)
floor_mat = bpy.data.materials.new('SS_Studio_Floor'); floor_mat.diffuse_color = linear_hex('8FADB8')
floor_mat.use_nodes = True
floor_bsdf = next(n for n in floor_mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
floor_bsdf.inputs['Base Color'].default_value = linear_hex('8FADB8')
floor_bsdf.inputs['Roughness'].default_value = 1
floor_mesh = bpy.data.meshes.new('SS_Studio_Floor')
floor_mesh.from_pydata([(-2500,-2500,-44),(2500,-2500,-44),(2500,2500,-44),(-2500,2500,-44)], [], [(0,1,2,3)])
floor = bpy.data.objects.new('SS_Studio_Floor', floor_mesh); stage.objects.link(floor)
floor.scale = (10, 10, 1)
floor_mesh.materials.append(floor_mat)
sun_data = bpy.data.lights.new('SS_Key_Sun', 'SUN'); sun_data.energy = 2.5; sun_data.angle = math.radians(5)
sun = bpy.data.objects.new('SS_Key_Sun', sun_data); stage.objects.link(sun)
sun.location = (-300, -350, 500); point_at(sun, (0,-60,0))
fill_data = bpy.data.lights.new('SS_Fill_Sun', 'SUN'); fill_data.energy = 0.35; fill_data.angle = math.radians(18)
fill = bpy.data.objects.new('SS_Fill_Sun', fill_data); stage.objects.link(fill)
fill.location = (350,140,280); point_at(fill, (0,-60,0))

bpy.context.view_layer.update()
center = sum((ship.matrix_world @ Vector(c) for c in ship.bound_box), Vector()) / 8
camera_specs = {
    'SS_Camera_Hero': ((330,-480,330), 530),
    'SS_Camera_Top': ((0,-0.1,700), 530),
    'SS_Camera_Side': ((700,0,75), 530),
    'SS_Camera_Rear': ((330,480,300), 530),
}
for name, (offset, scale) in camera_specs.items():
    camera_data = bpy.data.cameras.new(name); camera_data.type = 'ORTHO'; camera_data.ortho_scale = scale
    camera_data.clip_end = 10000
    camera = bpy.data.objects.new(name, camera_data); stage.objects.link(camera)
    camera.location = center + Vector(offset); point_at(camera, center)
    if name == 'SS_Camera_Top':
        camera.location = center + Vector((0, 0, 700))
        camera.rotation_euler = (0, 0, math.pi / 2)
study.camera = bpy.data.objects['SS_Camera_Hero']
original_scene.camera = study.camera

# A dedicated comparison scene uses the same source and simplified mesh data.
comparison = bpy.data.scenes.new('Ship_Compare_Original_Stylized')
configure_scene(comparison)
comparison.collection.objects.link(sun); comparison.collection.objects.link(fill)
comparison.collection.objects.link(floor)
for name, obj, x_offset in [('Original', original, -150), ('Stylized', ship, 150)]:
    copy = obj.copy(); copy.name = 'SS_Compare_' + name
    comparison.collection.objects.link(copy)
    copy.matrix_world = obj.matrix_world.copy()
    copy.location.x += x_offset
comparison_camera_data = bpy.data.cameras.new('SS_Camera_Compare'); comparison_camera_data.type = 'ORTHO'
comparison_camera_data.ortho_scale = 850; comparison_camera_data.clip_end = 10000
comparison_camera = bpy.data.objects.new('SS_Camera_Compare', comparison_camera_data)
comparison.collection.objects.link(comparison_camera)
comparison_camera.location = center + Vector((0,-420,670)); point_at(comparison_camera, center)
comparison.camera = comparison_camera
comparison.render.resolution_x = 1800; comparison.render.resolution_y = 1050

# Save with the finished model in the open Blender viewport.
bpy.context.window.scene = study
bpy.ops.object.select_all(action='DESELECT')
ship.select_set(True); bpy.context.view_layer.objects.active = ship
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == 'VIEW_3D':
            space = area.spaces.active
            space.clip_end = 10000; space.overlay.show_overlays = False
            space.shading.type = 'RENDERED'
            space.region_3d.view_perspective = 'CAMERA'
            space.region_3d.view_camera_zoom = 5
            space.region_3d.view_location = center
            space.region_3d.view_distance = 650
            space.region_3d.view_rotation = study.camera.rotation_euler.to_quaternion()

for img in bpy.data.images:
    if img.source == 'FILE' and img.has_data:
        img.pack()
report = {
    'source': original.name, 'source_mesh': ship['source_asset'],
    'original_triangles': ship['source_triangles'], 'study_triangles': ship['study_triangles'],
    'removed_interior_detail_triangles': removed_triangles,
    'original_dimensions_m': list(original.dimensions), 'study_dimensions_m': list(ship.dimensions),
    'palette_srgb': PALETTE, 'render_engine': study.render.engine,
    'scenes': [study.name, original_scene.name, comparison.name],
    'shape_policy': 'No global decimation or proportion edits; small interior islands removed; planar edges dissolved at 2 degrees.',
}
(ROOT/'study_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'GuLiStrike_Ship_AnimeStudy.blend'))
result = report

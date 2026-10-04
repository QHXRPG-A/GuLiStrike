"""Stage A material/camera study on source mesh; NO remodeling or decimation."""
import bpy
import json
import math
from collections import defaultdict
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT / 'References_A_v1'
OUT.mkdir(parents=True, exist_ok=True)
if (OUT / 'reference_manifest.json').exists():
    raise RuntimeError('Published A-v1 is frozen. Create a sibling version before revising reference images.')
scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == 'MESH']
palette = {'WarmWhite': '#F1E7D5', 'Coral': '#E97868', 'Chassis': '#48413E',
           'Steel': '#756A60', 'Amber': '#F4B65C', 'Ink': '#241F20'}


def linear(hexcode):
    a = [int(hexcode[i:i+2], 16)/255 for i in (1,3,5)]
    return tuple(v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in a)


def toon(name, color):
    mat = bpy.data.materials.new('Reference_' + name)
    mat.use_nodes = True
    mat.diffuse_color = (*color, 1)
    nt = mat.node_tree
    nt.nodes.clear()
    output = nt.nodes.new('ShaderNodeOutputMaterial')
    emission = nt.nodes.new('ShaderNodeEmission')
    geom = nt.nodes.new('ShaderNodeNewGeometry')
    dot = nt.nodes.new('ShaderNodeVectorMath')
    dot.operation = 'DOT_PRODUCT'
    dot.inputs[1].default_value = Vector((.35, -.55, .76)).normalized()
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.interpolation = 'CONSTANT'
    cr = ramp.color_ramp
    cr.elements.remove(cr.elements[1])
    for i, (threshold, factor) in enumerate([(0, .42), (.12, .74), (.55, 1)]):
        e = cr.elements[0] if i == 0 else cr.elements.new(threshold)
        e.position = threshold
        e.color = (*[c*factor for c in color], 1)
    nt.links.new(geom.outputs['Normal'], dot.inputs[0])
    nt.links.new(dot.outputs['Value'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], emission.inputs['Color'])
    nt.links.new(emission.outputs[0], output.inputs['Surface'])
    return mat


materials = {name: toon(name, linear(hexcode)) for name, hexcode in palette.items()}
material_index = {name: i for i, name in enumerate(materials)}
parts = json.loads((ROOT / 'Baseline/source_connected_parts.json').read_text())
assignments = []
normal_study = []
for obj in meshes:
    # Presentation normals only: smooth regular curves across FBX split seams.
    # Original FBX/gray baseline are retained; positions/topology/rig never change.
    data = obj.data
    incident = defaultdict(list)
    for poly in data.polygons:
        for vi in poly.vertices:
            key = tuple(round(a,5) for a in data.vertices[vi].co)
            incident[key].append((poly.normal.copy(), math.sqrt(max(poly.area, 1e-10))))
    normals = [(0,0,0)]*len(data.loops)
    for poly in data.polygons:
        n = poly.normal.copy()
        poly.use_smooth = True
        for li in poly.loop_indices:
            key = tuple(round(a,5) for a in data.vertices[data.loops[li].vertex_index].co)
            normal = Vector((0,0,0))
            for neighbor, weight in incident[key]:
                if n.dot(neighbor) > math.cos(math.radians(42)):
                    normal += neighbor*weight
            normals[li] = tuple(normal.normalized()) if normal.length_squared else tuple(n)
    data.normals_split_custom_set(normals)
    normal_study.append({'object': obj.name, 'mode': 'reference-only angle-limited normals',
                         'angle_degrees': 42, 'positions_and_topology_unchanged': True})
    obj.data.materials.clear()
    for mat in materials.values():
        obj.data.materials.append(mat)
    for p in [p for p in parts if p['object'] == obj.name]:
        bone = p['dominant_bone'] or ''
        x, y, z = p['center_m']
        dx, dy, dz = p['dimensions_m']
        size = max(dx,dy,dz)
        cid = p['component']
        color = 'Chassis'
        if bone.startswith(('FrontLeg', 'MiddleLeg', 'BackLeg')):
            if 'Leg3_' in bone and p['vertex_count'] in (299, 298, 300):
                color = 'WarmWhite'  # six main shin armor shells
            elif 'Leg3_' in bone and 1.08 < dz < 1.25 and size > 1.05:
                color = 'Coral'  # six knee armor shells
            elif 'Leg3_' in bone and .84 < dz < .96 and p['vertex_count'] > 200:
                color = 'Coral'  # six shin service inserts
            elif 'Leg4_' in bone and dz > 1.0:
                color = 'Coral'  # toe guards
            elif 'Leg4_' in bone and p['vertex_count'] > 500:
                color = 'Steel'  # ankle bearings
            elif 'Leg3_' in bone and .48 < dz < .6 and p['vertex_count'] > 250:
                color = 'Steel'  # knee bearings
        elif bone.startswith('Shotgun'):
            if dy > .9 and dz > .9 and dx > .5:
                color = 'WarmWhite' if y > -2.0 else 'Chassis'
            elif dx > .8 and dy < .5 and dz > 1:
                color = 'Coral'  # large outboard receiver side plates
            elif dx > .45 and dy > .7 and dz > .8 and y > -2.0:
                color = 'Coral'
            elif y < -3.9:
                color = 'Steel'
            elif size < .4:
                color = 'Steel'
        elif bone == 'Top_M':
            if cid in (77, 10127, 8853, 1508, 5249, 28551, 6334, 7046):
                color = 'WarmWhite'
            elif cid in (3280, 859, 942, 4518, 2724, 7520, 4012, 6571):
                color = 'Coral'
            elif cid in (3152, 3187, 6721, 6905):
                color = 'Amber'
            elif p['vertex_count'] > 90 and size < .75:
                color = 'Steel'
            if z > 5.5 and dz > 1:
                color = 'Chassis'  # preserve two source antennae
        for pi in p['polygon_ids']:
            poly = obj.data.polygons[pi]
            chosen = color
            normal_world = obj.matrix_world.to_3x3() @ poly.normal
            # Proposed status window uses an existing top panel surface.
            if cid == 6571 and normal_world.y < -.65:
                chosen = 'Amber'
            poly.material_index = material_index[chosen]
        assignments.append({'component': cid, 'bone': bone, 'base_color': color})

# Enforce matching L/R color assignments from the physical component locations.
assignment_map = {a['component']: a for a in assignments}
symmetry = {'paired_components': 0, 'corrected_color_pairs': []}
left = [p for p in parts if p['center_m'][0] > .01]
for p in [p for p in parts if p['center_m'][0] < -.01]:
    x,y,z = p['center_m']
    target_bone = (p['dominant_bone'] or '').replace('_R','_L')
    matches = [q for q in left if q['dominant_bone'] == target_bone
               and max(abs(a-b) for a,b in zip(q['center_m'], (-x,y,z))) < .002
               and max(abs(a-b) for a,b in zip(q['dimensions_m'], p['dimensions_m'])) < .002]
    if len(matches) != 1:
        continue
    q = matches[0]
    symmetry['paired_components'] += 1
    color = assignment_map[q['component']]['base_color']
    a = assignment_map[p['component']]
    if a['base_color'] != color:
        symmetry['corrected_color_pairs'].append([p['component'],q['component']])
        a['base_color'] = color
        obj = bpy.data.objects[p['object']]
        for pi in p['polygon_ids']:
            obj.data.polygons[pi].material_index = material_index[color]

scene.render.resolution_x = scene.render.resolution_y = 2048
scene.render.resolution_percentage = 100
scene.render.engine = 'BLENDER_EEVEE'
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
world = scene.world.node_tree.nodes['Background']
world.inputs['Color'].default_value = (*linear('#F7F2E8'), 1)
world.inputs['Strength'].default_value = 1
scene.render.use_freestyle = True
layer = scene.view_layers[0]
fs = layer.freestyle_settings
fs.crease_angle = math.radians(100)
ls = fs.linesets[0] if fs.linesets else fs.linesets.new('StructureLines')
ls.select_silhouette = True
ls.select_border = False
ls.select_crease = True
ls.select_external_contour = True
ls.select_material_boundary = True
ls.select_edge_mark = False
ls.select_suggestive_contour = False
ls.select_ridge_valley = False
if ls.linestyle is None:
    ls.linestyle = bpy.data.linestyles.new('ReferenceStructuralInk')
ls.linestyle.color = linear(palette['Ink'])
ls.linestyle.thickness = 1.65
outer = fs.linesets.new('OuterContour')
outer.select_silhouette = True
outer.select_border = False
outer.select_crease = False
outer.select_external_contour = True
outer.select_material_boundary = False
outer.linestyle = bpy.data.linestyles.new('ReferenceOuterInk')
outer.linestyle.color = linear(palette['Ink'])
outer.linestyle.thickness = 2.2

points = [o.matrix_world @ Vector(v) for o in meshes for v in o.bound_box]
minimum = Vector([min(p[a] for p in points) for a in range(3)])
maximum = Vector([max(p[a] for p in points) for a in range(3)])
center = (minimum + maximum)*.5
camera = scene.camera
camera.data.type = 'ORTHO'
camera.data.ortho_scale = max(maximum-minimum)*1.22
views = {'Front': (0,-1,0), 'Left': (1,0,0), 'Back': (0,1,0), 'Hero': (.85,-1.1,.65)}
camera_report = {}
for name, direction in views.items():
    camera.location = center+Vector(direction).normalized()*25
    camera.rotation_euler = (center-camera.location).to_track_quat('-Z','Y').to_euler()
    camera_report[name] = {'location_m': list(camera.location),
                           'rotation_radians': list(camera.rotation_euler),
                           'ortho_scale_m': camera.data.ortho_scale}
    scene.render.filepath = str(OUT / f'RSG_A_v1_{name}_SourceStyle.png')
    bpy.ops.render.render(write_still=True)

bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'RSG_A_v1_ReferenceMaterialStudy.blend'))

# Also export the exact same grey baseline cameras at final resolution.
gray = bpy.data.materials.get('SourceInspection_Gray')
for obj in meshes:
    for slot in obj.material_slots:
        slot.material = gray
scene.render.use_freestyle = False
world.inputs['Color'].default_value = (0.8,0.8,0.8,1)
world.inputs['Strength'].default_value = .6
for name, pose in camera_report.items():
    camera.location = pose['location_m']
    camera.rotation_euler = pose['rotation_radians']
    scene.render.filepath = str(ROOT / f'Baseline/RSG_Source_{name}_2048.png')
    bpy.ops.render.render(write_still=True)

(OUT / 'reference_setup.json').write_text(json.dumps({
    'version': 'A-v1', 'stage': 'reference_material_study', 'palette_srgb': palette,
    'pose': 'original UE reference pose', 'orthographic_scale_m': camera.data.ortho_scale,
    'cameras': camera_report, 'component_color_assignments': assignments,
    'presentation_normals': normal_study, 'symmetric_palette_check': symmetry,
    'geometry_changed': False, 'decimation_applied': False,
    'production_model': False, 'A_approval': 'pending', 'B_approval': 'not_started'
}, indent=2), encoding='utf-8')
print('Stage A source-based color references rendered at 2048x2048', flush=True)

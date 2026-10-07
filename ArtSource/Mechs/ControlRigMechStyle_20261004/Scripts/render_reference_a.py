"""Source-based Stage A design references. Changes materials/lines only, no remodeling."""
import bpy
import hashlib
import json
import math
import struct
from collections import Counter
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
OUT = ROOT / 'References_A_v1'
OUT.mkdir(parents=True, exist_ok=True)
if (OUT / 'reference_manifest.json').exists():
    raise RuntimeError('Published A-v1 is frozen; revise into a sibling version.')
bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'Baseline/ControlRigMech_SourceInspection.blend'))
scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == 'MESH']
parts = json.loads((ROOT / 'Baseline/source_connected_parts.json').read_text())
source_info = json.loads((ROOT / 'Baseline/blender_source_inspection.json').read_text())
palette = {'WarmWhite': '#F1E7D5', 'Coral': '#E97868', 'Chassis': '#48413E',
           'Steel': '#756A60', 'Amber': '#F4B65C', 'Ink': '#241F20'}


def linear(hexcode):
    srgb = [int(hexcode[i:i+2], 16)/255 for i in (1,3,5)]
    return tuple(v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in srgb)


def geometry_hash(obj):
    h = hashlib.sha256()
    for v in obj.data.vertices:
        h.update(struct.pack('<3f', *v.co))
    for p in obj.data.polygons:
        h.update(struct.pack('<I', len(p.vertices)))
        h.update(struct.pack('<' + 'I'*len(p.vertices), *p.vertices))
    for v in obj.data.vertices:
        for g in v.groups:
            h.update(struct.pack('<IIf', v.index, g.group, g.weight))
    return h.hexdigest()


def toon(name, hexcode):
    color = linear(hexcode)
    mat = bpy.data.materials.new('A_Reference_' + name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
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
        e.color = (*[c * factor for c in color], 1)
    nt.links.new(geom.outputs['Normal'], dot.inputs[0])
    nt.links.new(dot.outputs['Value'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], emission.inputs['Color'])
    nt.links.new(emission.outputs[0], output.inputs['Surface'])
    return mat


materials = {name: toon(name, code) for name, code in palette.items()}
transparent = bpy.data.materials.new('A_Reference_RemoveOriginalGrimeAndDecalCards')
transparent.use_nodes = True
transparent.node_tree.nodes.clear()
outnode = transparent.node_tree.nodes.new('ShaderNodeOutputMaterial')
transnode = transparent.node_tree.nodes.new('ShaderNodeBsdfTransparent')
transparent.node_tree.links.new(transnode.outputs[0], outnode.inputs['Surface'])
transparent.diffuse_color = (1,1,1,0)
materials['OriginalDecalsSuppressed'] = transparent
material_index = {name: i for i, name in enumerate(materials)}
before = {o.name: geometry_hash(o) for o in meshes}
assignments = []
triangle_colors = Counter()
for obj in meshes:
    original_indices = [int(p.material_index) for p in obj.data.polygons]
    face_marks = obj.data.attributes.get('freestyle_face') or obj.data.attributes.new('freestyle_face', 'BOOLEAN', 'FACE')
    # The gray inspection file retained original material indices; its slots all point to gray.
    obj.data.materials.clear()
    for mat in materials.values():
        obj.data.materials.append(mat)
    for part in (p for p in parts if p['object'] == obj.name):
        bone = part['dominant_bone'] or ''
        x,y,z = part['center_m']
        dx,dy,dz = part['dimensions_m']
        choices = Counter()
        for pi in part['polygon_ids']:
            p = obj.data.polygons[pi]
            original = original_indices[pi]
            color = 'Chassis'
            if original in (0,1,12,13):
                color = 'OriginalDecalsSuppressed'
            elif original == 5:
                color = 'Amber'
            elif original in (3,9,10):
                color = 'Steel'
            elif original in (2,7):
                color = 'WarmWhite'
                if bone.startswith('antenna'):
                    color = 'Chassis'
                elif bone.startswith('cannon_03'):
                    color = 'Coral'
                elif bone == 'turret_base' and abs(x)>1 and dy>1.8 and dz>.6:
                    color = 'Coral'  # paired existing side service/vent housings
                elif bone.startswith('pistonbase_') or bone.startswith('leg_') and '_04_' in bone:
                    color = 'Coral'  # four shin/ankle service armor groups
                elif bone == 'base' and z<1.9:
                    color = 'Chassis'
                elif bone.startswith('piston_') and max(dx,dy,dz)>.4:
                    color = 'Coral'
            elif original == 8 and bone.startswith('leg_') and .6<dz<1.05 and max(dx,dy)<1.1:
                color = 'Steel'  # circular leg bearing caps remain readable
            p.material_index = material_index[color]
            face_marks.data[pi].value = original not in (0,1,12,13)
            choices[color] += 1
            triangle_colors[color] += max(len(p.vertices)-2,0)
        assignments.append({'component': part['component'], 'dominant_bone': bone,
                            'colors_by_polygon_count': dict(choices)})
after = {o.name: geometry_hash(o) for o in meshes}
assert before == after, 'Reference setup changed positions, topology or weights'

scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = scene.render.resolution_y = 2048
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
world = scene.world.node_tree.nodes['Background']
world.inputs['Color'].default_value = (*linear('#F7F2E8'),1)
world.inputs['Strength'].default_value = 1
scene.render.use_freestyle = True
fs = scene.view_layers[0].freestyle_settings
fs.crease_angle = math.radians(100)
ls = fs.linesets[0] if fs.linesets else fs.linesets.new('StructuralLines')
ls.select_silhouette = False
ls.select_border = False
ls.select_crease = True
ls.select_external_contour = False
ls.select_material_boundary = True
ls.select_edge_mark = False
ls.select_suggestive_contour = False
ls.select_ridge_valley = False
ls.select_by_face_marks = True
ls.face_mark_condition = 'ONE'
ls.face_mark_negation = 'INCLUSIVE'
if ls.linestyle is None:
    ls.linestyle = bpy.data.linestyles.new('A_Reference_StructuralInk')
ls.linestyle.color = linear(palette['Ink'])
ls.linestyle.thickness = 1.4
outer = fs.linesets.new('OuterContour')
outer.select_silhouette = True
outer.select_border = False
outer.select_crease = False
outer.select_external_contour = True
outer.select_material_boundary = False
outer.select_by_face_marks = True
outer.face_mark_condition = 'ONE'
outer.face_mark_negation = 'INCLUSIVE'
outer.linestyle = bpy.data.linestyles.new('A_Reference_OuterInk')
outer.linestyle.color = linear(palette['Ink'])
outer.linestyle.thickness = 2.25

minimum = Vector(source_info['bounds_m']['min'])
maximum = Vector(source_info['bounds_m']['max'])
center = (minimum+maximum)*.5
size = max(maximum-minimum)
camera = scene.camera
camera.data.type = 'ORTHO'
camera.data.ortho_scale = size * 1.22
views = {'Hero': (.85,-1.1,.65), 'Front': (0,-1,0), 'Left': (1,0,0), 'Back': (0,1,0)}
camera_report = {}
for name, direction in views.items():
    camera.location = center + Vector(direction).normalized()*size*4
    camera.rotation_euler = (center-camera.location).to_track_quat('-Z','Y').to_euler()
    camera_report[name] = {'type': 'ORTHO', 'location_m': list(camera.location),
                           'rotation_radians': list(camera.rotation_euler),
                           'ortho_scale_m': camera.data.ortho_scale}
    scene.render.filepath = str(OUT / f'ControlRigMech_A_v1_{name}.png')
    bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ControlRigMech_A_v1_ReferenceStudy.blend'))

# Gray comparisons retain exactly the same geometry, scale, pose and four cameras.
gray = bpy.data.materials.get('SourceInspection_Gray')
bsdf = next(n for n in gray.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
bsdf.inputs['Base Color'].default_value = (.24,.24,.24,1)
for o in meshes:
    for slot in o.material_slots:
        slot.material = gray
scene.render.use_freestyle = False
world.inputs['Color'].default_value = (*linear('#F7F2E8'),1)
world.inputs['Strength'].default_value = 1
for name, config in camera_report.items():
    camera.location = config['location_m']
    camera.rotation_euler = config['rotation_radians']
    scene.render.filepath = str(ROOT / f'Baseline/ControlRigMech_Source_{name}_2048.png')
    bpy.ops.render.render(write_still=True)
setup = {
    'version': 'A-v1', 'stage': 'source_based_reference_design',
    'production_model': False, 'A_approval': 'pending', 'B_approval': 'not_started',
    'palette_srgb': palette, 'light_world': [.35,-.55,.76],
    'tone_thresholds': [0,.12,.55], 'tone_linear_factors': [.42,.74,1],
    'pose': 'original UE skeletal mesh reference pose, gun elevated as in source',
    'same_scale_all_views': True, 'cameras': camera_report,
    'geometry_skinweight_hash_before': before, 'geometry_skinweight_hash_after': after,
    'geometry_changed': False, 'decimation_applied': False,
    'normals': 'source imported custom normals, unchanged',
    'original_decals': 'source grime/normal/decal cards suppressed only by transparent material',
    'outline_method_for_reference': 'Freestyle; production will use the approved simplified outline shell',
    'line_widths_px': {'internal': 1.4, 'silhouette': 2.25},
    'component_color_assignments': assignments, 'reference_triangles_by_color': dict(triangle_colors),
    'production_lod_budgets_not_yet_implemented': True,
}
(OUT / 'reference_setup.json').write_text(json.dumps(setup, ensure_ascii=False, indent=2), encoding='utf-8')
print('CONTROLRIG_REFERENCE_A_RENDER_OK', flush=True)

"""Versioned Blender-only hub-seat revision and real-model review renders.

Run in Blender Python. Does not export FBX, touch UE, or generate card art.
"""
import bpy
import bmesh
import hashlib
import json
import math
import sys
from pathlib import Path

from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

ROOT = Path('D:/UE5.7/test1')
SCRIPTS = ROOT / 'Scripts/Blender'
sys.path.insert(0, str(SCRIPTS))
import build_tactical_from_drawings as author
import war_machine_reference as design
import package_tactical_handbuilt as package
import build_tactical_cel_pass as cel

SOURCE = ROOT / 'ArtSource/TacticalStyle_20260916/Production_Handbuilt'
SOURCE_EDITABLE = SOURCE / 'WarMachine_Handbuilt_Editable.blend'
OUT = ROOT / 'ArtSource/TacticalStyle_20260916/WarMachine_HubSeat_v1'
PREVIEWS = OUT / 'Previews'
EDITABLE = OUT / 'WarMachine_HubSeat_Editable.blend'
REVIEW = OUT / 'WarMachine_HubSeat_Review.blend'
REPORT = json.loads((SOURCE / 'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
SCALE = REPORT['source_scale']
OFFSET = Vector(REPORT['source_offset'])
CHANGED_PREFIXES = ('WM suspension diagonal_', 'WM suspension toe ', 'WM toe amber optic_')
EXPECTED_REPLACEMENTS = 28
REVISION_DESCRIPTION = 'Four ankle soles seated on original hub caps'


def log(message):
    print('HUB_SEAT: ' + message, flush=True)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')


def mesh_parts():
    return [o for o in bpy.context.scene.objects if o.type == 'MESH']


def signature(o):
    values = {'vertices': [list(v.co) for v in o.data.vertices],
              'faces': [(list(p.vertices), p.material_index) for p in o.data.polygons],
              'matrix': [list(row) for row in o.matrix_world],
              'modifiers': [(m.type, getattr(m, 'width', None)) for m in o.modifiers]}
    return hashlib.sha256(json.dumps(values, separators=(',', ':')).encode()).hexdigest()


def world(p):
    return (Vector(p) + OFFSET) * SCALE


def prepare():
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE_EDITABLE))
    before = mesh_parts()
    initial = {o.name: signature(o) for o in before}
    lo0, hi0, _, _ = author.evaluated_stats(before)
    old_names = [o.name for o in before if o.name.startswith(CHANGED_PREFIXES)]
    assert len(old_names) == EXPECTED_REPLACEMENTS, old_names
    for name in old_names:
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
    author.PARTS = []
    for label, x, inner in [('F', 2.75, .63), ('R', -2.82, -1.60)]:
        design.build_hover_leg_connection(author, label, x, inner)
    for o in list(author.PARTS):
        author.mirror(o)
    transform = Matrix.Scale(SCALE, 4) @ Matrix.Translation(OFFSET)
    for o in author.PARTS:
        o.data.transform(transform)
        for mod in o.modifiers:
            if mod.type == 'BEVEL':
                mod.width *= SCALE
    bpy.context.view_layer.update()
    after = mesh_parts()
    kept = [name for name in initial if name not in old_names]
    unchanged = all(signature(bpy.data.objects[name]) == initial[name] for name in kept)
    disc_names = [name for name in kept if name.startswith('WM hover ')]
    lo1, hi1, tris, symmetry = author.evaluated_stats(after)
    dg = bpy.context.evaluated_depsgraph_get()

    def evaluated(o):
        ev = o.evaluated_get(dg)
        mesh = ev.to_mesh()
        vertices = [o.matrix_world @ v.co for v in mesh.vertices]
        faces = [list(p.vertices) for p in mesh.polygons]
        ev.to_mesh_clear()
        return vertices, faces

    contacts = []
    intersections = []
    for corner in ('F_L', 'F_R', 'R_L', 'R_R'):
        cap = bpy.data.objects['WM hover hub cap_' + corner]
        seat = bpy.data.objects['WM suspension hub seat_' + corner]
        cap_v, _ = evaluated(cap)
        seat_v, _ = evaluated(seat)
        top = max(v.z for v in cap_v)
        bottom = min(v.z for v in seat_v)
        center_cap = sum(cap_v, Vector()) / len(cap_v)
        center_seat = sum(seat_v, Vector()) / len(seat_v)
        contacts.append({'corner': corner, 'cap_top_m': top, 'seat_bottom_m': bottom,
                         'vertical_gap_m': bottom - top,
                         'center_error_xy_m': math.hypot(center_cap.x-center_seat.x, center_cap.y-center_seat.y),
                         'contact_radius_author_units': .485,
                         'cap_radius_author_units': .46 * 1.10})
        discs = [o for o in after if o.name.startswith('WM hover ') and corner in o.name]
        legs = [o for o in after if o.name.startswith(CHANGED_PREFIXES) and corner in o.name]
        for leg in legs:
            lv, lf = evaluated(leg)
            lt = BVHTree.FromPolygons(lv, lf)
            for disc in discs:
                if leg.name == 'WM suspension hub seat_' + corner and disc.name == 'WM hover hub cap_' + corner:
                    continue  # Deliberate coincident bearing faces, checked by contact heights.
                dv, df = evaluated(disc)
                overlaps = lt.overlap(BVHTree.FromPolygons(dv, df))
                if overlaps:
                    intersections.append({'leg': leg.name, 'disc': disc.name, 'triangle_pairs': len(overlaps)})
    result = {'source': str(SOURCE_EDITABLE),
              'editable': str(EDITABLE), 'old_parts': len(before), 'new_parts': len(after),
              'replaced_parts': old_names,
              'added_parts': [o.name for o in after if o.name not in initial],
              'regenerated_but_identical': [n for n in old_names if signature(bpy.data.objects[n]) == initial[n]],
              'unchanged_parts_checked': len(kept), 'unchanged_parts_identical': unchanged,
              'unchanged_hover_parts': disc_names, 'unchanged_hover_parts_count': len(disc_names),
              'bounds_before_m': [list(lo0), list(hi0)], 'bounds_after_m': [list(lo1), list(hi1)],
              'bounds_max_delta_m': max((lo1-lo0).length, (hi1-hi0).length),
              'symmetry_error_m': symmetry, 'triangles_evaluated': tris,
              'contacts': contacts, 'leg_disc_surface_intersections': intersections,
              'sockets_m': REPORT['sockets_m'],
              'art_review': 'pending_user_review', 'ue_import': 'not_performed',
              'card_generation': 'waiting_for_model_approval'}
    write_json(OUT / 'geometry-check.json', result)
    assert unchanged, 'An out-of-scope part changed'
    assert result['bounds_max_delta_m'] < 1e-4
    assert symmetry < 1e-4
    assert all(abs(c['vertical_gap_m']) < 1e-4 and c['center_error_xy_m'] < 1e-4 for c in contacts)
    assert not intersections, intersections
    bpy.context.scene.name = 'WarMachine_HubSeat_Editable'
    bpy.context.scene['revision'] = REVISION_DESCRIPTION
    bpy.context.scene['approval'] = 'Pending user model review; no UE import'
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(EDITABLE), check_existing=False)
    write_json(OUT / 'WarMachine_handbuilt_report.json', dict(REPORT, parts=len(after), triangles=tris,
               design_revision=REVISION_DESCRIPTION))
    log('Editable revision saved; four contacts and unchanged geometry checked')


def set_camera(scene, name, target, direction, span, resolution=(1400, 1000)):
    target, direction = Vector(target), Vector(direction).normalized()
    camera = bpy.data.objects.get(name)
    if camera is None:
        data = bpy.data.cameras.new(name)
        camera = bpy.data.objects.new(name, data)
        scene.collection.objects.link(camera)
    camera.location = target + direction * 140
    camera.rotation_euler = (target-camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = span
    camera.data.clip_end = 2000
    scene.camera = camera
    scene.render.resolution_x, scene.render.resolution_y = resolution
    return camera


def configure_scene(scene):
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.film_transparent = False
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '8'
    if hasattr(scene, 'eevee'):
        scene.eevee.taa_render_samples = 32
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    scene.world = bpy.data.worlds.new('HubSeat Review Background')
    scene.world.use_nodes = True
    background = next(n for n in scene.world.node_tree.nodes if n.type == 'BACKGROUND')
    background.inputs['Color'].default_value = (.035, .05, .075, 1)
    background.inputs['Strength'].default_value = .7


def render(scene, filename):
    scene.render.filepath = str(PREVIEWS / filename)
    log('Rendering ' + filename)
    bpy.ops.render.render(write_still=True)


def isolate_front_right(body, name):
    subset = body.copy()
    subset.data = body.data.copy()
    subset.name = name
    bpy.context.scene.collection.objects.link(subset)
    groups = {g.index for g in subset.vertex_groups if g.name.startswith('ReviewPart::') and 'F_R' in g.name}
    keep = {v.index for v in subset.data.vertices if any(g.group in groups for g in v.groups)}
    bm = bmesh.new()
    bm.from_mesh(subset.data)
    bm.verts.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context='VERTS')
    bm.to_mesh(subset.data)
    bm.free()
    subset.data.update()
    return subset


def package_review(stage):
    path = SOURCE_EDITABLE if stage == 'Before' else EDITABLE
    bpy.ops.wm.open_mainfile(filepath=str(path))
    scene = bpy.context.scene
    scene.name = 'WarMachine_HubSeat_' + stage
    for o in list(scene.objects):
        if o.type in {'LIGHT', 'CAMERA'}:
            bpy.data.objects.remove(o, do_unlink=True)
    parts = mesh_parts()
    for o in parts:
        package.apply_modifiers(o)
        group = o.vertex_groups.new(name='Body')
        group.add(list(range(len(o.data.vertices))), 1, 'REPLACE')
        tag = o.vertex_groups.new(name='ReviewPart::' + o.name)
        tag.add(list(range(len(o.data.vertices))), 1, 'REPLACE')
    package.remove_hidden_faces(parts, SCALE)
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    body = bpy.context.object
    body.name = 'WarMachine_' + stage + '_Body'
    slots = [int(m.name.split('_')[-1].split('.')[0]) for m in body.data.materials]
    for p in body.data.polygons:
        p.material_index = slots[p.material_index]
    output = OUT / 'ReviewData' / stage
    output.mkdir(parents=True, exist_ok=True)
    package.ROOT = output
    package.atlas(body, REPORT, 'WarMachine')
    cel.SOURCE = output
    cel.OUT = output
    atlas, mask, details = cel.ink_mask(body, 'WarMachine', SCALE)
    body.data.materials[0] = cel.cel_material('WarMachine_' + stage, atlas, mask)
    shell = cel.outline(body, 'WarMachine_' + stage, SCALE)
    shell.name = 'WarMachine_' + stage + '_Contour'
    detail = isolate_front_right(body, 'Connection_' + stage + '_Body')
    detail_shell = cel.outline(detail, 'Connection_' + stage, SCALE)
    detail_shell.name = 'Connection_' + stage + '_Contour'
    configure_scene(scene)
    center = world((0, 0, 2.45))
    target = world((2.60, -2.38, .95))
    cameras = [
        ('Hero', center, (1.25, -1.85, 1.20), 88, (1500, 1125)),
        ('Connection_Oblique', target, (1.15, -1.65, .90), 27.5, (1400, 1050)),
        ('Connection_Side', world((2.25, -2.75, .95)), (1, 0, .015), 30, (1400, 1050)),
        ('Connection_Top', world((2.25, -2.75, .95)), (0, 0, 1), 34, (1400, 1050)),
    ]
    for label, aim, direction, span, res in cameras:
        is_full = label == 'Hero'
        body.hide_render = shell.hide_render = not is_full
        detail.hide_render = detail_shell.hide_render = is_full
        set_camera(scene, 'Review_' + label, aim, direction, span, res)
        render(scene, stage + '_' + label + '.png')
    body.hide_render = shell.hide_render = False
    detail.hide_render = detail_shell.hide_render = True
    detail.hide_set(True)
    detail_shell.hide_set(True)
    scene.camera = bpy.data.objects['Review_Hero']
    bpy.ops.object.select_all(action='DESELECT')
    for area in bpy.context.screen.areas if bpy.context.screen else []:
        if area.type == 'VIEW_3D':
            space = area.spaces.active
            space.shading.type = 'MATERIAL'
            space.overlay.show_overlays = False
            space.region_3d.view_perspective = 'CAMERA'
    scene['approval'] = 'Pending user model review'
    save_path = OUT / ('WarMachine_Before_Review.blend' if stage == 'Before' else REVIEW.name)
    bpy.ops.wm.save_as_mainfile(filepath=str(save_path), check_existing=False)
    write_json(output / 'cel-review.json', details)
    log(stage + ' model review saved')


def comparison():
    bpy.ops.wm.open_mainfile(filepath=str(REVIEW))
    oldpath = OUT / 'WarMachine_Before_Review.blend'
    with bpy.data.libraries.load(str(oldpath), link=False) as (source, target):
        target.objects = [n for n in source.objects if n in ('Connection_Before_Body', 'Connection_Before_Contour')]
    scene = bpy.data.scenes.new('Connection_Before_After')
    configure_scene(scene)
    camera = set_camera(scene, 'Comparison_Camera', (0, 0, 0), (1.15, -1.65, .90), 57, (1800, 1100))
    rotation = camera.rotation_euler.to_quaternion()
    right = rotation @ Vector((1, 0, 0))
    up = rotation @ Vector((0, 1, 0))
    connection_center = world((2.60, -2.38, .95))
    for stage, sign in [('Before', -1), ('After', 1)]:
        for suffix in ('Body', 'Contour'):
            source = bpy.data.objects['Connection_' + stage + '_' + suffix]
            copy = source.copy()
            copy.name = 'Compare_' + stage + '_' + suffix
            scene.collection.objects.link(copy)
            copy.hide_render = False
            copy.hide_set(False)
            copy.location = -connection_center + right * sign * 14.0
        data = bpy.data.curves.new('Label_' + stage, 'FONT')
        data.body = 'Before' if stage == 'Before' else 'After'
        data.align_x = 'CENTER'
        data.size = 1.4
        label = bpy.data.objects.new('Label_' + stage, data)
        scene.collection.objects.link(label)
        label.rotation_euler = camera.rotation_euler
        label.location = right * sign * 14.0 + up * 14.4
        material = bpy.data.materials.new('Label_' + stage)
        material.use_nodes = True
        n, links = material.node_tree.nodes, material.node_tree.links
        n.clear()
        emission = n.new('ShaderNodeEmission')
        emission.inputs['Color'].default_value = (.82, .9, 1, 1)
        output = n.new('ShaderNodeOutputMaterial')
        links.new(emission.outputs[0], output.inputs[0])
        data.materials.append(material)
    bpy.context.window.scene = scene
    render(scene, 'Connection_Before_After.png')
    bpy.context.window.scene = bpy.data.scenes['WarMachine_HubSeat_After']
    bpy.ops.wm.save_as_mainfile(filepath=str(REVIEW), check_existing=False)
    log('Comparison and final review saved')


def reframe_details():
    """Reframe only the two orthographic details, without rebuilding meshes."""
    for stage, path in [('Before', OUT / 'WarMachine_Before_Review.blend'), ('After', REVIEW)]:
        bpy.ops.wm.open_mainfile(filepath=str(path))
        scene = bpy.data.scenes['WarMachine_HubSeat_' + stage]
        bpy.context.window.scene = scene
        for suffix in ('Body', 'Contour'):
            bpy.data.objects['WarMachine_' + stage + '_' + suffix].hide_render = True
            detail = bpy.data.objects['Connection_' + stage + '_' + suffix]
            detail.hide_render = False
            detail.hide_set(False)
        for label, direction, span in [('Side', (1, 0, .015), 30), ('Top', (0, 0, 1), 34)]:
            set_camera(scene, 'Review_Connection_' + label, world((2.25, -2.75, .95)), direction, span, (1400, 1050))
            render(scene, stage + '_Connection_' + label + '.png')
        for suffix in ('Body', 'Contour'):
            bpy.data.objects['WarMachine_' + stage + '_' + suffix].hide_render = False
            detail = bpy.data.objects['Connection_' + stage + '_' + suffix]
            detail.hide_render = True
            detail.hide_set(True)
        scene.camera = bpy.data.objects['Review_Hero']
        scene.render.resolution_x, scene.render.resolution_y = 1500, 1125
        bpy.ops.wm.save_as_mainfile(filepath=str(path), check_existing=False)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    PREVIEWS.mkdir(exist_ok=True)
    stage = sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    if stage in ('all', 'prepare'):
        prepare()
    if stage in ('all', 'render'):
        package_review('Before')
        package_review('After')
        comparison()
    if stage == 'reframe':
        reframe_details()
    log('Completed ' + stage)


if __name__ == '__main__':
    main()

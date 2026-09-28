"""Blender-only v3: four outward/downward 30-degree arms, horizontal feet.

Stages: prepare, render, all. Prior sources and production UE assets are retained.
"""
import bpy
import bmesh
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).parent))
import revise_warmachine_hub_seat as review

design, author = review.design, review.author
review.SOURCE = review.ROOT / 'ArtSource/TacticalStyle_20260916/WarMachine_LegClevis_v2'
review.SOURCE_EDITABLE = review.SOURCE / 'WarMachine_LegClevis_Editable.blend'
review.OUT = review.ROOT / 'ArtSource/TacticalStyle_20260916/WarMachine_Slope30_v3'
review.PREVIEWS = review.OUT / 'Previews'
review.EDITABLE = review.OUT / 'WarMachine_Slope30_Editable.blend'
review.REVIEW = review.OUT / 'WarMachine_Slope30_Review.blend'
review.REPORT = json.loads((review.SOURCE / 'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
review.SCALE = review.REPORT['source_scale']
review.OFFSET = Vector(review.REPORT['source_offset'])
review.REVISION_DESCRIPTION = 'v3: actual beam axes slope outward/downward 30 degrees; all four hover discs remain level and unchanged'
review.COMPARISON_CENTER = (2.25, -2.60, 1.45)
review.COMPARISON_SPAN = 64
forward = Vector((2.75-.63, -(3.02-1.02), 0)).normalized()
beam_side = Vector((forward.y, -forward.x, 0))
review.CAMERA_SPECS = [
    ('Hero', review.world((0, 0, 3.0)), (1.25, -1.85, 1.15), 92, (1500, 1125)),
    ('Full_Side', review.world((0, 0, 3.0)), (0, -1, 0), 74, (1800, 1300)),
    ('Full_Front', review.world((0, 0, 3.0)), (1, 0, 0), 78, (1500, 1125)),
    ('Connection_Oblique', review.world((2.10, -2.35, 1.40)), (1.15, -1.65, .85), 31, (1400, 1050)),
    ('Connection_Side', review.world((2.10, -2.35, 1.40)), beam_side, 34, (1400, 1050)),
    ('Connection_Top', review.world((2.10, -2.70, 1.40)), (0, 0, 1), 38, (1400, 1050)),
]


def evaluated(obj, dg):
    ev = obj.evaluated_get(dg)
    mesh = ev.to_mesh()
    vertices = [obj.matrix_world @ v.co for v in mesh.vertices]
    faces = [list(p.vertices) for p in mesh.polygons]
    ev.to_mesh_clear()
    return vertices, faces


def prepare():
    bpy.ops.wm.open_mainfile(filepath=str(review.SOURCE_EDITABLE))
    before = review.mesh_parts()
    initial = {o.name: review.signature(o) for o in before}
    old_vertices = {o.name: [v.co.copy() for v in o.data.vertices] for o in before}
    lo0, hi0, _, _ = author.evaluated_stats(before)
    rebuild = [o.name for o in before if o.name.startswith(design.CONNECTION_PREFIXES)]
    assert len(rebuild) == 64, rebuild
    shifts = {}
    for obj in before:
        if obj.name in rebuild:
            continue
        shift = design.part_stance_lift(obj.name) * review.SCALE
        shifts[obj.name] = shift
        if shift:
            obj.data.transform(Matrix.Translation((0, 0, shift)))
    for name in rebuild:
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
    author.PARTS = []
    for label, x, inner in [('F', 2.75, .63), ('R', -2.82, -1.60)]:
        design.build_hover_leg_connection(author, label, x, inner)
    for obj in list(author.PARTS):
        author.mirror(obj)
    transform = Matrix.Scale(review.SCALE, 4) @ Matrix.Translation(review.OFFSET)
    for obj in author.PARTS:
        obj.data.transform(transform)
        for modifier in obj.modifiers:
            if modifier.type == 'BEVEL':
                modifier.width *= review.SCALE
    bpy.context.view_layer.update()
    after = review.mesh_parts()
    lo1, hi1, triangles, symmetry = author.evaluated_stats(after)
    dg = bpy.context.evaluated_depsgraph_get()
    max_translation_error = 0
    for name, shift in shifts.items():
        obj = bpy.data.objects[name]
        assert len(obj.data.vertices) == len(old_vertices[name])
        max_translation_error = max(max_translation_error, max(
            (v.co - old - Vector((0, 0, shift))).length
            for v, old in zip(obj.data.vertices, old_vertices[name])))
    discs = [o for o in after if o.name.startswith('WM hover ')]
    disc_unchanged = all(review.signature(o) == initial[o.name] for o in discs)
    unchanged_connections = [n for n in rebuild if not n.startswith('WM suspension diagonal_')]
    connections_unchanged = all(review.signature(bpy.data.objects[n]) == initial[n] for n in unchanged_connections)
    contacts, axes, interference, holes = [], [], [], []
    for corner in ('F_L', 'F_R', 'R_L', 'R_R'):
        cap = bpy.data.objects['WM hover hub cap_' + corner]
        seat = bpy.data.objects['WM suspension hub seat_' + corner]
        cv, _ = evaluated(cap, dg)
        sv, _ = evaluated(seat, dg)
        cap_center = sum(cv, Vector()) / len(cv)
        seat_center = sum(sv, Vector()) / len(sv)
        contacts.append({'corner': corner, 'vertical_gap_m': min(p.z for p in sv)-max(p.z for p in cv),
                         'center_error_xy_m': (cap_center-seat_center).xy.length,
                         'disc_normal': [0, 0, 1], 'all_disc_geometry_unchanged': disc_unchanged})
        beam = bpy.data.objects['WM suspension diagonal_' + corner]
        root = sum((v.co for v in list(beam.data.vertices)[:4]), Vector()) / 4
        pin = bpy.data.objects['WM suspension cross pin_' + corner]
        pivot = sum((v.co for v in pin.data.vertices), Vector()) / len(pin.data.vertices)
        axis = pivot-root
        down = math.degrees(math.atan2(-axis.z, axis.xy.length))
        axes.append({'corner': corner, 'start_m': list(root), 'end_pin_m': list(pivot),
                     'outward_down_degrees_from_horizontal': down,
                     'horizontal_reach_m': axis.xy.length, 'vertical_drop_m': -axis.z})
        cross_axis = Vector((-axis.y, axis.x, 0)).normalized()
        radius = max(((v.co-pivot)-cross_axis*(v.co-pivot).dot(cross_axis)).length for v in pin.data.vertices)
        for suffix in ('pivot eye_', 'clevis lug_'):
            names = ['WM suspension '+suffix+corner] if suffix == 'pivot eye_' else [
                'WM suspension '+suffix+corner+' inner', 'WM suspension '+suffix+corner+' outer']
            for name in names:
                ring = [v.co for v in bpy.data.objects[name].data.vertices][40:60]
                center = sum(ring, Vector()) / len(ring)
                delta = center-pivot
                error = (delta-cross_axis*delta.dot(cross_axis)).length
                gap = min(((p-pivot)-cross_axis*(p-pivot).dot(cross_axis)).length for p in ring)-radius
                holes.append({'name': name, 'axis_error_m': error, 'radial_clearance_m': gap})
        feet = [o for o in discs if corner in o.name]
        legs = [o for o in after if o.name in rebuild and corner in o.name]
        for leg in legs:
            lv, lf = evaluated(leg, dg)
            lt = BVHTree.FromPolygons(lv, lf)
            for foot in feet:
                if leg == seat and foot == cap:
                    continue
                dv, df = evaluated(foot, dg)
                count = len(lt.overlap(BVHTree.FromPolygons(dv, df)))
                if count:
                    interference.append({'leg': leg.name, 'disc': foot.name, 'triangle_pairs': count})
    invalid = []
    for name in rebuild:
        bm = bmesh.new()
        bm.from_mesh(bpy.data.objects[name].data)
        if any(not e.is_manifold for e in bm.edges) or bm.calc_volume(signed=True) <= 0:
            invalid.append(name)
        bm.free()
    lift = design.BODY_STANCE_LIFT * review.SCALE
    sockets = {name: [p[0], p[1], p[2]+lift] for name, p in review.REPORT['sockets_m'].items()}
    report = {'source': str(review.SOURCE_EDITABLE), 'editable': str(review.EDITABLE),
              'parts': len(after), 'triangles': triangles, 'symmetry_error_m': symmetry,
              'beam_axes': axes, 'contacts': contacts, 'pin_holes': holes,
              'leg_disc_surface_intersections': interference, 'non_manifold_or_inward_rebuilt_parts': invalid,
              'hover_parts_unchanged': disc_unchanged, 'hover_parts_count': len(discs),
              'ankle_and_pin_parts_unchanged': connections_unchanged,
              'ankle_and_pin_parts_count': len(unchanged_connections),
              'body_lift_author_units': design.BODY_STANCE_LIFT,
              'hip_lift_author_units': {p: design.hip_lift(p) for p in ('F', 'R')},
              'rigidly_translated_parts': {n: s for n, s in shifts.items() if s},
              'translation_max_error_m': max_translation_error,
              'bounds_before_m': [list(lo0), list(hi0)], 'bounds_after_m': [list(lo1), list(hi1)],
              'sockets_m': sockets, 'sockets_relative_to_hull_unchanged': True,
              'art_review': 'pending_user_review', 'ue_import': 'not_performed',
              'card_generation': 'waiting_for_model_approval'}
    review.write_json(review.OUT/'geometry-check.json', report)
    assert len(after) == 279 and len(discs) == 56
    assert disc_unchanged and connections_unchanged
    assert symmetry < 1e-4 and max_translation_error < 1e-4
    assert all(abs(a['outward_down_degrees_from_horizontal']-30) < 1e-4 for a in axes)
    assert all(abs(c['vertical_gap_m']) < 1e-4 and c['center_error_xy_m'] < 1e-4 for c in contacts)
    assert all(h['axis_error_m'] < 1e-4 and h['radial_clearance_m'] > 0 for h in holes)
    assert not interference and not invalid
    assert (lo1-lo0).length < 1e-4 and (hi1-hi0-Vector((0,0,lift))).length < 1e-4
    bpy.context.scene.name = 'WarMachine_Slope30_Editable'
    bpy.context.scene['revision'] = review.REVISION_DESCRIPTION
    bpy.context.scene['approval'] = 'Pending user model review; no UE import'
    bpy.context.scene['suspension_down_degrees'] = 30.0
    bpy.context.scene['hover_discs_horizontal'] = True
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(review.EDITABLE), check_existing=False)
    updated = dict(review.REPORT, parts=len(after), triangles=triangles, bounds_m=[list(lo1),list(hi1)],
                   symmetry_error_m=symmetry, sockets_m=sockets, suspension_down_degrees=30.0,
                   stance_body_lift=design.BODY_STANCE_LIFT, design_revision=review.REVISION_DESCRIPTION)
    review.write_json(review.OUT/'WarMachine_handbuilt_report.json', updated)
    review.log('v3 prepared: four measured 30-degree slopes; 56 foot parts and 60 ankle/pin parts unchanged')


def side_comparison(before_label='v2 / Before', after_label='v3 / 30 deg down'):
    bpy.ops.wm.open_mainfile(filepath=str(review.REVIEW))
    oldpath = review.OUT/'WarMachine_Before_Review.blend'
    with bpy.data.libraries.load(str(oldpath), link=False) as (source, target):
        target.objects = [n for n in source.objects if n in ('WarMachine_Before_Body', 'WarMachine_Before_Contour')]
    scene = bpy.data.scenes.new('Side_Before_After')
    review.configure_scene(scene)
    camera = review.set_camera(scene, 'Side_Compare_Camera', (0,0,0), (0,-1,0), 130, (2000,1000))
    right = camera.rotation_euler.to_quaternion() @ Vector((1,0,0))
    up = Vector((0,0,1))
    center = review.world((0,0,3.0))
    for stage, sign in [('Before',-1),('After',1)]:
        for suffix in ('Body','Contour'):
            source = bpy.data.objects['WarMachine_'+stage+'_'+suffix]
            obj = source.copy()
            obj.name = 'SideCompare_'+stage+'_'+suffix
            scene.collection.objects.link(obj)
            obj.hide_render = False
            obj.hide_set(False)
            obj.location = -center + right*sign*32
        data = bpy.data.curves.new('Side_Label_'+stage, 'FONT')
        data.body = before_label if stage == 'Before' else after_label
        data.align_x = 'CENTER'
        data.size = 2
        obj = bpy.data.objects.new(data.name, data)
        scene.collection.objects.link(obj)
        obj.rotation_euler = camera.rotation_euler
        obj.location = right*sign*32+up*27
        mat = bpy.data.materials.new(data.name)
        mat.diffuse_color = (.82,.90,1,1)
        mat.use_nodes = True
        n, links = mat.node_tree.nodes, mat.node_tree.links
        n.clear()
        emission = n.new('ShaderNodeEmission')
        emission.inputs['Color'].default_value = (.82,.9,1,1)
        output = n.new('ShaderNodeOutputMaterial')
        links.new(emission.outputs[0], output.inputs[0])
        data.materials.append(mat)
    bpy.context.window.scene = scene
    review.render(scene, 'Side_Before_After.png')
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'SINGLE'
    scene.display.shading.single_color = (.57,.61,.66)
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'BOTH'
    scene.display.shading.background_type = 'WORLD'
    for obj in scene.objects:
        if obj.name.endswith('_Contour'):
            obj.hide_render = True
    review.render(scene, 'Side_Before_After_Clay.png')
    scene.render.engine = 'BLENDER_EEVEE'
    for obj in scene.objects:
        if obj.name.endswith('_Contour'):
            obj.hide_render = False
    bpy.context.window.scene = bpy.data.scenes['WarMachine_HubSeat_After']
    bpy.context.scene.camera = bpy.data.objects['Review_Hero']
    bpy.ops.wm.save_as_mainfile(filepath=str(review.REVIEW), check_existing=False)
    review.log('Full side color and clay comparisons saved')


def main():
    review.OUT.mkdir(parents=True, exist_ok=True)
    review.PREVIEWS.mkdir(exist_ok=True)
    stage = sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    if stage in ('prepare','all'):
        prepare()
    if stage in ('render','all'):
        review.package_review('Before')
        review.package_review('After')
        review.comparison()
        side_comparison()
    if stage == 'reframe':
        for label, path in [('Before', review.OUT/'WarMachine_Before_Review.blend'), ('After', review.REVIEW)]:
            bpy.ops.wm.open_mainfile(filepath=str(path))
            scene = bpy.data.scenes['WarMachine_HubSeat_'+label]
            bpy.context.window.scene = scene
            for suffix in ('Body','Contour'):
                bpy.data.objects['WarMachine_'+label+'_'+suffix].hide_render = True
                obj = bpy.data.objects['Connection_'+label+'_'+suffix]
                obj.hide_render = False
                obj.hide_set(False)
            review.set_camera(scene, 'Review_Connection_Top', review.world((2.10,-2.70,1.40)),
                              (0,0,1), 38, (1400,1050))
            review.render(scene, label+'_Connection_Top.png')
            for suffix in ('Body','Contour'):
                bpy.data.objects['WarMachine_'+label+'_'+suffix].hide_render = False
                obj = bpy.data.objects['Connection_'+label+'_'+suffix]
                obj.hide_render = True
                obj.hide_set(True)
            scene.camera = bpy.data.objects['Review_Hero']
            scene.render.resolution_x, scene.render.resolution_y = 1500,1125
            bpy.ops.wm.save_as_mainfile(filepath=str(path), check_existing=False)
    review.log('Slope30 completed '+stage)


if __name__ == '__main__':
    main()

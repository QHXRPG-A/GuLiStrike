"""Import untouched FBX baselines into isolated scenes for Stage A inspection."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT = ROOT / 'Baseline'
OUT.mkdir(parents=True, exist_ok=True)
source = json.loads((ROOT / 'Source/source_manifest.json').read_text(encoding='utf-8'))
bpy.ops.wm.read_factory_settings(use_empty=True)
report = {'purpose': 'Untouched source mesh inspection for reference A; no production modeling',
          'blender_version': bpy.app.version_string, 'assets': []}

def bounds(objects):
    points = [o.matrix_world @ Vector(p) for o in objects for p in o.bound_box]
    return (Vector([min(p[a] for p in points) for a in range(3)]),
            Vector([max(p[a] for p in points) for a in range(3)]))

def inspect_parts(obj):
    # Weld only the analysis graph, leaving original FBX seam vertices untouched.
    parents = list(range(len(obj.data.vertices)))
    def find(i):
        while parents[i] != i:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i
    def union(a, b):
        a, b = find(a), find(b)
        if a != b:
            parents[b] = a
    same = {}
    for v in obj.data.vertices:
        key = tuple(round(x, 5) for x in v.co)
        if key in same:
            union(v.index, same[key])
        else:
            same[key] = v.index
    for e in obj.data.edges:
        union(e.vertices[0], e.vertices[1])
    groups = {}
    for p in obj.data.polygons:
        groups.setdefault(find(p.vertices[0]), []).append(p.index)
    result = []
    for i, polygons in enumerate(groups.values()):
        verts = sorted({v for pi in polygons for v in obj.data.polygons[pi].vertices})
        points = [obj.matrix_world @ obj.data.vertices[v].co for v in verts]
        lo = Vector([min(p[a] for p in points) for a in range(3)])
        hi = Vector([max(p[a] for p in points) for a in range(3)])
        weights = {}
        for vi in verts:
            for g in obj.data.vertices[vi].groups:
                n = obj.vertex_groups[g.group].name
                weights[n] = weights.get(n, 0) + g.weight
        dominant = max(weights, key=weights.get) if weights else None
        result.append({'id': i, 'object': obj.name, 'polygons': polygons, 'vertices': len(verts),
                       'center_m': list((lo + hi) / 2), 'dimensions_m': list(hi - lo),
                       'dominant_bone': dominant,
                       'material_slots': sorted({obj.data.polygons[pi].material_index for pi in polygons})})
    return result

gray = bpy.data.materials.new('SourceGray')
gray.use_nodes = True
nt = gray.node_tree
nt.nodes.clear()
out = nt.nodes.new('ShaderNodeOutputMaterial')
geom = nt.nodes.new('ShaderNodeNewGeometry')
dot = nt.nodes.new('ShaderNodeVectorMath')
dot.operation = 'DOT_PRODUCT'
dot.inputs[1].default_value = Vector((.35, -.55, .76)).normalized()
mul = nt.nodes.new('ShaderNodeMath'); mul.operation = 'MULTIPLY_ADD'
mul.inputs[1].default_value = .29; mul.inputs[2].default_value = .40
em = nt.nodes.new('ShaderNodeEmission')
nt.links.new(geom.outputs['Normal'], dot.inputs[0])
nt.links.new(dot.outputs['Value'], mul.inputs[0])
nt.links.new(mul.outputs[0], em.inputs['Color'])
nt.links.new(em.outputs[0], out.inputs[0])

for row in source['meshes']:
    scene = bpy.data.scenes.new('Baseline_' + row['key'])
    bpy.context.window.scene = scene
    bpy.ops.import_scene.fbx(filepath=str(ROOT / row['file']), use_custom_normals=True,
                             automatic_bone_orientation=False, ignore_leaf_bones=False)
    meshes = [o for o in scene.objects if o.type == 'MESH']
    rigs = [o for o in scene.objects if o.type == 'ARMATURE']
    for rig in rigs:
        rig.data.pose_position = 'REST'
    scene.frame_set(0)
    bpy.context.view_layer.update()
    lo, hi = bounds(meshes)
    parts = [p for o in meshes for p in inspect_parts(o)]
    rec = {'key': row['key'], 'scene': scene.name, 'min_m': list(lo), 'max_m': list(hi),
           'dimensions_m': list(hi - lo), 'parts': parts, 'meshes': [], 'rigs': []}
    for o in meshes:
        o.data.calc_loop_triangles()
        rec['meshes'].append({'name': o.name, 'vertices': len(o.data.vertices),
                            'triangles': len(o.data.loop_triangles),
                            'materials': [s.name for s in o.material_slots],
                            'groups': [g.name for g in o.vertex_groups]})
    for rig in rigs:
        rec['rigs'].append({'name': rig.name, 'scale': list(rig.scale),
                            'bones': [{'name': b.name, 'parent': b.parent.name if b.parent else None}
                                      for b in rig.data.bones]})
    report['assets'].append(rec)
    originals = [[s.material for s in o.material_slots] for o in meshes]
    for o in meshes:
        for s in o.material_slots:
            s.material = gray
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.eevee.taa_render_samples = 24
    scene.view_settings.view_transform = 'Standard'; scene.view_settings.look = 'None'
    scene.world = bpy.data.worlds.new('GrayWorld_' + row['key'])
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.8, .79, .76, 1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1
    cam = bpy.data.objects.new('InspectCamera_' + row['key'], bpy.data.cameras.new('InspectCamera'))
    scene.collection.objects.link(cam); scene.camera = cam
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = max(hi - lo) * 1.5
    cam.data.clip_end = max(hi - lo) * 50
    center = (lo + hi) / 2
    for name, direction in [('hero', (1, -1.3, .8)), ('minusY', (0, -1, 0)),
                            ('plusX', (1, 0, 0)), ('plusY', (0, 1, 0)), ('minusX', (-1, 0, 0))]:
        cam.location = center + Vector(direction).normalized() * max(hi - lo) * 4
        cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = str(OUT / (row['key'] + '_' + name + '_Gray.png'))
        bpy.ops.render.render(write_still=True)
    for o, mats in zip(meshes, originals):
        for s, m in zip(o.material_slots, mats):
            s.material = m
    print('BASELINE_ASSET_DONE', row['key'], tuple(hi-lo), 'parts', len(parts), flush=True)
(OUT / 'blender_source_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'SSF_Untouched_Source_Baselines.blend'))
print('SSF_BASELINES_COMPLETE', flush=True)

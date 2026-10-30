"""Export evaluated copies of the approved B_v1; preserve the editable source."""
import bpy
import hashlib
import json
import math
from collections import Counter
from pathlib import Path

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
OUT.mkdir(parents=True, exist_ok=True)
source = Path(bpy.data.filepath)
assert source.name in ('Outpost_Symmetric_B_v1.blend','Outpost_Symmetric_B_v1_Approved.blend')
parts = bpy.data.collections['OP_Editable_Parts']
original_scene = bpy.context.window.scene
selected = list(bpy.context.selected_objects)
active = bpy.context.view_layer.objects.active
export_scene = bpy.data.scenes.new('OP_UE_Export_Temporary')
bpy.context.window.scene = export_scene
deps = original_scene.view_layers[0].depsgraph
objects = []
counts = Counter()
try:
    for original in sorted(parts.objects, key=lambda o: o.name):
        if original.type != 'MESH':
            continue
        mesh = bpy.data.meshes.new_from_object(original.evaluated_get(deps), preserve_all_data_layers=True, depsgraph=deps)
        mesh.transform(original.matrix_world)
        # Private UV1 class keeps the approved eight colors independent of FBX color conversion.
        while mesh.uv_layers:
            mesh.uv_layers.remove(mesh.uv_layers[0])
        mesh.uv_layers.new(name='GuLi_LineMask')
        uv = mesh.uv_layers.new(name='GuLi_MaterialClass')
        color = mesh.color_attributes.new(name='GuLi_PaintRole', type='BYTE_COLOR', domain='CORNER')
        mesh.color_attributes.active_color = color
        mesh.color_attributes.render_color_index = list(mesh.color_attributes).index(color)
        for face in mesh.polygons:
            mat_index = face.material_index
            counts[mat_index] += 1
            for li in face.loop_indices:
                uv.data[li].uv = (mat_index, 0)
                color.data[li].color = (1, 1, 1, (7 if mat_index in (6, 7) else 0) / 255)
        obj = bpy.data.objects.new('Export_' + original.name, mesh)
        export_scene.collection.objects.link(obj)
        objects.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = 'SM_OutpostSymmetric'
    mesh = joined.data
    # Repack an independent line-mask UV; the source UVs remain untouched.
    mesh.uv_layers.active_index = 0
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.006, area_weight=0.2, correct_aspect=True, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    mesh.calc_loop_triangles()
    assert len(mesh.loop_triangles) == 4824, len(mesh.loop_triangles)
    # Mark genuine structure/material boundaries, never tessellation diagonals.
    adjacency = [[] for _ in mesh.edges]
    for face in mesh.polygons:
        for li in face.loop_indices:
            adjacency[mesh.loops[li].edge_index].append(face.index)
    freestyle = mesh.attributes.get('freestyle_edge')
    edge_ids = set()
    for edge in mesh.edges:
        faces = adjacency[edge.index]
        if freestyle and freestyle.data[edge.index].value:
            edge_ids.add(edge.index)
        elif len(faces) != 2:
            edge_ids.add(edge.index)
        elif mesh.polygons[faces[0]].material_index != mesh.polygons[faces[1]].material_index:
            edge_ids.add(edge.index)
        elif mesh.polygons[faces[0]].normal.dot(mesh.polygons[faces[1]].normal) < math.cos(math.radians(48)):
            edge_ids.add(edge.index)
    segments = []
    uv0 = mesh.uv_layers[0]
    for face in mesh.polygons:
        loops = list(face.loop_indices)
        # Core windows are luminous; retain their physical frames without black grid lines.
        if mesh.uv_layers[1].data[loops[0]].uv.x >= 4:
            continue
        for index, li in enumerate(loops):
            if mesh.loops[li].edge_index in edge_ids:
                segments.append([list(uv0.data[li].uv), list(uv0.data[loops[(index+1) % len(loops)]].uv)])
    payload = {'resolution': 2048, 'width_pixels': 1.35, 'segments': segments}
    (OUT / 'line-mask-segments.json').write_text(json.dumps(payload, separators=(',', ':')), encoding='utf8')
    mesh.materials.clear()
    mesh.materials.append(bpy.data.materials.new('OutpostSurface_ExportOnly'))
    for face in mesh.polygons:
        face.material_index = 0
    export_scene.unit_settings.system = 'METRIC'
    export_scene.unit_settings.scale_length = 1
    filename = OUT / 'SM_OutpostSymmetric.fbx'
    bpy.ops.export_scene.fbx(filepath=str(filename), use_selection=True, object_types={'MESH'},
        global_scale=1, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Y', axis_up='Z', use_mesh_modifiers=False,
        mesh_smooth_type='FACE', use_tspace=True, use_triangles=True,
        bake_anim=False, add_leaf_bones=False, use_custom_props=False, colors_type='LINEAR')
    bounds = [[min(v.co[i] for v in mesh.vertices), max(v.co[i] for v in mesh.vertices)] for i in range(3)]
    report = {'success': True, 'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'fbx': str(filename), 'fbx_sha256': hashlib.sha256(filename.read_bytes()).hexdigest(),
        'triangles': len(mesh.loop_triangles), 'source_meshes': 53, 'exported_meshes': 1,
        'bounds_m': bounds, 'size_m': [b-a for a,b in bounds], 'uv_channels': ['LineMask', 'MaterialClass'],
        'material_class_faces': dict(counts), 'line_segments': len(segments),
        'source_saved': False, 'source_geometry_modified': False,
        'paint_contract': 'UV1.x = original material index 0..7; vertex alpha 7/255 = core, otherwise fixed 0',
        'outline': 'runtime backface overlay; no duplicated exported geometry'}
    (OUT / 'blender-export.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    result = report
finally:
    bpy.context.window.scene = original_scene
    for obj in list(export_scene.objects):
        data = obj.data
        bpy.data.objects.remove(obj, do_unlink=True)
        if data.users == 0:
            bpy.data.meshes.remove(data)
    bpy.data.scenes.remove(export_scene)
    for obj in selected:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active

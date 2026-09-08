"""Prepare studio views and portable exports of the editable outpost model."""
from pathlib import Path
import json
import math
import shutil
import bpy
import bmesh
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1')
ASSET = ROOT/'ArtSource/Buildings/OutpostMonument'
PREVIEWS = ROOT/'outputs/outpost-monument-20260905'
SCENE = 'GS_OutpostMonument_v01'


def point_at(obj, target):
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z', 'Y').to_euler()


def prepare_views():
    scene = bpy.data.scenes[SCENE]
    bpy.context.window.scene = scene
    world = bpy.data.worlds.get('GS_Outpost_Studio') or bpy.data.worlds.new('GS_Outpost_Studio')
    world.use_nodes = True
    background = next(n for n in world.node_tree.nodes if n.type == 'BACKGROUND')
    background.inputs['Color'].default_value = (.075, .10, .145, 1)
    background.inputs['Strength'].default_value = .30
    scene.world = world
    lights = bpy.data.collections['GS_91_Cameras_Lighting']
    for name, position, energy, size, color in (
        ('LIGHT_StudioKey', (-230, -350, 480), 4500000, 160, (1, .95, .86)),
        ('LIGHT_StudioFill', (230, -150, 240), 900000, 240, (.76, .85, 1)),
        ('LIGHT_StudioRim', (120, 230, 420), 4000000, 170, (.87, .94, 1))):
        obj = bpy.data.objects.get(name)
        if not obj:
            data = bpy.data.lights.new(name, 'AREA')
            obj = bpy.data.objects.new(name, data)
            lights.objects.link(obj)
        obj.location = position
        obj.data.energy = energy
        obj.data.shape = 'DISK'
        obj.data.size = size
        obj.data.color = color
        point_at(obj, (0, 0, 145))
    ground = bpy.data.objects['PRESENTATION_Ground_NotExported']
    bsdf = next(n for n in ground.data.materials[0].node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Base Color'].default_value = (.016, .023, .031, 1)
    hero = bpy.data.objects['CAM_01_Hero']
    hero.data.type = 'ORTHO'
    hero.data.ortho_scale = 348
    hero.location = (440, -665, 390)
    point_at(hero, (0, 0, 148))
    reverse = bpy.data.objects['CAM_02_Reverse']
    reverse.data.type = 'ORTHO'
    reverse.data.ortho_scale = 348
    scene.camera = hero
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'OPTIX'
        prefs.refresh_devices()
        has_gpu = False
        for device in prefs.devices:
            device.use = device.type == 'OPTIX'
            has_gpu = has_gpu or device.use
        scene.cycles.device = 'GPU' if has_gpu else 'CPU'
    except Exception:
        scene.cycles.device = 'CPU'
    scene.view_settings.exposure = 0
    return scene


def render_view(camera_name, filename, width=1100, height=1400):
    scene = bpy.data.scenes[SCENE]
    bpy.context.window.scene = scene
    scene.camera = bpy.data.objects[camera_name]
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.filepath = str(PREVIEWS/filename)
    bpy.ops.render.render(write_still=True)
    return scene.render.filepath


def export_model():
    source = bpy.data.scenes[SCENE]
    bpy.context.window.scene = source
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    model = bpy.data.collections['GS_01_Editable_Architecture']
    export_name = 'GS_Outpost_Export_v01'
    if bpy.data.scenes.get(export_name):
        raise RuntimeError('Export scene exists. Inspect it instead of overwriting source data.')
    export_scene = bpy.data.scenes.new(export_name)
    export_scene.unit_settings.system = 'METRIC'
    export_scene.unit_settings.scale_length = 1
    copies = []
    for obj in model.objects:
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph),
                                              preserve_all_data_layers=True, depsgraph=depsgraph)
        copied = bpy.data.objects.new('EXPORT_'+obj.name, mesh)
        export_scene.collection.objects.link(copied)
        copied.matrix_world = obj.matrix_world.copy()
        copies.append(copied)
    bpy.context.window.scene = export_scene
    bpy.ops.object.select_all(action='DESELECT')
    for obj in copies:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = 'SM_OutpostMonument_v01'
    joined.data.name = 'SM_OutpostMonument_v01_Mesh'
    # A packed secondary channel, separate from the continuous 4 m material UVs.
    joined.data.uv_layers.new(name='UV1_Lightmap')
    joined.data.uv_layers.active_index = 1
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.003,
                             margin_method='FRACTION', scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    joined.data.uv_layers.active_index = 0
    joined.data.uv_layers[0].active_render = True
    # The final export is explicitly triangulated; the source parts stay editable.
    bm = bmesh.new()
    bm.from_mesh(joined.data)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(joined.data)
    bm.free()
    joined.data.update()
    mesh = joined.data
    mesh.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    quality = {
        'vertices':len(mesh.vertices), 'triangles':len(mesh.loop_triangles),
        'non_manifold_edges':sum(not e.is_manifold for e in bm.edges),
        'zero_area_faces':sum(f.calc_area() < 1e-8 for f in bm.faces),
        'uv_channels':[u.name for u in mesh.uv_layers],
        'material_slots':len(mesh.materials),
        'object_scale':list(joined.scale), 'object_location':list(joined.location),
        'export_dimensions_metres':list(joined.dimensions),
    }
    bm.free()
    fbx = ASSET/'Exports/SM_OutpostMonument_v01.fbx'
    glb = ASSET/'Exports/SM_OutpostMonument_v01.glb'
    bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True,
        object_types={'MESH'}, global_scale=1, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
        use_mesh_modifiers=True, mesh_smooth_type='FACE', use_tspace=True,
        bake_anim=False, add_leaf_bones=False, path_mode='RELATIVE')
    bpy.ops.export_scene.gltf(filepath=str(glb), export_format='GLB',
        use_selection=True, use_active_scene=True, export_animations=False, export_cameras=False,
        export_lights=False, export_apply=True, export_tangents=True)
    quality['fbx_bytes'] = fbx.stat().st_size
    quality['glb_bytes'] = glb.stat().st_size
    manifest_path = ASSET/'asset_manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest['export'] = quality
    manifest['exports'] = [str(fbx.relative_to(ROOT)), str(glb.relative_to(ROOT))]
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    bpy.context.window.scene = source
    return quality


def save_source():
    scene = bpy.data.scenes[SCENE]
    bpy.context.window.scene = scene
    scene.camera = bpy.data.objects['CAM_01_Hero']
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 1400
    scene.render.filepath = str(PREVIEWS/'01_hero.png')
    for image in bpy.data.images:
        if image.name.startswith('T_OutpostConcrete_'):
            image.pack()
    for area in bpy.context.screen.areas:
        if area.type == 'VIEW_3D':
            space = area.spaces.active
            space.clip_end = 20000
            space.overlay.show_overlays = False
            space.shading.type = 'MATERIAL'
            space.shading.use_scene_world = True
            space.shading.use_scene_lights = True
            space.region_3d.view_perspective = 'CAMERA'
    bpy.ops.wm.save_as_mainfile(filepath=str(ASSET/'OutpostMonument_v01.blend'))
    return str(ASSET/'OutpostMonument_v01.blend')


if __name__ == '__main__':
    prepare_views()
    render_view('CAM_01_Hero', '01_hero.png')
    render_view('CAM_02_Reverse', '02_reverse.png')
    render_view('CAM_03_Ground', '03_ground.png', 1400, 1400)
    render_view('CAM_04_FormDetail', '04_detail.png', 1400, 1100)
    result = export_model()
    result['blend_file'] = save_source()

"""Scoped existing-asset art-review scene; never saves formal assets or maps."""
import json
import math
import random
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/Environment/PineStyleComparison_20260917'
BASE = '/Game/GuLiStrike/Environment/ArtReview/PineStyleComparison_20260917'
MAP = BASE + '/LVL_Pine_Units_Comparison'
VENDOR = '/Game/StylizedPineEnvironment/Assets'
OWNER = 'GuLi.PineStyleComparison.20260917'
LIB = unreal.EditorAssetLibrary
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

def write(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def path(obj):
    return obj.get_path_name() if obj else None

def vec(v):
    return list(v.to_tuple())

def dirty():
    return {kind: [path(p) for p in fn()] for kind, fn in (
        ('content', unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages),
        ('maps', unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages))}

def mesh_info(mesh):
    b = mesh.get_bounds()
    return {'path': path(mesh), 'size_cm': vec(b.box_extent * 2), 'origin_cm': vec(b.origin),
            'triangles_by_lod': [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())],
            'materials': [path(s.material_interface) for s in mesh.static_materials]}

def inspect():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/StylizedPineEnvironment'], True)
    actors = ACTORS.get_all_level_actors()
    result = {'world': path(EDITOR.get_editor_world()), 'game_world': path(EDITOR.get_game_world()),
              'dirty_before': dirty(), 'camera': str(EDITOR.get_level_viewport_camera_info()),
              'actors_before': [{'label': a.get_actor_label(), 'class': a.get_class().get_name()} for a in actors],
              'lights_before': [], 'meshes': {}, 'unit_tables': {}, 'ship_components': []}
    for a in actors:
        for c in a.get_components_by_class(unreal.LightComponent):
            result['lights_before'].append({'actor': a.get_actor_label(), 'intensity': c.intensity, 'rotation': vec(a.get_actor_rotation())})
    candidates = {
        'PineFull': VENDOR + '/Meshes/Trees/SM_PineTree_Full',
        'PineTall': VENDOR + '/Meshes/Trees/SM_PineTreeTall_Full',
        'PineHalf': VENDOR + '/Meshes/Trees/SM_PineTree_HalfFull',
        'Grass': VENDOR + '/Meshes/Grass/SM_Grass',
        'Rock1': VENDOR + '/Meshes/Rocks/Light/SM_Rock1_Light',
        'Rock3': VENDOR + '/Meshes/Rocks/Light/SM_Rock3_Light',
        'RockTall': VENDOR + '/Meshes/Rocks/Light/SM_RockTall1_Light',
        'Sweeper': '/Game/Commander/Units/Tactical/Cel/Sweeper/Meshes/SM_Sweeper_Cel',
        'WarMachine': '/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Cel',
        'Ship': '/Game/GuLiStrike/Ship/Stylized/Meshes/SM_Ship_AnimeHull',
        'ShipContour': '/Game/GuLiStrike/Ship/Stylized/Meshes/SM_Ship_AnimeContour'}
    for key, name in candidates.items():
        mesh = unreal.load_asset(name)
        result['meshes'][key] = mesh_info(mesh) if mesh else {'missing': name}
    for data in registry.get_assets_by_path('/Game/GuLiStrike/Data', recursive=False):
        if 'Commander' in str(data.asset_name) and str(data.asset_class_path.asset_name) == 'DataTable':
            result['unit_tables'][str(data.asset_name)] = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(data.get_asset()))
    cls = LIB.load_blueprint_class('/Game/GuLiStrike/Ship/BP_CombatAvatarFly01')
    if cls:
        cdo = unreal.get_default_object(cls)
        for c in cdo.get_components_by_class(unreal.SceneComponent):
            row = {'name': c.get_name(), 'class': c.get_class().get_name(), 'scale': vec(c.relative_scale3d), 'location': vec(c.relative_location), 'rotation': vec(c.relative_rotation)}
            if isinstance(c, unreal.StaticMeshComponent):
                row.update(mesh=path(c.static_mesh), materials=[path(c.get_material(i)) for i in range(c.get_num_materials())])
            result['ship_components'].append(row)
    result['success'] = all('missing' not in row for row in result['meshes'].values())
    write('inspection.json', result)
    return {'success': result['success'], 'report': str(OUT / 'inspection.json'), 'meshes': result['meshes'], 'world': result['world'], 'dirty_before': result['dirty_before']}

def owned_world():
    world = EDITOR.get_editor_world()
    assert world.get_path_name().split('.')[0] == MAP, path(world)
    assert LIB.get_metadata_tag(world, 'GuLi.ArtReview.Owner') == OWNER
    assert not EDITOR.get_game_world(), 'Do not author during PIE'
    return world

def spawn(cls, name, loc=(0, 0, 0), rot=None, folder='Environment'):
    actor = ACTORS.spawn_actor_from_class(cls, unreal.Vector(*loc), rot or unreal.Rotator())
    assert actor, name
    actor.set_actor_label('PineReview_' + name)
    actor.set_folder_path('PineComparison/' + folder)
    return actor

def subject(key, xy, scale=1., yaw=0., clearance=0., folder='Units'):
    info = json.loads((OUT / 'inspection.json').read_text(encoding='utf-8'))['meshes'][key]
    mesh = unreal.load_asset(info['path'])
    a = spawn(unreal.StaticMeshActor, key, rot=unreal.Rotator(yaw=yaw), folder=folder)
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    origin, extent = a.get_actor_bounds(False)
    a.set_actor_location(unreal.Vector(xy[0] - origin.x, xy[1] - origin.y, clearance - origin.z + extent.z), False, False)
    return a

def set_view(name, target, direction, distance, fov=45):
    cam = next((a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == 'PineReview_Camera'), None)
    if not cam:
        cam = spawn(unreal.CameraActor, 'Camera', folder='Cameras')
    aim = unreal.Vector(*target)
    pos = aim + unreal.Vector(*direction).normal() * distance
    rot = unreal.MathLibrary.find_look_at_rotation(pos, aim)
    cam.set_actor_location(pos, False, False)
    cam.set_actor_rotation(rot, False)
    cc = cam.get_component_by_class(unreal.CameraComponent)
    cc.set_field_of_view(fov)
    cc.set_editor_property('aspect_ratio', 16 / 9)
    EDITOR.set_level_viewport_camera_info(pos, rot)
    unreal.ViewportService.set_view_mode('lit')
    unreal.ViewportService.set_realtime(True)
    unreal.ViewportService.set_game_view(True)
    unreal.ViewportService.set_exposure(True, 0.)
    ACTORS.set_selected_level_actors([])
    return cam

def shell():
    assert not EDITOR.get_game_world()
    assert not dirty()['maps'], 'Protect existing unsaved map'
    assert not LIB.does_asset_exist(MAP), 'New map exists; refusing overwrite'
    assert LEVEL.new_level(MAP)
    world = EDITOR.get_editor_world()
    LIB.set_metadata_tag(world, 'GuLi.ArtReview.Owner', OWNER)
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ReviewGround', BASE + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    assert material
    edit = unreal.MaterialEditingLibrary
    color = edit.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -240, 0)
    color.set_editor_property('constant', unreal.LinearColor(.12, .16, .07, 1))
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -240, 180)
    rough.set_editor_property('r', 1.)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(material)
    LIB.set_metadata_tag(material, 'GuLi.ArtReview.Owner', OWNER)
    assert LIB.save_loaded_asset(material, False)
    floor = spawn(unreal.StaticMeshActor, 'ReviewGround', (0, 0, -150), folder='Stage')
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.static_mesh_component.set_material(0, material)
    floor.set_actor_scale3d(unreal.Vector(20000, 20000, 3))
    light_dir = unreal.Vector(.35, -.55, .76).normal()
    sun_rot = unreal.MathLibrary.find_look_at_rotation(light_dir * 1000, unreal.Vector())
    sun = spawn(unreal.DirectionalLight, 'Sun_ArtLightAligned', rot=sun_rot, folder='Lighting')
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    sc.set_intensity(7.)
    sc.set_editor_property('atmosphere_sun_light', True)
    sky = spawn(unreal.SkyLight, 'SkyFill', folder='Lighting').get_component_by_class(unreal.SkyLightComponent)
    sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.set_intensity(1.2)
    sky.set_editor_property('lower_hemisphere_color', unreal.LinearColor(.12, .15, .20, 1))
    spawn(unreal.SkyAtmosphere, 'Atmosphere', folder='Lighting')
    pp = spawn(unreal.PostProcessVolume, 'FixedExposure', folder='Lighting')
    pp.set_editor_property('unbound', True)
    settings = pp.get_editor_property('settings')
    for key, value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
        auto_exposure_apply_physical_camera_exposure=False, auto_exposure_bias=0.,
        motion_blur_amount=0., bloom_intensity=.08, vignette_intensity=.1,
        ambient_occlusion_intensity=.35).items():
        settings.set_editor_property('override_' + key, True)
        settings.set_editor_property(key, value)
    pp.set_editor_property('settings', settings)
    ship = subject('Ship', (-1000, 12500), yaw=-90, clearance=1800)
    edge = spawn(unreal.StaticMeshActor, 'ShipContour', folder='Units')
    edge.static_mesh_component.set_static_mesh(unreal.load_asset('/Game/GuLiStrike/Ship/Stylized/Meshes/SM_Ship_AnimeContour'))
    edge.set_actor_transform(ship.get_actor_transform(), False, False)
    edge.static_mesh_component.set_cast_shadow(False)
    edge.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    subject('WarMachine', (3000, -9000), yaw=-65, clearance=20)
    subject('Sweeper', (-6500, -13500), yaw=-35, clearance=10)
    set_view('overview', (0, 2000, 4500), (0.65, -1., .75), 103000)
    for command in ('r.ScreenPercentage 100', 'r.Streaming.FullyLoadUsedTextures 1', 'r.AntiAliasingMethod 2'):
        unreal.SystemLibrary.execute_console_command(world, command)
    assert LEVEL.save_current_level()
    return {'success': True, 'map': MAP, 'saved': True, 'dirty_after': dirty()}

def capture(name='01_shell'):
    owned_world()
    cam = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == 'PineReview_Camera')
    global SHOT
    SHOT = unreal.AutomationLibrary.take_high_res_screenshot(1920, 1080, str(OUT / 'Previews' / (name + '.png')), cam, delay=1.)
    return {'success': True, 'queued': name}

def nature():
    world = owned_world()
    assert not any(a.get_folder_path() == 'PineComparison/Nature/Trees' for a in ACTORS.get_all_level_actors())
    tree_specs = [('PineFull', (-19000, -1200), 10., 20),
                  ('PineTall', (-23100, 6000), 10., 110),
                  ('PineHalf', (-14500, 4000), 10., 210),
                  ('PineTall', (17000, 500), 10., 270),
                  ('PineFull', (23800, 6000), 10., 70)]
    for i, (key, xy, scale, yaw) in enumerate(tree_specs):
        a = subject(key, xy, scale, yaw, folder='Nature/Trees')
        a.set_actor_label('PineReview_Tree_%02d_%s' % (i + 1, key))
    for i, (key, xy, scale, yaw) in enumerate([
        ('Rock1', (-13300, -7600), 10., 25),
        ('Rock3', (-15500, -9500), 10., 135),
        ('RockTall', (11500, -4100), 10., 240),
        ('Rock1', (14700, -3000), 8., -20)]):
        a = subject(key, xy, scale, yaw, clearance=-80., folder='Nature/Rocks')
        a.set_actor_label('PineReview_Rock_%02d_%s' % (i + 1, key))
    grass = VENDOR + '/Meshes/Grass/SM_Grass'
    ft_path = BASE + '/Foliage/FT_ReviewGrass'
    assert not LIB.does_asset_exist(ft_path)
    created = unreal.FoliageService.create_foliage_type(grass, BASE + '/Foliage', 'FT_ReviewGrass', 8., 12., False, 0., 45., 260000.)
    assert created.success, created.error_message
    ft = unreal.load_asset(ft_path)
    ft.set_editor_property('collision_with_world', False)
    ft.set_editor_property('custom_navigable_geometry', unreal.HasCustomNavigableGeometry.NO)
    body = ft.get_editor_property('body_instance')
    body.set_editor_property('collision_enabled', unreal.CollisionEnabled.NO_COLLISION)
    ft.set_editor_property('body_instance', body)
    LIB.set_metadata_tag(ft, 'GuLi.ArtReview.Owner', OWNER)
    assert LIB.save_loaded_asset(ft, False)
    rng = random.Random(20260917)
    points = []
    # A single irregular lawn; leave a clear apron around both mechanical units.
    for x in range(-14200, -1500, 185):
        for y in range(-18500, -7400, 185):
            px, py = x + rng.uniform(-72, 72), y + rng.uniform(-72, 72)
            r = ((px + 8400) / 6200) ** 2 + ((py + 12500) / 5000) ** 2
            edge = .91 + .13 * math.sin(px / 870) * math.cos(py / 990)
            if r > edge or rng.random() < .025:
                continue
            if ((px + 6500) / 1750) ** 2 + ((py + 13500) / 1450) ** 2 < 1.:
                continue
            points.append(unreal.Vector(px, py, 0))
    added = unreal.FoliageService.add_foliage_instances(ft_path, points, 8., 12., False, True, False)
    assert added.success, added.error_message
    for a in ACTORS.get_all_level_actors():
        if isinstance(a, unreal.InstancedFoliageActor):
            a.set_folder_path('PineComparison/Nature/Grass')
    sun = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == 'PineReview_Sun_ArtLightAligned')
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('dynamic_shadow_distance_movable_light', 200000.)
    assert LEVEL.save_current_level()
    report = {'success': True, 'map': MAP, 'trees': 5, 'rocks': 4,
              'grass_instances': unreal.FoliageService.get_instance_count(ft_path),
              'grass_lod0_triangles_total': added.instances_added * 37,
              'foliage_type': ft_path, 'nature_scale': '10x trees/rocks; 8-12x grass; one rock 8x',
              'unit_scale': '1x current meshes; Ship hull plus its separate contour',
              'material_adaptation': False, 'dirty_after': dirty()}
    write('scene_build.json', report)
    return report

COMMANDER_DIR = (math.cos(math.radians(55)) / math.sqrt(2),
                 -math.cos(math.radians(55)) / math.sqrt(2), math.sin(math.radians(55)))
VIEWS = {
    '03_overview': ((0, 9000, 4700), (.65, -1., .78), 110000, 45),
    '04_low_angle': ((-1800, 3000, 4100), (.22, -1., .28), 93000, 45),
    '05_top': ((0, 9000, 1000), (.08, -.1, 1.), 158000, 45),
    '06_commander_200m': ((-6500, -13500, 600), COMMANDER_DIR, 20000, 45),
    '07_commander_800m': ((0, 9000, 0), COMMANDER_DIR, 80000, 45),
    '08_commander_1800m': ((0, 9000, 0), COMMANDER_DIR, 180000, 45),
    '09_units_detail': ((-1800, -10300, 1500), (.25, -1., .62), 45000, 45),
}

def refine():
    world = owned_world()
    # Clear the actual tree/ship footprint rather than hiding an intersection.
    for a in ACTORS.get_all_level_actors():
        if a.get_actor_label() in ('PineReview_Ship', 'PineReview_ShipContour'):
            p = a.get_actor_location()
            a.set_actor_location(p + unreal.Vector(0, 12000, 0), False, False)
    set_view('03_overview', *VIEWS['03_overview'])
    assert LEVEL.save_current_level()
    write('camera_presets.json', {k: {'target_cm': v[0], 'direction': v[1], 'distance_cm': v[2], 'fov': v[3]} for k, v in VIEWS.items()})
    return {'success': True, 'saved': True}

def gallery():
    import time
    owned_world()
    global GALLERY_HANDLE, GALLERY_STATE
    if globals().get('GALLERY_HANDLE'):
        raise RuntimeError('Gallery already in progress')
    GALLERY_STATE = {'phase': 'camera', 'index': 0, 'last': time.monotonic(), 'start': time.monotonic(), 'images': []}
    names = list(VIEWS)
    def tick(dt):
        global GALLERY_HANDLE
        state = GALLERY_STATE
        try:
            if time.monotonic() - state['start'] > 240:
                raise RuntimeError('Screenshot sequence timeout')
            if state['index'] >= len(names):
                # Saving can pump Slate ticks. Detach first to prevent re-entry.
                handle = GALLERY_HANDLE
                GALLERY_HANDLE = None
                unreal.unregister_slate_post_tick_callback(handle)
                set_view('03_overview', *VIEWS['03_overview'])
                assert LEVEL.save_current_level()
                write('gallery.json', {'success': True, 'images': state['images']})
                write('camera_presets.json', {k: {'target_cm': v[0], 'direction': v[1], 'distance_cm': v[2], 'fov': v[3]} for k, v in VIEWS.items()})
                return
            name = names[state['index']]
            if state['phase'] == 'camera':
                set_view(name, *VIEWS[name])
                state.update(phase='settle', last=time.monotonic())
            elif state['phase'] == 'settle' and time.monotonic() - state['last'] > 3:
                capture(name)
                state.update(phase='capture', last=time.monotonic())
            elif state['phase'] == 'capture' and SHOT.is_task_done():
                file = OUT / 'Previews' / (name + '.png')
                assert file.is_file() and file.stat().st_size > 10000, str(file)
                state['images'].append(str(file))
                state.update(phase='camera', index=state['index'] + 1)
        except Exception:
            write('gallery.json', {'success': False, 'error': traceback.format_exc(), 'state': state})
            if GALLERY_HANDLE:
                unreal.unregister_slate_post_tick_callback(GALLERY_HANDLE)
                GALLERY_HANDLE = None
    GALLERY_HANDLE = unreal.register_slate_post_tick_callback(tick)
    return {'success': True, 'queued_views': names}

def readback():
    world = owned_world()
    report = {'success': True, 'map': path(world), 'actors': [], 'foliage': [],
              'materials': {}, 'dirty': dirty(), 'review_status': 'candidate_pending_user_review',
              'gameplay_map_modified': False, 'source_engine': 'D:/UnrealEngine-5.7'}
    for a in ACTORS.get_all_level_actors():
        if not a.get_actor_label().startswith('PineReview_'):
            continue
        origin, extent = a.get_actor_bounds(False)
        row = {'label': a.get_actor_label(), 'folder': str(a.get_folder_path()), 'location': vec(a.get_actor_location()),
               'scale': vec(a.get_actor_scale3d()), 'bounds_center_cm': vec(origin), 'bounds_extent_cm': vec(extent)}
        if isinstance(a, unreal.StaticMeshActor):
            c = a.static_mesh_component
            row['mesh'] = path(c.static_mesh)
            row['materials'] = [path(c.get_material(i)) for i in range(c.get_num_materials())]
            row['collision'] = str(c.get_collision_enabled())
            for i in range(c.get_num_materials()):
                m = c.get_material(i)
                if not m or path(m) in report['materials']:
                    continue
                name = path(m)
                while isinstance(m, unreal.MaterialInstanceConstant):
                    m = m.parent
                report['materials'][name] = {'base': path(m), 'blend': str(m.blend_mode),
                    'shading_model': unreal.MaterialService.get_property(path(m), 'ShadingModel'), 'two_sided': m.get_editor_property('two_sided')}
        report['actors'].append(row)
    for f in unreal.FoliageService.list_foliage_types():
        report['foliage'].append({'type': f.foliage_type_path, 'mesh': f.mesh_path, 'count': f.instance_count})
    for a in ACTORS.get_all_level_actors():
        for c in a.get_components_by_class(unreal.FoliageInstancedStaticMeshComponent):
            report['foliage_component'] = {'class': c.get_class().get_name(), 'count': c.get_instance_count(), 'collision': str(c.get_collision_enabled()), 'mesh': path(c.static_mesh)}
            mat = c.get_material(0)
            while isinstance(mat, unreal.MaterialInstanceConstant):
                mat = mat.parent
            report['grass_material'] = {'base': path(mat), 'blend': str(mat.blend_mode), 'shading_model': unreal.MaterialService.get_property(path(mat), 'ShadingModel'), 'two_sided': mat.get_editor_property('two_sided')}
    write('scene_readback.json', report)
    return {'success': True, 'report': str(OUT / 'scene_readback.json'), 'foliage': report['foliage'], 'dirty': report['dirty']}

def reload_saved():
    owned_world()
    assert not globals().get('GALLERY_HANDLE'), 'Screenshot sequence running'
    assert not dirty()['maps'], 'Do not discard unsaved state'
    # Leave and return so foliage/map serialization is exercised, not just memory.
    assert LEVEL.load_level('/Game/Commander/Units/Tactical/Cel/Review/LVL_CelModels_Review')
    assert LEVEL.load_level(MAP)
    result = readback()
    report = json.loads((OUT / 'scene_readback.json').read_text(encoding='utf-8'))
    labels = [a['label'] for a in report['actors']]
    assert sum(n.startswith('PineReview_Tree_') for n in labels) == 5
    assert sum(n.startswith('PineReview_Rock_') for n in labels) == 4
    assert all('PineReview_' + name in labels for name in ('Ship', 'ShipContour', 'Sweeper', 'WarMachine'))
    assert report['foliage_component']['count'] == 2293
    assert 'NO_COLLISION' in report['foliage_component']['collision']
    report['reloaded_from_saved_map'] = True
    write('saved_readback.json', report)
    set_view('03_overview', *VIEWS['03_overview'])
    assert LEVEL.save_current_level()
    return dict(result, saved_map_readback=True, final_dirty=dirty())

def dispatch(action='inspect'):
    try:
        result = globals()[action]()
    except Exception:
        result = {'success': False, 'traceback': traceback.format_exc()}
        write('error_' + action + '.json', result)
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))

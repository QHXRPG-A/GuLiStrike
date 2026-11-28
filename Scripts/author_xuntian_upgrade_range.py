"""Author the local Xuntian cosmetic loadout fixture and its numeric-key controls."""

import json
import sys
from pathlib import Path

import unreal

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SCRIPTS / 'Cards'))
from Models.model_catalog import resource
from card_reveal_graph import Graph, Node, function, require, variable


MAP = '/Game/Maps/LVL_CommanderMassPrototype'
FOLDER = '/Game/Development/ShipUpgradeRange'
DISPLAY = FOLDER + '/BP_XuntianUpgradeRange'
ENTRY = FOLDER + '/BP_XuntianUpgradeRangeEntry'
SHIP = '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01'
OWNER = 'GuLi.XuntianUpgradeRange.20261124'
EDITOR_FOLDER = 'Review/XuntianUpgradeRange'
STATION = (62000, -82000, 15000)
FAMILIES = (
    ('One', 'Bottom', '底炮', ('01', '02', '03')),
    ('Two', 'Air', '防空炮', ('04', '05')),
    ('Three', 'Thor', '雷神导弹舱', ('06', '07')),
    ('Four', 'Bay', '舰载舱', ('08', '11', '12', '13')),
    ('Five', 'Support', '支援装置', ('09', '10')),
)
ORBIT_START = (-8500, -11400, 8100)
UPPER_PITCH, LOWER_PITCH = -30.0, 22.0
MIN_PITCH, MAX_PITCH = -75.0, 38.0
MIN_DISTANCE, MAX_DISTANCE, ZOOM_STEP = 8500.0, 20000.0, 800.0
CURSOR_ORBIT_SCALE = 0.3
SERVICE = unreal.BlueprintService
ASSETS = unreal.EditorAssetLibrary
DATA = unreal.SubobjectDataBlueprintFunctionLibrary


def display_blueprint(path):
    if not ASSETS.does_asset_exist(path):
        folder, name = path.rsplit('/', 1)
        require(SERVICE.create_blueprint(name, '/Script/Engine.Actor', folder), path)
        blueprint = require(unreal.load_asset(path), path)
        ASSETS.set_metadata_tag(blueprint, 'GuLi.Authoring.Owner', OWNER)
    blueprint = require(unreal.load_asset(path), path)
    require(ASSETS.get_metadata_tag(blueprint, 'GuLi.Authoring.Owner') == OWNER, path)
    return blueprint


def templates(blueprint):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    return {
        str(DATA.get_variable_name(DATA.get_data(handle))): handle
        for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    }


def add_component(blueprint, handles, name, component_class, parent):
    if name not in handles:
        subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        parameters = unreal.AddNewSubobjectParams(
            parent_handle=handles[parent], new_class=component_class, blueprint_context=blueprint
        )
        handle, reason = subsystem.add_new_subobject(parameters)
        require(DATA.is_handle_valid(handle), f'{name} under {parent}: {reason}')
        require(subsystem.rename_subobject(handle, unreal.Text(name)), name)
        handles[name] = handle
    component = require(DATA.get_object(DATA.get_data(handles[name])), name)
    require(isinstance(component, component_class), f'{name}: wrong component class')
    return component


def place_component(component, transform):
    for name, value in (('relative_location', transform.translation),
                        ('relative_rotation', transform.rotation.rotator()),
                        ('relative_scale3d', transform.scale3d)):
        component.set_editor_property(name, value)


def render_settings(component, visible=True, shadow=True):
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_collision_profile_name('NoCollision')
    component.set_editor_property('generate_overlap_events', False)
    component.set_editor_property('can_ever_affect_navigation', False)
    component.set_cast_shadow(shadow)
    component.set_visibility(visible, False)
    component.set_hidden_in_game(False, False)


def component_from(blueprint, name):
    handle = templates(blueprint)[name]
    return DATA.get_object(DATA.get_data(handle))


def part_visual(part):
    model_id = int(part.get_editor_property('model_id'))
    if model_id > 0:
        return require(unreal.load_object(None, resource(model_id)), str(model_id))
    return require(part.get_editor_property('skeletal_mesh') or part.get_editor_property('static_mesh'),
                   part.get_path_name())


def mount_pose(hull_mesh, mount):
    part = unreal.get_default_object(mount.part_class)
    slot = mount.socket_name
    socket = require(hull_mesh.find_socket(slot), f'{hull_mesh.get_name()}: {slot}')
    require(slot in part.get_editor_property('compatible_sockets'), f'{part.get_name()}: {slot}')
    local_socket = unreal.Transform(socket.relative_location, socket.relative_rotation, socket.relative_scale)
    relative = unreal.MathLibrary.compose_transforms(part.get_editor_property('part_relative_transform'), local_socket)
    return part, part_visual(part), relative


def mount_display(blueprint):
    ship = unreal.load_asset(SHIP)
    cdo = unreal.get_default_object(ship.generated_class())
    source_hull = cdo.get_editor_property('hull_mesh')
    source_contour = component_from(ship, 'AnimeContour')
    catalog = cdo.get_ship_assembly().get_editor_property('catalog')
    handles = templates(blueprint)
    scale_root = add_component(blueprint, handles, 'DisplayScale', unreal.SceneComponent, 'DefaultSceneRoot')
    place_component(scale_root, cdo.get_editor_property('mesh').get_relative_transform())

    hull = add_component(blueprint, handles, 'Hull', unreal.StaticMeshComponent, 'DisplayScale')
    hull.set_static_mesh(source_hull.static_mesh)
    place_component(hull, source_hull.get_relative_transform())
    render_settings(hull, shadow=source_hull.get_editor_property('cast_shadow'))
    contour = add_component(blueprint, handles, 'Contour', unreal.StaticMeshComponent, 'Hull')
    contour.set_static_mesh(source_contour.static_mesh)
    place_component(contour, source_contour.get_relative_transform())
    render_settings(contour, shadow=source_contour.get_editor_property('cast_shadow'))

    mounted = {}
    for group in catalog.groups:
        group_name = str(group.configuration_id)
        parent = add_component(blueprint, handles, group_name, unreal.SceneComponent, 'Hull')
        parent.set_visibility(False, False)
        mounted[group_name] = len(group.mounts)
        for mount in group.mounts:
            part, mesh, relative = mount_pose(hull.static_mesh, mount)
            name = f'{group_name}_{mount.socket_name}'
            kind = unreal.SkeletalMeshComponent if isinstance(mesh, unreal.SkeletalMesh) else unreal.StaticMeshComponent
            component = add_component(blueprint, handles, name, kind, group_name)
            if isinstance(component, unreal.SkeletalMeshComponent):
                component.set_skeletal_mesh_asset(mesh)
                # The display uses reference poses and switches whole groups by visibility.
                tick = component.get_editor_property('primary_component_tick')
                tick.set_editor_property('start_with_tick_enabled', False)
                component.set_editor_property('primary_component_tick', tick)
            else:
                require(isinstance(mesh, unreal.StaticMesh), name)
                component.set_static_mesh(mesh)
            render_settings(component, visible=False, shadow=part.get_editor_property('cast_shadow'))
            for index, material in enumerate(part.get_editor_property('override_materials')):
                if material:
                    component.set_material(index, material)
            place_component(component, relative)

    expected = {'Group_' + node for _, _, _, nodes in FAMILIES for node in nodes}
    require(set(mounted) == expected, 'Preview families must cover the Ship catalogue')
    blueprint.modify()
    return mounted


def mount_orbit_camera(blueprint):
    handles = templates(blueprint)
    for name in ('UpperCamera', 'LowerCamera'):
        if name in handles:
            require(SERVICE.remove_component(DISPLAY, name), name)

    handles = templates(blueprint)
    orbit = add_component(blueprint, handles, 'CameraOrbit', unreal.SceneComponent, 'DefaultSceneRoot')
    camera = add_component(blueprint, handles, 'OrbitCamera', unreal.CameraComponent, 'CameraOrbit')
    start = unreal.Vector(*ORBIT_START)
    orbit.set_editor_property('relative_rotation', unreal.MathLibrary.find_look_at_rotation(start, unreal.Vector()))
    camera.set_editor_property('relative_location', unreal.Vector(-start.length(), 0, 0))
    camera.set_editor_property('field_of_view', 55.0)
    camera.set_editor_property('auto_activate', True)
    blueprint.modify()


def key_event(graph, key):
    existing = {node.node_id for node in SERVICE.get_nodes_in_graph(graph.bp, graph.name)}
    x, y = graph.pos()
    asset = unreal.load_asset(graph.bp)
    result = json.loads(unreal.MCPythonHelper.add_blueprint_node(
        asset, graph.name, json.dumps({'type': 'InputKey', 'key_name': key, 'pos_x': x, 'pos_y': y})
    ))
    require(result.get('success'), result)
    created = [node.node_id for node in SERVICE.get_nodes_in_graph(graph.bp, graph.name)
               if node.node_id not in existing]
    require(len(created) == 1, f'One input event expected for {key}: {created}')
    graph.tail = Node(graph, created[0])['Pressed']


def print_status(graph, message, seconds=3):
    graph.call('KismetSystemLibrary', 'PrintString', InString=message,
               bPrintToScreen=True, bPrintToLog=False, Duration=seconds)


def compile_checked(bp):
    result = SERVICE.compile_blueprint(bp)
    require(result.success and result.num_errors == 0, f'{bp}: {list(result.errors)}')
    return list(result.warnings)


def build_display_graph():
    for _, name, _, _ in FAMILIES:
        variable(DISPLAY, name + 'Level', 'int', 0, category='Upgrade Range')
        function(DISPLAY, 'Show' + name, [('Level', 'int')])
    variable(DISPLAY, 'HasCursorSample', 'bool', False, category='Orbit Camera')
    for name in ('LastCursorX', 'LastCursorY'):
        variable(DISPLAY, name, 'float', 0, category='Orbit Camera')
    function(DISPLAY, 'SetObserverActive', [('Active', 'bool')])
    function(DISPLAY, 'OrbitFromMouse', [('YawDelta', 'float'), ('PitchDelta', 'float')])
    compile_checked(DISPLAY)

    for _, name, _, groups in FAMILIES:
        graph = Graph(DISPLAY, 'Show' + name, function=True)
        graph.set(name + 'Level', graph.arg('Level'))
        for level, group in enumerate(groups, 1):
            shown = graph.native('EqualEqual_IntInt', A=graph.arg('Level'), B=level)
            graph.method('SceneComponent', 'SetVisibility', graph.get('Group_' + group),
                         bNewVisibility=shown, bPropagateToChildren=True)

    graph = Graph(DISPLAY, 'SetObserverActive', function=True)
    graph.set('HasCursorSample', False)
    graph.method('Actor', 'SetActorTickEnabled', graph.get('self'), bEnabled=graph.arg('Active'))

    graph = Graph(DISPLAY, 'OrbitFromMouse', function=True)
    yaw_delta, pitch_delta = graph.arg('YawDelta'), graph.arg('PitchDelta')
    graph.branch(has_mouse_delta(graph, yaw_delta, pitch_delta))
    orbit = graph.get('CameraOrbit')
    angles = orbit_angles(graph, orbit)
    pitch = graph.clamp(angles['Pitch'] + pitch_delta, MIN_PITCH, MAX_PITCH)
    graph.method('SceneComponent', 'K2_SetRelativeRotation', orbit,
                 NewRotation=graph.rot(pitch=pitch, yaw=angles['Yaw'] + yaw_delta))

    graph = Graph(DISPLAY, 'EventGraph')
    for key, name, label, groups in FAMILIES:
        key_event(graph, key)
        step = graph.native('Add_IntInt', A=graph.get(name + 'Level'), B=1)
        level = graph.native('Percent_IntInt', A=step, B=len(groups) + 1)
        graph.call('self', 'Show' + name, Level=level)
        message = graph.call('KismetStringLibrary', 'BuildString_Int', AppendTo='',
                             Prefix=label + '  ', InInt=graph.get(name + 'Level'),
                             Suffix='（0 为卸下）')['ReturnValue']
        print_status(graph, message)
    key_event(graph, 'Zero')
    for _, name, _, _ in FAMILIES:
        graph.call('self', 'Show' + name, Level=0)
    print_status(graph, '巡天配件已全部卸下')

    build_orbit_graph(graph)


def break_relative_transform(graph, component):
    transform = graph.method('SceneComponent', 'GetRelativeTransform', component)['ReturnValue']
    return graph.call('KismetMathLibrary', 'BreakTransform', InTransform=transform)


def orbit_angles(graph, orbit):
    rotation = break_relative_transform(graph, orbit)['Rotation']
    return graph.call('KismetMathLibrary', 'BreakRotator', InRot=rotation)


def has_mouse_delta(graph, delta_x, delta_y):
    return graph.either(graph.native('NotEqual_DoubleDouble', A=delta_x, B=0),
                        graph.native('NotEqual_DoubleDouble', A=delta_y, B=0))


def build_orbit_graph(graph):
    orbit = graph.get('CameraOrbit')
    camera = graph.get('OrbitCamera')

    key_event(graph, 'Eight')
    angles = orbit_angles(graph, orbit)
    from_above = graph.compare('Less', angles['Pitch'], 0)
    pitch = graph.select(LOWER_PITCH, UPPER_PITCH, from_above)
    graph.method('SceneComponent', 'K2_SetRelativeRotation', orbit,
                 NewRotation=graph.rot(pitch=pitch, yaw=angles['Yaw']))

    for key, step in (('MouseScrollUp', ZOOM_STEP), ('MouseScrollDown', -ZOOM_STEP)):
        key_event(graph, key)
        location = break_relative_transform(graph, camera)['Location']
        offset = graph.call('KismetMathLibrary', 'BreakVector', InVec=location)['X']
        x = graph.clamp(offset + step, -MAX_DISTANCE, -MIN_DISTANCE)
        graph.method('SceneComponent', 'K2_SetRelativeLocation', camera, NewLocation=graph.vec(x=x))

    graph.event('ReceiveBeginPlay')
    graph.invoke('SetObserverActive', Active=False)

    graph.event('ReceiveTick')
    player = graph.call('GameplayStatics', 'GetPlayerController', PlayerIndex=0)['ReturnValue']
    graph.branch(graph.call('KismetSystemLibrary', 'IsValid', Object=player)['ReturnValue'])
    view = graph.method('PlayerController', 'GetViewTarget', player)['ReturnValue']
    graph.branch(graph.native('EqualEqual_ObjectObject', A=view, B=graph.get('self')))
    mouse = graph.method('PlayerController', 'GetInputMouseDelta', player)
    delta_x, delta_y = mouse['DeltaX'], mouse['DeltaY']
    cursor = graph.method('PlayerController', 'GetMousePosition', player)
    cursor_x, cursor_y = cursor['LocationX'], cursor['LocationY']
    visible_cursor = graph.member('PlayerController', 'bShowMouseCursor', player)
    has_cursor = graph.both(visible_cursor, cursor['ReturnValue'])
    use_cursor = graph.both(has_cursor, graph.get('HasCursorSample'),
                            graph.native('Not_PreBool', A=has_mouse_delta(graph, delta_x, delta_y)))
    yaw = graph.select((cursor_x - graph.get('LastCursorX')) * CURSOR_ORBIT_SCALE, delta_x, use_cursor)
    pitch = graph.select((graph.get('LastCursorY') - cursor_y) * CURSOR_ORBIT_SCALE, delta_y, use_cursor)
    graph.invoke('OrbitFromMouse', YawDelta=yaw, PitchDelta=pitch)
    graph.set('LastCursorX', cursor_x)
    graph.set('LastCursorY', cursor_y)
    graph.set('HasCursorSample', has_cursor)


def build_entry_graph():
    variable(ENTRY, 'RangeActor', 'Actor', editable=True, category='Upgrade Range')
    variable(ENTRY, 'PreviousView', 'Actor', category='Upgrade Range')
    variable(ENTRY, 'RangeActive', 'bool', False, category='Upgrade Range')
    compile_checked(ENTRY)
    graph = Graph(ENTRY, 'EventGraph')
    key_event(graph, 'F12')
    player = graph.call('GameplayStatics', 'GetPlayerController', PlayerIndex=0)['ReturnValue']
    graph.branch(graph.call('KismetSystemLibrary', 'IsValid', Object=player)['ReturnValue'])
    graph.branch(graph.method('Controller', 'IsLocalController', player)['ReturnValue'])
    range_actor = graph.get('RangeActor')
    observer = graph.cast(DISPLAY, range_actor)
    observer_class = unreal.load_asset(DISPLAY).generated_class().get_name()
    state = graph.branch(graph.get('RangeActive'))

    graph.method('Actor', 'DisableInput', range_actor, PlayerController=player)
    graph.method(observer_class, 'SetObserverActive', observer, Active=False)
    graph.set('RangeActive', False)
    current_view = graph.method('PlayerController', 'GetViewTarget', player)['ReturnValue']
    still_viewing = graph.native('EqualEqual_ObjectObject', A=current_view, B=range_actor)
    graph.branch(still_viewing)
    previous = graph.get('PreviousView')
    saved = graph.branch(graph.call('KismetSystemLibrary', 'IsValid', Object=previous)['ReturnValue'])
    graph.method('PlayerController', 'SetViewTargetWithBlend', player, NewViewTarget=previous, BlendTime=0)
    graph.tail = saved['else']
    pawn = graph.method('Controller', 'K2_GetPawn', player)['ReturnValue']
    valid_pawn = graph.branch(graph.call('KismetSystemLibrary', 'IsValid', Object=pawn)['ReturnValue'])
    graph.method('PlayerController', 'SetViewTargetWithBlend', player, NewViewTarget=pawn, BlendTime=0)
    graph.tail = valid_pawn['else']
    graph.method('PlayerController', 'SetViewTargetWithBlend', player, NewViewTarget=player, BlendTime=0)

    graph.tail = state['else']
    graph.set('PreviousView', current_view)
    graph.method('Actor', 'EnableInput', range_actor, PlayerController=player)
    graph.method('PlayerController', 'SetViewTargetWithBlend', player, NewViewTarget=range_actor, BlendTime=0)
    graph.method(observer_class, 'SetObserverActive', observer, Active=True)
    graph.set('RangeActive', True)
    print_status(graph, '巡天换装靶场\n鼠标环绕  滚轮缩放  8 上下视角\n1 底炮  2 防空  3 雷神  4 舰载舱  5 支援\n0 全部卸下  F12 返回', 8)


def author_assets():
    display = display_blueprint(DISPLAY)
    entry = display_blueprint(ENTRY)
    Graph(DISPLAY, 'EventGraph')
    if SERVICE.variable_exists(DISPLAY, 'LowerView'):
        require(SERVICE.remove_variable(DISPLAY, 'LowerView'), 'LowerView')
    expected = mount_display(display)
    mount_orbit_camera(display)
    compile_checked(DISPLAY)
    build_display_graph()
    build_entry_graph()
    warnings = compile_checked(DISPLAY) + compile_checked(ENTRY)
    require(not warnings, warnings)

    display_cdo = unreal.get_default_object(display.generated_class())
    display_cdo.modify()
    tick = display_cdo.get_editor_property('primary_actor_tick')
    tick.set_editor_property('start_with_tick_enabled', False)
    display_cdo.set_editor_property('primary_actor_tick', tick)
    display.modify()
    require(ASSETS.save_loaded_asset(display, False), DISPLAY)

    entry_cdo = unreal.get_default_object(entry.generated_class())
    entry_cdo.modify()
    entry_cdo.set_editor_property('auto_receive_input', unreal.AutoReceiveInput.PLAYER0)
    entry.modify()
    require(ASSETS.save_loaded_asset(entry, False), ENTRY)
    return {'groups': expected, 'mount_count': sum(expected.values()), 'nodes': {
                bp: len(SERVICE.get_nodes_in_graph(bp, 'EventGraph')) for bp in (DISPLAY, ENTRY)
            }, 'blueprint_warnings': warnings}


def place_range(*, updating=False):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, world.get_path_name())
    require(not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before placing the range')
    dirty = [package.get_path_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    require(not dirty or (updating and dirty == [MAP]), 'An unexpected map has unsaved edits')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    location = unreal.Vector(*STATION)
    desired = ((DISPLAY, 'XuntianUpgradeRange'), (ENTRY, 'XuntianUpgradeRange_Controls'))
    instances = []
    for path, label in desired:
        bp = unreal.load_asset(path)
        cls = bp.generated_class()
        matches = [a for a in actors.get_all_level_actors() if a.get_class() == cls]
        require(len(matches) <= 1, f'Extra range instances: {label}')
        actor = matches[0] if matches else require(
            actors.spawn_actor_from_class(cls, location, unreal.Rotator()), label
        )
        actor.set_actor_label(label)
        actor.set_folder_path(unreal.Name(EDITOR_FOLDER))
        instances.append(actor)
    display, entry = instances
    entry.modify()
    entry.set_editor_property('RangeActor', display)
    require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), MAP)
    return {'map': MAP, 'actors': [a.get_actor_label() for a in instances]}


def read_range():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, world.get_path_name())
    instances = {}
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if actor.get_actor_label() in ('XuntianUpgradeRange', 'XuntianUpgradeRange_Controls'):
            instances[actor.get_actor_label()] = actor
    display = instances['XuntianUpgradeRange']
    entry = instances['XuntianUpgradeRange_Controls']
    require(entry.get_editor_property('RangeActor') == display, 'Range controls are not linked')
    require(entry.get_editor_property('auto_receive_input') == unreal.AutoReceiveInput.PLAYER0, 'Entry input')
    require(display.get_editor_property('auto_receive_input') == unreal.AutoReceiveInput.DISABLED, 'Scoped digits')
    meshes = [component for component in display.get_components_by_class(unreal.MeshComponent)
              if not component.get_editor_property('is_editor_only')]
    require(len(meshes) == 96, f'{len(meshes)} visual components')
    mounted = [m for m in meshes if m.get_name().startswith('Group_')]
    require(len(mounted) == 94, 'Mount count')
    require(all(not m.is_visible() for m in mounted), 'Initial build must be empty')
    require(all(m.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION for m in meshes), 'Display collision')
    components = {c.get_name().removesuffix('_GEN_VARIABLE'): c
                  for c in display.get_components_by_class(unreal.SceneComponent)}
    ship = unreal.load_asset(SHIP)
    ship_cdo = unreal.get_default_object(ship.generated_class())
    source_hull = ship_cdo.get_hull_mesh_component()
    source_contour = component_from(ship, 'AnimeContour')
    hull, contour = components['Hull'], components['Contour']
    for component, source in ((hull, source_hull), (contour, source_contour)):
        require(component.static_mesh == source.static_mesh, component.get_name())
        require(component.get_relative_transform().is_near_equal(source.get_relative_transform()), component.get_name())
    source_base = ship_cdo.get_editor_property('mesh').get_relative_transform()
    require(components['DisplayScale'].get_relative_transform().is_near_equal(source_base), 'Hull display transform')
    catalogue = ship_cdo.get_ship_assembly().get_editor_property('catalog')
    for group in catalogue.groups:
        parent = components[str(group.configuration_id)]
        require(parent.get_attach_parent() == hull, parent.get_name())
        for mount in group.mounts:
            component = components[f'{group.configuration_id}_{mount.socket_name}']
            _, mesh, pose = mount_pose(hull.static_mesh, mount)
            assigned_mesh = (component.static_mesh if isinstance(component, unreal.StaticMeshComponent)
                             else component.get_skeletal_mesh_asset())
            require(component.get_attach_parent() == parent, component.get_name())
            require(assigned_mesh == mesh and component.get_relative_transform().is_near_equal(pose), component.get_name())
    cameras = display.get_components_by_class(unreal.CameraComponent)
    require(len(cameras) == 1 and cameras[0] == components['OrbitCamera'], 'Orbit camera')
    camera = cameras[0]
    orbit = components['CameraOrbit']
    require(camera.get_attach_parent() == orbit and camera.get_editor_property('auto_activate'), 'Camera setup')
    require(camera.get_editor_property('relative_location').x < 0 and orbit.relative_rotation.pitch < 0, 'Initial camera pose')
    tick = unreal.get_default_object(display.get_class()).get_editor_property('primary_actor_tick')
    require(not tick.get_editor_property('start_with_tick_enabled'), 'Preview tick must be disabled outside the range')
    require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'The map was not saved')
    loc = display.get_actor_location()
    return {'map': MAP, 'location_cm': [loc.x, loc.y, loc.z], 'mounted_components': len(mounted),
            'orbit_camera': camera.get_name(), 'controls_linked': True, 'initially_equipped': 0}

"""Temporary UE viewport acceptance preview; no animation or map is saved.

Run start(original_map, original_camera) in the live editor. A temporary blank
map is required. Unsaved animation sequences only drive the acceptance preview;
all preview assets and actors are removed on completion.
"""
import json
import math
import traceback
import unreal
import author_ship_component_rigs as rigs

TEMP = '/Game/Developers/ShipComponentRigPreview_20260910'
STATE = {}


def make_animation(name, mesh):
    path = TEMP + '/A_' + name
    existing = unreal.EditorAssetLibrary.does_asset_exist(path)
    # The CIWS sequence is the explicitly created probe from this same session.
    if existing:
        if name != 'CIWS':
            raise RuntimeError('Preview path already exists: ' + path)
        return unreal.load_asset(path)
    factory = unreal.AnimSequenceFactory()
    factory.target_skeleton = mesh.skeleton
    factory.preview_skeletal_mesh = mesh
    anim = unreal.AssetToolsHelpers.get_asset_tools().create_asset('A_' + name, TEMP, unreal.AnimSequence, factory)
    c = anim.controller
    c.open_bracket('Temporary turret acceptance preview', False)
    c.set_frame_rate(unreal.FrameRate(30, 1), False)
    c.set_number_of_frames(unreal.FrameNumber(90), False)
    for bone in unreal.SkeletonService.list_bones(mesh.get_path_name()):
        c.add_bone_curve(bone.bone_name, False)
        values = [unreal.Transform(rotation=unreal.Rotator(pitch=a)).multiply(bone.local_transform)
                  if str(bone.bone_name) == 'BarrelPitch' else bone.local_transform for a in range(-15, 76)]
        c.set_bone_track_keys(bone.bone_name, [t.translation for t in values],
                             [t.rotation for t in values], [t.scale3d for t in values], False)
    c.close_bracket(False)
    return anim


def start(original_map, original_camera):
    global STATE
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Temp/'), 'Use an isolated unsaved preview map'
    for actor in editor.get_all_level_actors():
        if actor.get_class() in [unreal.Actor.static_class(), unreal.SkeletalMeshActor.static_class()]:
            editor.destroy_actor(actor)
    manifest = json.loads((rigs.OUT / 'authoring-manifest.json').read_text(encoding='utf-8'))
    assets = {name: unreal.load_asset(rigs.DEST + '/SKM_SC_' + name) for name in rigs.COUNTS}
    animations = {name: make_animation(name, mesh) for name, mesh in assets.items()}
    actor = editor.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector())
    actor.set_actor_label('Temporary_ShipComponent_Acceptance')
    component = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    component.set_update_animation_in_editor(True)
    component.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    camera = editor.spawn_actor_from_class(unreal.CameraActor, unreal.Vector())
    camera_component = camera.get_component_by_class(unreal.CameraComponent)
    camera_component.set_field_of_view(35)
    camera_component.set_editor_property('aspect_ratio', 4/3)
    pp = unreal.PostProcessSettings()
    for k, v in dict(override_auto_exposure_method=True, auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
                     override_auto_exposure_apply_physical_camera_exposure=True, auto_exposure_apply_physical_camera_exposure=False,
                     override_auto_exposure_bias=True, auto_exposure_bias=0,
                     override_motion_blur_amount=True, motion_blur_amount=0).items():
        pp.set_editor_property(k, v)
    camera_component.set_editor_property('post_process_settings', pp)
    camera_component.set_editor_property('post_process_blend_weight', 1)
    for rotation, intensity, color in [((-48, -35, 0), 5, (1, .91, .8)),
                                        ((-25, 145, 0), 3, (.72, .86, 1)),
                                        ((65, 15, 0), 4, (1, .93, .84)),
                                        ((35, -155, 0), 2, (.8, .9, 1))]:
        light = editor.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(), unreal.Rotator(*rotation))
        lc = light.get_component_by_class(unreal.DirectionalLightComponent)
        lc.set_intensity(intensity)
        lc.set_light_color(unreal.LinearColor(*color, 1))
    editor.set_selected_level_actors([])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_set_viewport_realtime(True)
    names = list(rigs.COUNTS)
    STATE = {'phase': 'sweep', 'index': 0, 'configured': False, 'frame_wait': 0,
             'task': None, 'warm': 0, 'current_name': None, 'complete': False,
             'checks': {}, 'captures': [], 'original_map': original_map}
    sweep_specs = [(name, angle) for name in names for angle in range(-15, 76)]
    capture_specs = [(name, angle) for name in names for angle in [-15, 0, 30, 75]]
    for name in names:
        STATE['checks'][name] = {'poses': 0, 'socket_samples': 0, 'socket_max_position_error_cm': 0,
                                 'socket_max_direction_error': 0, 'preview_angle_max_error_deg': 0}

    def configure(name, angle, capture):
        if STATE['current_name'] != name:
            component.set_skeletal_mesh_asset(assets[name])
            component.play_animation(animations[name], False)
            component.set_play_rate(0)
            STATE['current_name'] = name
        component.set_position((angle + 15) / 30, False)
        if capture:
            lo, hi = [unreal.Vector(*v) for v in manifest[name]['motion_bounds_cm']]
            center = (lo + hi) * .5
            extent = max(rigs.vec(hi-lo))
            sign = manifest[name]['positive_elevation_mesh_z_sign']
            direction = unreal.Vector(1.1, 1.5, 1.1 * sign).normal()
            position = center + direction * extent * 2.6
            camera.set_actor_location(position, False, False)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, center), False)

    def check(name, angle):
        row = STATE['checks'][name]
        root = component.get_socket_transform('Root', unreal.RelativeTransformSpace.RTS_COMPONENT)
        assert rigs.transforms_match(root, unreal.Transform()), (name, 'base moved')
        actual_bone = component.get_socket_transform('BarrelPitch', unreal.RelativeTransformSpace.RTS_COMPONENT)
        desired_bone = unreal.Transform(rotation=unreal.Rotator(pitch=angle)).multiply(
            unreal.SkeletonService.list_bones(assets[name].get_path_name())[1].global_transform)
        dot = abs(sum(getattr(actual_bone.rotation, k)*getattr(desired_bone.rotation, k) for k in ['x','y','z','w']))
        error_angle = math.degrees(2*math.acos(min(1, dot)))
        # Temporary engine preview sequences use the project's normal compression.
        # Socket-to-bone accuracy is tested separately against the evaluated pose.
        assert error_angle < .3, (name, angle, error_angle)
        row['preview_angle_max_error_deg'] = max(row['preview_angle_max_error_deg'], error_angle)
        for i in range(1, rigs.COUNTS[name] + 1):
            socket_name = 'Socket_' + str(i)
            socket = assets[name].find_socket(socket_name)
            expected = unreal.Transform(location=socket.relative_location, rotation=socket.relative_rotation,
                                        scale=socket.relative_scale).multiply(actual_bone)
            actual = component.get_socket_transform(socket_name, unreal.RelativeTransformSpace.RTS_COMPONENT)
            position_error = (actual.translation - expected.translation).length()
            direction_error = (actual.rotation.rotate_vector(unreal.Vector(1,0,0)) -
                               actual_bone.rotation.rotate_vector(unreal.Vector(1,0,0))).length()
            assert position_error < .005 and direction_error < .00001, (name, angle, socket_name)
            row['socket_samples'] += 1
            row['socket_max_position_error_cm'] = max(row['socket_max_position_error_cm'], position_error)
            row['socket_max_direction_error'] = max(row['socket_max_direction_error'], direction_error)
        row['poses'] += 1

    def restore():
        component.set_position(.5, False)
        component.set_update_animation_in_editor(False)
        # Drop scene references before deleting the unsaved preview sequences.
        component.set_animation(None)
        component.set_skeletal_mesh_asset(None)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(original_map)
        if original_camera:
            unreal.EditorLevelLibrary.set_level_viewport_camera_info(*original_camera)
        for anim in animations.values():
            unreal.EditorAssetLibrary.delete_loaded_asset(anim)

    def tick(delta):
        try:
            specs = sweep_specs if STATE['phase'] == 'sweep' else capture_specs
            if STATE['index'] == len(specs):
                if STATE['phase'] == 'sweep':
                    rigs.write_json(rigs.OUT / 'ue-motion-validation.json', STATE['checks'])
                    STATE.update(phase='capture', index=0, configured=False)
                    return
                unreal.unregister_slate_post_tick_callback(STATE['handle'])
                restore()
                STATE.update(complete=True, phase='complete')
                rigs.write_json(rigs.OUT / 'ue-preview-report.json', {k:v for k,v in STATE.items() if k != 'task'})
                return
            name, angle = specs[STATE['index']]
            if not STATE['configured']:
                configure(name, angle, STATE['phase'] == 'capture')
                STATE.update(configured=True, frame_wait=3, warm=.8)
                return
            if STATE['frame_wait']:
                STATE['frame_wait'] -= 1
                return
            if STATE['phase'] == 'sweep':
                check(name, angle)
                STATE.update(index=STATE['index']+1, configured=False)
                return
            if STATE['task']:
                if not STATE['task'].is_task_done():return
                filename = name + '_UE_%+03d.png' % angle
                assert (rigs.OUT / filename).exists(), filename
                STATE['captures'].append(filename)
                STATE.update(index=STATE['index']+1, configured=False, task=None)
                return
            STATE['warm'] -= delta
            if STATE['warm'] > 0:return
            STATE['task'] = unreal.AutomationLibrary.take_high_res_screenshot(1280, 960,
                str(rigs.OUT / (name + '_UE_%+03d.png' % angle)), camera)
        except Exception:
            error = traceback.format_exc()
            unreal.unregister_slate_post_tick_callback(STATE['handle'])
            STATE.update(phase='error', error=error)
            rigs.write_json(rigs.OUT / 'ue-preview-error.json', STATE['error'])
            # Keep the isolated preview available for diagnosis; original map is recorded.
    STATE['handle'] = unreal.register_slate_post_tick_callback(tick)
    return {'sweep_poses': len(sweep_specs), 'captures': len(capture_specs), 'temporary_map': world.get_path_name()}

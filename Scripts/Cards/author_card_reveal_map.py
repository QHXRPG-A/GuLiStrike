"""Save the dedicated card demonstration map after the Blueprint checks pass."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = '/Game/GuLiStrike/Cards/RevealDemo'
MAP = ROOT + '/Maps/LVL_CardRevealDemo'
OUT = Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')
report = {'map': MAP, 'success': False}

try:
    commandlet = '-run=pythonscript' in unreal.SystemLibrary.get_command_line().lower()
    if not commandlet:
        assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Existing PIE must be ended by its owner first'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Current map has unsaved work'
    for p in ['Blueprints/BP_ParallaxRevealCard', 'Blueprints/BP_CardRevealDirector',
              'Blueprints/BP_CardRevealPlayerController', 'Blueprints/BP_CardRevealGameMode', 'UI/WBP_CardRevealHUD']:
        result = unreal.BlueprintService.compile_blueprint(ROOT + '/' + p)
        assert result.success, str(result)
    assert not unreal.EditorAssetLibrary.does_asset_exist(MAP), 'Map already exists: inspect it before editing'
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    gm = unreal.EditorAssetLibrary.load_blueprint_class(ROOT + '/Blueprints/BP_CardRevealGameMode')
    world.get_world_settings().set_editor_property('default_game_mode', gm)

    def spawn(cls, label, location=(0, 0, 0), rotation=unreal.Rotator()):
        a = actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation)
        assert a, label
        a.set_actor_label(label)
        a.set_folder_path('CardRevealDemo')
        return a

    director = spawn(unreal.EditorAssetLibrary.load_blueprint_class(ROOT + '/Blueprints/BP_CardRevealDirector'), 'CardReveal_Director')
    camera = director.get_component_by_class(unreal.CameraComponent)
    assert camera
    camera.set_editor_property('override_aspect_ratio_axis_constraint', True)
    camera.set_editor_property('aspect_ratio_axis_constraint', unreal.AspectRatioAxisConstraint.ASPECT_RATIO_MAINTAIN_XFOV)
    pp = unreal.PostProcessSettings()
    for key, value in {
        'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
        'override_auto_exposure_bias': True, 'auto_exposure_bias': 0.0,
        'override_motion_blur_amount': True, 'motion_blur_amount': 0.0,
        'override_bloom_intensity': True, 'bloom_intensity': 0.25,
        'override_vignette_intensity': True, 'vignette_intensity': 0.15}.items():
        pp.set_editor_property(key, value)
    camera.set_editor_property('post_process_settings', pp)
    camera.set_editor_property('post_process_blend_weight', 1.0)
    sun = spawn(unreal.DirectionalLight, 'CardReveal_Key', (0, 250, 120), unreal.Rotator(pitch=-25, yaw=-70))
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(2.5)
    sun.light_component.set_editor_property('light_color', unreal.Color(255, 243, 223, 255))
    sky = spawn(unreal.SkyLight, 'CardReveal_Ambient', (0, 0, 100)).light_component
    sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.set_intensity(0.7)
    spawn(unreal.PlayerStart, 'CardReveal_PlayerStart', (0, 140, 0), unreal.Rotator(yaw=-90))
    if not commandlet:
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(0, 140, 0), unreal.Rotator(yaw=-90))
        unreal.ViewportService.set_fov(50)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    # Read the actual saved scene entities and class references, rather than the intended input data.
    report['actors'] = [{'name': a.get_name(), 'label': a.get_actor_label(), 'class': a.get_class().get_path_name(),
                         'location': list(a.get_actor_location().to_tuple())} for a in actors.get_all_level_actors()]
    report['game_mode'] = world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
    report['controller'] = unreal.get_default_object(gm).get_editor_property('player_controller_class').get_path_name()
    report['camera'] = {'location': list(camera.get_world_location().to_tuple()), 'rotation': str(camera.get_world_rotation()),
                        'horizontal_fov': camera.field_of_view, 'fixed_exposure': str(pp.auto_exposure_method),
                        'constrain_aspect_ratio': camera.constrain_aspect_ratio}
    report['card_spawn_class'] = ROOT + '/Blueprints/BP_ParallaxRevealCard'
    report['card_count_at_start'] = 3
    report['cards_are_spawned_at_runtime'] = True
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
(OUT / 'saved-map.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

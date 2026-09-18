"""Read-only map/runtime lighting snapshot. Saves evidence, never UE packages."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/TestResults/GameplayLighting_20260918')
MAPS = ['/Game/Maps/LVL_CommanderMassPrototype', '/Game/Maps/LVL_ShipWingmanAirCombatPrototype']
PP_FIELDS = [
    'auto_exposure_method', 'auto_exposure_min_brightness', 'auto_exposure_max_brightness',
    'auto_exposure_bias', 'auto_exposure_apply_physical_camera_exposure',
    'auto_exposure_bias_curve', 'auto_exposure_meter_mask',
    'color_gamma', 'color_contrast', 'color_gain', 'color_offset', 'color_saturation',
    'color_gamma_shadows', 'color_gamma_midtones', 'color_gamma_highlights',
    'color_contrast_shadows', 'color_contrast_midtones', 'color_contrast_highlights',
    'scene_color_tint', 'white_temp', 'white_tint', 'film_slope', 'film_toe',
    'film_shoulder', 'film_black_clip', 'film_white_clip', 'vignette_intensity',
    'ambient_occlusion_intensity', 'bloom_intensity', 'color_grading_intensity',
    'color_grading_lut', 'local_exposure_highlight_contrast_scale',
    'local_exposure_shadow_contrast_scale', 'indirect_lighting_intensity',
    'dynamic_global_illumination_method', 'reflection_method', 'weighted_blendables',
]

def value(v):
    if v is None or isinstance(v, (str, int, float, bool)):
        return v
    if isinstance(v, unreal.Object):
        return v.get_path_name()
    if hasattr(v, 'export_text'):
        return v.export_text()
    return str(v)

def props(obj, names):
    result = {}
    for name in names:
        try:
            result[name] = value(obj.get_editor_property(name))
        except Exception:
            pass
    return result

def post_process(settings):
    return props(settings, PP_FIELDS + ['override_' + f for f in PP_FIELDS])

def snapshot(world):
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    rows = []
    for actor in actors:
        components = []
        for component in actor.get_components_by_class(unreal.ActorComponent):
            if isinstance(component, unreal.LightComponentBase):
                components.append({'name': component.get_name(), 'class': component.get_class().get_name(),
                    'properties': props(component, ['intensity', 'light_color', 'mobility', 'visible',
                        'cast_shadows', 'affects_world', 'indirect_lighting_intensity', 'volumetric_scattering_intensity',
                        'lower_hemisphere_is_black', 'lower_hemisphere_color', 'source_type', 'cubemap',
                        'real_time_capture', 'atmosphere_sun_light', 'light_source_angle'])})
            elif isinstance(component, unreal.CameraComponent):
                components.append({'name': component.get_name(), 'class': component.get_class().get_name(),
                    'properties': props(component, ['post_process_blend_weight', 'field_of_view']),
                    'post_process': post_process(component.get_editor_property('post_process_settings'))})
            elif isinstance(component, unreal.PostProcessComponent):
                components.append({'name': component.get_name(), 'class': component.get_class().get_name(),
                    'properties': props(component, ['enabled', 'unbound', 'priority', 'blend_weight']),
                    'post_process': post_process(component.get_editor_property('settings'))})
        if components or isinstance(actor, (unreal.PostProcessVolume, unreal.WorldSettings)):
            row = {'name': actor.get_name(), 'label': actor.get_actor_label(),
                'class': actor.get_class().get_name(), 'transform': actor.get_actor_transform().export_text(),
                'components': components}
            if isinstance(actor, unreal.PostProcessVolume):
                settings = actor.get_editor_property('settings')
                row['properties'] = props(actor, ['enabled', 'unbound', 'priority', 'blend_weight', 'blend_radius'])
                row['post_process'] = post_process(settings)
                row['post_process_export'] = settings.export_text()
            if isinstance(actor, unreal.WorldSettings):
                row['properties'] = props(actor, ['default_game_mode', 'force_no_precomputed_lighting'])
            rows.append(row)
    return {'world': world.get_path_name(), 'actor_count': len(actors), 'actors': rows}

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    editor_world = editor.get_editor_world()
    result = {'current_world': editor_world.get_path_name() if editor_world else None,
        'pie_running': unreal.WidgetService.is_pie_running(),
        'dirty_maps': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        'dirty_content': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
        'maps': [], 'runtime': [], 'cvars': {}}
    # Inspect only the requested world(s) when comparing the larger Demo reference.
    # Avoid keeping all three maps loaded merely to take one diagnostic snapshot.
    for map_path in getattr(unreal, '_guli_lighting_snapshot_maps', MAPS):
        world = unreal.load_asset(map_path)
        result['maps'].append(snapshot(world))
    for world in unreal.ObjectIterator(unreal.World):
        if 'UEDPIE_' in world.get_path_name():
            result['runtime'].append(snapshot(world))
    for name in ['r.EyeAdaptationQuality', 'r.DefaultFeature.AutoExposure',
            'r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange', 'r.TonemapperGamma',
            'r.ExposureOffset', 'r.Color.Mid', 'r.Color.Max', 'r.Color.Min']:
        result['cvars'][name] = unreal.SystemLibrary.get_console_variable_float_value(name)
    tag = getattr(unreal, '_guli_lighting_snapshot_tag', 'baseline')
    path = OUT / (tag + '.json')
    if path.exists():
        raise RuntimeError('Evidence already exists; select a new snapshot tag: ' + str(path))
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return {'evidence': str(path), 'current_world': result['current_world'],
        'pie_running': result['pie_running'], 'map_counts': [m['actor_count'] for m in result['maps']],
        'runtime_worlds': [m['world'] for m in result['runtime']], 'dirty_maps': result['dirty_maps'],
        'dirty_content': result['dirty_content']}

unreal.MCPythonHelper.submit_result(json.dumps(main(), ensure_ascii=False))

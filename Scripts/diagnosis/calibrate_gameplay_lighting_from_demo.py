"""Apply the freshly captured Demo lighting style to an explicitly selected world.

Library only: loading this file does not change or save a UE world. Preview in PIE
first. Formal application requires allow_formal=True, and saving is a separate step.
"""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/TestResults/GameplayLighting_20260918')
TARGETS = ('LVL_CommanderMassPrototype', 'LVL_ShipWingmanAirCombatPrototype')
PP_FIELDS = [
    'auto_exposure_method', 'auto_exposure_min_brightness', 'auto_exposure_max_brightness',
    'auto_exposure_bias', 'auto_exposure_bias_curve', 'auto_exposure_meter_mask',
    'white_temp', 'white_tint', 'scene_color_tint',
    'film_slope', 'film_toe', 'film_shoulder', 'film_black_clip', 'film_white_clip',
    'vignette_intensity', 'ambient_occlusion_intensity',
    'color_grading_lut', 'color_grading_intensity',
    'local_exposure_highlight_contrast_scale', 'local_exposure_shadow_contrast_scale',
]
for group in ('', '_shadows', '_midtones', '_highlights'):
    for control in ('saturation', 'contrast', 'gamma', 'gain', 'offset'):
        PP_FIELDS.append('color_' + control + group)


def printable(value):
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return value.export_text() if hasattr(value, 'export_text') else str(value)


def actor_of_class(world, actor_class):
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, actor_class)
    if len(actors) != 1:
        raise RuntimeError('Expected one ' + actor_class.__name__ + ', got ' + str(len(actors)))
    return actors[0]


def calibrate(world, tag, copy_lights=True, allow_formal=False):
    path = world.get_path_name()
    if not any(path.endswith('.' + name) or path.endswith('_' + name) for name in TARGETS):
        raise RuntimeError('World outside the two approved gameplay maps: ' + path)
    preview = 'UEDPIE_' in path
    if not preview and (not allow_formal or unreal.WidgetService.is_pie_running()):
        raise RuntimeError('Preview in PIE first; formal writes require explicit opt-in outside PIE.')
    if not preview and unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError('Preserve unrelated or user map edits; inspect dirty maps first.')
    report_path = OUT / (tag + '.json')
    if report_path.exists():
        raise RuntimeError('Do not overwrite evidence: ' + str(report_path))

    reference = json.loads((OUT / 'demo-reference-restart.json').read_text(encoding='utf-8'))
    demo = reference['maps'][0]
    if demo['world'] != '/Game/StylizedPineEnvironment/Maps/Demo_Map.Demo_Map':
        raise RuntimeError('Unexpected reference world')
    source_pp = next(a for a in demo['actors'] if a['class'] == 'PostProcessVolume')
    settings_ref = unreal.PostProcessSettings()
    if not settings_ref.import_text(source_pp['post_process_export']):
        raise RuntimeError('Cannot parse reference post process')
    pp = actor_of_class(world, unreal.PostProcessVolume)
    settings = pp.get_editor_property('settings')
    report = {'world': path, 'reference': demo['world'], 'preview': preview,
              'postprocess_actor': pp.get_name(), 'before_export': settings.export_text(),
              'changes': [], 'saved_packages': []}
    # Validate the full allowlist before touching any actor. Preserve bloom, motion
    # blur, blendables, GI/reflection method, navigation and all gameplay settings.
    values = {name: settings_ref.get_editor_property(name)
              for field in PP_FIELDS for name in (field, 'override_' + field)}
    # The approved visual reference was shown with the editor's fixed EV100=0,
    # not Demo_Map's 0.5..0.6 adaptive range. Reproduce that actual appearance in
    # gameplay; copying only the map's numbers would retain a viewport/PIE mismatch.
    viewport = json.loads((OUT / 'reference-viewport.json').read_text(encoding='utf-8'))
    if not viewport['exposure_fixed']:
        raise RuntimeError('Reference no longer uses fixed exposure; recalibrate from evidence.')
    if unreal.SystemLibrary.get_console_variable_int_value(
            'r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange') != 1:
        raise RuntimeError('Expected the project EV100 exposure range; do not change global settings.')
    values.update(auto_exposure_method=unreal.AutoExposureMethod.AEM_HISTOGRAM,
                  override_auto_exposure_method=True,
                  auto_exposure_min_brightness=viewport['exposure_ev100'],
                  auto_exposure_max_brightness=viewport['exposure_ev100'],
                  auto_exposure_bias=0.0,
                  override_auto_exposure_min_brightness=True,
                  override_auto_exposure_max_brightness=True,
                  override_auto_exposure_bias=True)
    report['reference_viewport'] = viewport
    for name, value in values.items():
        before = printable(settings.get_editor_property(name))
        settings.set_editor_property(name, value)
        after = printable(settings.get_editor_property(name))
        if before != after:
            report['changes'].append({'owner': pp.get_name(), 'field': name,
                                      'before': before, 'after': after})
    pp.modify()
    pp.set_editor_property('settings', settings)
    report['postprocess_readback'] = {
        name: printable(pp.get_editor_property('settings').get_editor_property(name)) for name in values}

    if copy_lights:
        for actor_class, component_class in (
                (unreal.DirectionalLight, unreal.DirectionalLightComponent),
                (unreal.SkyLight, unreal.SkyLightComponent)):
            actor = actor_of_class(world, actor_class)
            component = actor.get_component_by_class(component_class)
            source = next(a for a in demo['actors'] if a['class'] == actor_class.__name__)
            props = source['components'][0]['properties']
            names = ['intensity', 'light_color', 'indirect_lighting_intensity']
            if actor_class == unreal.SkyLight:
                names += ['lower_hemisphere_color', 'lower_hemisphere_is_black']
            actor.modify()
            component.modify()
            for name in names:
                before = component.get_editor_property(name)
                value = props[name]
                # The two prototype maps use a different lit checker landscape.
                # PIE v1 proved Demo's sun=3 was too dark; v2 at 45 preserves the
                # readable ground while leaving unlit cel unit colors unchanged.
                if actor_class == unreal.DirectionalLight and name == 'intensity':
                    value = 45.0
                if hasattr(before, 'import_text'):
                    value = type(before)()
                    if not value.import_text(props[name]):
                        raise RuntimeError('Cannot parse reference light field ' + name)
                old = printable(before)
                component.set_editor_property(name, value)
                after = printable(component.get_editor_property(name))
                if old != after:
                    report['changes'].append({'owner': actor.get_name(), 'field': name,
                                              'before': old, 'after': after})
    report['success'] = True
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return {'world': path, 'preview': preview, 'changes': len(report['changes']),
            'evidence': str(report_path), 'saved_packages': []}

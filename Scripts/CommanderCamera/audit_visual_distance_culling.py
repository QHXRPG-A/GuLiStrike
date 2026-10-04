"""Read registered VFX and their distance gates through UE APIs; never reads asset bytes."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / globals().get('GULI_DISTANCE_AUDIT_OUT', 'ArtSource/WarMachineHover_20260930/LOD2/asset-audit.json')
NS = unreal.NiagaraService
report = {'success': False, 'systems': [], 'effect_types': {}, 'renderer_distance_gates': [], 'errors': []}


def distance_settings(settings):
    return [{'cull_by_distance': bool(s.get_editor_property('cull_by_distance')),
             'max_distance_cm': float(s.get_editor_property('max_distance')),
             'full_settings': str(s)} for s in settings]


try:
    table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    report['registry_rows'] = len(rows)
    systems = {}
    for row in rows:
        path = row['ResourcePath']
        if not path or path in systems:
            continue
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.NiagaraSystem):
            systems[path] = asset
    effect_types = {}
    for path, system in systems.items():
        try:
            effect = system.get_editor_property('effect_type')
            if effect:
                effect_types[effect.get_path_name()] = effect
            overrides = system.get_editor_property('system_scalability_overrides').get_editor_property('overrides')
            settings = NS.get_all_editable_settings(path)
            distance_parameters = [str(p) for p in settings.rapid_iteration_parameters
                                   if any(x in p.setting_path.lower() for x in ['distance', 'cull', 'screensize'])]
            emitter_states = []
            for emitter in NS.list_emitters(path):
                name = str(emitter.emitter_name)
                for module in unreal.NiagaraEmitterService.list_modules(path, name):
                    if not str(module.module_name).startswith('EmitterState'):
                        continue
                    emitter_states.append({'emitter': name, 'module': str(module.module_name),
                        'distance_culling': unreal.NiagaraEmitterService.get_module_input(path, name, str(module.module_name), 'Distance'),
                        'scalability_mode': unreal.NiagaraEmitterService.get_module_input(path, name, str(module.module_name), 'Scalability')})
            report['systems'].append({'path': path, 'effect_type': effect.get_path_name() if effect else None,
                                      'override_scalability_settings': bool(system.get_editor_property('override_scalability_settings')),
                                      'overrides': distance_settings(overrides), 'distance_parameters': distance_parameters,
                                      'emitter_states': emitter_states})
        except Exception:
            report['errors'].append({'path': path, 'error': traceback.format_exc()})
    # These generators still support legacy project assets; audit them so a rebuild
    # cannot silently restore an old limit even when the current registry moved on.
    for path in ['/Game/GuLiStrike/FX/UnitFeedback/FXT_UnitDestruction',
                 '/Game/GuLiStrike/FX/WingmanFlight/FXT_WingmanFlight']:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            effect = unreal.load_asset(path)
            effect_types[effect.get_path_name()] = effect
    for path, effect in effect_types.items():
        report['effect_types'][path] = distance_settings(
            effect.get_editor_property('system_scalability_settings').get_editor_property('settings'))
    packages = {asset.get_outermost().get_path_name() for asset in systems.values()}
    inspected = 0
    for renderer in unreal.ObjectIterator(unreal.NiagaraRendererProperties):
        if renderer.get_outermost().get_path_name() not in packages:
            continue
        if not isinstance(renderer, unreal.NiagaraSpriteRendererProperties) and renderer.get_class().get_name() != 'NiagaraMeshRendererProperties':
            continue
        inspected += 1
        if renderer.get_editor_property('bEnableCameraDistanceCulling'):
            report['renderer_distance_gates'].append({'object': renderer.get_path_name(),
                'min_cm': float(renderer.get_editor_property('MinCameraDistance')),
                'max_cm': float(renderer.get_editor_property('MaxCameraDistance'))})
    report['renderer_objects_inspected'] = inspected
    report['success'] = not report['errors']
except Exception:
    report['errors'].append({'error': traceback.format_exc()})
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'systems': len(report['systems']),
    'effect_types': report['effect_types'], 'renderer_distance_gates': report['renderer_distance_gates'],
    'errors': report['errors'], 'report': str(OUT)}))

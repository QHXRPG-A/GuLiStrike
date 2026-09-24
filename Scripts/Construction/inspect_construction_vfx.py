"""Read-only source audit; all UE object references remain function-local."""
import gc
import json
from pathlib import Path
import unreal


def run():
    systems = {}
    for path in [
        '/Game/Assets/VFX/NiagaraUpgradeGlow/Particles/P_UpgradeGlow011_Converted',
        '/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green',
    ]:
        info = unreal.NiagaraService.get_system_info(path)
        data = {'info': str(info), 'emitters': {}}
        for emitter in unreal.NiagaraService.list_emitters(path):
            name = str(emitter.emitter_name)
            data['emitters'][name] = {
                'properties': str(unreal.NiagaraEmitterService.get_emitter_properties(path, name)),
                'modules': [{'name': m.module_name, 'stage': m.module_type,
                             'path': m.script_asset_path, 'enabled': m.is_enabled}
                            for m in unreal.NiagaraEmitterService.list_modules(path, name)],
                'renderers': [str(r) for r in unreal.NiagaraEmitterService.list_renderers(path, name)],
                'renderer_details': [str(unreal.NiagaraEmitterService.get_renderer_details(path, name, r.renderer_index))
                                     for r in unreal.NiagaraEmitterService.list_renderers(path, name)],
                'parameters': [{'name': p.input_name, 'type': p.input_type, 'value': p.current_value}
                               for p in unreal.NiagaraEmitterService.get_rapid_iteration_parameters(path, name, '')],
            }
        systems[path] = data
    body = unreal.load_asset('/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Meshes/SM_RPF_Body')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    return {'systems': systems, 'body_bounds': str(body.get_bounding_box()),
            'map': world.get_path_name(),
            'existing_candidates': list(unreal.NiagaraService.search_systems('/Game/GuLiStrike/Buildings/Construction', ''))}


def main():
    try:
        report = run()
        report['success'] = True
    except Exception as error:
        import traceback
        report = {'success': False, 'error': str(error), 'trace': traceback.format_exc()}
    out = Path('D:/UE5.7/test1/outputs/construction-vfx')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'source-inspection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'error': report.get('error'), 'report': str(out / 'source-inspection.json')}))


main()
gc.collect()

"""Read production and prior candidates through Unreal; do not change assets."""
import json
from pathlib import Path
import unreal


def audit():
    out = Path(unreal.Paths.project_dir()) / 'outputs/performance/20261009-all-optimizations'
    rows = []
    for role in ('Muzzle', 'Impact'):
        for suffix in ('Optimized', 'AllOptimizations'):
            path = '/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun' + role + '_' + suffix
            asset = unreal.load_asset(path)
            row = {'path': path, 'properties': {}, 'emitters': [], 'parameters': []}
            for key in ('fixed_bounds', 'fixed_bounds_enabled', 'effect_type', 'override_scalability_settings',
                        'warmup_time', 'warmup_tick_count', 'max_time_without_render'):
                try:
                    row['properties'][key] = str(asset.get_editor_property(key))
                except Exception:
                    pass
            settings = unreal.NiagaraService.get_all_editable_settings(path)
            for param in settings.rapid_iteration_parameters:
                row['parameters'].append({'path': str(param.setting_path), 'value': str(param.current_value)})
            for name in ('Glow', 'Flash', 'RibbonCore', 'RIbbonTrailFollower', 'Sparks', 'Debris'):
                row['emitters'].append({'name': name, 'modules': [str(x) for x in unreal.NiagaraEmitterService.list_modules(path, name)]})
            rows.append(row)
    lasers = []
    for role, base in [('Mining', '/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized'),
                       ('Construction', '/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized')]:
        for suffix in ('', '_Nodes16', '_Nodes8', '_GPU_All'):
            path = base + suffix
            if not unreal.EditorAssetLibrary.does_asset_exist(path):
                continue
            settings = unreal.NiagaraService.get_all_editable_settings(path)
            counts = [{'path': str(p.setting_path), 'value': str(p.current_value)} for p in settings.rapid_iteration_parameters
                      if 'SpawnBurst_Instantaneous.Spawn Count' in str(p.setting_path)]
            lasers.append({'role': role, 'path': path, 'compiled_node_counts': counts})
    report = {'success': True, 'flashes': rows, 'lasers': lasers}
    (out / 'assets-final-module-audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'flashes': len(rows), 'lasers': lasers}))


audit()

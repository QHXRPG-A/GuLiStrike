"""Read-only inventory of active effects visible in the authorized runtime session."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')


def run():
    worlds = {s.get_world() for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem)
              if s.get_world() and 'UEDPIE_' in s.get_world().get_path_name()}
    effects = []
    for component in unreal.ObjectIterator(unreal.NiagaraComponent):
        if component.get_world() not in worlds:
            continue
        system = component.get_asset()
        if not system:
            continue
        point = component.get_world_location()
        effects.append({'component': component.get_path_name(), 'system': system.get_path_name(),
                        'position': list(point.to_tuple()), 'active': component.is_active()})
    report = {'effects': effects, 'systems': sorted({e['system'] for e in effects}), 'read_only': True}
    (OUT / 'dark-particle-source-inventory.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return {'components': len(effects), 'systems': report['systems'], 'near_selected': [e for e in effects if e['active'] and -15000 < e['position'][0] < 0 and 62000 < e['position'][1] < 70000]}


unreal.MCPythonHelper.submit_result(json.dumps(run()))

"""Re-export an existing acceptance profile without recreating or editing assets.

Run in the idle source editor after an authorized native build.
"""
import hashlib
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
REPORT = Path(globals().get('REPORT', ROOT / 'TestResults/HybridFullAcceptance/Implementation/manifest-resource-supply-readback.json'))
result = {'passed': False, 'scope': 'Manifest export/readback only; no gameplay pass', 'loads': []}
try:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split('.')[0] != '/Game/Maps/LVL_CommanderMassPrototype':
        if not unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/LVL_CommanderMassPrototype'):
            raise RuntimeError('Original prototype map did not load')
    profile_path = ROOT / 'Config/Hybrid/AcceptanceProfile.json'
    profile = json.loads(profile_path.read_text(encoding='utf-8-sig'))
    overrides = profile['overrides']
    setting = {'class': '/Script/GuLiStrike.GuLiHybridReviewSubsystem',
               'property': 'bEnableResourceSupply', 'value': 'True'}
    overrides[:] = [v for v in overrides if (v['class'], v['property']) != (setting['class'], setting['property'])]
    overrides.append(setting)
    for population in (200, 600):
        for acceptance in (True, False):
            path = ROOT / f"Config/Hybrid/{'Acceptance' if acceptance else 'Runtime'}Manifest-{population}.json"
            exported = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest_with_overrides(
                population, str(path), json.dumps(overrides)) if acceptance else
                unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
            if not exported.get('success'):
                raise RuntimeError('Manifest export failed: ' + json.dumps(exported))
            validation = json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
            if not validation.get('success'):
                raise RuntimeError('Manifest validation failed: ' + json.dumps(validation))
            manifest = json.loads(path.read_text(encoding='utf-8-sig'))
            supplies = {r['id'] for r in manifest['rules'] if r['id'].startswith('acceptance/resource_supply/')}
            expected = {f'acceptance/resource_supply/{c}/{1 << n}' for c in ('blue', 'red') for n in range(11)} if acceptance else set()
            if supplies != expected or bool(manifest['numbers'].get('acceptance/resource_supply', 0)) != acceptance:
                raise RuntimeError('Supply rules leaked into production or are missing from the explicit acceptance profile')
            info = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                    'config_hash': manifest['config_hash'], 'map_hash': manifest['map_hash'],
                    'objects': len(manifest['objects'])}
            if acceptance:
                profile['manifests'][str(population)] = info
            result['loads'].append({**info, 'population': population, 'acceptance': acceptance, 'supply_rules': len(supplies)})
    sections = {}
    for value in overrides:
        sections.setdefault(value['class'], []).append(value['property']+'='+value['value'])
    config = ROOT / 'Config/Custom/HybridAcceptance/DefaultGame.ini'
    config.write_text('\n\n'.join('['+key+']\n'+'\n'.join(values) for key, values in sections.items())+'\n', encoding='utf-8')
    profile_path.write_text(json.dumps(profile, ensure_ascii=False, indent=2), encoding='utf-8')
    result['passed'] = True
except Exception as error:
    result['error'] = str(error)
    raise
finally:
    REPORT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')

"""Scan and verify the delivered packages through the live Unreal editor."""

import collections
import json
from pathlib import Path
import traceback
import unreal


REPORTS = Path('D:/UE5.7/test1/TestResults/AssetMigration20261002')
migration = json.loads((REPORTS / 'unreal_migration.json').read_text(encoding='utf-8'))
report = {'passed': False, 'packs': [], 'phase': 'starting'}


def checkpoint():
    (REPORTS / 'project_validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


try:
    actual_project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())).resolve()
    if actual_project != Path('D:/UE5.7/test1/GuLiStrike.uproject').resolve():
        raise RuntimeError('Live editor is not the destination project')
    if not migration.get('passed'):
        raise RuntimeError('Scratch migration validation did not pass')
    report['editor_world_before'] = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    targets = [p['target'] for p in migration['packs']]
    registry.scan_paths_synchronous(targets, True)
    registry.wait_for_completion()
    options = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True, include_soft_package_references=True)
    original_roots = [p['source'] for p in migration['packs']]
    for pack in migration['packs']:
        report['phase'] = 'validating ' + pack['root']
        checkpoint()
        assets = [a for a in registry.get_assets_by_path(pack['target'], True)
                  if not str(a.asset_class_path.asset_name).endswith('GeneratedClass')]
        packages = sorted({str(a.package_name) for a in assets})
        state = {'root': pack['root'], 'target': pack['target'], 'package_count': len(packages),
                 'classes': dict(collections.Counter(str(a.asset_class_path.asset_name) for a in assets)),
                 'inventory_matches': packages == sorted(pack['destination_packages']),
                 'load_failures': [], 'missing_game_dependencies': {}, 'old_path_dependencies': {}, 'blueprint_packages': []}
        report['packs'].append(state)
        for package in packages:
            obj = unreal.EditorAssetLibrary.load_asset(package)
            if obj is None:
                state['load_failures'].append(package)
            elif isinstance(obj, unreal.Blueprint):
                state['blueprint_packages'].append(package)
            deps = [str(d) for d in registry.get_dependencies(package, options) or []]
            missing = [d for d in deps if d.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(d)]
            old_paths = [d for d in deps if any(d.startswith(root + '/') for root in original_roots)]
            if missing:
                state['missing_game_dependencies'][package] = missing
            if old_paths:
                state['old_path_dependencies'][package] = old_paths
        state['passed'] = state['inventory_matches'] and not any(state[k] for k in
                         ['load_failures', 'missing_game_dependencies', 'old_path_dependencies'])
        checkpoint()
    report['editor_world_after'] = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
    report['world_unchanged'] = report['editor_world_before'] == report['editor_world_after']
    report['total_packages'] = sum(p['package_count'] for p in report['packs'])
    report['passed'] = report['world_unchanged'] and all(p['passed'] for p in report['packs'])
    report['phase'] = 'complete'
except Exception:
    report['error'] = traceback.format_exc()
    report['phase'] = 'failed'
finally:
    checkpoint()
    print(json.dumps({'passed': report['passed'], 'phase': report['phase'], 'total_packages': report.get('total_packages'),
                      'packs': [{k: p[k] for k in ['target', 'package_count', 'passed']} for p in report['packs'] if 'passed' in p],
                      'error': report.get('error')}, ensure_ascii=False))

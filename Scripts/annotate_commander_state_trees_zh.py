"""Add Chinese descriptions to the existing Commander StateTree states.

Run in an idle UE editor through Scripts/commander_editor_python.py. This edits
only UStateTreeState.Description. Existing graphs, names and user-written
Chinese descriptions are preserved. It recompiles and saves the existing assets
with gs.Commander.RefreshStateTrees, then recursively reads them back.
"""

import json
import re
import shutil
import traceback
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/CommanderStateTree/ChineseDescriptions'
NAMES = ('ST_CommanderMiner', 'ST_CommanderBuilder', 'ST_CommanderMass')
DESCRIPTIONS = json.loads((OUT / 'descriptions.zh-CN.json').read_text(encoding='utf-8'))
REPORT = {'success': False, 'assets': [], 'gameplay_started': False}


def has_chinese(text):
    return bool(re.search(r'[\u3400-\u9fff]', text or ''))


def walk_readback(state):
    yield state
    for child in state['children']:
        yield from walk_readback(child)


def editor_data(asset):
    for index in range(20):
        name = 'CommanderHierarchyEditorData_' + str(index)
        found = unreal.find_object(asset, name, unreal.StateTreeEditorData.static_class())
        if found:
            return found
    raise RuntimeError('Could not locate existing StateTree editor data: ' + asset.get_path_name())


def actual_states(data):
    result = {}

    def collect(outer, prefix):
        for index in range(200):
            state = unreal.find_object(outer, 'StateTreeState_' + str(index), unreal.StateTreeState.static_class())
            if not state:
                continue
            name = str(state.get_editor_property('name'))
            path = prefix + '/' + name if prefix else name
            if path in result:
                raise RuntimeError('Duplicate state path: ' + path)
            result[path] = state
            collect(state, path)

    collect(data, '')
    return result


def inspect(world):
    path = ROOT / 'Artifacts/CommanderStateTree/hierarchy-readback.json'
    path.unlink(missing_ok=True)
    unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.InspectStateTrees')
    if not path.exists():
        raise RuntimeError('Read-only StateTree inspector is unavailable in the loaded editor.')
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    if not report['success'] or len(report['assets']) != 3:
        raise RuntimeError('StateTree inspection failed; no descriptions were accepted.')
    return report


def graph_without_descriptions(asset):
    # Compare the complete exported graph while excluding only the intended text field.
    graph = json.loads(json.dumps(asset, ensure_ascii=False))

    def strip(state):
        state.pop('description', None)
        for child in state['children']:
            strip(child)

    for root in graph['roots']:
        strip(root)
    for key in ('editor_hash', 'compiled_matches_editor', 'dirty', 'mutated', 'ready', 'valid'):
        graph.pop(key, None)
    return graph


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError('A PIE/Standalone world is active. End that session before editing the three StateTrees.')
    world = editor.get_editor_world()
    if world is None:
        raise RuntimeError('No idle editor world is available.')

    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() or [])
    target_packages = {'/Game/GuLiStrike/Commander/Behavior/' + name for name in NAMES}
    conflicting = sorted(package.get_path_name() for package in dirty
                         if package.get_path_name() in target_packages)
    if conflicting:
        raise RuntimeError('Target StateTree has unsaved edits: ' + ', '.join(conflicting))

    before = inspect(world)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'before.json').write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
    expected_names = {state['name'] for asset in before['assets'] for root in asset['roots']
                      for state in walk_readback(root)}
    if set(DESCRIPTIONS) != expected_names or not all(has_chinese(text) for text in DESCRIPTIONS.values()):
        raise RuntimeError('Chinese description catalog no longer matches the actual tree states.')

    targets = []
    for expected in before['assets']:
        name = expected['asset'].split('.')[-1]
        if name not in NAMES or expected['hierarchy_version'] != '2' or expected['dirty']:
            raise RuntimeError('Unexpected or unsaved asset in readback: ' + name)
        asset = unreal.load_asset('/Game/GuLiStrike/Commander/Behavior/' + name)
        states = actual_states(editor_data(asset))
        paths = {state['path'] for root in expected['roots'] for state in walk_readback(root)}
        if set(states) != paths or len(states) != expected['state_count']:
            raise RuntimeError('Actual editor objects do not match read-only hierarchy: ' + name)
        targets.append((name, asset, expected, states))

    backup = OUT / 'Backup'
    backup.mkdir(parents=True, exist_ok=True)
    for name, _, _, _ in targets:
        source = ROOT / 'Content/GuLiStrike/Commander/Behavior' / (name + '.uasset')
        destination = backup / (name + '.uasset')
        if not source.is_file():
            raise RuntimeError('Saved source asset is missing: ' + str(source))
        if not destination.exists():
            shutil.copy2(source, destination)
        REPORT['assets'].append({'asset': name, 'backup': str(destination),
                                 'backup_size': destination.stat().st_size, 'edited': [], 'preserved_chinese': []})

    with unreal.ScopedEditorTransaction('Describe Commander StateTrees in Chinese'):
        for (name, asset, expected, states), entry in zip(targets, REPORT['assets']):
            for root in expected['roots']:
                for item in walk_readback(root):
                    state = states[item['path']]
                    previous = state.get_editor_property('description') or ''
                    if has_chinese(previous):
                        entry['preserved_chinese'].append(item['path'])
                        continue
                    current = DESCRIPTIONS[item['name']]
                    state.modify()
                    state.set_editor_property('description', current)
                    entry['edited'].append({'path': item['path'], 'previous': previous, 'description': current})

    refresh_path = ROOT / 'Artifacts/CommanderStateTree/tree-refresh.json'
    refresh_path.unlink(missing_ok=True)
    unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.RefreshStateTrees')
    if not refresh_path.exists():
        raise RuntimeError('StateTree refresh command is unavailable; descriptions remain unsaved in the editor.')
    refresh = json.loads(refresh_path.read_text(encoding='utf-8-sig'))
    (OUT / 'refresh.json').write_text(json.dumps(refresh, ensure_ascii=False, indent=2), encoding='utf-8')
    if not refresh['success'] or len(refresh['assets']) != 3:
        raise RuntimeError('One of the three StateTrees did not compile and save. Inspect refresh.json.')

    after = inspect(world)
    (OUT / 'after.json').write_text(json.dumps(after, ensure_ascii=False, indent=2), encoding='utf-8')
    before_by_name = {a['asset'].split('.')[-1]: a for a in before['assets']}
    for asset in after['assets']:
        name = asset['asset'].split('.')[-1]
        if graph_without_descriptions(asset) != graph_without_descriptions(before_by_name[name]):
            raise RuntimeError('Graph or policy changed while describing ' + name)
        if not asset['ready'] or asset['dirty'] or not asset['compiled_matches_editor']:
            raise RuntimeError('Compiled readback is incomplete for ' + name)
        all_states = [state for root in asset['roots'] for state in walk_readback(root)]
        if len(all_states) != before_by_name[name]['state_count'] or not all(
                has_chinese(state['description']) for state in all_states):
            raise RuntimeError('At least one state lacks a Chinese description in ' + name)
        next(entry for entry in REPORT['assets'] if entry['asset'] == name)['state_count'] = len(all_states)

    REPORT['success'] = True
    REPORT['edited_count'] = sum(len(item['edited']) for item in REPORT['assets'])
    REPORT['preserved_chinese_count'] = sum(len(item['preserved_chinese']) for item in REPORT['assets'])
    REPORT['graph_unchanged'] = True
    REPORT['compiled_and_saved'] = True


try:
    run()
except Exception:
    REPORT['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'delivery.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': REPORT['success'],
    'error': REPORT.get('error'),
    'state_counts': {asset['asset']: asset.get('state_count') for asset in REPORT['assets']},
    'edited_count': REPORT.get('edited_count'),
    'preserved_chinese_count': REPORT.get('preserved_chinese_count'),
    'graph_unchanged': REPORT.get('graph_unchanged'),
    'compiled_and_saved': REPORT.get('compiled_and_saved'),
}))

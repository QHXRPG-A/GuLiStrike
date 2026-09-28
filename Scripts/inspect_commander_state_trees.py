"""Read existing Commander StateTrees and four unit bindings in the live editor.

Never creates, compiles, saves, imports, or regenerates UE assets.
The C++ inspector recursively reads UStateTreeEditorData, including user edits.
"""
import json
from pathlib import Path
import unreal


def inspect():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    out = root / 'Artifacts/CommanderStateTree'
    out.mkdir(parents=True, exist_ok=True)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    path = out / 'hierarchy-readback.json'
    path.unlink(missing_ok=True)
    unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.InspectStateTrees')
    if not path.exists():
        raise RuntimeError('Inspector command is unavailable. Load the approved new native build first.')
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    table_path = '/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers'
    table = unreal.load_asset(table_path)
    if not table:
        raise RuntimeError('Existing Soldiers table is missing.')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    expected = {1: 'ST_CommanderMass', 2: 'ST_CommanderMass', 3: 'ST_CommanderMiner', 4: 'ST_CommanderBuilder'}
    actual = {int(row['Id']): str(row['StateTreeAsset']) for row in rows}
    match = set(actual) == set(expected) and all(
        '/Game/GuLiStrike/Commander/Behavior/' + name in actual[unit]
        for unit, name in expected.items())
    report['unit_bindings'] = actual
    report['soldiers_table'] = table_path
    report['four_bindings_match'] = match
    report['success'] = bool(report['success'] and match and len(report['assets']) == 3)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    (out / 'tree-assets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    (out / 'soldiers-readback.json').write_text(json.dumps({
        'success': match, 'read_only': True, 'table': table_path, 'rows': rows,
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    result = inspect()
    unreal.MCPythonHelper.submit_result(json.dumps(result))

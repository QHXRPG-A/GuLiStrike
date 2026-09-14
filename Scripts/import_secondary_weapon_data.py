"""Import the single secondary-weapon workbook and wire its projectile profiles.

Run after export/build, via Scripts/ue_exec.py or source-engine UnrealEditor-Cmd.
Imports the unified SpellFields table first, then the SecondaryWeapons tables.
Does not recreate loadouts or overwrite tuning.
"""
import json
from pathlib import Path
import traceback
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out = root / 'TestResults/SecondaryWeapons'
out.mkdir(parents=True, exist_ok=True)
report = {'tables': [], 'projectile_profiles': [], 'errors': [], 'passed': False}
try:
    # LevelEditor has no tab manager in a Python commandlet; querying PIE there
    # dereferences editor-only state. Commandlets cannot be in PIE.
    if '-run=pythonscript' not in unreal.SystemLibrary.get_command_line().lower():
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level and level.is_in_play_in_editor():
            raise RuntimeError('Stop PIE before importing secondary weapon configuration')
    namespace = {'__name__': 'secondary_weapon_import'}
    source = (root / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    definitions = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
    exec(compile(definitions, 'import_data_to_engine.py', 'exec'), namespace)
    namespace['PROGRESS'] = str(out / 'import-progress.log')
    manifest = json.loads((root / 'Data/Json/manifest.json').read_text(encoding='utf-8'))
    field_table = 'DT_GuLiStrikeSpellFields_Fields'
    report['tables'].append(namespace['import_table'](field_table, manifest['tables'][field_table]))
    if not report['tables'][-1].get('imported'):
        raise RuntimeError('Import the unified SpellFields dependency before weapon tables')
    for name, config in manifest['tables'].items():
        if any(s['excel'] == 'GuLiStrikeSecondaryWeapons.xlsx' for s in config.get('sources', [config])):
            report['tables'].append(namespace['import_table'](name, config))
    if not report['tables'] or not all(entry.get('imported') for entry in report['tables']):
        raise RuntimeError('Secondary weapon table import/readback failed')
    report['projectile_profiles'] = namespace['wire_secondary_projectile_profiles']()
    report['wingman_profiles'] = namespace['wire_secondary_wingman_profiles']()
    report['passed'] = True
except Exception:
    report['errors'].append(traceback.format_exc())
finally:
    (out / 'import-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': report['passed'], 'tables': len(report['tables']), 'errors': report['errors']}, ensure_ascii=True))
if not report['passed']:
    raise RuntimeError('Secondary weapon import failed; see TestResults/SecondaryWeapons/import-report.json')

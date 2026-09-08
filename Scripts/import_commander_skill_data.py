"""Import only Commander skill tables, using the existing verified CSV import path.

Run with Scripts/ue_exec.py or UnrealEditor-Cmd -ExecutePythonScript.
Avoids touching unrelated Ship blueprints or tables.
"""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
namespace = {'__name__': 'commander_skill_import'}
source = (root / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
# Reuse definitions, excluding the import-all application section.
definitions = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
exec(compile(definitions, 'import_data_to_engine.py', 'exec'), namespace)
out_dir = root / 'outputs/skill-bridge'
out_dir.mkdir(parents=True, exist_ok=True)
namespace['PROGRESS'] = str(out_dir / 'import_progress.log')
manifest = json.loads((root / 'data/Json/manifest.json').read_text(encoding='utf-8'))
tables = ['DT_GuLiStrikeCommander_Soldiers', 'DT_GuLiStrikeCommander_Skills',
          'DT_GuLiStrikeCommander_UnitSkills', 'DT_GuLiStrikeCommander_SpellFields',
          'DT_GuLiStrikeCommander_WeaponMounts']
report = {'tables': [namespace['import_table'](name, manifest['tables'][name]) for name in tables]}
report['passed'] = all(entry.get('imported') for entry in report['tables'])
(out_dir / 'import_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
if not report['passed']:
    raise RuntimeError('Commander skill data import failed; see outputs/skill-bridge/import_report.json')

"""Import the unified SpellFields table containing the four commander teleport tiers."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
namespace = {'__name__': 'teleport_import'}
source = (root / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
definitions = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
exec(compile(definitions, 'import_data_to_engine.py', 'exec'), namespace)
out = root / 'TestResults/CommanderTeleport'
out.mkdir(parents=True, exist_ok=True)
namespace['PROGRESS'] = str(out / 'import_progress.log')
manifest = json.loads((root / 'data/Json/manifest.json').read_text(encoding='utf-8'))
name = 'DT_GuLiStrikeSpellFields_Fields'
report = namespace['import_table'](name, manifest['tables'][name])
(out / 'data_import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
assert report.get('imported'), report

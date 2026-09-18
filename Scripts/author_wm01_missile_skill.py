"""Refresh WM01 weapon tables, then merge the independent secondary-unit skill source."""
from pathlib import Path
import json
import unreal
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
namespace = {'__name__': 'wm01_weapon_import'}
source = (ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8')
source = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0]
exec(compile(source,'import_data_to_engine.py','exec'), namespace)
namespace['PROGRESS'] = str(ROOT/'outputs/wm01_q/table-import.log')
manifest = json.loads((ROOT/'data/Json/manifest.json').read_text(encoding='utf8'))
for table in ['DT_GuLiStrikeCommander_Skills','DT_GuLiStrikeCommander_UnitSkills']:
    result = namespace['import_table'](table, manifest['tables'][table])
    assert result.get('imported'), result
script = ROOT/'Scripts/author_secondary_unit_skill_assets.py'
exec(compile(script.read_text(encoding='utf8'),str(script),'exec'), {'__name__':'secondary_skill_authoring'})

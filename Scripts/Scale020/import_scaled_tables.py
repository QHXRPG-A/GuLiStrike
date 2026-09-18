"""Use the project's CSVImportFactory pipeline, limited to the scaled source tables."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
OUT=ROOT/'TestResults/Scale020'
names=['DT_GuLiStrikeSpellFields_Fields','DT_GuLiStrikeCommander_Soldiers',
       'DT_GuLiStrikeBuildings_Buildings','DT_GuLiStrikeShip_Tuning','DT_GuLiStrikeShip_Camera',
       'DT_GuLiStrikeCommander_UnitSkills','DT_GuLiStrikeSecondaryWeapons_Projectiles',
       'DT_GuLiStrikeShip_WingmanWeapons','DT_GuLiStrikeShip_WingmanTargeting',
       'DT_GuLiStrikeSecondaryUnitSkills_Skills','DT_GuLiStrikeSecondaryUnitSkills_UnitSkills']
namespace={'__name__':'scale020_import'}
source=(ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
exec(compile(source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0],
             'import_data_to_engine.py','exec'),namespace)
namespace['PROGRESS']=str(OUT/'table-import.log')
manifest=json.loads((ROOT/'data/Json/manifest.json').read_text(encoding='utf-8'))
report={'tables':[]}
for name in names:
    row=namespace['import_table'](name,manifest['tables'][name])
    report['tables'].append(row)
    if not row.get('imported'):
        raise RuntimeError('Scaled table import failed: '+json.dumps(row,ensure_ascii=False))
# This script is data driven and preserves commander tactics outside the Q source table.
skill_namespace={'__name__':'scale020_unit_skills'}
exec(compile((ROOT/'Scripts/author_secondary_unit_skill_assets.py').read_text(encoding='utf-8'),
             'author_secondary_unit_skill_assets.py','exec'),skill_namespace)
report['passed']=True
(OUT/'table-import-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'passed':True,'tables':len(report['tables'])}))

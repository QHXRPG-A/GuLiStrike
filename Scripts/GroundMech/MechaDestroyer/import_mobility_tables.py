"""Feature-scoped entry to the standard Excel export -> UE import pipeline."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
report_path=out/globals().get('GULI_MOBILITY_IMPORT_REPORT','ground_mobility_import.json')
scope={'__name__':'ground_mobility_import',
       'GULI_TABLE_FILTER':{'DT_GuLiStrikeMech_'+s for s in ('Mobility','Gait','GroundIK','Camera')},
       'GULI_IMPORT_REPORT':str(report_path),
       'GULI_IMPORT_PROGRESS':str(out/'ground_mobility_import.log')}
source=root/'Scripts/import_data_to_engine.py'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'),scope)
report=json.loads(report_path.read_text(encoding='utf8'))
assert not report['errors'],report['errors']
assert all(t['imported'] for t in report['tables']) and len(report['tables'])==4
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'tables':len(report['tables']),'post_import':report['ground_mobility']['success'],'contact_curves_rebaked':report['ground_mobility']['contact_curves_rebaked'],'fingerprint':report['ground_mobility']['configuration_fingerprint'],'report':str(report_path)}))

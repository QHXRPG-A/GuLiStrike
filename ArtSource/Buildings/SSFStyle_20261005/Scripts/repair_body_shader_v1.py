"""Repair the owned body shader's reserved HLSL identifier only."""
import unreal,json,runpy,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
assert '-SSFShaderRepairWorker' in unreal.SystemLibrary.get_command_line()
ns=runpy.run_path(str(R/'Scripts/import_ue_delivery_v1.py'),run_name='ssf_import_functions')
im=json.loads((D/'ue_import.json').read_text(encoding='utf8'));assert im['success'];ns['REPORT'].update(im)
report={'success':False,'source_packages_modified':False}
try:
 material=ns['body_master']();report['material']=material.get_path_name()
 report['success']=True;ns['checkpoint']()
except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
finally:
 (D/'body_shader_repair.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()

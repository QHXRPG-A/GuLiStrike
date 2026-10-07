import unreal,json,runpy,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
assert '-SSFFinishWorker' in unreal.SystemLibrary.get_command_line()
ns=runpy.run_path(str(R/'Scripts/import_ue_delivery_v1.py'),run_name='ssf_import_functions')
report=ns['REPORT'];prior=json.loads((D/'ue_import.json').read_text(encoding='utf8'))
assert len(prior['assets'])==10;report.update(prior);report.pop('error',None);report['animations']=[]
try:
 source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'));meshes={a['key']:a for a in report['assets']}
 skeletons={k:unreal.load_asset(a['skeleton']) for k,a in meshes.items() if a['skeleton']}
 report['stage']='complete_raw_animation_copies';ns['checkpoint']()
 anims=ns['clone_animations'](source,meshes,skeletons)
 report['stage']='drone_blueprint_copy';ns['checkpoint']();ns['drone_blueprint'](source,meshes,anims)
 report.update(success=True,stage='import_saved_pending_independent_reload',textures=38,skeletons=7,engine=unreal.SystemLibrary.get_engine_version())
except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
finally:ns['checkpoint']();unreal.SystemLibrary.quit_editor()

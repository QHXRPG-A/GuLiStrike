import unreal,json,traceback
from pathlib import Path
D=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1');result={}
try:
 assert '-SSFAnimProbeWorker' in unreal.SystemLibrary.get_command_line()
 for name in ('AnimationDataModel','AnimationCurveIdentifier','RichCurveKey'):
  cls=getattr(unreal,name,None)
  result[name]={n:str(getattr(cls,n).__doc__)[:1400] for n in dir(cls) if not n.startswith('_')} if cls else None
 source=json.loads((D.parent/'Source/source_manifest.json').read_text(encoding='utf8'))
 result['curves']=[]
 for a in source['animations']:
  old=unreal.load_asset(a['path']);m=old.data_model_interface
  if m.get_number_of_float_curves() or m.get_number_of_transform_curves():
   result['curves'].append({'name':old.get_name(),'float':m.get_number_of_float_curves(),'transform':m.get_number_of_transform_curves(),
    'text':m.get_editor_property('LegacyCurveData').export_text()})
except Exception:result['error']=traceback.format_exc()
(D/'anim_binding_API.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()

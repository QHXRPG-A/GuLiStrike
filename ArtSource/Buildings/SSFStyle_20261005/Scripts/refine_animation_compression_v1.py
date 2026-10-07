"""Use a private ACL preset that protects metre-scale mechanical parts.

Original editable poses are exact; original/copy lossy compression can choose
opposite tiny rotations. Keep original keys, rates, skeletons and source assets.
"""
import unreal,json,runpy,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
assert '-SSFAnimPrecisionWorker' in unreal.SystemLibrary.get_command_line()
ns=runpy.run_path(str(R/'Scripts/import_ue_delivery_v1.py'),run_name='ssf_import_functions')
im=json.loads((D/'ue_import.json').read_text(encoding='utf8'));assert im['success'];ns['REPORT'].update(im)
report={'success':False,'source_modified':False,'animations':[]}
try:
 first=unreal.load_asset(im['animations'][0]['source']).get_editor_property('bone_compression_settings')
 path=ns['TARGET']+'/Shared/Compression/BCS_SSF_MechanicalPrecision'
 preset=ns['duplicate'](first.get_path_name(),path)
 report['preset']=preset.get_path_name();report['source_preset']=first.get_path_name();report['codecs']=[]
 for codec in preset.get_editor_property('codecs'):
  assert codec.get_outer()==preset,codec.get_path_name()
  entry={'class':codec.get_class().get_name(),'source_error_threshold':codec.get_editor_property('ErrorThreshold')}
  codec.set_editor_property('ErrorThreshold',.001)
  codec.set_editor_property('DefaultVirtualVertexDistance',100.)
  codec.set_editor_property('SafeVirtualVertexDistance',100.)
  entry.update(error_threshold_cm=.001,virtual_vertex_distance_cm=100.);report['codecs'].append(entry)
 ns['save'](preset)
 for row in im['animations']:
  ns['owned'](row['path']);old=unreal.load_asset(row['source']);new=unreal.load_asset(row['path'])
  assert old.get_editor_property('bone_compression_settings')==first
  new.set_editor_property('bone_compression_settings',preset);ns['save'](new)
  report['animations'].append(row['path'])
 ns['REPORT']['animation_compression_preset']=preset.get_path_name();ns['checkpoint']()
 report['success']=True
except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
finally:
 (D/'animation_compression_precision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()

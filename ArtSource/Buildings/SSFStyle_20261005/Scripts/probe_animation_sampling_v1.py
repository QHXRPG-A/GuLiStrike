import unreal,json,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1';out={}
assert '-SSFAnimProbeWorker' in unreal.SystemLibrary.get_command_line()
try:
 out['AnimSequence_API']={n:str(getattr(unreal.AnimSequence,n).__doc__)[:750] for n in dir(unreal.AnimSequence) if any(s in n for s in ('sampling','frame_rate','number_of','target_frame'))}
 out['animations']=[]
 for row in json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))['animations']:
  old=unreal.load_asset(row['path']);model=old.data_model_interface;entry={'name':old.get_name(),'length':old.sequence_length,'num_frames':unreal.AnimationLibrary.get_num_frames(old),'model_frames':model.get_number_of_frames(),'model_keys':model.get_number_of_keys(),'model_rate':model.get_frame_rate().export_text()}
  for prop in ('sampling_frame_rate','target_frame_rate','number_of_sampled_keys'):
   try:
    v=old.get_editor_property(prop);entry[prop]=v.export_text() if hasattr(v,'export_text') else v
   except Exception as e:entry[prop]=str(e)
  key=next(a['key'] for a in json.loads((D/'ue_import.json').read_text(encoding='utf8'))['assets'] if a.get('skeleton_source','').split('.')[0]==row['skeleton'].split('.')[0])
  path='/Game/GuLiStrike/Buildings/SSFStylized/'+key+'/Animations/'+old.get_name()
  if unreal.EditorAssetLibrary.does_asset_exist(path):
   new=unreal.load_asset(path);m=new.data_model_interface;entry['copy']={'length':new.sequence_length,'num_frames':unreal.AnimationLibrary.get_num_frames(new),'model_frames':m.get_number_of_frames(),'model_keys':m.get_number_of_keys(),'model_rate':m.get_frame_rate().export_text()}
  out['animations'].append(entry)
except Exception:out['error']=traceback.format_exc()
(D/'animation_sampling_probe.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()

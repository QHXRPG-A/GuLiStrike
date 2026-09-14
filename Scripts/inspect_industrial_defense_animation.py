import unreal,json,traceback
from pathlib import Path
out=Path('D:/UE5.7/test1/outputs/hardsurface-models-20260914')
anim=unreal.load_asset('/Game/GuLiStrike/Buildings/HeavyDefenseCannon/Animations/A_HeavyDefenseCannon_AimDemo')
data={'animation':anim.get_path_name(),'methods':[n for n in dir(anim) if any(k in n for k in ['bone','model','track'])],
      'library_methods':[n for n in dir(unreal.AnimationLibrary) if any(k in n for k in ['bone_pose','track','num_frames'])] if hasattr(unreal,'AnimationLibrary') else []}
try:
    model=anim.get_editor_property('data_model')
    data['model_methods']=[n for n in dir(model) if any(k in n for k in ['bone','track','key','frame'])]
except Exception:data['model_error']=traceback.format_exc()
for cls in ['SkeletalMeshComponent','AnimSingleNodeInstance']:
    data[cls]={n:str(getattr(getattr(unreal,cls),n).__doc__)[:1600] for n in ['play_animation','set_position','tick_animation','refresh_bone_transforms','set_update_animation_in_editor','set_force_ref_pose'] if hasattr(getattr(unreal,cls),n)}
(out/'animation_inspection.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,**data}))

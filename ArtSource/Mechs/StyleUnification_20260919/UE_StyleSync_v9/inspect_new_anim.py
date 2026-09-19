import unreal,json
ab=unreal.load_asset('/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech');cd=unreal.get_default_object(ab.generated_class())
fresh=unreal.new_object(ab.generated_class(),outer=next(a for a in unreal.ObjectIterator(unreal.GuLiGroundMechCharacter) if 'UEDPIE_1' in a.get_path_name()).mesh)
r={'fresh':str(fresh.get_editor_property('root_motion_mode')),'default':str(cd.get_editor_property('root_motion_mode')),'cd_bs':str(cd.get_editor_property('locomotion_blend_space')),'new_bs':str(fresh.get_editor_property('locomotion_blend_space'))}
fresh=None
unreal.MCPythonHelper.submit_result(json.dumps(r))

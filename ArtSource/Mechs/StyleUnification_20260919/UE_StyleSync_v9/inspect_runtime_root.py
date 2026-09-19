import unreal,json
ab=unreal.load_asset('/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech')
report={'editor_class':ab.generated_class().get_path_name(),'editor_cdo_mode':str(unreal.get_default_object(ab.generated_class()).get_editor_property('root_motion_mode')),'worlds':[],'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()}
for w0 in unreal.ObjectIterator(unreal.World):
    if 'UEDPIE_' not in w0.get_path_name():continue
    pc0=unreal.GameplayStatics.get_player_controller(w0,0);p0=pc0.get_controlled_pawn() if pc0 else None
    row={'world':w0.get_path_name(),'pc':pc0.get_path_name() if pc0 else None,'pawn':p0.get_path_name() if p0 else None}
    if isinstance(p0,unreal.GuLiGroundMechCharacter):
        an=p0.mesh.get_anim_instance();row.update(mode=str(an.get_editor_property('root_motion_mode')),cls=an.get_class().get_path_name(),cdo_mode=str(unreal.get_default_object(an.get_class()).get_editor_property('root_motion_mode')))
    report['worlds'].append(row)
unreal.MCPythonHelper.submit_result(json.dumps(report))

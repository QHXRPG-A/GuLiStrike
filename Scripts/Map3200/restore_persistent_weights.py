"""Register persistent UE 5.7 target layers, then restore original resampled weights."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map3200/20260923'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    land=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.Landscape))
    target=json.loads((out/'terrain/layout-3200.json').read_text(encoding='utf-8'))
    assert unreal.GuLiLandscapeAuthoringLibrary.register_target_layers(land,[unreal.load_asset(layer['layer_info_path']) for layer in target['source_layers']])
    assert unreal.GuLiLandscapeAuthoringLibrary.import_target_layer_weights(land,[unreal.load_asset(layer['layer_info_path']) for layer in target['source_layers']],str(out/'terrain/weights-interleaved.raw'))
    assert len(land.get_editor_property('target_layers'))==3
    return {'success':True,'persistent_target_layers':3,'height_changed':False,'saved':False}


unreal.MCPythonHelper.submit_result(json.dumps(main()))

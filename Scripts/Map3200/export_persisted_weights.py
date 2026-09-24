"""After reloading the saved map, export its actual merged weights for comparison."""
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
    assert len(land.get_editor_property('target_layers'))==3
    records=[]
    for layer in target['source_layers']:
        result=unreal.LandscapeService.export_weight_map(land.get_name(),layer['layer_name'],str(out/('terrain/persisted-weight-'+layer['layer_name']+'.png')))
        assert result.success,str(result)
        records.append({'layer':layer['layer_name'],'size':[result.width,result.height]})
    return {'success':True,'layers':records,'registered_or_imported_weights':False}


unreal.MCPythonHelper.submit_result(json.dumps(main()))

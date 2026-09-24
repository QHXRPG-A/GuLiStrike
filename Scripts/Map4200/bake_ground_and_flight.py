"""Build the source world's navigation before sampling resources. Does not run gameplay."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map4200/20260922'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    result=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,False)
    payload={name:getattr(result,name) for name in ('success','message','ground_rebuilds','flight_rebuilds','total_seconds')}
    payload['entries']=[{name:getattr(e,name) for name in ('kind','object_path','status','source_hash','message','build_seconds')} for e in result.entries]
    (out/'navigation-first-bake.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
    return payload


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

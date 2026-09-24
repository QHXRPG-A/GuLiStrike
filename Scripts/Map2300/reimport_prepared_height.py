"""Apply a numerical terrain refinement to the already resized, backed-up Landscape."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map2300/20260923'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    lands=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.LandscapeProxy)]
    assert len(lands)==1
    _,extent=lands[0].get_actor_bounds(False)
    assert abs(extent.x-115000)<1
    result=unreal.LandscapeService.import_heightmap(lands[0].get_name(),str(out/'terrain/height-2300.png'))
    assert result.success,str(result)
    navigation=unreal.NavigationSystemV1.get_navigation_system(world)
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(actor,unreal.NavMeshBoundsVolume):
            navigation.on_navigation_bounds_updated(actor)
    return {'success':True,'landscape':lands[0].get_path_name(),'height_refined':True}


unreal.MCPythonHelper.submit_result(json.dumps(main()))

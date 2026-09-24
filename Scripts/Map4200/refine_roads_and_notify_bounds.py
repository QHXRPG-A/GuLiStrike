"""Apply the measured road-junction correction and notify registered navigation bounds."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map4200/20260922'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    land=next(a for a in actors if isinstance(a,unreal.Landscape))
    assert unreal.LandscapeService.get_landscape_info(land.get_name()).num_components==256
    result=unreal.LandscapeService.import_heightmap(land.get_name(),str(out/'terrain/height-4200.png'))
    assert result.success,str(result)
    navigation=unreal.NavigationSystemV1.get_navigation_system(world)
    assert navigation
    bounds=[]
    for actor in actors:
        if isinstance(actor,unreal.NavMeshBoundsVolume):
            origin,extent=actor.get_actor_bounds(False)
            assert abs(extent.x-140000)<1 and abs(extent.y-140000)<1
            navigation.on_navigation_bounds_updated(actor)
            bounds.append({'path':actor.get_path_name(),'origin':list(origin.to_tuple()),'extent':list(extent.to_tuple())})
    assert len(bounds)==1
    result={'success':True,'bounds_notified':bounds,'terrain':json.loads((out/'terrain/layout-4200.json').read_text())}
    (out/'road-refinement.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'success':True,'navigation_bounds_notified':len(bounds),'maximum_road_core_slope_deg':result['terrain']['maximum_road_core_slope_deg']}


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

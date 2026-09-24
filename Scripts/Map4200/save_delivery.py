"""Validate and save only the map and the explicitly owned associated assets."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map4200/20260922'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    resource=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert resource.success,str(resource.issues)
    navigation=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,False)
    assert navigation.success,navigation.message
    allowed={'/Game/Maps/LVL_CommanderMassPrototype',
             '/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap',
             '/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy',
             '/Game/GuLiStrike/Editor/MapAuthoring/Types/DA_Outpost',
             '/Game/GuLiStrike/Navigation/Baked/LVL_CommanderMassPrototype/DA_FlightNav_LVL_CommanderMassPrototype'}
    dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages=[p for p in dirty if p.get_path_name() in allowed]
    unrelated=[p.get_path_name() for p in dirty if p.get_path_name() not in allowed]
    assert unreal.EditorLoadingAndSavingUtils.save_packages(packages,True)
    remaining=[p.get_path_name() for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())]
    assert not set(remaining)&allowed,remaining
    validation=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    assert validation.success,validation.message
    result={'success':True,'saved':[p.get_path_name() for p in packages],'unrelated_dirty_not_saved':unrelated,
            'resource_source_hash':resource.source_hash,'resource_layout_hash':resource.layout_hash,
            'navigation':[{'kind':e.kind,'status':e.status,'source_hash':e.source_hash,'object':e.object_path} for e in validation.entries],
            'pie_run':False}
    (out/'saved-delivery.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return result


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

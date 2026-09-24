"""Reuse the backed-up 256-component Landscape and import the new numerical terrain."""
import json
from pathlib import Path
import unreal


def main():
    root=Path(unreal.Paths.project_dir()).resolve()
    out=root/'Artifacts/Map3200/20260923'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert (out/'backup/Content/Maps/LVL_CommanderMassPrototype.umap').is_file()
    target=json.loads((out/'terrain/layout-3200.json').read_text(encoding='utf-8'))
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lands=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
    assert len(lands)==1
    land=lands[0];info=unreal.LandscapeService.get_landscape_info(land.get_name())
    assert info.num_components==256 and info.resolution_x==4081
    _,extent=land.get_actor_bounds(False)
    assert abs(extent.x-210000)<1,'Terrain migration already applied; inspect before repeating.'
    for actor in actors:
        if isinstance(actor,unreal.NavMeshBoundsVolume):
            _,extent=actor.get_actor_bounds(False);scale=actor.get_actor_scale3d()
            low,high=target['min_z_cm']-1000,target['max_z_cm']+10000
            actor.set_actor_location(unreal.Vector(0,0,(low+high)/2),False,False)
            actor.set_actor_scale3d(unreal.Vector(scale.x*135000/extent.x,scale.y*135000/extent.y,scale.z*(high-low)/2/extent.z))
            unreal.NavigationSystemV1.get_navigation_system(world).on_navigation_bounds_updated(actor)
    land.set_actor_location(unreal.Vector(*target['location']),False,False)
    land.set_actor_scale3d(unreal.Vector(*target['scale']))
    land.set_actor_label('Landscape_3200')
    imported=unreal.LandscapeService.import_heightmap(land.get_name(),str(out/'terrain/height-3200.png'))
    assert imported.success,str(imported)
    assert unreal.GuLiLandscapeAuthoringLibrary.register_target_layers(land,[unreal.load_asset(layer['layer_info_path']) for layer in target['source_layers']])
    assert unreal.GuLiLandscapeAuthoringLibrary.import_target_layer_weights(land,[unreal.load_asset(layer['layer_info_path']) for layer in target['source_layers']],str(out/'terrain/weights-interleaved.raw'))
    weights=[layer['layer_name'] for layer in target['source_layers']]
    info=unreal.LandscapeService.get_landscape_info(land.get_name())
    origin,extent=land.get_actor_bounds(False)
    assert info.num_components==256 and info.resolution_x==4081 and info.resolution_y==4081
    assert abs(origin.x)<1 and abs(origin.y)<1 and abs(extent.x-160000)<1 and abs(extent.y-160000)<1
    data={'success':True,'stage':'terrain_ready','landscape':land.get_path_name(),'reused_components':256,
          'resolution':[4081,4081],'origin':list(origin.to_tuple()),'extent':list(extent.to_tuple()),'weights_imported':weights,'saved':False}
    (out/'landscape-stage.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    return data


unreal.MCPythonHelper.submit_result(json.dumps(main()))

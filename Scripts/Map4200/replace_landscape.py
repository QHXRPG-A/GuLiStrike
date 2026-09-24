"""Rebuild only the backed-up Commander Landscape in the live source editor."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/Map4200/20260922'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'


def write(name, data):
    (OUT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')


def main():
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]==MAP
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert (OUT/'backup/Content/Maps/LVL_CommanderMassPrototype.umap').is_file()
    baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
    target=json.loads((OUT/'terrain/layout-4200.json').read_text(encoding='utf-8'))
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lands=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
    assert len(lands)==1
    old=lands[0]
    old_info=unreal.LandscapeService.get_landscape_info(old.get_name())
    assert old_info.num_components==1024, 'Do not repeat a terrain replacement; inspect the current stage.'
    preserved={}
    for key in ('landscape_material','collision_mip_level','simple_collision_mip_level',
                'navigation_geometry_gathering_mode','cast_shadow','cast_shadow_as_two_sided'):
        preserved[key]=old.get_editor_property(key)
    # Resize navigation before removing the old collision, to avoid generating old large bounds.
    for a in actors:
        if isinstance(a,unreal.NavMeshBoundsVolume):
            origin,extent=a.get_actor_bounds(False)
            low,high=target['min_z_cm']-1000,target['max_z_cm']+10000
            scale=a.get_actor_scale3d()
            a.set_actor_location(unreal.Vector(0,0,(low+high)/2),False,False)
            a.set_actor_scale3d(unreal.Vector(scale.x*140000/extent.x,scale.y*140000/extent.y,scale.z*(high-low)/2/extent.z))
    write('landscape-stage.json',{'stage':'removing_original','original':old.get_path_name()})
    assert unreal.LandscapeService.delete_landscape(old.get_name())
    result=unreal.LandscapeService.create_landscape(unreal.Vector(*target['location']),unreal.Rotator(),
        unreal.Vector(*target['scale']),1,255,16,16,'Landscape_4200')
    assert result.success,str(result)
    lands=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.LandscapeProxy)]
    assert len(lands)==1
    land=lands[0]
    for key,val in preserved.items(): land.set_editor_property(key,val)
    land.set_folder_path('GuLi/Terrain')
    write('landscape-stage.json',{'stage':'importing_height','landscape':land.get_path_name()})
    imported=unreal.LandscapeService.import_heightmap(land.get_name(),str(OUT/'terrain/height-4200.png'))
    assert imported.success,str(imported)
    weights=[]
    for layer in baseline['landscape']['info']['layers']:
        assert unreal.LandscapeService.add_layer(land.get_name(),layer['layer_info_path'])
        imported=unreal.LandscapeService.import_weight_map(land.get_name(),layer['layer_name'],
            str(OUT/('terrain/weight-4200-'+layer['layer_name']+'.png')))
        assert imported.success,str(imported)
        weights.append(layer['layer_name'])
        write('landscape-stage.json',{'stage':'importing_weights','completed':weights})
    info=unreal.LandscapeService.get_landscape_info(land.get_name())
    origin,extent=land.get_actor_bounds(False)
    assert info.num_components==256 and info.resolution_x==4081 and info.resolution_y==4081
    assert abs(origin.x)<1 and abs(origin.y)<1 and abs(extent.x-210000)<1 and abs(extent.y-210000)<1
    result={'success':True,'stage':'terrain_ready','landscape':land.get_path_name(),
            'components':info.num_components,'resolution':[info.resolution_x,info.resolution_y],
            'origin':list(origin.to_tuple()),'extent':list(extent.to_tuple()),'weights':weights,
            'collision_mip':land.get_editor_property('collision_mip_level'),
            'saved':False}
    write('landscape-stage.json',result)
    return result


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))

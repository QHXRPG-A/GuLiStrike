"""Temporary approved three-color static preview under commander map lighting."""
import json,math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1/UEImport'
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
TAG='GuLi.CommanderIsland.ArtPreview'

def main():
    if ISLAND_ACTION=='remove':
        actors=[a for a in ACTORS.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
        assert len(actors)==6
        for a in actors:assert ACTORS.destroy_actor(a)
        return {'success':True,'temporary_actors_removed':6}
    assert ISLAND_ACTION=='create' and not any(TAG in [str(t) for t in a.tags] for a in ACTORS.get_all_level_actors())
    mesh=unreal.load_asset('/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder')
    bounds=mesh.get_bounds();scale=1/30
    ground=unreal.LandscapeService.get_height_at_location('Landscape_CommanderIsland_1800m_v1',10000,11000)
    assert ground.valid
    target=unreal.Vector(10000,15000,ground.height+500)
    camera=target+unreal.Vector(0,math.cos(math.radians(55))*16000,math.sin(math.radians(55))*16000)
    for index,team in enumerate(('Blue','Neutral','Red')):
        center=target+unreal.Vector((index-1)*2600,0,0)
        for part in ('Glow','Halo'):
            position=center+unreal.Vector(-bounds.origin.x*scale,-bounds.origin.y*scale,-500) if part=='Glow' else center
            a=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,position,
                unreal.Rotator() if part=='Glow' else unreal.MathLibrary.make_rot_from_z(camera-center))
            a.set_actor_label('ArtPreview_Outpost_'+team+'_'+part);a.set_folder_path('CommanderIsland/Review/Temporary')
            a.set_editor_property('tags',[TAG]);a.set_editor_property('is_editor_only_actor',False)
            a.set_actor_scale3d(unreal.Vector(scale,scale,scale) if part=='Glow' else unreal.Vector(16,18,1))
            c=a.static_mesh_component
            assert c.set_static_mesh(mesh if part=='Glow' else unreal.load_asset('/Engine/BasicShapes/Plane'))
            c.set_material(0,unreal.load_asset('/Game/GuLiStrike/FX/StrongholdOutpost/Candidate_v1/MI_Outpost'+part+'_'+team))
            c.set_collision_profile_name('NoCollision',True);c.set_editor_property('can_ever_affect_navigation',False)
            a.set_actor_enable_collision(False)
    p=FILES/'camera_presets.json';presets=json.loads(p.read_text(encoding='utf-8-sig'))
    presets['07_outpost_three_colors']={'location':list(camera.to_tuple()),'target':list(target.to_tuple()),'fov':45}
    p.write_text(json.dumps(presets,indent=2),encoding='utf-8')
    return {'success':True,'temporary_actors':6,'static_pose_height_m':0,'runtime_animation_run':False}

unreal.MCPythonHelper.submit_result(json.dumps(main()))

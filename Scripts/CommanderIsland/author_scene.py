"""Prepare editor-only pose references and the real commander acceptance entrance."""
import json,math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1'
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
assert not LEVELS.is_in_play_in_editor()
TAG='GuLi.CommanderIsland.EditorPose'
LAND='Landscape_CommanderIsland_1800m_v1'

def ground(x,y):
    h=unreal.LandscapeService.get_height_at_location(LAND,x,y)
    assert h.valid and h.height>0,(x,y)
    return unreal.Vector(x,y,h.height)

def fixtures():
    # Refresh only the existing outpost references; never recreate the island.
    import sys
    sys.path.insert(0, str(ROOT/'Scripts'))
    from CommanderIsland.outpost_presentation import prepare_scene
    return prepare_scene()


def flight_bounds():
    volumes=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.GuLiFlightNavigationVolume)]
    assert len(volumes)==1
    padding=[]
    for name in ('DoubleRing','SwarmOrbit'):
        f=unreal.load_asset('/Game/GuLiStrike/Ship/Abilities/Formations/DA_WingmanFormation_'+name)
        radius=f.agent_radius_centimeters
        padding.append(max(f.inner_ring_radius_centimeters,f.outer_ring_radius_centimeters,
            f.swarm_orbit.outer_soft_radius_centimeters,f.recovery_distance_centimeters)+radius+
            max(2000,f.obstacle_look_ahead_centimeters,radius*2))
    half=90000+max(padding)
    a=volumes[0];origin,extent=a.get_actor_bounds(False);scale=a.get_actor_scale3d()
    a.set_actor_scale3d(unreal.Vector(scale.x*half/extent.x,scale.y*half/extent.y,scale.z))
    after_origin,after_extent=a.get_actor_bounds(False)
    assert abs(after_extent.x-half)<1 and abs(after_extent.y-half)<1 and abs(after_extent.z-extent.z)<1
    return {'success':True,'physical_map_half_extent_cm':90000,'flight_half_extent_xy_cm':half,
        'formation_padding_cm':max(padding),'vertical_range_cm':[origin.z-extent.z,origin.z+extent.z],
        'navigation_asset':a.navigation_data.get_path_name()}

def navigation():
    r=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(WORLD,True)
    entries=[{k:getattr(e,k) for k in ('object_path','kind','status','source_hash','message','check_seconds','build_seconds')} for e in r.entries]
    return {**{k:getattr(r,k) for k in ('success','message','ground_rebuilds','flight_rebuilds','save_seconds','total_seconds')},'entries':entries}

def navigation_query_budget():
    meshes=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.RecastNavMesh)]
    assert len(meshes)==2
    entries=[]
    for a in meshes:
        for key in ('DefaultMaxSearchNodes','DefaultMaxHierarchicalSearchNodes'):
            assert unreal.ActorService.set_property(a.get_name(),key,'16384')
        entries.append({'actor':a.get_actor_label(),'maximum_search_nodes':16384})
    return {'success':True,'entries':entries,'map_scope':WORLD.get_path_name()}

def preview_visibility():
    visible=ISLAND_ACTION=='preview_on';count=0
    for a in ACTORS.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            a.set_editor_property('is_editor_only_actor',not visible)
            a.set_actor_hidden_in_game(not visible);count+=1
    assert count==49
    return {'success':True,'editor_poses':count,'preview_visible':visible}

unreal.MCPythonHelper.submit_result(json.dumps({'fixtures':fixtures,'flight_bounds':flight_bounds,'navigation':navigation,'navigation_query_budget':navigation_query_budget,
    'preview_on':preview_visibility,'preview_off':preview_visibility}[ISLAND_ACTION](),ensure_ascii=False))

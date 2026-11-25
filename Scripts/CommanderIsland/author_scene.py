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



def navigation():
    r=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(WORLD,True)
    entries=[{k:getattr(e,k) for k in ('object_path','kind','status','source_hash','message','check_seconds','build_seconds')} for e in r.entries]
    return {**{k:getattr(r,k) for k in ('success','message','ground_rebuilds','save_seconds','total_seconds')},'entries':entries}

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

unreal.MCPythonHelper.submit_result(json.dumps({'fixtures':fixtures,'navigation':navigation,'navigation_query_budget':navigation_query_budget,
    'preview_on':preview_visibility,'preview_off':preview_visibility}[ISLAND_ACTION](),ensure_ascii=False))

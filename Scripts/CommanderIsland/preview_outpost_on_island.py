"""Compatibility entry for the current editor-only production outpost preview."""
import json
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts'))
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TAG='GuLi.CommanderIsland.ArtPreview'

def main():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    if ISLAND_ACTION=='remove':
        actors=[a for a in ACTORS.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
        for actor in actors:assert ACTORS.destroy_actor(actor)
        return {'success':True,'temporary_actors_removed':len(actors)}
    if ISLAND_ACTION!='create':raise ValueError(ISLAND_ACTION)
    from CommanderIsland.outpost_presentation import prepare_scene
    return prepare_scene()

unreal.MCPythonHelper.submit_result(json.dumps(main()))

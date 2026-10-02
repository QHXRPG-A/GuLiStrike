"""Read and preserve the actual commander source, without altering the editor world."""
import json
from pathlib import Path
import unreal


def capture():
    root=Path(unreal.Paths.project_dir()).resolve()/"ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1/Before"
    root.mkdir(parents=True,exist_ok=True)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world=editor.get_editor_world()
    assert world.get_path_name().split('.')[0]=="/Game/Maps/LVL_CommanderMassPrototype"
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    author=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    layout=author.get_snapshot()
    density=author.get_density_snapshot()
    assert layout.success and density.success
    data={"map":world.get_path_name(),"layout":json.loads(layout.json),"density":json.loads(density.json),
        "army_layout":json.loads(unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()),
        "actors":[{"path":a.get_path_name(),"label":a.get_actor_label(),"class":a.get_class().get_name(),
            "location":list(a.get_actor_location().to_tuple()),"rotation":list(a.get_actor_rotation().to_tuple()),
            "scale":list(a.get_actor_scale3d().to_tuple()),"tags":[str(t) for t in a.tags]}
            for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()]}
    (root/"editor_snapshot.json").write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding="utf-8")
    return {"success":True,"path":str(root/"editor_snapshot.json"),"actors":len(data["actors"]),"markers":len(data["layout"]["markers"])}


unreal.MCPythonHelper.submit_result(json.dumps(capture(),ensure_ascii=False))

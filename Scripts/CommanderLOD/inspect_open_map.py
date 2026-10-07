"""Inspect the active editor map before showing the LOD review bay."""
import json
import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
game_world = editor.get_game_world()
review = [a for a in actors.get_all_level_actors()
          if a.get_actor_label().startswith('CommanderLOD_BiZhiMao_LOD')]
result = dict(success=True, world=world.get_path_name() if world else None,
              game_world=game_world.get_path_name() if game_world else None,
              marker_classes=sorted({a.get_class().get_name() for a in actors.get_all_level_actors()
                                     if 'Marker' in a.get_class().get_name()}),
              hide_temporarily=unreal.Actor.set_is_temporarily_hidden_in_editor.__doc__,
              review_actors=[dict(label=a.get_actor_label(),
                                  location=str(a.get_actor_location()),
                                  bounds=str(a.get_actor_bounds(False))) for a in review],
              camera=editor.get_level_viewport_camera_info.__doc__,
              set_camera=editor.set_level_viewport_camera_info.__doc__,
              select=actors.set_selected_level_actors.__doc__)
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))

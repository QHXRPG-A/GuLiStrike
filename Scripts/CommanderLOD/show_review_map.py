"""Show the saved LOD review map without changing its actors or assets."""
import json
import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
expected = '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
if not world or world.get_path_name() != expected:
    raise RuntimeError('The requested review map is not the active editor world.')
if editor.get_game_world():
    raise RuntimeError('Gameplay is active; leave its viewport unchanged.')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
labels = ['CommanderLOD_BiZhiMao_LOD' + str(i) for i in range(3)]
present = {a.get_actor_label() for a in actors.get_all_level_actors()}
if not all(label in present for label in labels):
    raise RuntimeError('The saved three-tier review group is missing.')
actors.set_selected_level_actors([])
hidden_markers = 0
for actor in actors.get_all_level_actors():
    if actor.get_class().get_name() == 'GuLiMapMarker':
        actor.set_is_temporarily_hidden_in_editor(True)
        hidden_markers += 1
location = unreal.Vector(58000, -18500, 12800)
target = unreal.Vector(54305, -34500, 1289)
rotation = unreal.MathLibrary.find_look_at_rotation(location, target)
editor.set_level_viewport_camera_info(location, rotation)
camera = editor.get_level_viewport_camera_info()
unreal.MCPythonHelper.submit_result(json.dumps(dict(
    success=True, world=world.get_path_name(), review_group=labels,
    temporarily_hidden_marker_count=hidden_markers,
    camera_location=str(camera[0]), camera_rotation=str(camera[1]))))

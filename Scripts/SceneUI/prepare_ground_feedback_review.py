"""Player-invoked review positioning only; does not start PIE or change targets.

Editor Python: REVIEW_DISTANCE_M=425; exec(open(
    r'D:/UE5.7/test1/Scripts/SceneUI/prepare_ground_feedback_review.py',
    encoding='utf-8').read())
Distances label observation anchors around the existing blue-side combat area.
Actual opacity uses camera-to-target-bounds distance, not the pawn or marker.
"""
import json
import unreal

distance = int(globals().get('REVIEW_DISTANCE_M', 350))
assert distance in (350, 425, 500, 600)
subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = subsystem.get_game_world()
assert world and 'LVL_CommanderMassPrototype' in world.get_path_name(), 'Enter the matching map and Ground seat first'
pc = unreal.GameplayStatics.get_player_controller(world, 0)
assert pc and pc.is_local_player_controller()
editor_world = subsystem.get_editor_world()
markers = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(editor_world, unreal.TargetPoint)
           if a.actor_has_tag('GroundSceneUIFeedback20261106')}
label = 'GroundUI_View_' + str(distance) + 'm'
assert label in markers and 'GroundUI_CombatAnchor' in markers, 'Save/load the Ground UI review entry first'
position = markers[label].get_actor_location()
aim = markers['GroundUI_CombatAnchor'].get_actor_location()
assert unreal.GuLiGroundRogueDebugLibrary.prepare_shooter(pc, position, aim), 'The local Ground pawn must be ready and simulated locally'
unreal.log(json.dumps({'review_position': label, 'position_cm': position.to_tuple(), 'aim_cm': aim.to_tuple(),
                       'nominal_distance_m': distance, 'targets_modified': False,
                       'note': 'Actual UI distance is measured from the camera to target bounds. Use normal combat for health/shield changes.'}))

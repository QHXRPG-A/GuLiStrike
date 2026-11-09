"""Move the local PIE Ground player to the saved ammo review area.

Preparation only: leaves keyboard input, combat, health, cards and weapon tiers
unchanged. Run from the editor console after choosing the Ground seat:
py "D:/UE5.7/test1/Scripts/GroundMech/Rogue/enter_ammo_review.py"
"""
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not world:
    raise RuntimeError('Enter PIE and choose the Ground seat first.')
controller = unreal.GameplayStatics.get_player_controller(world, 0)
points = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TargetPoint)
          if actor.get_actor_label() == 'GroundAmmo_ObserveDamage']
if len(points) != 1:
    raise RuntimeError('Open LVL_CommanderMassPrototype with its saved Ground ammo review area.')
if not unreal.GuLiGroundRogueDebugLibrary.prepare_shooter(
        controller, points[0].get_actor_location(), unreal.Vector(183, 52060, 1117)):
    raise RuntimeError('The local Ground player is not ready; wait for the session to enter the game.')
unreal.log('游骑兵已到达 GroundAmmo_ObserveDamage。关闭控制台后，左键开火，F4 选牌。')

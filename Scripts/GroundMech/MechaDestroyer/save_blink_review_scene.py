"""Save only the existing Ground review map and read back the changed entities."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/BlinkImpact_B1_20261026'
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors={a.get_actor_label():a for a in sub.get_all_level_actors()}
assert all(n in actors for n in ['GroundMobility_Entry','GroundMobility_Flat','GroundMobility_Slope18'])
# Reuse the existing collision platforms/steps as impact and blink blockers.
# A separate new static wall would invalidate the map's saved flight navigation.
actor=actors.get('GroundBlink_ImpactWall')
if actor: assert sub.destroy_actor(actor)
settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))
assert settings.get_editor_property('bEnabled') and str(settings.get_editor_property('MainRole'))=='<GuLiCommanderRole.GROUND: 2>'
gm=unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
assert 'BP_GroundMech_Light' in str(gm.get_editor_property('default_ground_pawn_class'))
assert level.save_current_level()
entities=[]
for a in sub.get_all_level_actors():
    if not a.get_actor_label().startswith(('GroundMobility_','GroundBlink_')): continue
    row={'label':a.get_actor_label(),'location':list(a.get_actor_location().to_tuple()),'scale':list(a.get_actor_scale3d().to_tuple())}
    if isinstance(a,unreal.StaticMeshActor):
        row['collision']=str(a.static_mesh_component.get_collision_profile_name()); assert row['collision']=='BlockAll'
    entities.append(row)
assert len(entities)==8,entities
result={'success':True,'map':world.get_path_name(),'entities':entities,'saved':True,'PIE_started':False,'visual_review':'pending user'}
out.mkdir(parents=True,exist_ok=True); (out/'scene.json').write_text(json.dumps(result,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))

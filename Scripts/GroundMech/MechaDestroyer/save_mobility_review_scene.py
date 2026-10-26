"""Author and save Ground flat/slope/step review conditions; never starts gameplay."""
import json,math
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing={a.get_actor_label():a for a in sub.get_all_level_actors()}
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
folder='Review/GroundMobility_20261024'
floor=905.0
specs=[('Flat',(-4500,57575,floor-25),(30,40,.5),0)]
angle=18.0; radians=math.radians(angle)
specs += [('Slope18',(-4750,56475,floor+700*math.sin(radians)-20*math.cos(radians)),(14,10,.4),angle),
          ('SlopeLanding',(-3820,56475,floor+1400*math.sin(radians)-20),(5.4,10,.4),0)]
for i in range(4):
    height=20*(i+1)
    specs.append((f'Step{height}',(-5300+400*i,58475,floor+height/2),(4,10,height/100),0))
for suffix,position,scale,pitch in specs:
    position=(position[0]+500,position[1]-7575,position[2])
    name='GroundMobility_'+suffix
    actor=existing.get(name)
    if actor is None: actor=sub.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position))
    assert isinstance(actor,unreal.StaticMeshActor),name
    actor.modify(); actor.set_actor_label(name); actor.set_folder_path(folder)
    actor.set_actor_location(unreal.Vector(*position),False,False)
    actor.set_actor_rotation(unreal.Rotator(pitch=pitch,yaw=0,roll=0),False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_actor_enable_collision(True)
    component=actor.static_mesh_component; component.modify()
    component.set_static_mesh(cube)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name('BlockAll')
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    actor.set_editor_property('tags',[unreal.Name('GroundMobilityReview')])
name='GroundMobility_Entry'
entry=existing.get(name) or sub.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector(-2800,50000,floor+244.04))
entry.set_actor_label(name); entry.set_folder_path(folder)
entry.set_actor_location(unreal.Vector(-2800,50000,floor+244.04),False,False)
entry.set_actor_rotation(unreal.Rotator(pitch=0,yaw=180,roll=0),False)
settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))
assert str(settings.get_editor_property('MainRole'))=='<GuLiCommanderRole.GROUND: 2>'
assert settings.get_editor_property('bEnabled')
gm=unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
pawn=str(gm.get_editor_property('default_ground_pawn_class'))
assert 'BP_GroundMech_Light' in pawn,pawn
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
# Only read the authored entities after saving.
readback=[]
for actor in sub.get_all_level_actors():
    if not actor.get_actor_label().startswith('GroundMobility_'): continue
    item={'label':actor.get_actor_label(),'class':actor.get_class().get_name(),'location':list(actor.get_actor_location().to_tuple()),'rotation_roll_pitch_yaw':list(actor.get_actor_rotation().to_tuple()),'scale':list(actor.get_actor_scale3d().to_tuple())}
    if isinstance(actor,unreal.StaticMeshActor):
        c=actor.static_mesh_component
        item.update({'mesh':c.static_mesh.get_path_name(),'collision':str(c.get_collision_enabled()),'profile':str(c.get_collision_profile_name())})
        assert item['profile']=='BlockAll'
    readback.append(item)
assert len(readback)==8,readback
manifests=[]
for population in (200,600):
    path=root/f'Config/Hybrid/RuntimeManifest-{population}.json'
    result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
    assert result['success'],result
    manifest=json.loads(path.read_text(encoding='utf-8-sig'))
    manifests.append({'population':population,'config_hash':manifest['config_hash'],'fingerprint':manifest['strings']['ground/mobility_configuration']})
report={'success':True,'map':world.get_path_name(),'saved':True,'pie_started':False,'visual_review':'not_run; user acceptance','main_role':str(settings.get_editor_property('MainRole')),'default_ground_pawn':pawn,'entities':readback,'manifests':manifests,'entry':'Red Ground spawn approximately (-2175,58575,1148.8); review floor center (-4000,50000,905), south of spawn. Entry TargetPoint (-2800,50000,1149.04) is an editor locator only.'}
(out/globals().get('GULI_SCENE_REPORT','ground_mobility_scene_delivery_20261024.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

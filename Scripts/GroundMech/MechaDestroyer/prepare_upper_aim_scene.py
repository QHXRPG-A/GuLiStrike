"""Save/reuse the existing mobility scene and read related entities; never start gameplay."""
import json
import math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Artifacts/GroundUpperAim_20261104'
FINALIZE=bool(globals().get('GULI_UPPER_AIM_FINALIZE',False))
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
if FINALIZE:
    imported=json.loads((OUT/'asset-import.json').read_text(encoding='utf-8'))
    assert not imported['errors'] and imported['ground_mobility']['upper_aim_binding']['success']
    for population in (200,600):
        manifest=json.loads((ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json').read_text(encoding='utf-8-sig'))
        assert int(manifest['numbers']['adapter/family/GroundPlayer/state_version'])==13
        assert manifest['strings']['ground/mobility_configuration']==imported['ground_mobility']['configuration_fingerprint']
assert level.save_current_level()
entities=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    label=actor.get_actor_label()
    if not label.startswith(('GroundMobility_','Ground3C_')): continue
    item=dict(label=label,type=actor.get_class().get_name(),location=list(actor.get_actor_location().to_tuple()),
              rotation_roll_pitch_yaw=list(actor.get_actor_rotation().to_tuple()),scale=list(actor.get_actor_scale3d().to_tuple()))
    if isinstance(actor,unreal.StaticMeshActor):
        item.update(mesh=actor.static_mesh_component.static_mesh.get_path_name(),
                    collision_profile=str(actor.static_mesh_component.get_collision_profile_name()))
    entities.append(item)
by_name={e['label']:e for e in entities}
assert len(by_name)==len(entities)
assert {'GroundMobility_Entry','GroundMobility_Flat','Ground3C_LookLeft','Ground3C_LookRight'}.issubset(by_name)
assert math.dist(by_name['GroundMobility_Entry']['location'],(-2800,50000,1149.04))<.01
mode=unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
pawn_class=mode.get_editor_property('default_ground_pawn_class')
assert pawn_class.get_path_name()=='/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C'
pawn=unreal.get_default_object(pawn_class); mesh=pawn.get_editor_property('mesh')
production=dict(model_id=pawn.get_editor_property('model_id'),mesh=mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
                anim_class=mesh.get_editor_property('anim_class').get_path_name())
assert production['model_id']==4001
assert production['mesh']=='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2.SK_GroundMech_A4B2'
assert production['anim_class']=='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/ABP_GroundMech_A4B2.ABP_GroundMech_A4B2_C'
report=dict(success=True,map=world.get_path_name(),map_saved=True,entities=entities,production_references=production,
            new_runtime_ready=FINALIZE,pie_started=False,visual_acceptance='pending user')
path=OUT/('scene-delivery.json' if FINALIZE else 'scene-before-build.json')
path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,entities=len(entities),new_runtime_ready=FINALIZE,report=str(path))))

"""Editor-only art display in FireReview. Play uses CommanderMassPrototype + Ground.

The current local Hybrid launcher does not create a room for FireReview; its
role-to-pawn mapping alone does not make it a supported normal PIE entry.
"""
import unreal,json
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
MAP='/Game/Maps/LVL_GroundMech_FireReview';BASE='/Game/GuLiStrike/GroundMech';NEW=BASE+'/MechaDestroyer_A4B2'
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not LEVEL.is_in_play_in_editor()
world=EDITOR.get_editor_world();assert world.get_path_name().split('.')[0]==MAP
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light');cdo=unreal.get_default_object(bp.generated_class())
catalog=json.loads(Path('D:/UE5.7/test1/Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))
definition=next(x for x in catalog if x['Id']==4001)
def color(hexstr):
    s=hexstr.lstrip('#');v=[int(s[i:i+2],16)/255 for i in (0,2,4)]
    v=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in v]
    return unreal.Vector4(*v,1)
objects=[];records=[]
for team,x in [('BLUE',8600),('RED',9400)]:
    label='GMA4B2_EditorReview_'+team
    matches=[a for a in ACTORS.get_all_level_actors() if a.get_actor_label()==label]
    assert len(matches)<=1
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,-8800,10000),unreal.Vector(x,-8800,-10000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,matches,unreal.DrawDebugTrace.NONE)
    assert hit,label
    h=hit.to_tuple();assert h[0] and h[6].z>.8,(label,h)
    floor=h[5].z
    pos=unreal.Vector(x,-8800,floor+cdo.get_editor_property('capsule_component').get_unscaled_capsule_half_height())
    actor=matches[0] if matches else ACTORS.spawn_actor_from_object(bp,pos,unreal.Rotator())
    assert actor;actor.modify();actor.set_actor_label(label);actor.set_folder_path('ArtReview/GroundMech_A4B2')
    # Editor instances do not run BeginPlay, which initializes DisplayAimYaw.
    # Keep their actor yaw at its zero default so torso and legs share an axis.
    actor.set_actor_location(pos,False,False);actor.set_actor_rotation(unreal.Rotator(),False)
    actor.set_actor_enable_collision(False);actor.set_editor_property('is_editor_only_actor',True)
    actor.set_editor_property('tags',[unreal.Name('GuLiGroundMechA4B2EditorReview')])
    comp=actor.mesh;comp.modify();comp.set_update_animation_in_editor(True)
    for index,key in [(8,'BluePrimaryHex' if team=='BLUE' else 'EnemyPrimaryHex'),(12,'BlueSecondaryHex' if team=='BLUE' else 'EnemySecondaryHex')]:
        comp.set_default_custom_primitive_data_vector4(index,color(definition[key]))
    comp.set_default_custom_primitive_data_float(16,1)
    pres=actor.get_component_by_class(unreal.GuLiModelPresentationComponent)
    assert pres and pres.model_id==4001
    anim=comp.get_anim_instance();assert anim and anim.get_class()==unreal.load_asset(NEW+'/Animations/ABP_GroundMech_A4B2').generated_class()
    sockets={k:comp.does_socket_exist(k) for k in ['Muzzle','Muzzle_L','Jet_Outlet_R','Jet_Outlet_L']}
    assert all(sockets.values())
    records.append({'label':label,'actor':actor.get_path_name(),'location':pos.to_tuple(),'floor_z':floor,'editor_only':actor.get_editor_property('is_editor_only_actor'),'mesh':comp.get_skeletal_mesh_asset().get_path_name(),'animation_instance':anim.get_class().get_path_name(),'model_registration':pres.model_id,'sockets':sockets,'preview_team':team})
    objects.append(actor)
ACTORS.set_selected_level_actors(objects)
focus=unreal.Vector(9000,-8800,records[0]['floor_z']+275);camera=focus+unreal.Vector(1650,2250,1000)
EDITOR.set_level_viewport_camera_info(camera,unreal.MathLibrary.find_look_at_rotation(camera,focus))
assert LEVEL.save_current_level()
gm=world.get_world_settings().get_editor_property('default_game_mode');m=unreal.get_default_object(gm)
roles=m.get_editor_property('role_pawn_classes')
ground=[v for k,v in roles.items() if 'GROUND' in str(k).upper()]
assert len(ground)==1 and ground[0]==unreal.load_asset(BASE+'/Review/BP_GroundMech_FireReview').generated_class()
report={'success':True,'map':MAP,'saved':True,'editor_review_actors':records,'game_mode':gm.get_path_name(),'role_pawn_classes':str(roles),'player_starts':[{'name':a.get_actor_label(),'location':a.get_actor_location().to_tuple()} for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.PlayerStart)],'play_started':False,'visual_review':'User'}
(OUT/'review_scene.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

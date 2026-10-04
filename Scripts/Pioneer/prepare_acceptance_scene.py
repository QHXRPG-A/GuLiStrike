"""Final delivery step: author and save the specified map, then read entity data only."""
import json,math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/Reports'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
BASE='/Game/GuLiStrike/Robots/RSGMech'
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD=EDITOR.get_editor_world()
assert WORLD.get_path_name().split('.')[0]==MAP and not EDITOR.get_game_world()
PREPARATION_ONLY=bool(globals().get('PIONEER_ALLOW_SCENE_PREPARATION',False))
if not PREPARATION_ONLY:
    assert unreal.load_asset(BASE+'/VAT/DA_Pioneer_VAT').is_valid_definition()
    assert json.loads((OUT/'ue_runtime_deployment.json').read_text(encoding='utf8'))['success']
    assert json.loads((OUT/'ue_runtime_readback.json').read_text(encoding='utf8'))['success']
MODE=WORLD.get_world_settings().get_editor_property('default_game_mode')
CONTROLLER=unreal.get_default_object(MODE).get_editor_property('player_controller_class') if MODE else None
assert MODE and MODE.get_path_name()=='/Script/GuLiStrike.GuLiCommanderGameMode'
assert CONTROLLER and CONTROLLER.get_path_name()=='/Script/GuLiStrike.GuLiCommanderPlayerController'
existing=ACTORS.get_all_level_actors()
assert not [a for a in existing if isinstance(a,unreal.GuLiCommanderDeploymentPoint) and not a.get_actor_label().startswith('PioneerQA_')], 'Preserve unrelated authored deployments; merge manually.'
NAV=next(a for a in existing if isinstance(a,unreal.RecastNavMesh) and 'CommanderSoldier' in a.get_name())

def project(x,y,z=1000):
    value=unreal.NavigationSystemV1.project_point_to_navigation(WORLD,unreal.Vector(x,y,z),NAV,None,unreal.Vector(150,150,5000))
    assert value is not None and abs(value.x-x)<.05 and abs(value.y-y)<.05,(x,y,value)
    return value

def actor(label,cls,location):
    matches=[a for a in ACTORS.get_all_level_actors() if a.get_actor_label()==label]
    assert len(matches)<=1,label
    a=matches[0] if matches else ACTORS.spawn_actor_from_class(cls,location)
    assert isinstance(a,cls),label
    a.set_actor_label(label);a.set_folder_path('PioneerQA')
    a.set_actor_location_and_rotation(location,unreal.Rotator(),False,True)
    return a

specs=[('PioneerQA_Single',0,65000,unreal.GuLiTeam.RED,1,True),
       ('PioneerQA_Multi_A',-2300,65000,unreal.GuLiTeam.RED,1,True),
       ('PioneerQA_Multi_B',-2300,67000,unreal.GuLiTeam.RED,1,True),
       ('PioneerQA_SizeReference_WM01',0,63000,unreal.GuLiTeam.RED,2,False),
       ('PioneerQA_Blocked',-45000,60000,unreal.GuLiTeam.RED,1,True),
       ('PioneerQA_Target_InRange',5600,65000,unreal.GuLiTeam.BLUE,2,False),
       ('PioneerQA_Target_OutOfRange',6900,63800,unreal.GuLiTeam.BLUE,2,False)]
deployed=[]
for label,x,y,team,unit,fire in specs:
    a=actor(label,unreal.GuLiCommanderDeploymentPoint,project(x,y))
    for key,value in dict(team=team,unit_type_id=unit,rows=1,columns=1,spacing_centimeters=1800,
        allow_automatic_fire=fire,start_idle=True).items():a.set_editor_property(key,value)
    deployed.append(a)
blocked=next(a for a in deployed if a.get_actor_label()=='PioneerQA_Blocked').get_actor_location()
walls=[]
# Inner opening is 800 cm. Thick surrounding walls cover all three candidate rings.
for side,offset,size in [('West',(-1200,0),(1600,4000)),('East',(1200,0),(1600,4000)),
                         ('North',(0,1200),(800,1600)),('South',(0,-1200),(800,1600))]:
    a=actor('PioneerQA_Blocker_'+side,unreal.StaticMeshActor,blocked+unreal.Vector(*offset,400))
    component=a.static_mesh_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    # A runtime Pawn-query fixture must not replace Landscape ground hits or ore authoring geometry.
    component.set_collision_object_type(unreal.CollisionChannel.ECC_WORLD_DYNAMIC)
    component.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN,unreal.CollisionResponseType.ECR_BLOCK)
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    a.set_actor_enable_collision(True)
    # Collision is queried directly by summon placement. Keep the existing baked navigation unchanged.
    component.set_editor_property('can_ever_affect_navigation',False)
    a.set_actor_scale3d(unreal.Vector(size[0]/100,size[1]/100,12))
    walls.append(a)
for label,xy,text in [('PioneerQA_Notes_Single',(0,66500),'PIONEER: 60m / TWO GUNS / Q: 5 SWEEPERS / 30s'),
                      ('PioneerQA_Notes_Multi',(-2300,68500),'SELECT BOTH PIONEERS: Q = 10 SWEEPERS'),
                      ('PioneerQA_Notes_Blocked',(-45000,62600),'BLOCKED Q: ZERO SPAWNS / NO COOLDOWN'),
                      ('PioneerQA_Notes_Targets',(5800,70000),'PASSIVE TARGETS: 56m IN / 65m+ OUT')]:
    a=actor(label,unreal.TextRenderActor,project(*xy)+unreal.Vector(0,0,120))
    a.text_render.set_text(text);a.text_render.set_world_size(90)
    a.set_actor_rotation(unreal.Rotator(0,-90,0),False)
assert LEVELS.save_current_level(),'Map save failed'
result={'success':True,'map':MAP,'initial_mass_count':7,'population_red':5,'population_blue':2,
    'preparation_only':PREPARATION_ONLY,'new_gameplay_ready':not PREPARATION_ONLY,
    'existing_default_army_replaced_by_authored_fixture':True,'play_started':False,'deployments':[], 'blockers':[], 'text_notes':[],
    'player_starts':[],'navigation':{'actor':NAV.get_name(),'radius_cm':NAV.get_editor_property('agent_radius')},
    'vat_asset':BASE+'/VAT/DA_Pioneer_VAT','game_mode':MODE.get_path_name(),'controller':CONTROLLER.get_path_name(),
    'operations':'See ../ACCEPTANCE.md; select Commander and manually start a game.'}
if not PREPARATION_ONLY:
    saved_rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')))
    result['unit_configuration']={str(r['Id']):{k:r[k] for k in ['DisplayName','ModelAsset','VATDefinition',
        'MovementSpeedCmPerSecond','ModelWidthMeters','bSummonOnly']} for r in saved_rows if r['Id'] in [1,2,5]}
    result['pioneer_avoidance_radius_cm']=result['unit_configuration']['1']['ModelWidthMeters']*50
    assert result['pioneer_avoidance_radius_cm']==312.5
for a in ACTORS.get_all_level_actors():
    label=a.get_actor_label()
    if label.startswith('PioneerQA_') and isinstance(a,unreal.GuLiCommanderDeploymentPoint):
        expected=next(s for s in specs if s[0]==label)
        assert a.unit_type_id==expected[4] and a.team==expected[3] and a.rows*a.columns==1,label
        assert a.unit_type_id!=5,'Summon-only units must not appear in initial deployment'
        p=a.get_actor_location();project(p.x,p.y,p.z)
        result['deployments'].append({'name':a.get_name(),'label':label,'team':str(a.team),'unit_type_id':a.unit_type_id,
            'position':list(p.to_tuple()),'rows':a.rows,'columns':a.columns,'spacing_cm':a.spacing_centimeters,
            'automatic_fire':a.allow_automatic_fire,'start_idle':a.start_idle})
    elif label.startswith('PioneerQA_Blocker_'):
        c=a.static_mesh_component
        assert c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)==unreal.CollisionResponseType.ECR_BLOCK
        result['blockers'].append({'label':label,'position':list(a.get_actor_location().to_tuple()),
            'scale':list(a.get_actor_scale3d().to_tuple()),'collision_profile':str(c.get_collision_profile_name()),
            'object_type':str(c.get_collision_object_type()),'pawn_response':str(c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)),
            'affects_navigation':c.get_editor_property('can_ever_affect_navigation'),'mesh':c.get_editor_property('static_mesh').get_path_name()})
    elif label.startswith('PioneerQA_Notes_'):
        result['text_notes'].append({'label':label,'text':str(a.text_render.get_editor_property('text')),
            'position':list(a.get_actor_location().to_tuple())})
    elif isinstance(a,unreal.PlayerStart):result['player_starts'].append({'name':a.get_name(),'position':list(a.get_actor_location().to_tuple())})
assert len(result['deployments'])==7 and len(result['blockers'])==4 and len(result['text_notes'])==4
assert not [p for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() if p.get_name()==MAP]
report_name=globals().get('PIONEER_SCENE_REPORT_NAME', 'acceptance_scene_preparation.json' if PREPARATION_ONLY else 'acceptance_scene_readback.json')
(OUT/report_name).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))

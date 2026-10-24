"""Inspect the saved graph as loaded and transient poses; never rebuild/compile/save it here."""
import json, math
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
BASE='/Game/GuLiStrike/GroundMech'
ANIMS=BASE+'/MechaDestroyer_A4B2/Animations'
H=unreal.GuLiGroundMechAnimationEditorLibrary
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
abp=unreal.load_asset(ANIMS+'/ABP_GroundMech_A4B2')
mesh=unreal.load_asset(BASE+'/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
curve=unreal.load_asset(BASE+'/Animations/CF_Machinegun_Recoil')
report={'success':False,'scope':'Transient editor pose/asset data, no gameplay execution or images','blendspaces':[],'poses':{}}

# Inspect before any pose sampling. Rebuilding here masked the cold-load missing-node fault.
nodes=unreal.BlueprintService.get_nodes_in_graph(abp.get_path_name(),'AnimGraph')
assert len(nodes)==34,('Unexpected saved graph size',len(nodes))
assert sum(n.node_type=='AnimGraphNode_GuLiGroundMechAim' for n in nodes)==1
required={
    'AnimGraphNode_Root':['Result'],
    'AnimGraphNode_LocalToComponentSpace':['LocalPose','ComponentPose'],
    'AnimGraphNode_ComponentToLocalSpace':['ComponentPose','Pose'],
    'AnimGraphNode_GuLiGroundMechAim':['ComponentPose','Pose','AimPointComponent','bAimValid','RightMuzzleLocal','LeftMuzzleLocal','RightRecoil','LeftRecoil','RecoilDistance'],
}
connections={}
for n in nodes:
    if n.node_type not in required:continue
    pins={p.pin_name:p.is_connected for p in unreal.BlueprintService.get_node_pins(abp.get_path_name(),'AnimGraph',n.node_id)}
    assert all(pins.get(name,False) for name in required[n.node_type]),(n.node_type,pins)
    connections[n.node_type]={name:pins[name] for name in required[n.node_type]}
report['graph']={'nodes':len(nodes),'connections_ok':True,'pose_connections':connections,'rebuild_executed':False,'compile_executed':False}

# Reopening must retain valid samples without running the repair again.
for suffix in ['Walk','Run']:
    bs=unreal.load_asset(ANIMS+'/BlendSpace_Loco_'+suffix)
    for x,y in [(0,0),(0,1),(0,-1),(-1,0),(1,0),(-.7,.7),(.7,.7),(-.7,-.7),(.7,-.7)]:
        r=json.loads(H.probe_blend_space(mesh,bs,unreal.Vector(x,y,0)))
        assert r['success'] and r['weights'] and r['max_leg_rotation_degrees']>.1,r
        assert len(set(f['player_time'] for f in r['frames']))>1,r
        assert abs(sum(w['weight'] for w in r['weights'])-1)<1.e-5,r
        report['blendspaces'].append(r)

for name,velocity,jump,shot,aim,thrust in [
    ('forward',[720,0,0],False,-1,[400,2500,800],True),
    ('right_fire',[720,0,0],False,0,[400,2500,800],True),
    ('left_fire',[720,0,0],False,1,[400,2500,800],True),
    ('alternating',[720,0,0],False,2,[400,2500,800],True),
    ('jump',[0,0,0],True,2,[400,2500,800],True),
    ('walk_off',[0,0,0],True,-1,[400,2500,800],False),
    ('strafe_left',[0,-720,0],False,-1,[400,2500,800],True),
    ('strafe_right',[0,720,0],False,-1,[400,2500,800],True),
    ('near',[0,0,0],False,2,[0,600,300],True),
    ('high',[0,0,0],False,2,[0,2000,4000],True),
    ('low',[0,0,0],False,2,[-500,2500,-500],True)]:
    r=json.loads(H.probe_animation_graph(bp.generated_class(),unreal.Vector(*velocity),jump,shot,unreal.Vector(*aim),curve,thrust))
    assert r['success'],name
    report['poses'][name]=r

poses=report['poses']
report['max_muzzle_error_degrees']=max(max(f['Muzzle_error_degrees'],f['Muzzle_L_error_degrees']) for p in poses.values() for f in p['frames'])
assert report['max_muzzle_error_degrees']<.02,report['max_muzzle_error_degrees']
for name in ['right_fire','left_fire','alternating']:
    assert all(a[b]==f[b] for a,f in zip(poses['forward']['frames'],poses[name]['frames']) for b in ['Foot_R','Foot_L']),name
assert max(f['right_recoil'] for f in poses['right_fire']['frames'])>.9
assert max(f['left_recoil'] for f in poses['right_fire']['frames'])==0
assert max(f['left_recoil'] for f in poses['left_fire']['frames'])>.9
assert max(f['right_recoil'] for f in poses['left_fire']['frames'])==0
assert [f['frame'] for f in poses['jump']['frames'] if f['jump_start']]==list(range(1,11))
assert not any(f['jump_start'] for f in poses['walk_off']['frames'])
for name in ['jump','walk_off']:
    assert all(f['airborne'] for f in poses[name]['frames'][1:35])
    assert not any(f['airborne'] for f in poses[name]['frames'][35:])
for name,bone in [('right_fire','Gun_R'),('left_fire','Gun_L')]:
    a=poses['forward']['frames'][4]; f=poses[name]['frames'][4]
    pos=lambda text:list(map(float,text.split('|')[0].split(',')))
    distance=math.dist(pos(a[bone]),pos(f[bone]))
    alpha=f['right_recoil' if name=='right_fire' else 'left_recoil']
    assert abs(distance-alpha*5)<.001,(name,distance,alpha)
report['fire_layers_preserve_legs']=True
report['recoil_cm_at_full_curve']=5

pawn=unreal.get_default_object(bp.generated_class())
boom=pawn.get_editor_property('camera_boom'); camera=pawn.get_editor_property('camera')
report['camera']={'arm_cm':boom.get_editor_property('target_arm_length'),'socket_offset':list(boom.get_editor_property('socket_offset').to_tuple()),'boom_rotation':list(boom.get_relative_transform().rotation.rotator().to_tuple()),'camera_location':list(camera.get_relative_transform().translation.to_tuple()),'camera_rotation':list(camera.get_relative_transform().rotation.rotator().to_tuple()),'fov':camera.get_editor_property('field_of_view'),'collision_test':boom.get_editor_property('do_collision_test'),'initial_distance':pawn.get_editor_property('initial_camera_distance')}
report['camera']['rotation_tuple_order']=['roll','pitch','yaw']
assert abs(report['camera']['arm_cm']-1084.4236261)<.01
assert report['camera']['fov']==90 and report['camera']['collision_test']
assert max(abs(x) for x in report['camera']['camera_location']+report['camera']['camera_rotation'])<.001
mapping=pawn.get_editor_property('mapping_context')
report['input']=[{'action':m.action.get_path_name(),'key':str(m.key.get_editor_property('key_name'))} for m in mapping.get_editor_property('default_key_mappings').get_editor_property('mappings')]
assert any(m['key']=='Mouse2D' and 'IA_GroundMech_Look' in m['action'] for m in report['input'])
assert pawn.get_editor_property('mesh').anim_class==abp.generated_class()
report['sockets']={n:str(mesh.find_socket(n).bone_name) for n in ['Muzzle','Muzzle_L','Jet_Outlet_R','Jet_Outlet_L']}
report['success']=True
(OUT/'animation_camera_cold_load_audit_20261024.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'graph':report['graph'],'blendspace_cases':len(report['blendspaces']),'pose_cases':len(poses),'max_muzzle_error_degrees':report['max_muzzle_error_degrees'],'camera':report['camera'],'fire_layers_preserve_legs':True,'report':str(OUT/'animation_camera_cold_load_audit_20261024.json')}))

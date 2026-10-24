"""Clone vendor pose graphs, replace input ownership, and isolate each gun's recoil."""
import unreal,json
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023')
BASE='/Game/GuLiStrike/GroundMech/MechaDestroyer_v1'
ABP=BASE+'/Animations/ABP_GroundMech_MechaDestroyer'
B=unreal.BlueprintService
LIB=unreal.EditorAssetLibrary
bp=unreal.load_asset(ABP) if LIB.does_asset_exist(ABP) else LIB.duplicate_asset('/Game/MechaDestroyer/Blueprints/BPanim_Mecha',ABP)
assert bp
for n in B.get_nodes_in_graph(ABP,'EventGraph'):
    assert B.delete_node(ABP,'EventGraph',n.node_id)
assert B.reparent_blueprint(ABP,'/Script/GuLiStrike.GuLiGroundMechAnimInstance')
assert B.compile_blueprint(ABP)
nodes=[{'ref':'event','type':'event','params':{'event':'BlueprintUpdateAnimation'}}]
links=[];defaults=[];previous='event'
mapping=[('Strafe Blend','IntegratedStrafeBlend'),('FWD Blend','IntegratedForwardBlend'),('InAir','bIsAirborne'),('Running','bIntegratedRunning'),('ControlRot','IntegratedControlRotation'),('Firing','IntegratedFireBlend'),('Firing Rockets','IntegratedFireBlendLeft')]
for i,(destination,source) in enumerate(mapping):
    get='get'+str(i);put='set'+str(i)
    nodes.extend([{'ref':get,'type':'variable_get','params':{'variable':source}},{'ref':put,'type':'variable_set','params':{'variable':destination}}])
    links.extend([{'from_':get+'.'+source,'to':put+'.'+destination},{'from_':previous+'.then','to':put+'.execute'}]);previous=put
for i,(destination,value) in enumerate([('AimingBlend','1.0'),('Montage_R','false'),('Montage_L','false')]):
    ref='constant'+str(i);nodes.append({'ref':ref,'type':'variable_set','params':{'variable':destination}})
    links.append({'from_':previous+'.then','to':ref+'.execute'});previous=ref
    defaults.append({'node_ref':ref,'pin_name':destination,'value':value})
result=B.build_graph(ABP,'EventGraph',nodes,links,defaults,True,False)
assert result.success,list(result.errors)
graph_prefix=ABP+'.'+ABP.rsplit('/',1)[-1]+':AnimGraph.'
objects={o.get_name():o for o in unreal.ObjectIterator(unreal.AnimGraphNode_Base) if o.get_path_name().startswith(graph_prefix)}
aim=objects['AnimGraphNode_ModifyBone_0']
data=aim.get_editor_property('node');data.set_editor_property('rotation_space',unreal.BoneControlSpace.BCS_COMPONENT_SPACE);data.set_editor_property('rotation_mode',unreal.BoneModificationMode.BMM_ADDITIVE);aim.set_editor_property('node',data)
for name,bone in [('AnimGraphNode_LayeredBoneBlend_1','GunArm_R'),('AnimGraphNode_LayeredBoneBlend_0','GunArm_L')]:
    node=objects[name];data=node.get_editor_property('node')
    f=unreal.BranchFilter();f.set_editor_property('bone_name',bone);f.set_editor_property('blend_depth',1)
    layer=unreal.InputBlendPose();layer.set_editor_property('branch_filters',[f])
    data.set_editor_property('layer_setup',[layer]);node.set_editor_property('node',data)
graph_nodes=B.get_nodes_in_graph(ABP,'AnimGraph')
shoot_id=next(n.node_id for n in graph_nodes if n.node_type=='AnimGraphNode_SequencePlayer' and 'Anim_Mecha_Shoot' in n.node_title)
left_id=next(c.target_node_id for c in B.get_connections(ABP,'AnimGraph') if c.source_pin_name=='Firing Rockets' and c.target_pin_name=='BlendWeights_0')
assert B.connect_nodes(ABP,'AnimGraph',shoot_id,'Pose',left_id,'BlendPoses_0')
cdo=unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('root_motion_mode',unreal.RootMotionMode.IGNORE_ROOT_MOTION)
assert B.compile_blueprint(ABP)
assert LIB.save_loaded_asset(bp,False)
report={'success':True,'asset':bp.get_path_name(),'parent':str(B.get_parent_class(ABP)),'event_nodes':result.nodes_created,'connections_failed':result.connections_failed,'skeleton':bp.get_editor_property('target_skeleton').get_path_name(),'root_motion':str(unreal.get_default_object(bp.generated_class()).get_editor_property('root_motion_mode')),'recoil_masks':{'right':'GunArm_R','left':'GunArm_L'},'vendor_graphs_unchanged':True}
(ART/'animation_adapter.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

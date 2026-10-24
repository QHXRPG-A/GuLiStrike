import unreal,json,math
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');ART=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023';OUT=ART/'Production_A4_B_v2/UE_Export'
BASE='/Game/GuLiStrike/GroundMech';NEW=BASE+'/MechaDestroyer_A4B2';AR=unreal.AssetRegistryHelpers.get_asset_registry()
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
mesh=unreal.load_asset(NEW+'/Meshes/SK_GroundMech_A4B2');skel=mesh.skeleton
report={'success':False,'assets':[],'missing_hard_dependencies':[],'preserved_gameplay_defaults':[],'source_animations':[]}
opts=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=False,include_editor_only_package_references=True,include_game_package_references=True)
for folder in ['Meshes','Animations','Materials']:
    for data in AR.get_assets_by_path(NEW+'/'+folder,True):
        asset=data.get_asset();assert asset,str(data.package_name)
        row={'path':asset.get_path_name(),'class':asset.get_class().get_name()}
        if isinstance(asset,unreal.AnimationAsset):
            assert asset.get_skeleton()==skel,row
            row['same_skeleton']=True
        if isinstance(asset,unreal.AnimSequence):
            tracks=[str(n) for n in unreal.AnimationLibrary.get_animation_track_names(asset)]
            row.update(tracks=len(tracks),seconds=asset.sequence_length)
            assert len(tracks)==17 and asset.sequence_length>0,row
        if isinstance(asset,unreal.BlendSpace):
            samples=asset.get_editor_property('sample_data')
            row['samples']=[s.get_editor_property('animation').get_path_name() for s in samples]
            assert len(samples)==5 and all(p.startswith(NEW+'/Animations/') for p in row['samples'])
        if isinstance(asset,unreal.AnimBlueprint):
            assert asset.get_editor_property('target_skeleton')==skel
            refs=[]
            for node in unreal.ObjectIterator(unreal.AnimGraphNode_Base):
                if not node.get_path_name().startswith(asset.get_path_name()+':'):continue
                cls=node.get_class().get_name();field='sequence' if cls=='AnimGraphNode_SequencePlayer' else 'blend_space' if cls=='AnimGraphNode_BlendSpacePlayer' else None
                if field:
                    obj=node.get_editor_property('node').get_editor_property(field)
                    assert obj and obj.get_path_name().startswith(NEW+'/Animations/')
                    refs.append(obj.get_path_name())
            assert refs;row['graph_animation_refs']=sorted(set(refs))
        for dep in AR.get_dependencies(data.package_name,opts):
            path=str(dep)
            if path.startswith('/Script/'):continue
            if not AR.get_assets_by_package_name(dep):report['missing_hard_dependencies'].append({'asset':str(data.package_name),'dependency':path})
        report['assets'].append(row)
assert not report['missing_hard_dependencies'],report['missing_hard_dependencies']
before=json.loads((ART/'b2_integration_before.json').read_text(encoding='utf8'))
for row in before['assets']:
    if row['class']!='AnimSequence' or not row['path'].startswith('/Game/MechaDestroyer/Animation/'):continue
    asset=unreal.load_asset(row['path']);assert asset and abs(asset.sequence_length-row['length'])<1e-5
    report['source_animations'].append({'path':row['path'],'unchanged_length':asset.sequence_length,'skeleton':asset.get_skeleton().get_path_name()})
assert len(report['source_animations'])==14
def val(v):
    if isinstance(v,unreal.Object):return v.get_path_name()
    if v is None or isinstance(v,(bool,int,float,str)):return v
    if hasattr(v,'to_tuple'):return list(v.to_tuple())
    return str(v)
def props(o,keys):return {k:val(o.get_editor_property(k)) for k in keys}
def snapshot(c):
    d=props(c,['model_id','walk_speed','standing_height','mapping_context','move_action','sprint_action','zoom_action','rocket_jump_action','replicates'])
    d['weapon']=props(c.get_editor_property('weapon'),['weapon_enabled','upgrade_table','skill_table','current_upgrade_id'])
    d['movement']=props(c.get_editor_property('character_movement'),['max_walk_speed','max_acceleration','braking_deceleration_walking','max_step_height','gravity_scale','rotation_rate'])
    d['capsule']=props(c.get_editor_property('capsule_component'),['capsule_half_height','capsule_radius'])
    d['camera']=props(c.camera,['field_of_view']);d['camera_boom']=props(c.camera_boom,['target_arm_length','do_collision_test','relative_rotation'])
    return d
for p,b in [(BASE+'/BP_GroundMech_Light',NEW+'/Rollback/BP_GroundMech_BeforeA4B2'),(BASE+'/Review/BP_GroundMech_FireReview',NEW+'/Rollback/BP_FireReview_BeforeA4B2')]:
    c=unreal.get_default_object(unreal.load_asset(p).generated_class());old=unreal.get_default_object(unreal.load_asset(b).generated_class())
    newvalues=snapshot(c);oldvalues=snapshot(old)
    assert newvalues==oldvalues,{'path':p,'before':oldvalues,'after':newvalues}
    assert newvalues['weapon']['weapon_enabled']
    report['preserved_gameplay_defaults'].append({'path':p,'values':newvalues})
    for field in ['upgrade_table','skill_table']:assert unreal.load_asset(newvalues['weapon'][field])
report.update(success=True,visual_checked=False,play_started=False,source_package_preserved=True)
(OUT/'resource_audit.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'new_assets':len(report['assets']),'missing_hard_dependencies':report['missing_hard_dependencies'],'source_animations':len(report['source_animations']),'preserved_gameplay_blueprints':len(report['preserved_gameplay_defaults'])}))

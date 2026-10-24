import unreal,json
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
BASE='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2'
LIB=unreal.EditorAssetLibrary
DATA=json.loads((OUT/'retargeted_animation_data.json').read_text())
mesh=unreal.load_asset(BASE+'/Meshes/SK_GroundMech_A4B2');skel=mesh.skeleton
def save(o):
    assert o.get_path_name().startswith(BASE+'/')
    LIB.set_metadata_tag(o,'GuLi.ModelProduction.Owner','GuLi.GroundMech.ApprovedA4.B2')
    assert LIB.save_loaded_asset(o,False)
def dup(p,name):
    dest=BASE+'/Animations/'+name
    return unreal.load_asset(dest) if LIB.does_asset_exist(dest) else LIB.duplicate_asset(p,dest)
mapped={};report={'success':False,'clips':[],'blendspaces':[],'montages':[]}
def create_animation(src,name,cls,factory_cls):
    dest=BASE+'/Animations/'+name
    old=unreal.load_asset(dest) if LIB.does_asset_exist(dest) else None
    if old and old.get_skeleton()!=skel:
        assert LIB.rename_asset(dest,BASE+'/Rollback/'+name+'_SourceSkeletonCopy')
        old=None
    if old and cls==unreal.AnimMontage and old.sequence_length==0:
        assert LIB.rename_asset(dest,BASE+'/Rollback/'+name+'_EmptyTemplate')
        old=None
    if old:return old
    factory=factory_cls();factory.set_editor_property('target_skeleton',skel)
    if cls==unreal.AnimSequence:factory.set_editor_property('preview_skeletal_mesh',mesh)
    if cls==unreal.AnimMontage:
        side='L' if '_L_' in name else 'R'
        factory.set_editor_property('source_animation',mapped['/Game/MechaDestroyer/Animation/Anim_Mecha_Turn_'+side+'.Anim_Mecha_Turn_'+side])
    o=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,BASE+'/Animations',cls,factory)
    assert o and o.get_skeleton()==skel
    return o
for b in DATA['bones']:assert unreal.SkeletonService.set_bone_retargeting_mode(skel.get_path_name(),b,'Animation')
save(skel)
for name,d in DATA['clips'].items():
    src=unreal.load_asset(d['source']);o=create_animation(src,name,unreal.AnimSequence,unreal.AnimSequenceFactory)
    o.clear_retarget_source_asset()
    ctl=o.controller;ctl.open_bracket('Ground B2 proportion and gun-axis retarget',False)
    try:
        ctl.set_frame_rate(unreal.FrameRate(30,1),False)
        ctl.set_number_of_frames(unreal.FrameNumber(len(d['frames'])-1),False)
        existing=set(str(n) for n in unreal.AnimationLibrary.get_animation_track_names(o))
        for j,b in enumerate(DATA['bones']):
            if b not in existing:ctl.add_bone_track(b,False)
            rows=[f[j] for f in d['frames']]
            assert ctl.set_bone_track_keys(b,[unreal.Vector(*x['t']) for x in rows],[unreal.Quat(*x['q']) for x in rows],[unreal.Vector(1,1,1)]*len(rows),False),b
    finally:ctl.close_bracket(False)
    assert not unreal.AnimationLibrary.get_animation_notify_events(src),'Notify copy must be explicit'
    for prop in ['rate_scale','interpolation','enable_root_motion','root_motion_root_lock','force_root_lock']:
        o.set_editor_property(prop,src.get_editor_property(prop))
    for curve in unreal.AnimationLibrary.get_animation_curve_names(src,unreal.RawCurveTrackTypes.RCT_FLOAT):
        if not unreal.AnimationLibrary.does_curve_exist(o,curve,unreal.RawCurveTrackTypes.RCT_FLOAT):unreal.AnimationLibrary.add_curve(o,curve,unreal.RawCurveTrackTypes.RCT_FLOAT)
        times,values=unreal.AnimationLibrary.get_float_keys(src,curve)
        unreal.AnimationLibrary.add_float_curve_keys(o,curve,times,values)
    save(o);mapped[src.get_path_name()]=o
    errors=[];qerrors=[]
    for f in [0,len(d['frames'])//2,len(d['frames'])-1]:
        poses=unreal.AnimationLibrary.get_bone_poses_for_frame(o,DATA['bones'],f,False)
        for p,row in zip(poses,d['frames'][f]):
            errors.append((p.translation-unreal.Vector(*row['t'])).length())
            assert max(abs(x-1) for x in p.scale3d.to_tuple())<.001
            qerrors.append(1-abs(p.rotation.x*row['q'][0]+p.rotation.y*row['q'][1]+p.rotation.z*row['q'][2]+p.rotation.w*row['q'][3]))
    assert max(errors)<.1,(name,max(errors))
    assert max(qerrors)<.001,(name,max(qerrors))
    assert abs(o.sequence_length-src.sequence_length)<.0001
    report['clips'].append({'name':name,'seconds':o.sequence_length,'keys':len(d['frames']),'max_readback_translation_error_cm':max(errors),'max_quaternion_error':max(qerrors),'notifies_preserved':len(unreal.AnimationLibrary.get_animation_notify_events(o))==len(unreal.AnimationLibrary.get_animation_notify_events(src))})
for name in ['BlendSpace_Loco_Walk','BlendSpace_Loco_Run']:
    p='/Game/MechaDestroyer/Animation/'+name;src=unreal.load_asset(p);o=create_animation(src,name,unreal.BlendSpace,unreal.BlendSpaceFactoryNew)
    for prop in ['blend_parameters','interpolation_param','target_weight_interpolation_speed_per_sec','axis_to_scale_animation']:
        o.set_editor_property(prop,src.get_editor_property(prop))
    samples=list(src.get_editor_property('sample_data'))
    for s in samples:s.set_editor_property('animation',mapped[s.get_editor_property('animation').get_path_name()])
    o.set_editor_property('sample_data',samples)
    assert unreal.GuLiGroundMechAnimationEditorLibrary.rebuild_blend_space(o,src)
    save(o);mapped[unreal.load_asset(p).get_path_name()]=o
    report['blendspaces'].append({'name':name,'samples':len(samples),'target_skeleton':o.get_editor_property('skeleton')==skel})
for name in ['Montage_Mecha_Turn_L_Montage','Montage_Mecha_Turn_R_Montage']:
    p='/Game/MechaDestroyer/Animation/'+name;src=unreal.load_asset(p);o=create_animation(src,name,unreal.AnimMontage,unreal.AnimMontageFactory)
    assert src.get_num_sections()==o.get_num_sections()==1
    assert str(src.get_section_name(0))==str(o.get_section_name(0))
    assert abs(o.sequence_length-src.sequence_length)<.001
    for prop in ['blend_in','blend_out','rate_scale','blend_out_trigger_time','enable_auto_blend_out']:o.set_editor_property(prop,src.get_editor_property(prop))
    save(o);mapped[unreal.load_asset(p).get_path_name()]=o
    report['montages'].append(name)
bp=dup('/Game/GuLiStrike/GroundMech/MechaDestroyer_v1/Animations/ABP_GroundMech_MechaDestroyer','ABP_GroundMech_A4B2')
bp.set_editor_property('target_skeleton',skel)
changes=[]
for node in unreal.ObjectIterator(unreal.AnimGraphNode_Base):
    if not node.get_path_name().startswith(bp.get_path_name()+':'):continue
    cls=node.get_class().get_name()
    field='sequence' if cls=='AnimGraphNode_SequencePlayer' else 'blend_space' if cls=='AnimGraphNode_BlendSpacePlayer' else None
    if field:
        data=node.get_editor_property('node');old=data.get_editor_property(field)
        if old:
            assert old.get_path_name() in mapped,old.get_path_name()
            data.set_editor_property(field,mapped[old.get_path_name()]);node.set_editor_property('node',data)
            changes.append({'node':node.get_path_name(),'asset':mapped[old.get_path_name()].get_path_name()})
graph_report=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.build_animation_graph(bp,BASE+'/Animations'))
assert graph_report['success'],graph_report
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
save(bp)
report.update(success=True,anim_blueprint=bp.get_path_name(),remapped_nodes=changes,skeleton=skel.get_path_name())
(OUT/'ue_animation_install.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

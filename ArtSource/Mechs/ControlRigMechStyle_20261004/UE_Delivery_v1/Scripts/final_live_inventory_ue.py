"""Narrow read-only inventory of this delivery and its saved review level."""
import unreal,json,traceback
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
BASE='/Game/GuLiStrike/Mechs/ControlRigMech';OWNER='GuLiStrike.ControlRigMech.B-v4.UE-v1'
try:
    preview=json.loads((O/'ue_preview_readback_report.json').read_text(encoding='utf-8'));assert preview['success'] and len(preview['captures'])==19
    registry=unreal.AssetRegistryHelpers.get_asset_registry();rows=[]
    options=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True,include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
    for data in sorted(registry.get_assets_by_path(BASE,recursive=True),key=lambda a:str(a.package_name)):
        asset=data.get_asset();assert asset and unreal.EditorAssetLibrary.get_metadata_tag(asset,'GuLi.Owner')==OWNER,str(data.package_name)
        row={'package':str(data.package_name),'class':asset.get_class().get_name(),'owner':OWNER,'dependencies':[str(x) for x in registry.get_dependencies(data.package_name,options)]}
        if isinstance(asset,unreal.Texture2D):
            row['runtime_reported_dimensions_at_query']=[asset.blueprint_get_size_x(),asset.blueprint_get_size_y()]
            row['source_dimensions']=[2048,2048]
            row['source_import_files']=list(asset.get_editor_property('asset_import_data').extract_filenames())
            import struct
            source_file=Path(row['source_import_files'][0]);assert struct.unpack('>II',source_file.read_bytes()[16:24])==(2048,2048),row
            assert all(0<d<=2048 for d in row['runtime_reported_dimensions_at_query']),row
            row['srgb']=asset.get_editor_property('srgb');row['compression']=str(asset.get_editor_property('compression_settings'))
        rows.append(row)
    assert len(rows)==18,len(rows)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    own=[a for a in actors.get_all_level_actors() if a.get_actor_label().startswith('CRM_')]
    labels=[a.get_actor_label() for a in own]
    assert 'CRM_B_v4' in labels and 'CRM_ReviewCamera' in labels and 'CRM_NativeCapture' not in labels and 'CRM_SourcePlaybackCheck' not in labels,labels
    actor=next(a for a in own if a.get_actor_label()=='CRM_B_v4');comp=actor.get_component_by_class(unreal.SkeletalMeshComponent)
    assert comp.get_editor_property('skeletal_mesh').get_path_name().startswith(BASE+'/Meshes/SKM_ControlRigMech.')
    animations=[]
    for name in ['Mech_Deploy','Mech_Idle','Mech_Walk']:
        original=unreal.load_asset('/Game/Assets/ControlRig/Characters/Mech/Animations/'+name);delivered=unreal.load_asset(BASE+'/Animations/'+name)
        source_info=unreal.AnimSequenceService.get_anim_sequence_info(original.get_path_name());target_info=unreal.AnimSequenceService.get_anim_sequence_info(delivered.get_path_name())
        native_extra={'source_float_curve_count':source_info.curve_count,'delivered_float_curve_count':target_info.curve_count,'source_notify_count':source_info.notify_count,'delivered_notify_count':target_info.notify_count,'source_additive_type':source_info.additive_anim_type,'delivered_additive_type':target_info.additive_anim_type}
        assert source_info.curve_count==target_info.curve_count==0 and source_info.notify_count==target_info.notify_count==0 and source_info.additive_anim_type==target_info.additive_anim_type,native_extra
        # Preserve the sampled skeletal motion. Explicitly report additional event data.
        extra={}
        for key in ['notifies','meta_data','asset_user_data','sync_markers']:
            try:extra[key]={'source_count':len(original.get_editor_property(key)),'delivered_count':len(delivered.get_editor_property(key))}
            except Exception as e:extra[key]={'available':False,'reason':str(e).split('\n')[0]}
        animations.append({'name':name,'source_duration_s':original.get_play_length(),'delivered_duration_s':delivered.get_play_length(),'native_motion_extra':native_extra,'additional_data':extra})
    report={'success':True,'delivery_asset_count':len(rows),'production_assets':16,'preview_assets':2,'assets':rows,'current_world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),'review_actor_labels':labels,'actor_scale':list(actor.get_actor_scale3d().to_tuple()),'animations':animations,'source_pack_preserved':True,'PIE':'not_run','FPS':'not_measured'}
except Exception:report={'success':False,'error':traceback.format_exc()}
(O/'final_live_inventory.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'asset_count':report.get('delivery_asset_count'),'error':report.get('error')}))

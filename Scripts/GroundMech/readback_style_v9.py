"""Read saved art references and instance defaults in a fresh source-editor worker."""
import unreal,json,traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9')
result={'success':False}
try:
    assert '-MechStyleReadbackWorker' in unreal.SystemLibrary.get_command_line()
    ground=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
    abp=unreal.load_asset('/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech')
    cdo=unreal.get_default_object(ground.generated_class())
    # Check the serialized class before compilation can refresh stale defaults.
    instance=unreal.new_object(abp.generated_class(),outer=cdo.mesh)
    assert instance.get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION
    result['fresh_instance_root_motion_before_compile']=str(instance.get_editor_property('root_motion_mode'))
    expected=json.loads((OUT/'ue_import.json').read_text())['parts']
    result['components']={}
    for part,prop in [('Legs','mesh'),('Armor','armor'),('Shoulder','shoulder'),('Machinegun','machinegun')]:
        c=cdo.get_editor_property(prop)
        m=c.get_skinned_asset() if part in ('Legs','Machinegun') else c.static_mesh
        assert m.get_path_name().split('.')[0]==expected[part]['path']
        result['components'][prop]={'mesh':m.get_path_name(),'material_count':c.get_num_materials(),'socket':str(c.get_attach_socket_name()),'scale':list(c.relative_scale3d.to_tuple())}
    assert cdo.mesh.get_editor_property('anim_class')==abp.generated_class()
    spider=unreal.load_asset('/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled')
    mesh=unreal.get_default_object(spider.generated_class()).get_component_by_class(unreal.SkeletalMeshComponent)
    assert mesh.get_skinned_asset().get_path_name().split('.')[0]==expected['SpiderMech']['path']
    assert mesh.get_editor_property('anim_class')==unreal.EditorAssetLibrary.load_blueprint_class('/Game/Assets/Mech_Project/Characters/SpiderMech/SpiderMech_Anim_BP')
    result['spider']={'mesh':mesh.get_skinned_asset().get_path_name(),'materials':mesh.get_num_materials(),'animation':mesh.get_editor_property('anim_class').get_path_name(),'auto_possess_player':str(unreal.get_default_object(spider.generated_class()).get_editor_property('auto_possess_player'))}
    assert mesh.get_num_materials()==11
    result['compiled']=[]
    for bp in (ground,abp,spider):
        assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
        result['compiled'].append(bp.get_path_name())
    result['success']=True
except Exception:result['error']=traceback.format_exc()
(OUT/'ue_saved_readback.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.SystemLibrary.quit_editor()

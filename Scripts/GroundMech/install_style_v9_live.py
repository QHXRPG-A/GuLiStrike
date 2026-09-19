"""Install imported visuals through stable gameplay Blueprint references."""
import unreal,json
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9')
report=json.loads((OUT/'ue_import.json').read_text());assert report['success']
LIB=unreal.EditorAssetLibrary;LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not LEVEL.is_in_play_in_editor(),'Install art before entering PIE'
assert EDITOR.get_editor_world().get_path_name().startswith('/Game/Maps/LVL_GroundMech_Demo.')
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Keep unrelated map edits separate'
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Game/GuLiStrike/GroundMech/Style_v9','/Game/GuLiStrike/Mechs/SpiderMech'],True)
BASE='/Game/GuLiStrike/GroundMech'
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light');before=unreal.get_default_object(bp.generated_class())
backup=BASE+'/Style_v9/Rollback/BP_GroundMech_Light_BeforeStyle'
if not LIB.does_asset_exist(backup):
    LIB.make_directory(backup.rsplit('/',1)[0]);copy=LIB.duplicate_asset(bp.get_path_name(),backup);assert copy
    assert LIB.save_loaded_asset(copy,False)
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
cdo=unreal.get_default_object(bp.generated_class());bp.modify();cdo.modify()
old={};new={}
for part,prop in [('Legs','mesh'),('Armor','armor'),('Shoulder','shoulder'),('Machinegun','machinegun')]:
    c=cdo.get_editor_property(prop);c.modify();sk=part in ('Legs','Machinegun')
    old[prop]=(c.get_skinned_asset() if sk else c.static_mesh).get_path_name()
    mesh=unreal.load_asset(report['parts'][part]['path']);assert mesh
    if sk:c.set_skeletal_mesh_asset(mesh)
    else:c.set_static_mesh(mesh)
    c.set_editor_property('override_materials',[]);new[prop]=mesh.get_path_name()
assert LIB.save_loaded_asset(bp,False)
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
cdo=unreal.get_default_object(bp.generated_class())
for part,prop in [('Legs','mesh'),('Armor','armor'),('Shoulder','shoulder'),('Machinegun','machinegun')]:
    c=cdo.get_editor_property(prop);assert (c.get_skinned_asset() if part in ('Legs','Machinegun') else c.static_mesh).get_path_name()==new[prop]
assert LIB.save_loaded_asset(bp,False)
abp=unreal.load_asset(BASE+'/Animations/ABP_GroundMech')
anim_backup=BASE+'/Style_v9/Rollback/ABP_GroundMech_BeforeStyleValidation'
if not LIB.does_asset_exist(anim_backup):
    copy=LIB.duplicate_asset(abp.get_path_name(),anim_backup);assert copy
    assert LIB.save_loaded_asset(copy,False)
unreal.BlueprintEditorLibrary.compile_blueprint(abp)
assert LIB.save_loaded_asset(abp,False)
assert cdo.mesh.get_editor_property('anim_class')==abp.generated_class()
assert unreal.get_default_object(abp.generated_class()).get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION
instance=unreal.new_object(abp.generated_class(),outer=cdo.mesh)
assert instance.get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION
instance=None

sp=unreal.load_asset(report['spider_blueprint']);scdo=unreal.get_default_object(sp.generated_class())
spmesh=scdo.get_component_by_class(unreal.SkeletalMeshComponent)
assert spmesh.get_skinned_asset().get_path_name().split('.')[0]==report['parts']['SpiderMech']['path']
assert spmesh.get_editor_property('anim_class')==LIB.load_blueprint_class('/Game/Assets/Mech_Project/Characters/SpiderMech/SpiderMech_Anim_BP')
assert unreal.BlueprintService.compile_blueprint(sp.get_path_name())

world=EDITOR.get_editor_world()
hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(18000,-6500,100000),unreal.Vector(18000,-6500,-100000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
values=hit.to_tuple();assert values[0] and not values[1]
floor=values[5]
label='GroundMech_SpiderMech_Styled_Reference'
existing=[a for a in ACTORS.get_all_level_actors() if a.get_actor_label()==label]
assert len(existing)<=1
spider=existing[0] if existing else ACTORS.spawn_actor_from_class(sp.generated_class(),floor+unreal.Vector(0,0,160.5),unreal.Rotator(yaw=-20))
spider.set_actor_label(label);spider.set_folder_path('GroundMech_Demo')
spider.tags=[unreal.Name('GuLi.MechStyleSync.v9')]
assert LEVEL.save_current_level()
result={'success':True,'ground_blueprint':bp.get_path_name(),'rollback':backup,'previous_meshes':old,'new_meshes':new,
    'ground_actor_scale':[1,1,1],'ground_mesh_scale':list(cdo.mesh.relative_scale3d.to_tuple()),
    'ground_animation':abp.get_path_name(),'root_motion':'IgnoreRootMotion','spider_blueprint':sp.get_path_name(),
    'spider_placed_actor':spider.get_path_name(),'spider_location_cm':list(spider.get_actor_location().to_tuple()),
    'spider_animation':spmesh.get_editor_property('anim_class').get_path_name(),
    'map':world.get_path_name(),'dirty_content_preserved':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
(OUT/'live_install.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))

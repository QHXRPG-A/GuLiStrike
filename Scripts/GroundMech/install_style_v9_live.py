"""Place the independent styled SpiderMech on its original demo map."""
import unreal,json
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9')
report_file=OUT/'spider-import.json'
if not report_file.exists():
    report_file=OUT/'ue_import.json'
report=json.loads(report_file.read_text(encoding='utf-8-sig'));assert report['success']
LIB=unreal.EditorAssetLibrary;LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not LEVEL.is_in_play_in_editor(),'Install art before entering PIE'
assert EDITOR.get_editor_world().get_path_name().startswith('/Game/Maps/LVL_GroundMech_Demo.')
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Keep unrelated map edits separate'
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Game/GuLiStrike/Mechs/SpiderMech'],True)
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
result={'success':True,'spider_blueprint':sp.get_path_name(),
    'spider_placed_actor':spider.get_path_name(),'spider_location_cm':list(spider.get_actor_location().to_tuple()),
    'spider_animation':spmesh.get_editor_property('anim_class').get_path_name(),
    'map':world.get_path_name(),'dirty_content_preserved':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
(OUT/'spider-install.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))

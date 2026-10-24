import unreal,json
from pathlib import Path
ROOT=Path('D:/UE5.7/test1')
ART=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023'
BASE='/Game/GuLiStrike/GroundMech'
NEW=BASE+'/MechaDestroyer_v1'
LIB=unreal.EditorAssetLibrary
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
rollback=NEW+'/Rollback/BP_GroundMech_BeforeMechaDestroyer'
if not LIB.does_asset_exist(rollback):assert LIB.duplicate_asset(BASE+'/BP_GroundMech_Light',rollback)
scope={'__name__':'mecha_catalog_import','GULI_TABLE_FILTER':['DT_GuLiStrikeModels_Models','DT_GuLiStrikeModels_Parts'],'GULI_IMPORT_REPORT':str(ART/'datatable_import.json'),'GULI_IMPORT_PROGRESS':str(ART/'datatable_import.log')}
exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8'),'scoped_mecha_import','exec'),scope)
assert not json.loads((ART/'datatable_import.json').read_text(encoding='utf8'))['errors']
for sub in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem):sub.reload_catalog()
mesh=unreal.load_asset(NEW+'/Meshes/SK_MechaDestroyer')
anim=unreal.load_asset(NEW+'/Animations/ABP_GroundMech_MechaDestroyer')
assert mesh and anim
bp.modify();cdo=unreal.get_default_object(bp.generated_class());cdo.modify()
component=cdo.get_editor_property('mesh');component.modify()
height=cdo.get_editor_property('standing_height');bounds=mesh.get_imported_bounds();scale=height/(bounds.box_extent.z*2)
capsule=cdo.get_editor_property('capsule_component');half=capsule.get_unscaled_capsule_half_height()
cdo.set_editor_property('integrated_body_mesh',True)
component.set_skeletal_mesh_asset(mesh)
component.set_anim_instance_class(anim.generated_class())
component.set_relative_scale3d(unreal.Vector(scale,scale,scale))
component.set_relative_rotation(unreal.Rotator(yaw=-90),False,False)
# Center the feet over the existing capsule, preserving its collision dimensions.
component.set_relative_location(unreal.Vector(16.7*scale,11.62*scale,-half-(bounds.origin.z-bounds.box_extent.z)*scale),False,False)
for p in ['armor','shoulder']:
    c=cdo.get_editor_property(p);c.modify();c.set_static_mesh(None)
gun=cdo.get_editor_property('machinegun');gun.modify();gun.set_skeletal_mesh_asset(None)
assert unreal.BlueprintService.compile_blueprint(BASE+'/BP_GroundMech_Light')
assert LIB.save_loaded_asset(bp,False)
report={'success':True,'blueprint':bp.get_path_name(),'rollback':rollback,'model':mesh.get_path_name(),'anim':anim.get_path_name(),'scale':scale,'height_preserved':height,'capsule_preserved':{'half_height':half,'radius':capsule.get_unscaled_capsule_radius()},'geometry_stylization':False}
(ART/'installed_player_adapter.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

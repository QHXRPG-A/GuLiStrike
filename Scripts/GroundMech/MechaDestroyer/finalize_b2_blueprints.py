import unreal,json
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
BASE='/Game/GuLiStrike/GroundMech';NEW=BASE+'/MechaDestroyer_A4B2'
BS=unreal.BlueprintService;LIB=unreal.EditorAssetLibrary
bp_path=BASE+'/BP_GroundMech_Light';name='GroundModelPresentation'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
if not BS.component_exists(bp_path,name):assert BS.add_component(bp_path,'GuLiModelPresentationComponent',name)
assert BS.set_component_property(bp_path,name,'ModelId','4001')
assert BS.compile_blueprint(bp_path)
assert BS.get_component_property(bp_path,name,'ModelId')=='4001'
assert LIB.save_asset(bp_path,False)
# FireReview is a separate native child, not a child of the formal Ground BP.
# Keep its gameplay defaults; apply only this same mesh/animation/registration.
review_path=BASE+'/Review/BP_GroundMech_FireReview'
backup=NEW+'/Rollback/BP_FireReview_BeforeA4B2'
if not LIB.does_asset_exist(backup):assert LIB.duplicate_asset(review_path,backup)
review=unreal.load_asset(review_path);review.modify()
rcdo=unreal.get_default_object(review.generated_class());rcdo.modify()
rmesh=rcdo.mesh;rmesh.modify()
mesh=unreal.load_asset(NEW+'/Meshes/SK_GroundMech_A4B2')
anim=unreal.load_asset(NEW+'/Animations/ABP_GroundMech_A4B2')
bounds=mesh.get_imported_bounds();scale=rcdo.get_editor_property('standing_height')/(bounds.box_extent.z*2)
half=rcdo.get_editor_property('capsule_component').get_unscaled_capsule_half_height()
rcdo.set_editor_property('integrated_body_mesh',True)
rmesh.set_skeletal_mesh_asset(mesh);rmesh.set_editor_property('override_materials',[]);rmesh.set_anim_instance_class(anim.generated_class())
rmesh.set_relative_scale3d(unreal.Vector(scale,scale,scale));rmesh.set_relative_rotation(unreal.Rotator(yaw=-90),False,False)
rmesh.set_relative_location(unreal.Vector(0,0,-half-(bounds.origin.z-bounds.box_extent.z)*scale),False,False)
for prop in ['armor','shoulder']:
    c=rcdo.get_editor_property(prop);c.modify();c.set_static_mesh(None)
rcdo.get_editor_property('machinegun').set_skeletal_mesh_asset(None)
if not BS.component_exists(review_path,name):assert BS.add_component(review_path,'GuLiModelPresentationComponent',name)
assert BS.set_component_property(review_path,name,'ModelId','4001')
records=[]
for path in [bp_path,BASE+'/Review/BP_GroundMech_FireReview',BASE+'/BP_GroundMech_DemoMode',BASE+'/Review/BP_GroundMech_FireReviewMode']:
    bp=unreal.load_asset(path);assert bp
    assert BS.compile_blueprint(path)
    cdo=unreal.get_default_object(bp.generated_class())
    rec={'path':path,'compiled':True,'class':cdo.get_class().get_path_name()}
    if isinstance(cdo,unreal.GuLiGroundMechCharacter):
        comp=cdo.get_editor_property('mesh')
        rec.update(mesh=comp.get_skeletal_mesh_asset().get_path_name(),anim=comp.anim_class.get_path_name(),integrated=cdo.get_editor_property('integrated_body_mesh'),model_id=cdo.model_id,
            presentation_model=BS.get_component_property(path,name,'ModelId'),root_motion=str(unreal.get_default_object(comp.anim_class).get_editor_property('root_motion_mode')))
        assert rec['mesh'].startswith(NEW+'/') and rec['anim'].startswith(NEW+'/') and rec['integrated']
        assert rec['presentation_model']=='4001'
        assert 'IGNORE_ROOT_MOTION' in rec['root_motion']
    else:
        rec['default_pawn']=str(cdo.get_editor_property('default_pawn_class'))
    assert LIB.save_loaded_asset(bp,False);records.append(rec)
materials=[]
for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(NEW+'/Materials',True):
    p=str(a.package_name)
    ok=bool(unreal.MaterialService.compile_material(p));assert ok,p
    materials.append({'path':p,'compiled':ok})
report={'success':True,'blueprints':records,'materials':materials,'team_registration':'GroundModelPresentation.BeginPlay -> RefreshModel -> Model 4001 / Body / CharacterMesh0 -> LocalTeamColorSubsystem','visual_verification':'User','pie_started':False}
(OUT/'final_blueprint_material_checks.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

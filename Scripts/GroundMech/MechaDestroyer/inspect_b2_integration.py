import unreal, json
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023')
REG=unreal.AssetRegistryHelpers.get_asset_registry()
BASE='/Game/GuLiStrike/GroundMech'
def path(o):return o.get_path_name() if o else None
def prop(o,n):
    try:return str(o.get_editor_property(n))
    except Exception as e:return str(e)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
report={'success':True,'world':path(world),'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'new_root_exists':unreal.EditorAssetLibrary.does_directory_exist(BASE+'/MechaDestroyer_A4B2'),'assets':[],'bp':{},'apis':{}}
for root in ['/Game/MechaDestroyer',BASE]:
    for a in REG.get_assets_by_path(root,True):
        p=str(a.package_name);kind=str(a.asset_class_path.asset_name)
        if kind in ['AnimSequence','AnimMontage','BlendSpace','AnimBlueprint','Skeleton','SkeletalMesh','Blueprint','World']:
            o=unreal.load_asset(p)
            d={'path':p,'class':kind}
            if hasattr(o,'skeleton'):d['skeleton']=path(o.skeleton)
            if kind=='AnimSequence':d.update(length=o.sequence_length,additive=prop(o,'additive_anim_type'))
            if kind=='AnimBlueprint':d.update(skeleton=prop(o,'target_skeleton'),status=prop(o,'status'))
            report['assets'].append(d)
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light');cdo=unreal.get_default_object(bp.generated_class());mesh=cdo.get_editor_property('mesh')
report['bp']={n:prop(cdo,n) for n in ['standing_height','integrated_body_mesh','model_id','team_id']}
report['bp'].update(mesh=path(mesh.get_skeletal_mesh_asset()),anim=prop(mesh,'anim_class'),location=prop(mesh,'relative_location'),rotation=prop(mesh,'relative_rotation'),scale=prop(mesh,'relative_scale3d'))
for cls in ['AnimationLibrary','AnimationBlueprintLibrary','AnimationDataController','AnimSequence','SkeletonService','SkinWeightModifier','AnimGraphService','BlueprintService']:
    c=getattr(unreal,cls,None)
    if c:report['apis'][cls]=[n for n in dir(c) if any(s in n for s in ['bone','track','retarget','animation','key','compile','property','node','skeleton','transform'])]
(ART/'b2_integration_before.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

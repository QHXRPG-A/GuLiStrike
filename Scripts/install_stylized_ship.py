"""Attach the cel hull to the retained collision/socket hull in the player BP."""
import unreal,json,traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/StylePass_20260917')
BP='/Game/GuLiStrike/Ship/BP_CombatAvatarFly01';BASE='/Game/GuLiStrike/Ship/Stylized'
LIB=unreal.EditorAssetLibrary;S=unreal.BlueprintService
REPORT={'success':False}

def run():
    assert not unreal.WidgetService.is_pie_running()
    bp=unreal.load_asset(BP);assert bp
    rollback=BASE+'/Rollback/BP_CombatAvatarFly01_BeforeCel'
    if not LIB.does_asset_exist(rollback):assert LIB.duplicate_asset(BP,rollback)
    cdo=unreal.get_default_object(bp.generated_class());hull=cdo.get_editor_property('HullMesh')
    REPORT['original_hull']={'mesh':hull.static_mesh.get_path_name(),'transform':str(hull.get_relative_transform()),'collision':str(hull.get_collision_enabled())}
    sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);data=unreal.SubobjectDataBlueprintFunctionLibrary
    for name,mesh_name in [('AnimeHull','SM_Ship_AnimeHull'),('AnimeContour','SM_Ship_AnimeContour')]:
        if not S.component_exists(BP,name):
            hs=sub.k2_gather_subobject_data_for_blueprint(bp)
            parent=next(h for h in hs if data.get_object(data.get_data(h)).get_name()=='Hull Mesh')
            h,reason=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=parent,new_class=unreal.StaticMeshComponent,blueprint_context=bp,conform_transform_to_parent=True))
            assert data.is_handle_valid(h),str(reason);assert sub.rename_subobject(h,unreal.Text(name))
        for key,value in [('StaticMesh',BASE+'/Meshes/'+mesh_name),('RelativeLocation','(X=0,Y=0,Z=0)'),('RelativeRotation','(Pitch=0,Yaw=0,Roll=0)'),('RelativeScale3D','(X=1,Y=1,Z=1)'),('bVisible','True'),('bHiddenInGame','False'),('CastShadow','False' if name=='AnimeContour' else 'True')]:
            assert S.set_component_property(BP,name,key,value),(name,key)
        assert S.set_collision_settings(BP,name,'NoCollision','','NoCollision',{})
    assert S.compile_blueprint(BP)
    cdo=unreal.get_default_object(bp.generated_class());hull=cdo.get_editor_property('HullMesh')
    hull.modify();hull.set_visibility(False,False);hull.set_cast_shadow(False);bp.modify()
    assert LIB.save_loaded_asset(bp,False)
    REPORT.update(success=True,blueprint=BP,rollback=rollback,hull_visible=hull.get_editor_property('visible'),components=str(S.get_component_hierarchy(BP)))

try:run()
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'ship_install.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(REPORT))

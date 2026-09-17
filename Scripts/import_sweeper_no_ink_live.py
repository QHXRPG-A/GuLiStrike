"""Deferred legacy FBX import on Slate tick, outside the MCP task-graph callback."""
import unreal,json,sys,traceback,time
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StyleAdjust_20260917'
sys.path.insert(0,str(ROOT/'Scripts'))
import import_cel_model_assets as cel
import finalize_cel_model_assets as finish
BASE='/Game/Commander/Units/Tactical/Cel/Sweeper'
REPORT={'success':False,'saved':[]}

def perform():
    assert not unreal.WidgetService.is_pie_running()
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    flag='Interchange.FeatureFlags.Import.Enable';old=unreal.SystemLibrary.get_console_variable_int_value(flag)
    unreal.SystemLibrary.execute_console_command(w,flag+' 0')
    try:
        for name in ['SM_Sweeper_Cel','SK_Sweeper_Cel']:
            p=BASE+'/Meshes/'+name;backup=BASE+'/Rollback/'+name+'_WithInk'
            if not cel.LIB.does_asset_exist(backup):
                asset=cel.LIB.duplicate_asset(p,backup);assert asset;cel.save(asset)
            unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(unreal.load_asset(p))
        mat_path=BASE+'/Materials/M_Sweeper_Cel';backup=BASE+'/Rollback/M_Sweeper_Cel_WithInk'
        if not cel.LIB.does_asset_exist(backup):
            asset=cel.LIB.duplicate_asset(mat_path,backup);assert asset;cel.save(asset)
        # The mesh rollback must retain the old body shader when the live material changes.
        old_mat=unreal.load_asset(backup)
        for name in ['SM_Sweeper_Cel','SK_Sweeper_Cel']:
            asset=unreal.load_asset(BASE+'/Rollback/'+name+'_WithInk')
            if name.startswith('SM_'):
                for i,s in enumerate(asset.static_materials):
                    if 'Contour' not in str(s.material_slot_name):asset.set_material(i,old_mat)
            else:
                slots=list(asset.get_editor_property('materials'))
                for s in slots:
                    if 'Contour' not in str(s.material_slot_name):s.set_editor_property('material_interface',old_mat)
                asset.set_editor_property('materials',slots)
            cel.save(asset)
        atlas=unreal.load_asset(BASE+'/Textures/T_Sweeper_BaseColor')
        mat=cel.material(mat_path,base=atlas,mask=None)
        for skeletal in (False,True):
            name=('SK_' if skeletal else 'SM_')+'Sweeper_Cel'
            source=OUT/'Models'/((('SK_' if skeletal else 'SM_')+'Sweeper_NoInk.fbx'))
            mesh=cel.imported(source,BASE+'/Meshes/'+name,skeletal)
            cel.mesh_settings(mesh,{'Body':mat,'Contour':mat},skeletal,no_ink=True)
            REPORT['saved'].append(mesh.get_path_name())
        REPORT['finalization']=finish.finish_models(units=['Sweeper'])
        REPORT['assets']=cel.REPORT['assets'];REPORT['material']=mat.get_path_name()
        REPORT['diagnostic']=str(unreal.MaterialNodeService.get_material_diagnostics(mat_path))
        mesh=unreal.load_asset(BASE+'/Meshes/SM_Sweeper_Cel')
        assert mesh.get_num_triangles(0)==5456
        assert mesh.get_num_sections(0)==1
        REPORT['material_slots']=[{'slot':str(s.material_slot_name),'material':s.material_interface.get_path_name() if s.material_interface else None} for s in mesh.static_materials]
        REPORT['success']=True
    finally:unreal.SystemLibrary.execute_console_command(w,flag+' '+str(old))

def tick(dt):
    unreal.unregister_slate_post_tick_callback(SWEEPER_IMPORT_HANDLE)
    try:perform()
    except Exception:REPORT['error']=traceback.format_exc()
    (OUT/'sweeper_import.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')

SWEEPER_IMPORT_HANDLE=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'scheduled':'Sweeper only legacy FBX import'}))

"""Reimport the corrected rigid UV metadata; retain the actual runtime mesh settings."""
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineHover_20260930/PodFix'
PATH='/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid'
LIB=unreal.EditorAssetLibrary
SUB=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
sys.path.insert(0,str(ROOT/'Scripts'))
from import_tactical_handbuilt_models import options
r={'success':False,'path':PATH}
flag='Interchange.FeatureFlags.Import.Enable'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
old_flag=unreal.SystemLibrary.get_console_variable_int_value(flag)
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert json.loads((OUT/'source-fix.json').read_text())['geometry_uv0_material_unchanged']
    mesh=unreal.load_asset(PATH);assert mesh
    mats=list(mesh.static_materials)
    probe=unreal.new_object(unreal.StaticMeshComponent);probe.set_static_mesh(mesh)
    sockets=[(str(n),mesh.find_socket(n).relative_location,mesh.find_socket(n).relative_rotation,mesh.find_socket(n).relative_scale) for n in probe.get_all_socket_names()]
    body=mesh.get_editor_property('body_setup')
    collision=body.get_editor_property('agg_geom');trace=body.get_editor_property('collision_trace_flag')
    bounds={k:mesh.get_editor_property(k) for k in ['positive_bounds_extension','negative_bounds_extension']}
    count=SUB.get_lod_count(mesh)
    builds=[SUB.get_lod_build_settings(mesh,i) for i in range(count)]
    reductions=[SUB.get_lod_reduction_settings(mesh,i) for i in range(count)]
    screens=list(SUB.get_lod_screen_sizes(mesh))
    r['before']={'triangles':[mesh.get_num_triangles(i) for i in range(count)],'screens':screens,
        'materials':[x.material_interface.get_path_name() for x in mats],'sockets':[x[0] for x in sockets]}
    unreal.SystemLibrary.execute_console_command(world,flag+' 0')
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(ROOT/'ArtSource/MechanicalAnimation_20260929/WarMachine/SM_WarMachine_Rigid.fbx'),
        destination_path=PATH.rsplit('/',1)[0],destination_name=PATH.rsplit('/',1)[1],automated=True,async_=False,
        replace_existing=True,replace_existing_settings=False,save=False).items():task.set_editor_property(k,v)
    ui=options(False);ui.set_editor_property('reset_to_fbx_on_material_conflict',False)
    data=ui.get_editor_property('static_mesh_import_data')
    data.set_editor_property('reorder_material_to_fbx_order',False);data.set_editor_property('generate_lightmap_u_vs',False)
    task.set_editor_property('options',ui);task.set_editor_property('factory',unreal.FbxFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);assert task.imported_object_paths
    mesh=unreal.load_asset(PATH)
    mesh.set_editor_property('static_materials',mats)
    body=mesh.get_editor_property('body_setup');body.set_editor_property('agg_geom',collision);body.set_editor_property('collision_trace_flag',trace)
    for k,v in bounds.items():mesh.set_editor_property(k,v)
    for name,loc,rot,scale in sockets:
        s=mesh.find_socket(name)
        if not s:s=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);s.set_editor_property('socket_name',name);mesh.add_socket(s)
        for k,v in [('relative_location',loc),('relative_rotation',rot),('relative_scale',scale)]:s.set_editor_property(k,v)
    for i in range(count):
        SUB.set_lod_reduction_settings(mesh,i,reductions[i]);SUB.set_lod_build_settings(mesh,i,builds[i])
    assert SUB.get_lod_count(mesh)==count
    LIB.set_metadata_tag(mesh,'GuLi.PodVisibility','v2; includes launcher caps hatches hinges feet and contour; geometry UV0 unchanged')
    assert LIB.save_loaded_asset(mesh,False)
    r['after']={'triangles':[mesh.get_num_triangles(i) for i in range(count)],'screens':list(SUB.get_lod_screen_sizes(mesh)),
        'materials':[x.material_interface.get_path_name() for x in mesh.static_materials],
        'uv_channels':[SUB.get_num_uv_channels(mesh,i) for i in range(count)]}
    assert r['before']['materials']==r['after']['materials']
    assert r['before']['screens']==r['after']['screens']
    assert r['before']['triangles'][0]==r['after']['triangles'][0]
    # The source-description API reports 0 for generated LODs. Inspect the
    # actual render buffers through the existing native diagnostic instead.
    r['render_lods']=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_missile_pod_mesh_diagnostics(mesh))
    assert all(l['uv_channels']>=3 and l['mixed_pod_triangles']==0 and l['nonintegral_parts']==0 for l in r['render_lods']['lods'])
    r['success']=True
except Exception:r['error']=traceback.format_exc()
finally:unreal.SystemLibrary.execute_console_command(world,flag+' '+str(old_flag))
(OUT/'ue-import.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(r))

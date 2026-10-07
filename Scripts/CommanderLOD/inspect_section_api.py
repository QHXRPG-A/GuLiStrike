import unreal,json
sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
names=[n for n in dir(sub) if 'section' in n or 'material' in n]
report=dict(api={n:str(getattr(sub,n).__doc__) for n in names},meshes=[])
for path in ['/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid','/Game/GuLiStrike/Commander/LODReview_20261005/WM01/Meshes/SM_WarMachine_Rigid']:
 mesh=unreal.load_asset(path);rows=[]
 for lod in range(sub.get_lod_count(mesh)):
  desc=mesh.get_static_mesh_description(lod)
  rows.append(dict(lod=lod,description_api={n:str(getattr(desc,n).__doc__) for n in dir(desc) if 'material' in n or 'polygon_group' in n}) if desc else dict(lod=lod,description=None))
 report['meshes'].append(dict(path=path,rows=rows))
unreal.MCPythonHelper.submit_result(json.dumps(report))

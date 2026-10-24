import unreal, json
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023')
def vec(v): return [v.x,v.y,v.z]
def tx(t): return {'location':vec(t.translation),'rotation':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'scale':vec(t.scale3d)}
mesh=unreal.load_asset('/Game/MechaDestroyer/SKM/SKM_Mecha')
report={}
report['bones']=[{'name':str(b.bone_name),'parent':str(b.parent_bone_name),'local':tx(b.local_transform),'global':tx(b.global_transform)} for b in unreal.SkeletonService.list_bones(mesh.get_path_name())]
report['bounds']=str(mesh.get_bounds())
report['sockets']=[]
for i in range(mesh.num_sockets()):
    s=mesh.get_socket_by_index(i)
    report['sockets'].append({p:str(s.get_editor_property(p)) for p in ['socket_name','bone_name','relative_location','relative_rotation']})
report['services']={k:[x for x in dir(getattr(unreal,k)) if not x.startswith('_')] for k in ['SkeletonService','BlueprintService']}
report['anim_apis']=[k for k in dir(unreal) if any(x in k for x in ['AnimGraph','AnimBlueprint','BlueprintNode','BlueprintGraph'])]
report['tables']={}
for name in ['Mech_Visuals','Mech_Skills','Mech_RocketJump','Models_Models','Models_Parts']:
    t=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrike'+name)
    if t: report['tables'][name]=unreal.DataTableFunctionLibrary.export_data_table_to_json_string(t)
report['game_world']=str(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())
(ART/'ue_contracts.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

import os,unreal,json,math
from pathlib import Path
assert os.getpid()==39244
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
items=[]
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.load_class(None,'/Script/GuLiStrike.GuLiCommanderPresentationActor')):
 for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
  if not c.get_name().startswith('UnitInstances'):continue
  for i in range(c.get_instance_count()):
   t=c.get_instance_transform(i,True); p=t.translation
   if math.hypot(p.x-40000,p.y+155000)<20 and abs(t.scale3d.x)>0.001:items.append({'component':c.get_path_name(),'index':i,'position':[p.x,p.y,p.z]})
assert len(items)==1,items
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'corner':items[0],'scenario':'existing stationary Mass obstacle replay at (40000,-155000)'}))

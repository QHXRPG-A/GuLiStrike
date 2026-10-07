"""Read actual vehicle animation bindings and blueprint component ownership."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,units
actor_sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib=unreal.SubobjectDataBlueprintFunctionLibrary
rows=[]
for unit in units():
 if unit['Id'] not in [3,4]:continue
 bp=unreal.load_asset(unit['PresentationClass'].split('.')[0])
 templates=[]
 for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
  obj=lib.get_object(lib.get_data(handle))
  if isinstance(obj,unreal.MeshComponent):
   templates.append(dict(name=obj.get_name(),type=obj.get_class().get_name(),path=obj.get_path_name(),outer=obj.get_outer().get_path_name()))
 cls=unreal.load_class(None,unit['PresentationClass'])
 actor=actor_sub.spawn_actor_from_class(cls,unreal.Vector(0,0,-60000),transient=True)
 try:
  components=[]
  for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent):
   anim=comp.get_editor_property('anim_class')
   single=comp.get_editor_property('animation_data')
   anim_asset=single.get_editor_property('anim_to_play')
   components.append(dict(name=comp.get_name(),animation_mode=str(comp.get_editor_property('animation_mode')),
    animation_class=anim.get_path_name() if anim else None,animation_asset=anim_asset.get_path_name() if anim_asset else None,
    bones=[str(n) for n in comp.get_all_socket_names()]))
  rows.append(dict(name=unit['Name'],source_class=unit['PresentationClass'],templates=templates,animation_bindings=components))
 finally:actor_sub.destroy_actor(actor)
(ART/'Reports/vehicle_configuration.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,vehicles=rows)))

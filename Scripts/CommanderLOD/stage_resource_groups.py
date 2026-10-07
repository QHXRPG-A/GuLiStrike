"""Prepare complete isolated actor/animation bindings, preserving gameplay classes and source assets."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,REVIEW_PACKAGE,units
from common import require_unapproved_candidate
require_unapproved_candidate()
lib=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);data_lib=unreal.SubobjectDataBlueprintFunctionLibrary
native={row['name']:row for row in json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))['units']}
groups=[]
for row in units():
 name=row['Name'];group=dict(id=row['Id'],name=name,approval='pending',formal_changed=False,
  original_model=row['ModelAsset'],original_presentation=row['PresentationClass'],original_vat=row['VATDefinition'],
  candidate_models=[],component_bindings=[],candidate_presentation=None,candidate_vat=None)
 if name=='BiZhiMao':
  group['candidate_models']=[REVIEW_PACKAGE+'/BiZhiMao/Meshes/SM_BiZhiMao_'+kind for kind in ['VAT','Construction']]
  group['vertex_configuration']=str(ART/'BiZhiMao/vertex_metadata.json')
  group['native_data_asset_state']='requires permitted native compile to expose vertex fields'
 else:
  data=native[name];mapping={x['source']:x['asset'] for x in data['meshes']}
  group['candidate_models']=list(mapping.values())
  if row['VATDefinition']:
   source=row['VATDefinition'];target=REVIEW_PACKAGE+'/'+name+'/VAT/'+source.split('/')[-1].split('.')[0]
   vat=lib.load_asset(target) if lib.does_asset_exist(target) else lib.duplicate_asset(source,target)
   assert vat and vat.is_valid_definition()
   lib.set_metadata_tag(vat,'GuLi.CommanderLOD','3Tier.v1; candidate; B pending');assert lib.save_loaded_asset(vat,False)
   group['candidate_vat']=vat.get_path_name()
  if data['components']:
   original=row['PresentationClass'].split('.')[0];target=REVIEW_PACKAGE+'/'+name+'/BP_'+name+'_LODReview'
   bp=lib.load_asset(target) if lib.does_asset_exist(target) else lib.duplicate_asset(original,target)
   assert bp
   changed={}
   for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
    obj=data_lib.get_object(data_lib.get_data(handle))
    if not isinstance(obj,unreal.MeshComponent) or obj.get_path_name() in changed:continue
    assert obj.get_path_name().startswith(target+'.'),obj.get_path_name()
    skin=isinstance(obj,unreal.SkeletalMeshComponent)
    mesh=obj.get_editor_property('skeletal_mesh_asset' if skin else 'static_mesh')
    if mesh and mesh.get_path_name() in mapping:
     source=mesh.get_path_name();obj.set_editor_property('skeletal_mesh_asset' if skin else 'static_mesh',unreal.load_asset(mapping[source]))
     changed[obj.get_path_name()]=dict(component=obj.get_name(),original=source,candidate=mapping[source])
   unreal.BlueprintEditorLibrary.compile_blueprint(bp)
   lib.set_metadata_tag(bp,'GuLi.CommanderLOD','3Tier.v1; candidate; B pending');assert lib.save_loaded_asset(bp,False)
   actor=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(0,0,-60000),transient=True)
   try:
    actual=[]
    for comp in actor.get_components_by_class(unreal.MeshComponent):
     if not comp.is_visible():continue
     mesh=comp.get_editor_property('skeletal_mesh_asset' if isinstance(comp,unreal.SkeletalMeshComponent) else 'static_mesh')
     if not mesh:continue
     expected=next(x for x in data['components'] if x['name']==comp.get_name())
     assert mesh.get_path_name()==mapping[expected['mesh']],(comp.get_name(),mesh.get_path_name())
     transform=unreal.MathLibrary.make_relative_transform(comp.get_world_transform(),actor.get_actor_transform())
     assert max(abs(a-b) for a,b in zip(transform.translation.to_tuple(),expected['location']))<.01,comp.get_name()
     assert max(abs(a-b) for a,b in zip(transform.scale3d.to_tuple(),expected['scale']))<.001,comp.get_name()
     materials=[mat.get_path_name() if mat else None for mat in comp.get_materials()]
     assert materials==expected['materials'],comp.get_name()
     actual.append(dict(component=comp.get_name(),mesh=mesh.get_path_name(),location=list(transform.translation.to_tuple()),
      rotation=list(transform.rotation.rotator().to_tuple()),scale=list(transform.scale3d.to_tuple()),materials=materials))
    assert len(actual)==8,len(actual)
    group['candidate_presentation']=bp.generated_class().get_path_name();group['component_bindings']=actual
   finally:actors.destroy_actor(actor)
 groups.append(group)
(ART/'Reports/resource_groups.json').write_text(json.dumps(dict(success=True,groups=groups),ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,groups=len(groups),formal_assets_changed=False)))

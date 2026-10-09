"""Read actual mesh-slot parameters, including assembled children; no asset writes."""
import json
from pathlib import Path
import unreal

ROOT=Path(r'D:/UE5.7/test1')
models={r['Id']:r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))}
parts=json.loads((ROOT/'Data/Json/DT_GuLiStrikeModels_Parts.json').read_text(encoding='utf8'))
lib=unreal.MaterialEditingLibrary
captured=[]
for part in parts:
    resource=models[part['ChildModelId'] or part['ModelId']]
    if resource['ResourceType']=='PresentationClass':continue
    mesh=unreal.load_object(None,resource['ResourcePath'])
    static=isinstance(mesh,unreal.StaticMesh)
    component=unreal.new_object(unreal.StaticMeshComponent if static else unreal.SkeletalMeshComponent)
    if static:component.set_static_mesh(mesh)
    else:component.set_skeletal_mesh_asset(mesh)
    slots=mesh.get_editor_property('static_materials' if static else 'materials')
    for slot in slots:
        mat=slot.material_interface
        if not mat:continue
        for typ in ('Vector','Scalar'):
            names=lib.get_vector_parameter_names(mat) if typ=='Vector' else lib.get_scalar_parameter_names(mat)
            for name in names:
                if typ=='Vector':
                    v=lib.get_material_instance_vector_parameter_value(mat,name) if isinstance(mat,unreal.MaterialInstanceConstant) else lib.get_material_default_vector_parameter_value(mat,name)
                    defaults=dict(DefaultR=v.r,DefaultG=v.g,DefaultB=v.b,DefaultA=v.a,DefaultScalar=0)
                    cpd=component.get_custom_primitive_data_index_for_vector_parameter(name)
                else:
                    v=lib.get_material_instance_scalar_parameter_value(mat,name) if isinstance(mat,unreal.MaterialInstanceConstant) else lib.get_material_default_scalar_parameter_value(mat,name)
                    defaults=dict(DefaultScalar=v,DefaultR=0,DefaultG=0,DefaultB=0,DefaultA=0)
                    cpd=component.get_custom_primitive_data_index_for_scalar_parameter(name)
                captured.append(dict(ModelId=part['ModelId'],PartKey=part['PartKey'],MaterialSlotName=str(slot.material_slot_name),
                    ParameterName=str(name),ParameterType=typ,CPDIndex=int(cpd),material=mat.get_path_name(),**defaults))
    # Unregistered transient components have no owner/world; drop the Python reference.
    del component
report={'parameters':captured,'models':len(models),'parts':len(parts),'asset_writes':False}
(ROOT/'Data/Models/registered-parameter-inventory.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(dict(parameters=len(captured),cpd=sum(p['CPDIndex']>=0 for p in captured),asset_writes=False)))

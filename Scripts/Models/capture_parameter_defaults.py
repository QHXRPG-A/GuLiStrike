"""Read current material defaults for the Excel contract. No asset writes."""
import json
from pathlib import Path
import unreal
root=Path(r'D:/UE5.7/test1')
models={r['Id']:r for r in json.loads((root/'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf-8'))}
parts={(r['ModelId'],r['PartKey']):r for r in json.loads((root/'Data/Json/DT_GuLiStrikeModels_Parts.json').read_text(encoding='utf-8'))}
bindings=json.loads((root/'Data/Json/DT_GuLiStrikeModels_MaterialParameters.json').read_text(encoding='utf-8'))
cache,values={},{}
for b in bindings:
    if b['Scope']!='Existing': continue
    p=parts.get((b['ModelId'],b['PartKey']));mid=(p['ChildModelId'] or p['ModelId']) if p else b['ModelId']
    if mid not in cache:
        mesh=unreal.load_object(None,models[mid]['ResourcePath'])
        mats=mesh.get_editor_property('static_materials' if isinstance(mesh,unreal.StaticMesh) else 'materials')
        cache[mid]={str(s.material_slot_name):s.material_interface for s in mats}
    material=cache[mid][b['MaterialSlotName']]
    instance=isinstance(material,unreal.MaterialInstanceConstant)
    lib=unreal.MaterialEditingLibrary
    if b['ParameterType']=='Vector':
        color=(lib.get_material_instance_vector_parameter_value if instance else lib.get_material_default_vector_parameter_value)(material,b['ParameterName'])
        values[b['Name']]={'DefaultR':color.r,'DefaultG':color.g,'DefaultB':color.b,'DefaultA':color.a}
    else:
        values[b['Name']]={'DefaultScalar':(lib.get_material_instance_scalar_parameter_value if instance else lib.get_material_default_scalar_parameter_value)(material,b['ParameterName'])}
(root/'Data/Models/parameter-defaults-readback.json').write_text(json.dumps(values,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'material_defaults':len(values),'asset_writes':False}))

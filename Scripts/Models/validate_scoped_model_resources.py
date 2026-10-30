"""Read-only resource/CPD preflight for an explicitly selected catalogue update.

Historical migration snapshots describe unchanged geometry, so they cannot be
used as an approval gate for an intentionally replaced, approved model.
"""
import json
from pathlib import Path
import unreal

def validate(project,model_ids):
    root=Path(project)
    def rows(sheet):return json.loads((root/('Data/Json/DT_GuLiStrikeModels_'+sheet+'.json')).read_text(encoding='utf-8-sig'))
    definitions={r['Id']:r for r in rows('Models')}
    parameters=rows('MaterialParameters');parts=rows('Parts')
    errors=[];checked=[]
    for model_id in model_ids:
        row=definitions.get(model_id)
        if not row:
            errors.append('Missing stable model '+str(model_id));continue
        cls={'StaticMesh':unreal.StaticMesh,'SkeletalMesh':unreal.SkeletalMesh,'PresentationClass':unreal.Class}.get(row['ResourceType'])
        asset=unreal.load_object(None,row['ResourcePath'])
        if cls is None or not isinstance(asset,cls):
            errors.append('Missing/wrong resource '+row['ResourcePath']);continue
        if row['CandidateResourcePath']:
            errors.append('Formal scoped import still has a candidate resource '+str(model_id))
        bindings=[b for b in parameters if b['ModelId']==model_id and b['Scope']=='Existing']
        for binding in bindings:
            if binding['Driver']!='CPD':continue
            targets=[p for p in parts if p['ModelId']==model_id and (binding['PartKey']=='Root' or binding['PartKey']==p['PartKey'])]
            for part in targets:
                target=definitions[part['ChildModelId'] or model_id]
                resource=unreal.load_object(None,target['ResourcePath'])
                component=unreal.new_object(unreal.StaticMeshComponent if target['ResourceType']=='StaticMesh' else unreal.SkeletalMeshComponent)
                if isinstance(component,unreal.StaticMeshComponent):component.set_static_mesh(resource)
                else:component.set_skeletal_mesh_asset(resource)
                getter=component.get_custom_primitive_data_index_for_vector_parameter if binding['ParameterType']=='Vector' else component.get_custom_primitive_data_index_for_scalar_parameter
                actual=getter(binding['ParameterName'])
                if actual!=binding['CustomDataIndex']:errors.append('Wrong CPD binding '+str((model_id,binding['ParameterName'],actual,binding['CustomDataIndex'])))
        for region in rows('ColorRegions'):
            if region['ModelId']==model_id and region['MaskSource'].startswith('/Game/'):
                if not isinstance(unreal.load_object(None,region['MaskSource']),unreal.Texture2D):errors.append('Missing region mask '+region['MaskSource'])
        checked.append({'model_id':model_id,'resource':asset.get_path_name(),'bindings':len(bindings)})
    return {'errors':errors,'checked':checked,'scope':'explicit current source resource/CPD contract; no historical geometry parity assertion'}

"""Static editor resource validation. Does not compile/import DataTables or start play."""
import json
from pathlib import Path
import unreal

ROOT = Path(r'D:/UE5.7/test1')
promotion_path=ROOT/'ArtSource/ModelInterface_B_20261008/formal-promotion.json'
promotion=json.loads(promotion_path.read_text(encoding='utf8')) if promotion_path.exists() else {}
replacements={r['source']:r['formal'] for r in promotion.get('replacements',[])}
approved_paths=set(replacements.values())
# Sweeper deliberately keeps the original mesh and fixed atlas, using an approved
# UV0 mask instead of the generic face-role palette. Validate that exact exception.
sweeper_evidence=ROOT/'ArtSource/SweeperTeamColor_v1_20261008/formal-ue-import.json'
sweeper=json.loads(sweeper_evidence.read_text(encoding='utf8')) if sweeper_evidence.exists() else {}
sweeper_path=sweeper.get('mesh') if sweeper.get('success') else None
def rows(sheet): return json.loads((ROOT / ('Data/Json/DT_GuLiStrikeModels_' + sheet + '.json')).read_text(encoding='utf-8-sig'))
models = {r['Id']:r for r in rows('Models')}
parts = {(r['ModelId'],r['PartKey']):r for r in rows('Parts')}
errors, deferred, checked = [], [], []
resources, slots = {}, {}
for model in models.values():
    resource = unreal.load_object(None, model['ResourcePath'])
    expected = {'StaticMesh':unreal.StaticMesh, 'SkeletalMesh':unreal.SkeletalMesh, 'PresentationClass':unreal.Class}[model['ResourceType']]
    if not resource or not isinstance(resource, expected):
        errors.append(f"Model {model['Id']}: resource missing/wrong type"); continue
    resources[model['Id']] = resource
    if isinstance(resource, (unreal.StaticMesh,unreal.SkeletalMesh)):
        mats = resource.get_editor_property('static_materials' if isinstance(resource,unreal.StaticMesh) else 'materials')
        slots[model['Id']] = {str(m.material_slot_name):m.material_interface for m in mats}
    checked.append({'id':model['Id'],'name':model['Name'],'resource':resource.get_path_name(),'type':model['ResourceType']})
    if model['VATDefinition'] and not unreal.load_object(None,model['VATDefinition']): errors.append(f"Model {model['Id']}: missing VAT")
    if model['bTeamColorEnabled'] and not model['CandidateResourcePath'] and model['ResourcePath'] not in approved_paths and model['ResourcePath']!=sweeper_path: deferred.append({'id':model['Id'],'reason':'No authored paint mask promoted for this model'})

baseline = json.loads((ROOT / 'Data/Models/migration-baseline-20261008.json').read_text(encoding='utf-8'))
import ast
# Reuse only the read-only snapshot helpers; never rerun/overwrite the baseline.
tree=ast.parse((ROOT/'Scripts/Models/capture_model_baseline.py').read_text(encoding='utf8'))
helpers=ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('mesh_info','component_info')],type_ignores=[])
exec(compile(helpers,'baseline_read_helpers','exec'),globals())
import re
def normalized(value):
    if isinstance(value,str):return re.sub(r' \(0x[0-9A-Fa-f]+\)','',value)
    if isinstance(value,list):return [normalized(v) for v in value]
    if isinstance(value,dict):return {k:normalized(v) for k,v in value.items()}
    return value
parity=[]
for original in baseline['models']:
    if original['type']=='PresentationClass':continue
    path=replacements.get(original['path'],original['path'])
    asset=unreal.load_object(None,path)
    if path==sweeper_path:
        source=json.loads((ROOT/'ArtSource/SweeperTeamColor_v1_20261008/original-source-readback.json').read_text(encoding='utf8'))
        current=mesh_info(asset)
        for slot in current['slots']:
            slot['vectors']=[n for n in slot['vectors'] if n!='GuLi_TeamPrimary']
            slot['scalars']=[n for n in slot['scalars'] if n!='GuLi_TeamEnabled']
        snapshot=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(asset))
        material=unreal.load_object(None,sweeper['material'])
        mask=unreal.load_object(None,sweeper['mask'])
        tone=next((e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer()==material and [str(i.get_editor_property('input_name')) for i in e.get_editor_property('inputs')]==['Base','N','L','Ink']),None)
        valid=(current==original and snapshot==source['snapshot'] and isinstance(mask,unreal.Texture2D)
               and unreal.EditorAssetLibrary.get_metadata_tag(mask,'GuLi.SourceSHA256')==sweeper['mask_sha256']
               and unreal.EditorAssetLibrary.get_metadata_tag(material,'GuLi.TeamMask')==sweeper['mask']
               and tone and tone.get_editor_property('code')==sweeper['tone_code'])
        if not valid:errors.append('Sweeper approved UV mask/native source invariants failed: '+path)
        else:parity.append({'resource':path,'all_native_attributes':'exact original across all LODs','fixed_atlas_and_three_tone':'preserved','team_mask':sweeper['mask']})
    elif path in approved_paths:
        validation=json.loads(unreal.EditorAssetLibrary.get_metadata_tag(asset,'GuLi.PaintValidation') or '{}')
        current=mesh_info(asset)
        if not validation.get('source_triangle_attributes_exact') or not validation.get('geometric_vertices_exact') or [s['name'] for s in current['slots']]!=[s['name'] for s in original['slots']]:errors.append('Promoted color-only source/slot validation failed: '+path)
        else:parity.append({'resource':path,'source_attributes':'exact color-only paint','gpu_repacked':validation.get('gpu_repacked'),'nanite_source_preserved':validation.get('nanite_source_preserved')})
    elif not asset or mesh_info(asset)!=original:errors.append('Existing mesh/material slot snapshot changed: '+original['path'])
    else:parity.append({'resource':original['path'],'material_slots_and_parameter_names':'unchanged'})
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
ldata=unreal.SubobjectDataBlueprintFunctionLibrary
for assembly in baseline['assemblies']:
    root = next(m for m in models.values() if m['ResourcePath']==replacements.get(assembly['class'],assembly['class']))
    for original in assembly['parts']:
        key=original['component'].removesuffix('_GEN_VARIABLE')
        part=parts.get((root['Id'],key))
        if not part or models[part['ChildModelId']]['ResourcePath']!=replacements.get(original['mesh']['path'],original['mesh']['path']):
            errors.append(f"Assembly {root['Id']}/{key}: component or exact source changed")
    bp=unreal.load_asset(root['ResourcePath'].rsplit('.',1)[0])
    current=[]
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        c=ldata.get_object(ldata.get_data(h))
        if isinstance(c,unreal.MeshComponent):
            info=component_info(c)
            if info and info['mesh']:current.append(info)
    def assembly_transform(info):return {k:v for k,v in info.items() if k not in ('mesh','overrides')}
    current_compare=[assembly_transform(c) for c in current] if root['ResourcePath'] in approved_paths else current
    original_compare=[assembly_transform(c) for c in assembly['parts']] if root['ResourcePath'] in approved_paths else assembly['parts']
    if normalized(sorted(current_compare,key=lambda c:c['component']))!=normalized(sorted(original_compare,key=lambda c:c['component'])):
        errors.append('Existing assembly transforms/attachments/material overrides changed: '+assembly['class'])
    else:parity.append({'assembly':assembly['class'],'components_transforms_sockets_materials':'unchanged'})
for ship in baseline['ships']:
    cdo=unreal.get_default_object(unreal.load_class(None,ship['class']))
    if normalized(component_info(cdo.get_editor_property('hull_mesh')))!=normalized(ship['hull']):errors.append('Existing ship hull changed: '+ship['class'])
    for original in ship['parts']:
        c=unreal.get_default_object(unreal.load_class(None,original['class']))
        mesh=c.get_editor_property('skeletal_mesh') or c.get_editor_property('static_mesh')
        if mesh_info(mesh)!=original['mesh']:errors.append('Existing ship part changed: '+original['class'])
    parity.append({'ship':ship['class'],'hull_and_part_source_snapshots':'compared'})
ground=unreal.get_default_object(unreal.load_class(None,'/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C'))
current=[component_info(c) for c in ground.get_components_by_class(unreal.MeshComponent)]
current=[c for c in current if c and c['mesh']]
if normalized(sorted(current,key=lambda c:c['component']))!=normalized(sorted(baseline['ground'],key=lambda c:c['component'])):errors.append('Existing Ground assembly changed')
else:parity.append({'ground':'BP_GroundMech_Light','components_transforms_sockets_materials':'unchanged'})

for region in rows('ColorRegions'):
    if region['Scope']=='Existing' and region['MaskSource'].startswith('/Game/'):
        mask=unreal.load_object(None,region['MaskSource'])
        if not isinstance(mask,unreal.Texture2D):errors.append('Color region texture mask missing/wrong type: '+region['Name'])
count=0
candidate_count=0
candidate_components={}
for binding in rows('MaterialParameters'):
    if binding['Driver']=='CPD':
        definition=models[binding['ModelId']]
        field='CandidateResourcePath' if binding['Scope']=='Candidate' else 'ResourcePath'
        if not definition[field]:continue
        targets=[p for p in parts.values() if p['ModelId']==binding['ModelId'] and (binding['PartKey']=='Root' or p['PartKey']==binding['PartKey'])]
        for p in targets:
            mid=p['ChildModelId'] or p['ModelId']
            d=models[mid];path=d[field]
            if not path:errors.append('Candidate part not bound: '+str((p['ModelId'],p['PartKey'])));continue
            if mid not in candidate_components:
                asset=unreal.load_object(None,path)
                expected={'StaticMesh':unreal.StaticMesh,'SkeletalMesh':unreal.SkeletalMesh}.get(d['ResourceType'])
                if expected is None or not asset or not isinstance(asset,expected):errors.append('Candidate source type differs: '+path);continue
                c=unreal.new_object(unreal.StaticMeshComponent if d['ResourceType']=='StaticMesh' else unreal.SkeletalMeshComponent)
                if isinstance(c,unreal.StaticMeshComponent):c.set_static_mesh(asset)
                else:c.set_skeletal_mesh_asset(asset)
                candidate_components[mid]=c
            c=candidate_components[mid]
            index=c.get_custom_primitive_data_index_for_vector_parameter(binding['ParameterName']) if binding['ParameterType']=='Vector' else c.get_custom_primitive_data_index_for_scalar_parameter(binding['ParameterName'])
            if index!=binding['CustomDataIndex']:errors.append('Candidate parameter missing/wrong type/CPD index: '+binding['Name']+'/'+p['PartKey'])
            else:candidate_count+=1
        continue
    part=parts.get((binding['ModelId'],binding['PartKey']))
    mid=part['ChildModelId'] or part['ModelId'] if part else binding['ModelId']
    material=slots.get(mid,{}).get(binding['MaterialSlotName'])
    if not material:
        errors.append(f"Parameter {binding['Name']}: invalid part/slot"); continue
    names=unreal.MaterialEditingLibrary.get_vector_parameter_names(material) if binding['ParameterType']=='Vector' else unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)
    if binding['ParameterName'] not in [str(n) for n in names]: errors.append(f"Parameter {binding['Name']}: missing or wrong parameter type")
    else: count+=1
build=json.loads((ROOT/'Data/Models/native-build.json').read_text(encoding='utf-8-sig'))
imports=json.loads((ROOT/'Data/Models/import-live-20261008.json').read_text(encoding='utf8'))
report = {'models_checked':checked,'existing_parameters_checked':count,'errors':errors,'candidate_deferred':deferred,
          'candidate_cpd_bindings_checked':candidate_count,
          'original_resource_parity':parity,
          'native_compile':build['result'],'new_datatables_import':'passed' if not imports['errors'] else 'failed','gameplay':'separate actual runtime evidence in Artifacts/ModelRegistryRuntime20261008',
          'geometry_rig_animation_lod':'existing resource references and assembly snapshots compared; geometry/rig/LOD binary parity not asserted; B source invariants separately documented'}
out=ROOT/'Data/Models/editor-static-validation.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
if globals().get('__name__')!='model_import_preflight':
    unreal.MCPythonHelper.submit_result(json.dumps({'models':len(checked),'parameters':count,'errors':errors[:30],'error_count':len(errors),'candidate_deferred':len(deferred)}))

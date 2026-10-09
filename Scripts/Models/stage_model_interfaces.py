"""After authorized native compile: stage local team paint in independent review assets.

No formal binding is switched. Exact triangle/LOD correspondence is required;
failures leave the original resource intact and are reported for review.
"""
import json
import re
import sys
from pathlib import Path
import unreal

ROOT = Path(r'D:/UE5.7/test1')
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
from Models.tone_style import GRAY, revise
OUT = ROOT/'ArtSource/ModelInterface_B_20261008'
DEST = '/Game/GuLiStrike/Review/ModelInterface'
lib = unreal.MaterialEditingLibrary
if not hasattr(unreal,'GuLiModelAuthoringLibrary'):
    raise RuntimeError('Native model authoring interface is not compiled. Obtain native compile authorization before staging meshes.')
contract=json.loads((OUT/'region-contract.json').read_text(encoding='utf-8'))
function=unreal.load_asset(DEST+'/Shared/MF_GuLiTeamRegion')
if not function: raise RuntimeError('Create the independent MF_GuLiTeamRegion first.')

def duplicate(asset, folder):
    target=folder+'/'+asset.get_name()
    copy=unreal.load_asset(target)
    if copy and unreal.EditorAssetLibrary.get_metadata_tag(copy,'GuLi.ModelApproved')=='1':
        raise RuntimeError('Approved paint candidate is frozen; use a new version directory: '+target)
    if not copy: copy=unreal.EditorAssetLibrary.duplicate_asset(asset.get_path_name().split('.')[0],target)
    if not copy: raise RuntimeError('Unable to duplicate '+asset.get_path_name())
    return copy

def body_material(source, folder):
    """Reuse WPO/VAT/rigid graph and original linework. Insert palette before its lighting bands."""
    base=source
    while isinstance(base,unreal.MaterialInstanceConstant): base=base.get_editor_property('parent')
    parent=duplicate(base,folder+'/Parents')
    if unreal.EditorAssetLibrary.get_metadata_tag(parent,'GuLi.ModelInterface')!='1':
        vertex=lib.create_material_expression(parent,unreal.MaterialExpressionVertexColor,-1500,-700)
        call=lib.create_material_expression(parent,unreal.MaterialExpressionMaterialFunctionCall,-1200,-700)
        if not call.set_material_function(function):
            raise RuntimeError('Shared palette function could not rebuild its call pins')
        if not lib.connect_material_expressions(vertex,'',call,'FixedPalette'):
            raise RuntimeError('Vertex color did not connect to the fixed palette')
        lib.connect_material_expressions(vertex,'A',call,'RoleAlpha')
        updated=0
        for expression in unreal.ObjectIterator(unreal.MaterialExpressionCustom):
            if expression.get_outer()!=parent: continue
            inputs=[str(i.get_editor_property('input_name')) for i in expression.get_editor_property('inputs')]
            if 'Color' in inputs:
                lib.connect_material_expressions(call,'PaletteBeforeThreeTone',expression,'Color');updated+=1
            elif 'Base' in inputs and 'N' in inputs and 'RG' not in inputs:
                lib.connect_material_expressions(call,'PaletteBeforeThreeTone',expression,'Base');updated+=1
            elif 'Palette' in inputs:
                code=expression.get_editor_property('code')
                code,count=re.subn(r'float3 s=round\(saturate\(Palette\.rgb\)\*255\)/255;\s*float3 c=.*?;', 'float3 c=Palette.rgb;', code, count=1)
                if count!=1: raise RuntimeError('Palette shader differs from the verified sRGB contract; preserve the parent and inspect it')
                expression.set_editor_property('code',code)
                lib.connect_material_expressions(call,'PaletteBeforeThreeTone',expression,'Palette');updated+=1
            elif 'RG' in inputs and 'BT' in inputs and 'Team' in inputs:
                if 'palette=lerp(palette,Team,teamMask);' not in expression.get_editor_property('code'):
                    raise RuntimeError('SSF shader team-mask stage is not the verified contract')
                value=unreal.CustomInput();value.set_editor_property('input_name','GuLiPalette')
                expression.set_editor_property('inputs',list(expression.get_editor_property('inputs'))+[value])
                expression.set_editor_property('code',expression.get_editor_property('code').replace('palette=lerp(palette,Team,teamMask);','palette=GuLiPalette;'))
                lib.connect_material_expressions(call,'PaletteBeforeThreeTone',expression,'GuLiPalette');updated+=1
        if not updated:
            # Static opaque legacy/PBR bodies receive the same three-tone style; geometry/UV/slots stay intact.
            normal=lib.create_material_expression(parent,unreal.MaterialExpressionPixelNormalWS,-1200,-300)
            toon=lib.create_material_expression(parent,unreal.MaterialExpressionCustom,-500,-500)
            toon.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            ci=[]
            for name in ['Palette','N','Lamp']:
                value=unreal.CustomInput();value.set_editor_property('input_name',name);ci.append(value)
            toon.set_editor_property('inputs',ci)
            toon.set_editor_property('code','float d=dot(normalize(N),normalize(float3(.35,.55,.76)));float band=d<.38?.40:(d<.68?.72:1.0);float edge=saturate((max(length(ddx(normalize(N))),length(ddy(normalize(N))))-.25)*4);return lerp(Palette*band,float3(.025,.030,.028),edge);')
            lib.connect_material_expressions(call,'PaletteBeforeThreeTone',toon,'Palette')
            lib.connect_material_expressions(normal,'',toon,'N')
            lib.connect_material_expressions(call,'TeamLampStrength',toon,'Lamp')
            parent.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
            lib.connect_material_property(toon,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        # Emission is added after the existing tone/line stage and only on role 7.
        current=lib.get_material_property_input_node(parent,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        output=lib.get_material_property_input_node_output_name(parent,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if not current: raise RuntimeError('Opaque body must have a verified emissive three-tone output')
        light=lib.create_material_expression(parent,unreal.MaterialExpressionMultiply,0,500)
        combined=lib.create_material_expression(parent,unreal.MaterialExpressionAdd,300,0)
        lib.connect_material_expressions(call,'PaletteBeforeThreeTone',light,'A')
        lib.connect_material_expressions(call,'TeamLampStrength',light,'B')
        lib.connect_material_expressions(current,output,combined,'A')
        lib.connect_material_expressions(light,'',combined,'B')
        lib.connect_material_property(combined,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        for expression in unreal.ObjectIterator(unreal.MaterialExpressionCustom):
            if expression.get_outer()==parent:expression.set_editor_property('code',revise(expression.get_editor_property('code')))
        for expression in unreal.ObjectIterator(unreal.MaterialExpressionVectorParameter):
            if expression.get_outer()==parent and str(expression.get_editor_property('parameter_name')) in ('InkColor','Ink Color'):
                expression.set_editor_property('default_value',unreal.LinearColor(*GRAY))
        unreal.EditorAssetLibrary.set_metadata_tag(parent,'GuLi.ModelInterface','1')
        lib.recompile_material(parent)
        unreal.EditorAssetLibrary.save_loaded_asset(parent)
    if isinstance(source,unreal.MaterialInstanceConstant):
        instance=duplicate(source,folder+'/Instances')
        lib.set_material_instance_parent(instance,parent);unreal.EditorAssetLibrary.save_loaded_asset(instance)
        return instance
    return parent

def stage_mesh(source, meshes, folder):
    copy=unreal.load_asset(folder+'/Meshes/'+source.get_name())
    if not copy:raise RuntimeError('Prepare independent display-preserving paint copies first')
    entries=json.loads((OUT/'paint-geometry-index.json').read_text(encoding='utf8'))
    entry=next(e for e in entries if e['candidate']==copy.get_path_name())
    invariant=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(source))
    base=entry['base_snapshot']
    if not all('triangle_attributes_sha1' in l for l in base['lods']):
        base=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(copy))
        entry['base_snapshot']=base
        (OUT/'paint-geometry-index.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf8')
    resolved=json.loads((OUT/'surface-paint-correspondence.json').read_text(encoding='utf8'))
    lods=[]
    for f in entry['files']:
        lod=f['lod']
        if entry['part']=='AccessRamp':
            payload=json.loads((OUT/meshes[0]['mask']).read_text(encoding='utf8'))
            paints={(t[9],*t[10:13]) for t in payload['triangles']}
            if len(paints)!=1 or next(iter(paints))[0]!=1:raise RuntimeError('Ramp must stay uniformly fixed sand')
            role,r,g,b=next(iter(paints))
            # Use the indexed writer so registering default attributes cannot
            # reset the engine cube's existing UV-channel count.
            geometry=json.loads((OUT/f['file']).read_text(encoding='utf8'))
            for tri in geometry['triangles']:tri[9:13]=[role,r,g,b]
            uniform=OUT/'ResolvedMasks/ResourceFactory__AccessRamp__uniform.json'
            uniform.write_text(json.dumps(geometry,separators=(',',':')),encoding='utf8')
            result=json.loads(unreal.GuLiModelAuthoringLibrary.encode_indexed_mesh_paint(copy,lod,str(uniform)))
        else:
            paint=next(r for r in resolved if r['model']==entry['model'] and r['part']==entry['part'] and r['lod']==lod)
            # The factory's original reduced display surfaces differ from its dense
            # B source. Paint is transferred within the existing part; the original
            # display geometry/UV/weights is independently verified below.
            if paint['surface_errors'] and entry['model']!='ResourceFactory' and not entry.get('nanite_source_preserved'):raise RuntimeError('Unresolved B surface correspondence: '+str(paint['surface_errors']))
            if entry['model']=='ResourceFactory' and paint['max_surface_distance']>.005:raise RuntimeError('Factory region transfer is outside its existing reduction tolerance')
            result=json.loads(unreal.GuLiModelAuthoringLibrary.encode_indexed_mesh_paint(copy,lod,str(OUT/paint['file'])))
        if not result['success']:raise RuntimeError(result['error'])
        lods.append(lod)
    after=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(copy))
    cpu_match=all(a.get('triangle_attributes_sha1')==b.get('triangle_attributes_sha1') and a['vertices']==b['vertices'] and a['triangles']==b['triangles'] and
        {k:v for k,v in a['attributes'].items() if k.startswith('vertex/')}=={k:v for k,v in b['attributes'].items() if k.startswith('vertex/')}
        for a,b in zip(after['lods'],base['lods']))
    if not cpu_match or after.get('reference_bones')!=base.get('reference_bones'):
        raise RuntimeError('Source positions, UVs, normals, weights or reference bones changed during paint encoding')
    expected=[x['triangle_geometry_uv_skin_sha1'] for x in invariant['render_lods']]
    actual=[x['triangle_geometry_uv_skin_sha1'] for x in after['render_lods']]
    # Source triangle/corner attributes are exact. GPU builders may reorder or
    # repack seams; Nanite additionally regenerates its non-Nanite fallback.
    # Record that separately rather than claiming buffer byte equality.
    gpu_repacked=expected!=actual
    skeletal=isinstance(copy,unreal.SkeletalMesh)
    slots=copy.get_editor_property('materials' if skeletal else 'static_materials')
    changed=[]
    for index,slot in enumerate(slots):
        material=slot.material_interface
        if not material:continue
        name=str(slot.material_slot_name).lower()
        parent=material
        while isinstance(parent,unreal.MaterialInstanceConstant):parent=parent.get_editor_property('parent')
        if parent.get_editor_property('blend_mode')!=unreal.BlendMode.BLEND_OPAQUE or any(t in name for t in ['outline','contour','glass','display']):continue
        slot.material_interface=body_material(material,folder+'/Materials');slots[index]=slot;changed.append(str(slot.material_slot_name))
    if not changed:raise RuntimeError('No body material staged')
    copy.set_editor_property('materials' if skeletal else 'static_materials',slots)
    final=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(copy))
    if [x['triangle_geometry_uv_skin_sha1'] for x in final['render_lods']]!=actual:raise RuntimeError('Material assignment changed display LOD data')
    if isinstance(copy,unreal.StaticMesh):
        bounds_result=json.loads(unreal.GuLiModelAuthoringLibrary.refresh_paint_display_bounds(copy))
        if not bounds_result['success']:raise RuntimeError(bounds_result['error'])
    unreal.EditorAssetLibrary.set_metadata_tag(copy,'GuLi.NonColorMeshInvariant',json.dumps(invariant,sort_keys=True))
    unreal.EditorAssetLibrary.set_metadata_tag(copy,'GuLi.PaintValidation',json.dumps(dict(source_triangle_attributes_exact=True,geometric_vertices_exact=True,rig_exact=True,gpu_repacked=gpu_repacked,nanite_source_preserved=entry.get('nanite_source_preserved',False))))
    unreal.EditorAssetLibrary.set_metadata_tag(copy,'GuLi.ModelInterface','B_v1; color-only; alpha role/255; original display surfaces/UV/rig/LOD')
    assert unreal.EditorAssetLibrary.save_loaded_asset(copy)
    return copy,lods,changed


def stage_assembly(definition, model):
    source=unreal.load_object(None,definition['ResourcePath'])
    original_bp=unreal.load_asset(source.get_path_name().removesuffix('_C').split('.')[0])
    bp=duplicate(original_bp,DEST+'/'+model['name']+'/Blueprints')
    parts=[p for p in model_catalog.rows('Parts') if p['ModelId']==definition['Id']]
    sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    libdata=unreal.SubobjectDataBlueprintFunctionLibrary
    components={}
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        c=libdata.get_object(libdata.get_data(h))
        if isinstance(c,unreal.MeshComponent) and c.get_path_name().startswith(bp.get_path_name()):
            components[c.get_name().removesuffix('_GEN_VARIABLE')]=c
    pending=[]
    for part in parts:
        key=part['PartKey']
        # B factory source has exactly three registered components, with no inferred reassembly.
        name={'Body':'SM_RPF_Body','Door':'SK_RPF_Door','AccessRamp':'AccessRamp'}[key]
        masks=[m for m in model['meshes'] if m['object'].endswith('_'+name)]
        if len(masks)!=1: raise RuntimeError('Missing exact B paint component '+key)
        child=model_catalog.definition(part['ChildModelId'])
        resource=unreal.load_object(None,child['ResourcePath'])
        candidate,lods,slots=stage_mesh(resource,masks,DEST+'/'+model['name']+'/'+key)
        component=components.get(part['ComponentPath'])
        if not component: raise RuntimeError('Missing original component '+part['ComponentPath'])
        before=component.get_relative_transform()
        if isinstance(component,unreal.SkeletalMeshComponent): component.set_skeletal_mesh_asset(candidate)
        else: component.set_static_mesh(candidate)
        # Preserve original overrides as independent derived materials; otherwise the
        # factory's ramp override would hide the new mesh's material interface.
        for slot,override in enumerate(component.get_editor_property('override_materials')):
            if not override:continue
            base=override
            while isinstance(base,unreal.MaterialInstanceConstant):base=base.get_editor_property('parent')
            if base.get_editor_property('blend_mode')==unreal.BlendMode.BLEND_OPAQUE:
                component.set_material(slot,body_material(override,DEST+'/'+model['name']+'/'+key+'/OverrideMaterials'))
        assert component.get_relative_transform()==before
        pending.append(dict(id=child['Id'],candidate=candidate.get_path_name(),source=child['ResourcePath'],part=key,lods=lods,slots=slots))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp) # Blueprint only; the native precondition is checked above.
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp)
    return bp.generated_class().get_path_name(),pending


selected=globals().get('GULI_STAGE_MODELS')
report=json.loads((OUT/'engine-candidate-staging.json').read_text(encoding='utf8')) if selected else {'staged':[],'errors':[],'formal_bindings_changed':False,'approval':'explicit current B_v1 formal import instruction recorded','native_compile':'caller prerequisite'}
if selected:
    report['staged']=[m for m in report['staged'] if m['model'] not in selected]
    report['errors']=[m for m in report['errors'] if m['model'] not in selected]
derived=dict(next(m for m in contract['models'] if m['name']=='BiZhiMao'))
derived['name']='BiZhiMaoConstruction' # Same approved design contract, exact construction LOD geometry is still required.
for model in contract['models']+[derived]:
    if selected and model['name'] not in selected:continue
    definition=model_catalog.definition(model_catalog.model_id(model['name']))
    try:
        if definition['ResourceType']=='PresentationClass':
            candidate,children=stage_assembly(definition,model)
            pending=children+[dict(id=definition['Id'],candidate=candidate,source=definition['ResourcePath'],part='Assembly')]
        else:
            source=unreal.load_object(None,definition['ResourcePath'])
            copy,lods,slots=stage_mesh(source,model['meshes'],DEST+'/'+model['name'])
            pending=[dict(id=definition['Id'],candidate=copy.get_path_name(),source=definition['ResourcePath'],part='Root',lods=lods,slots=slots)]
        # VAT assets remain identical because geometry/UV/vertex animation order is preserved.
        for item in pending:
            d=model_catalog.definition(item['id'])
            model_catalog.write_binding_cells(ROOT/'Data/Excel/GuLiStrikeModels.xlsx',item['id'],item['candidate'],d['VATDefinition'],candidate=True)
        report['staged'].append(dict(model=model['name'],id=definition['Id'],bindings=pending,formal_unchanged=True))
    except Exception as exc:
        report['errors'].append(dict(model=model['name'],reason=str(exc)))
report['next']='Export Excel, verify technical/visual readback, then promote the explicitly authorized B_v1 and retire replaced UE resources'
(OUT/'engine-candidate-staging.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

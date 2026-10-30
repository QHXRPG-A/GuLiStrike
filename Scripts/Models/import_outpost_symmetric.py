"""Owned, legacy FBX import worker. Writes only the new OutpostSymmetric root."""
import hashlib
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
sys.path.insert(0, str(ROOT / 'Scripts'))
from Models.outpost_surface_style import surface_code
OUT = ROOT / 'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
DEST = '/Game/GuLiStrike/Buildings/OutpostSymmetric'
OWNER = 'GuLi.OutpostSymmetric.B_v1.20261029'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDIT = unreal.MaterialEditingLibrary

def status(stage, **kw):
    (OUT / 'import-status.json').write_text(json.dumps({'stage': stage, **kw}, ensure_ascii=False, indent=2), encoding='utf8')

def save(asset):
    assert asset.get_path_name().startswith(DEST + '/')
    LIB.set_metadata_tag(asset, 'GuLi.ModelProduction.Owner', OWNER)
    LIB.set_metadata_tag(asset, 'GuLi.ModelApproved', '1')
    LIB.set_metadata_tag(asset, 'GuLi.ModelProduction.SourceSHA256', json.loads((OUT/'blender-export.json').read_text(encoding='utf8'))['source_sha256'])
    if not LIB.save_loaded_asset(asset, False):
        raise RuntimeError('Save failed ' + asset.get_path_name())

def imported(filename, folder, name, options=None, factory=None):
    existing = unreal.load_asset(folder + '/' + name)
    if existing and LIB.get_metadata_tag(existing, 'GuLi.ModelProduction.Owner') != OWNER:
        raise RuntimeError('Unowned destination: ' + existing.get_path_name())
    LIB.make_directory(folder)
    task = unreal.AssetImportTask()
    for k,v in dict(filename=str(filename), destination_path=folder, destination_name=name,
                    automated=True, async_=False, replace_existing=bool(existing), save=False).items():
        task.set_editor_property(k,v)
    if options: task.set_editor_property('options', options)
    if factory: task.set_editor_property('factory', factory)
    TOOLS.import_asset_tasks([task])
    asset = unreal.load_asset(folder + '/' + name)
    if not asset: raise RuntimeError('Missing imported asset ' + name)
    save(asset)
    return asset

def material(name):
    path = DEST + '/Materials/' + name
    value = unreal.load_asset(path)
    if value and LIB.get_metadata_tag(value, 'GuLi.ModelProduction.Owner') != OWNER:
        raise RuntimeError('Unowned material ' + path)
    if not value:
        LIB.make_directory(DEST+'/Materials')
        value = TOOLS.create_asset(name,DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    while EDIT.get_num_material_expressions(value):
        EDIT.delete_all_material_expressions(value)
    value.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    return value

def node(mat, cls, x=0, y=0):
    return EDIT.create_material_expression(mat, cls, x, y)

def connect(source, output, dest, input_name):
    if not EDIT.connect_material_expressions(source, output, dest, input_name):
        raise RuntimeError('Invalid material connection ' + input_name)

def custom(mat, code, inputs, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    result=node(mat,unreal.MaterialExpressionCustom,0,0)
    pins=[]
    for name,_,_ in inputs:
        pin=unreal.CustomInput(); pin.set_editor_property('input_name',name); pins.append(pin)
    result.set_editor_property('inputs',pins)
    result.set_editor_property('code',code)
    result.set_editor_property('output_type',output)
    for name,source,out in inputs: connect(source,out,result,name)
    return result

def linear(code):
    vals=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in vals)

def install():
    source=json.loads((OUT/'blender-export.json').read_text(encoding='utf8'))
    approval=json.loads((OUT/'approval_B.json').read_text(encoding='utf8'))
    assert approval['approved'] and approval['source_sha256']==source['source_sha256']
    approved_source=Path(source.get('approved_source_snapshot',source['source']))
    assert hashlib.sha256(approved_source.read_bytes()).hexdigest()==source['source_sha256']
    assert hashlib.sha256((OUT/'SM_OutpostSymmetric.fbx').read_bytes()).hexdigest()==source['fbx_sha256']
    status('importing-fbx')
    options=unreal.FbxImportUI()
    for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=False,
        import_animations=False,automated_import_should_detect_type=False,
        mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH,
        original_import_type=unreal.FBXImportType.FBXIT_STATIC_MESH).items(): options.set_editor_property(k,v)
    data=options.get_editor_property('static_mesh_import_data')
    for k,v in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,
        combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=False,build_nanite=False,
        transform_vertex_to_absolute=True,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
        vertex_color_import_option=unreal.VertexColorImportOption.REPLACE).items():data.set_editor_property(k,v)
    mesh=imported(OUT/'SM_OutpostSymmetric.fbx',DEST+'/Meshes','SM_OutpostSymmetric',options,unreal.FbxFactory())
    status('building-materials')
    tex=imported(OUT/'T_OutpostSymmetric_LineMask.png',DEST+'/Textures','T_OutpostSymmetric_LineMask')
    tex.set_editor_property('srgb',False)
    tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    save(tex)
    mat=material('M_OutpostSymmetric_Toon')
    uv=node(mat,unreal.MaterialExpressionTextureCoordinate,-800,0);uv.set_editor_property('coordinate_index',1)
    normal=node(mat,unreal.MaterialExpressionPixelNormalWS,-800,150)
    local_position=node(mat,unreal.MaterialExpressionPreSkinnedPosition,-800,225)
    local_interpolator=node(mat,unreal.MaterialExpressionVertexInterpolator,-600,225)
    connect(local_position,'',local_interpolator,EDIT.get_material_expression_input_names(local_interpolator)[0])
    line=node(mat,unreal.MaterialExpressionTextureSample,-800,300)
    line.set_editor_property('texture',tex);line.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    bindings=[]
    for name,index,is_vector,value in [('GuLi_TeamPrimary',8,True,linear('#6AA4BE')),
        ('GuLi_TeamEnabled',16,False,1),('GuLi_TeamLightStrength',17,False,.35),('GuLi_CoreUseTeamColor',18,False,0)]:
        p=node(mat,unreal.MaterialExpressionVectorParameter if is_vector else unreal.MaterialExpressionScalarParameter,-800,500+index*20)
        p.set_editor_property('parameter_name',name);p.set_editor_property('use_custom_primitive_data',True)
        p.set_editor_property('primitive_data_index',index)
        p.set_editor_property('default_value',unreal.LinearColor(*value,1) if is_vector else value)
        bindings.append(p)
    code=surface_code()
    shader=custom(mat,code,[('Class',uv,''),('Normal',normal,''),('LocalPosition',local_interpolator,''),('Line',line,'R'),
        ('Primary',bindings[0],''),('Enabled',bindings[1],''),('Strength',bindings[2],''),('Owned',bindings[3],'')])
    assert EDIT.connect_material_property(shader,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat);save(mat)
    outline=material('M_OutpostSymmetric_Outline')
    outline.set_editor_property('two_sided',True)
    outline.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    sign=node(outline,unreal.MaterialExpressionTwoSidedSign)
    mask=custom(outline,'return saturate(-Side);',[('Side',sign,'')],unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    assert EDIT.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    vn=node(outline,unreal.MaterialExpressionVertexNormalWS)
    width=node(outline,unreal.MaterialExpressionScalarParameter);width.set_editor_property('parameter_name','OutlineWidthCm');width.set_editor_property('default_value',5.)
    offset=node(outline,unreal.MaterialExpressionMultiply);connect(vn,'',offset,'A');connect(width,'',offset,'B')
    assert EDIT.connect_material_property(offset,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    ink=node(outline,unreal.MaterialExpressionConstant3Vector);ink.set_editor_property('constant',unreal.LinearColor(*linear('#1A182F'),1))
    assert EDIT.connect_material_property(ink,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(outline);save(outline)
    mesh.set_material(0,mat)
    # Visibility traces hit the floating model; physical/navigation collision stays at the existing ground anchor.
    body=mesh.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    save(mesh)
    b=mesh.get_bounds();size=[v*2 for v in b.box_extent.to_tuple()]
    expected=json.loads((OUT/'blender-export.json').read_text(encoding='utf8'))['size_m']
    if max(abs(a-b*100) for a,b in zip(size,expected))>.01:raise RuntimeError('Imported axes/units mismatch '+str(size))
    result={'success':True,'mesh':mesh.get_path_name(),'size_cm':size,'bounds_origin_cm':list(b.origin.to_tuple()),
        'material':mat.get_path_name(),'outline':outline.get_path_name(),'texture':tex.get_path_name(),
        'material_slots':len(mesh.static_materials),'triangles':json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))['render_lods'][0]['triangles'],
        'render_vertices':unreal.EditorStaticMeshLibrary.get_number_verts(mesh,0),
        'cpd':{'primary':8,'enabled':16,'light_strength':17,'core_use_team_color':18},
        'outline_draw_pass':'backface overlay, no additional mesh asset', 'collision':'complex query only; ground gameplay anchor unchanged'}
    (OUT/'ue-asset-import.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    status('complete',success=True)

if __name__=='__main__':
    if '-OutpostSymmetricImportWorker' not in unreal.SystemLibrary.get_command_line():
        raise RuntimeError('Run in the isolated owned-root import worker, never a live MCP callback.')
    previous=unreal.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.Enable')
    try:
        unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.Enable 0')
        install()
    except Exception:
        status('failed',success=False,traceback=traceback.format_exc())
        unreal.log_error(traceback.format_exc())
    finally:
        unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.Enable '+str(previous))
        unreal.SystemLibrary.quit_editor()

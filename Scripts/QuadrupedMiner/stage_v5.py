"""Scoped v5 import in the live editor. MODE=materials can run before native reload."""
import unreal
import json
import hashlib
from pathlib import Path

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ART = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
BASE = '/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_10k_v5'
OWNER = 'GuLi.QuadrupedMiner.10k.v5'
META = json.loads((ART / 'metadata.json').read_text(encoding='utf8'))
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
REPORT = dict(success=False, source_sha256=META['source_sha256'], assets=[], mode=globals().get('MODE', 'all'))


def own(path):
    asset = unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not asset or LIB.get_metadata_tag(asset, 'GuLi.Owner') == OWNER, ('Unowned asset', path)
    return asset


def save(asset):
    assert asset.get_path_name().startswith(BASE + '/')
    LIB.set_metadata_tag(asset, 'GuLi.Owner', OWNER)
    LIB.set_metadata_tag(asset, 'GuLi.SourceSHA256', META['source_sha256'])
    assert LIB.save_loaded_asset(asset, False), asset.get_path_name()
    REPORT['assets'].append(asset.get_path_name())
    return asset


def create(path, cls, factory):
    asset = own(path)
    if not asset:
        folder, name = path.rsplit('/', 1)
        LIB.make_directory(folder)
        asset = TOOLS.create_asset(name, folder, cls, factory)
        assert asset, path
        LIB.set_metadata_tag(asset, 'GuLi.Owner', OWNER)
    return asset


def node(owner, cls, **properties):
    obj = EDIT.create_material_expression_in_function(owner, cls) if isinstance(owner, unreal.MaterialFunction) else EDIT.create_material_expression(owner, cls)
    for name, value in properties.items():
        obj.set_editor_property(name, value)
    return obj


def link(source, target, pin='', output=''):
    assert EDIT.connect_material_expressions(source, output, target, pin), (target.get_name(), pin, output)


def constant(owner, value):
    return node(owner, unreal.MaterialExpressionConstant, r=float(value))


def custom(owner, code, inputs, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, outputs=None):
    obj = node(owner, unreal.MaterialExpressionCustom, code=code, output_type=output_type)
    names = []
    for name in inputs:
        pin = unreal.CustomInput(); pin.set_editor_property('input_name', name); names.append(pin)
    obj.set_editor_property('inputs', names)
    if outputs:
        extra = []
        for name, typ in outputs:
            pin = unreal.CustomOutput(); pin.set_editor_property('output_name', name); pin.set_editor_property('output_type', typ); extra.append(pin)
        obj.set_editor_property('additional_outputs', extra)
    for name, (source, output) in inputs.items():
        link(source, obj, name, output)
    return obj


textures = []
for lod in META['lods']:
    pair = []
    for role, extension in [('Position', 'exr'), ('Normal', 'png')]:
        path = BASE + f'/Textures/T_{role}_LOD{lod["lod"]}'
        tex = own(path)
        source = ART / f'Textures/{role}_LOD{lod["lod"]}.{extension}'
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if not tex or LIB.get_metadata_tag(tex, 'GuLi.TextureSHA256') != digest:
            task = unreal.AssetImportTask()
            for k, v in dict(filename=str(source), destination_path=path.rsplit('/', 1)[0], destination_name=path.rsplit('/', 1)[1],
                             automated=True, replace_existing=bool(tex), save=False, factory=unreal.TextureFactory()).items():
                task.set_editor_property(k, v)
            TOOLS.import_asset_tasks([task]); tex = unreal.load_asset(path)
            assert tex, path
            LIB.set_metadata_tag(tex, 'GuLi.TextureSHA256', digest)
        for k, v in dict(srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_HDR if role == 'Position' else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
                         compression_no_alpha=True, filter=unreal.TextureFilter.TF_NEAREST, mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                         address_x=unreal.TextureAddress.TA_CLAMP, address_y=unreal.TextureAddress.TA_CLAMP, never_stream=True).items():
            tex.set_editor_property(k, v)
        pair.append(save(tex))
    textures.append(pair)

function = create(BASE + '/Materials/MF_QuadrupedMiner_VAT', unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
while EDIT.get_num_material_expressions_in_function(function):
    EDIT.delete_all_material_expressions_in_function(function)
inputs = {'Rest': node(function, unreal.MaterialExpressionPreSkinnedPosition),
          'PUV': node(function, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0),
          'NUV': node(function, unreal.MaterialExpressionTextureCoordinate, coordinate_index=2)}
for name, offset in [('FrameA', 0), ('FrameB', 1), ('Blend', 2)]:
    current = node(function, unreal.MaterialExpressionScalarParameter, parameter_name='GuLi_Miner' + name, use_custom_primitive_data=True, primitive_data_index=19 + offset, default_value=0.)
    previous = node(function, unreal.MaterialExpressionScalarParameter, parameter_name='GuLi_MinerPrevious' + name, use_custom_primitive_data=True, primitive_data_index=22 + offset, default_value=0.)
    switch = node(function, unreal.MaterialExpressionPreviousFrameSwitch)
    link(current, switch, 'Current Frame'); link(previous, switch, 'Previous Frame'); inputs[name] = switch
for i, lod in enumerate(META['lods']):
    for role, short, index in [('Position', 'P', 0), ('Normal', 'N', 1)]:
        inputs[role + str(i)] = node(function, unreal.MaterialExpressionTextureObject, texture=textures[i][index], sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        inputs[short + 'Rows' + str(i)] = constant(function, lod[role.lower()]['rows'])
        inputs[short + 'Height' + str(i)] = constant(function, lod[role.lower()]['height'])
vat = custom(function, (ROOT / 'Scripts/QuadrupedMiner/QuadrupedMinerV5.hlsl').read_text(),
             {k: (v, '') for k, v in inputs.items()}, outputs=[('AnimatedNormal', unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
for name, output in [('Offset', ''), ('Normal', 'AnimatedNormal')]:
    n = node(function, unreal.MaterialExpressionFunctionOutput, output_name=name)
    link(vat, n, output=output)
EDIT.update_material_function(function); save(function)

team_function = unreal.load_asset('/Game/GuLiStrike/Models/TeamColor_v1/Shared/MF_GuLiTeamRegion_v3')
assert team_function
materials = {}
for kind in ['Body', 'Outline', 'Lamp', 'Hit', 'Wreck']:
    material = create(BASE + '/Materials/M_QuadrupedMiner_' + kind, unreal.Material, unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(material)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)
    material.set_editor_property('max_world_position_offset_displacement', 1000.)
    if kind == 'Hit': material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    call = node(material, unreal.MaterialExpressionMaterialFunctionCall)
    assert call.set_material_function(function)
    offset = node(material, unreal.MaterialExpressionTransform, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                  transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(call, offset, output='Offset')
    assert EDIT.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    world_normal = node(material, unreal.MaterialExpressionTransform, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(call, world_normal, output='Normal')
    normal = node(material, unreal.MaterialExpressionVertexInterpolator); link(world_normal, normal)
    vertex = node(material, unreal.MaterialExpressionVertexColor)
    fixed = custom(material, 'float3 s=round(saturate(SRGB)*255)/255; return lerp(s/12.92,pow((s+0.055)/1.055,2.4),step(0.04045,s));', {'SRGB': (vertex, '')})
    uv1 = node(material, unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
    uv2 = node(material, unreal.MaterialExpressionTextureCoordinate, coordinate_index=2)
    shade_inputs = {'Color': (fixed, ''), 'N': (normal, ''), 'D1': (uv1, ''), 'D2': (uv2, '')}
    ink = 'float3(0.01032982,0.00907268,0.02732089)'
    if kind == 'Outline': code = 'return ' + ink + ';'
    elif kind == 'Lamp':
        team = node(material, unreal.MaterialExpressionMaterialFunctionCall); assert team.set_material_function(team_function)
        link(fixed, team, 'FixedPalette'); link(vertex, team, 'RoleAlpha', 'A')
        shade_inputs['Team'] = (team, 'PaletteBeforeThreeTone'); shade_inputs['Strength'] = (team, 'TeamLampStrength')
        code = 'return Team * (Strength>0 ? Strength : 0.58);'
    elif kind == 'Hit':
        shade_inputs['Time'] = (node(material, unreal.MaterialExpressionTime), '')
        shade_inputs['Start'] = (node(material, unreal.MaterialExpressionScalarParameter, parameter_name='GuLi_HitTime', use_custom_primitive_data=True, primitive_data_index=7, default_value=-1000.), '')
        code = 'return float3(0.5,0.8,1.0)*saturate(1.0-max(0,Time-Start)/0.18)*4;'
        assert EDIT.connect_material_property(constant(material, 1), '', unreal.MaterialProperty.MP_OPACITY)
    else:
        code = 'float d=dot(normalize(N),normalize(float3(.35,-.55,.76)));float band=d<.32?.58:(d<.68?.80:1.0);'
        if kind == 'Wreck': code += 'Color=dot(Color,float3(1,1,1))<0.001?float3(0,0,0):float3(.025,.038,.035);'
        code += 'float edge=min(min(D1.x,D1.y),D2.x);float inkWeight=1-smoothstep(.45,.65,edge); return lerp(Color*band,' + ink + ',inkWeight*.45);'
    shade = custom(material, code, shade_inputs)
    assert EDIT.connect_material_property(shade, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(material); materials[kind] = save(material)

if REPORT['mode'] != 'materials':
    assert hasattr(unreal, 'GuLiQuadrupedMinerVATData'), 'Compile/reload the approved native code before formal wiring'
    mesh = create(BASE + '/Meshes/SM_QuadrupedMiner_VAT', unreal.StaticMesh, None)
    slots = []
    for k in META['lods'][0]['materials']:
        slot = unreal.StaticMaterial(material_interface=materials[k], material_slot_name=k)
        slot.import_text('(ImportedMaterialSlotName="' + k + '")')
        assert str(slot.get_editor_property('imported_material_slot_name')) == k
        slots.append(slot)
    mesh.set_editor_property('static_materials', slots)
    result = unreal.GuLiQuadrupedMinerVATData.build_approved_mesh(mesh, str(ART))
    assert result == 'OK', result
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    sub.set_lod_screen_sizes(mesh, META['screen_sizes'])
    rest = json.loads((ART / 'lod0.json').read_text())['positions']
    low = [min(l['bounds_min'][i] for l in META['lods']) for i in range(3)]
    high = [max(l['bounds_max'][i] for l in META['lods']) for i in range(3)]
    # These properties are extensions beyond rest bounds, not absolute extents.
    mesh.set_editor_property('negative_bounds_extension', unreal.Vector(*[max(0, min(p[i] for p in rest) - low[i]) + 2 for i in range(3)]))
    mesh.set_editor_property('positive_bounds_extension', unreal.Vector(*[max(0, high[i] - max(p[i] for p in rest)) + 2 for i in range(3)]))
    save(mesh)
    factory = unreal.DataAssetFactory(); factory.set_editor_property('data_asset_class', unreal.GuLiQuadrupedMinerVATData)
    data = create(BASE + '/Animations/DA_QuadrupedMiner_VAT', unreal.GuLiQuadrupedMinerVATData, factory)
    lods = []
    for i, row in enumerate(META['lods']):
        lod = unreal.GuLiMinerVATLOD()
        for k, v in dict(position=textures[i][0], normal=textures[i][1], vertex_count=row['vertices'], normal_count=row['normal_count'],
                         position_rows=row['position']['rows'], normal_rows=row['normal']['rows']).items(): lod.set_editor_property(k, v)
        lods.append(lod)
    for k, v in dict(source_sha256=META['source_sha256'], LODs=lods,
                     walk_cycle_distance_cm=META['walk_cycle_distance_cm'], reference_muzzle=unreal.Vector(*META['reference_muzzle_cm']),
                     muzzle_samples=[unreal.Vector(*v) for v in META['muzzle_samples_cm']], render_bounds=unreal.Box(min=unreal.Vector(*low), max=unreal.Vector(*high)),
                     hit_material=materials['Hit'], wreck_material=materials['Wreck']).items(): data.set_editor_property(k, v)
    assert data.is_valid_data(); save(data)
    bp_factory = unreal.BlueprintFactory(); bp_factory.set_editor_property('parent_class', unreal.GuLiQuadrupedMinerPresentation)
    bp = create(BASE + '/BP_QuadrupedMiner', unreal.Blueprint, bp_factory)
    cdo = unreal.get_default_object(bp.generated_class())
    body = cdo.get_editor_property('body_vat')
    body.set_static_mesh(mesh); body.set_editor_property('animation', data)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp); save(bp)
    REPORT['lods'] = [{'lod': i, 'triangles': mesh.get_num_triangles(i), 'sections': mesh.get_num_sections(i)} for i in range(3)]
    assert [x['triangles'] for x in REPORT['lods']] == [x['triangles'] for x in META['lods']]
    REPORT['blueprint'] = bp.generated_class().get_path_name()
REPORT['success'] = True
(ART / ('ue_' + REPORT['mode'] + '.json')).write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(REPORT, ensure_ascii=False))

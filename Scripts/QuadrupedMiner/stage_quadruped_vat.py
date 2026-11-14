"""Import the baked quadruped VAT into its owned formal asset directory.
Runs in the open UE editor. No play session, rendering, or visual inspection.
"""
import unreal
import json
import hashlib
from pathlib import Path

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ART = ROOT / 'ArtSource' / 'QuadrupedMiner_20261113'
META = json.loads((ART / 'Reports' / 'vat_metadata.json').read_text(encoding='utf8'))
CONTRACT = json.loads((ART / 'runtime_contract.json').read_text(encoding='utf8'))
assert META['offline_bake_completed'] and len(CONTRACT['mining_socket_samples']) == 50
BASE = '/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_20261113'
OWNER = 'GuLiStrike.QuadrupedMiner.20261113'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDIT = unreal.MaterialEditingLibrary
REPORT = {'success': False, 'assets': [], 'formal_reference_changed': False}

def save(asset):
    assert asset and asset.get_path_name().startswith(BASE + '/'), str(asset)
    LIB.set_metadata_tag(asset, 'GuLi.Owner', OWNER)
    LIB.set_metadata_tag(asset, 'GuLi.SourceBlendSHA256', META['source_sha256'])
    assert LIB.save_loaded_asset(asset, False), asset.get_path_name()
    if asset.get_path_name() not in REPORT['assets']:
        REPORT['assets'].append(asset.get_path_name())
    return asset

def owned(path):
    asset = unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not asset or LIB.get_metadata_tag(asset, 'GuLi.Owner') == OWNER, ('Unowned asset', path)
    return asset

def create(path, cls, factory):
    asset = owned(path)
    if asset is None:
        folder, name = path.rsplit('/', 1)
        LIB.make_directory(folder)
        asset = TOOLS.create_asset(name, folder, cls, factory)
    assert asset
    return save(asset)

def imported(filename, path, options=None, factory=None, revision=''):
    existing = owned(path)
    digest = hashlib.sha256(Path(filename).read_bytes()).hexdigest()
    if existing and LIB.get_metadata_tag(existing, 'GuLi.SourceSHA256') == digest and LIB.get_metadata_tag(existing, 'GuLi.ImportRevision') == revision:
        return existing
    folder, name = path.rsplit('/', 1)
    LIB.make_directory(folder)
    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(filename), destination_path=folder, destination_name=name,
        automated=True, async_=False, replace_existing=bool(existing), replace_existing_settings=False, save=False).items():
        task.set_editor_property(key, value)
    if options:
        task.set_editor_property('options', options)
    if factory:
        task.set_editor_property('factory', factory)
    TOOLS.import_asset_tasks([task])
    asset = unreal.load_asset(path)
    assert asset, (path, list(task.imported_object_paths))
    LIB.set_metadata_tag(asset, 'GuLi.SourceSHA256', digest)
    LIB.set_metadata_tag(asset, 'GuLi.ImportRevision', revision)
    return save(asset)

def node(owner, cls, **props):
    result = EDIT.create_material_expression_in_function(owner, cls) if isinstance(owner, unreal.MaterialFunction) else EDIT.create_material_expression(owner, cls)
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result

def link(a, b, pin='', output=''):
    assert EDIT.connect_material_expressions(a, output, b, pin), (a.get_name(), b.get_name(), pin, output)

def constant(owner, value):
    if isinstance(value, (int, float)):
        return node(owner, unreal.MaterialExpressionConstant, r=value)
    values = list(value) + [0] * max(0, 4 - len(value))
    return node(owner, unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(*values[:4]))

def custom(owner, code, inputs, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, outputs=None):
    result = node(owner, unreal.MaterialExpressionCustom, code=code, output_type=output_type)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    result.set_editor_property('inputs', pins)
    for name, (source, socket) in inputs.items():
        link(source, result, name, socket)
    if outputs:
        additional = []
        for name, typ in outputs:
            pin = unreal.CustomOutput()
            pin.set_editor_property('output_name', name)
            pin.set_editor_property('output_type', typ)
            additional.append(pin)
        result.set_editor_property('additional_outputs', additional)
    return result

textures = []
for lod in META['lods']:
    pair = []
    for role in ('Position', 'Normal'):
        tex = imported(lod[role.lower()], BASE + f'/Textures/T_QuadrupedMiner_{role}_LOD{lod["lod"]}', factory=unreal.TextureFactory())
        for key, value in dict(srgb=False,
            compression_settings=unreal.TextureCompressionSettings.TC_HDR if role == 'Position' else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
            compression_no_alpha=True, filter=unreal.TextureFilter.TF_NEAREST,
            mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
            address_x=unreal.TextureAddress.TA_CLAMP, address_y=unreal.TextureAddress.TA_CLAMP,
            never_stream=True, flip_green_channel=False).items():
            tex.set_editor_property(key, value)
        save(tex)
        pair.append(tex)
    textures.append(pair)

fn = create(BASE + '/Materials/MF_QuadrupedMiner_VAT', unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
EDIT.delete_all_material_expressions_in_function(fn)
inputs = {'Position': node(fn, unreal.MaterialExpressionPreSkinnedPosition),
    'PositionUV': node(fn, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0),
    'NormalUV': node(fn, unreal.MaterialExpressionTextureCoordinate, coordinate_index=2),
    'LODIndex': node(fn, unreal.MaterialExpressionScalarParameter, parameter_name='LODIndex', default_value=0.0)}
for i, lod in enumerate(META['lods']):
    for role in ('Position', 'Normal'):
        inputs[role + str(i)] = node(fn, unreal.MaterialExpressionTextureObject, texture=textures[i][0 if role == 'Position' else 1], sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        inputs[role + 'Rows' + str(i)] = constant(fn, lod[role.lower() + '_rows_per_frame'])
        inputs[role + 'Height' + str(i)] = constant(fn, lod[role.lower() + '_size'][1])
for name, index in (('Frame', 0), ('PreviousFrame', 1)):
    current = node(fn, unreal.MaterialExpressionPerInstanceCustomData, data_index=index, const_default_value=0)
    previous = node(fn, unreal.MaterialExpressionPerInstanceCustomData, data_index=index + 2, const_default_value=0)
    switch = node(fn, unreal.MaterialExpressionPreviousFrameSwitch)
    link(current, switch, 'Current Frame')
    link(previous, switch, 'Previous Frame')
    inputs[name] = switch
vat = custom(fn, (ROOT / 'Scripts' / 'QuadrupedMiner' / 'QuadrupedMinerVAT.hlsl').read_text(encoding='utf8'),
    {name: (value, '') for name, value in inputs.items()}, outputs=[('AnimatedNormal', unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
for i, (name, pin) in enumerate((('Offset', ''), ('Normal', 'AnimatedNormal'))):
    output = node(fn, unreal.MaterialExpressionFunctionOutput, output_name=name, sort_priority=i)
    link(vat, output, output=pin)
EDIT.update_material_function(fn)
save(fn)

mat = create(BASE + '/Materials/M_QuadrupedMiner_Toon3', unreal.Material, unreal.MaterialFactoryNew())
EDIT.delete_all_material_expressions(mat)
for key, value in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=False,
    used_with_instanced_static_meshes=True, max_world_position_offset_displacement=1000).items():
    mat.set_editor_property(key, value)
call = node(mat, unreal.MaterialExpressionMaterialFunctionCall)
assert call.set_material_function(fn)
assert EDIT.connect_material_property(call, 'Offset', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
animated = node(mat, unreal.MaterialExpressionVertexInterpolator)
link(call, animated, output='Normal')
# Palette belongs to the material slot, not imported vertex colors. Explicit
# mesh descriptions preserve the exact sRGB palette on every LOD.
palette = node(mat, unreal.MaterialExpressionVectorParameter, parameter_name='PaletteSRGB', default_value=unreal.LinearColor(.18, .18, .18, 1.0))
d1 = node(mat, unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
d2 = node(mat, unreal.MaterialExpressionTextureCoordinate, coordinate_index=2)
strength = node(mat, unreal.MaterialExpressionScalarParameter, parameter_name='InternalLineStrength', default_value=0.45)
width = node(mat, unreal.MaterialExpressionScalarParameter, parameter_name='InternalLineWidthCm', default_value=0.55)
emission = node(mat, unreal.MaterialExpressionScalarParameter, parameter_name='EmissionStrength', default_value=1.0)
code = '''float3 s = round(saturate(Palette.rgb) * 255) / 255;
float3 c = lerp(s / 12.92, pow((s + 0.055) / 1.055, 2.4), step(0.04045, s));
float d = dot(normalize(N), normalize(float3(0.35, -0.55, 0.76)));
float band = d < 0.32 ? 0.58 : (d < 0.68 ? 0.80 : 1.0);
float distanceToEdge = min(min(D1.x, D1.y), D2.x);
float line = 1.0 - smoothstep(Width - 0.10, Width + 0.10, distanceToEdge);
float3 ink = float3(0.01032982, 0.00907268, 0.02732089);
return lerp(c * band, ink, saturate(line * Strength)) * Emission;'''
toon = custom(mat, code, {'Palette': (palette, ''), 'N': (animated, ''), 'D1': (d1, ''), 'D2': (d2, ''),
    'Strength': (strength, ''), 'Width': (width, ''), 'Emission': (emission, '')})
assert EDIT.connect_material_property(toon, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
EDIT.recompile_material(mat)
save(mat)
outline = create(BASE + '/Materials/M_QuadrupedMiner_Outline', unreal.Material, unreal.MaterialFactoryNew())
EDIT.delete_all_material_expressions(outline)
for key, value in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=False,
    used_with_instanced_static_meshes=True, max_world_position_offset_displacement=1000).items():
    outline.set_editor_property(key, value)
call = node(outline, unreal.MaterialExpressionMaterialFunctionCall)
assert call.set_material_function(fn)
assert EDIT.connect_material_property(call, 'Offset', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
ink = constant(outline, (0.01032982, 0.00907268, 0.02732089))
assert EDIT.connect_material_property(ink, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
EDIT.recompile_material(outline)
save(outline)

instances = []
outline_instances = []
for i in range(3):
    row = []
    for source in META['materials']:
        if source['outline']:
            continue
        name = source['name'].split(' · ')[-1]
        instance = create(BASE + f'/Materials/MI_QuadrupedMiner_{name}_LOD{i}', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        EDIT.set_material_instance_parent(instance, mat)
        EDIT.set_material_instance_scalar_parameter_value(instance, 'LODIndex', float(i))
        EDIT.set_material_instance_vector_parameter_value(instance, 'PaletteSRGB', unreal.LinearColor(*source['srgb'], 1.0))
        EDIT.set_material_instance_scalar_parameter_value(instance, 'EmissionStrength', source['emission'])
        EDIT.set_material_instance_scalar_parameter_value(instance, 'InternalLineStrength', [0.45, 0.30, 0.18][i])
        save(instance)
        row.append(instance)
    instances.append(row)
    outline_instance = create(BASE + f'/Materials/MI_QuadrupedMiner_Outline_LOD{i}', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    EDIT.set_material_instance_parent(outline_instance, outline)
    EDIT.set_material_instance_scalar_parameter_value(outline_instance, 'LODIndex', float(i))
    save(outline_instance)
    outline_instances.append(outline_instance)

def fbx_options():
    ui = unreal.FbxImportUI()
    kind = unreal.FBXImportType.FBXIT_STATIC_MESH
    for key, value in dict(import_materials=False, import_textures=False, import_mesh=True, import_as_skeletal=False,
        import_animations=False, automated_import_should_detect_type=False, mesh_type_to_import=kind, original_import_type=kind).items():
        ui.set_editor_property(key, value)
    data = ui.get_editor_property('static_mesh_import_data')
    for key, value in dict(combine_meshes=True, auto_generate_collision=False, generate_lightmap_u_vs=False, build_nanite=False,
        transform_vertex_to_absolute=True, remove_degenerates=False, convert_scene=True, convert_scene_unit=False,
        normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
        vertex_color_import_option=unreal.VertexColorImportOption.REPLACE).items():
        data.set_editor_property(key, value)
    return ui

sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
mesh = imported(META['lods'][0]['fbx'], BASE + '/Meshes/SM_QuadrupedMiner_VAT', fbx_options(), unreal.FbxFactory(), revision='NumericCm_UEAxes_v4')
# LOD copies quantize NormalUV. Rebuild every LOD from independent explicit
# triangle descriptions instead; this script does not touch a staging mesh.
builder = ROOT / 'Scripts' / 'QuadrupedMiner' / 'build_explicit_lods.py'
exec(compile(builder.read_text(encoding='utf8'), str(builder), 'exec'), {'__name__': '__main__', '__file__': str(builder)})
outline_index = len(instances[0])
slots = []
for level in range(3):
    slots.extend(unreal.StaticMaterial(material_interface=instance, material_slot_name=f'Body_LOD{level}_{i}') for i, instance in enumerate(instances[level]))
    slots.append(unreal.StaticMaterial(material_interface=outline_instances[level], material_slot_name=f'Outline_LOD{level}'))
mesh.set_editor_property('static_materials', slots)
for i in range(3):
    build = sub.get_lod_build_settings(mesh, i)
    for key, value in dict(use_full_precision_u_vs=True, recompute_normals=False, recompute_tangents=False,
        generate_lightmap_u_vs=False, remove_degenerates=False).items():
        build.set_editor_property(key, value)
    sub.set_lod_build_settings(mesh, i, build)
    for j in range(outline_index):
        sub.set_lod_material_slot(mesh, i * (outline_index + 1) + j, i, j)
    sub.set_lod_material_slot(mesh, i * (outline_index + 1) + outline_index, i, outline_index)
sub.set_lod_screen_sizes(mesh, META['screen_sizes'])
# Mesh.GetBounds covers all LODs and previously cached extensions. Compute the
# LOD0 rest bounds directly; never use the aggregate bounds as a VAT baseline.
description = mesh.get_static_mesh_description(0)
points = [description.get_vertex_position(unreal.VertexID(i)) for i in range(description.get_vertex_count())]
rest_low = [min(getattr(v, axis) for v in points) for axis in ('x', 'y', 'z')]
rest_high = [max(getattr(v, axis) for v in points) for axis in ('x', 'y', 'z')]
origin = unreal.Vector(*[(lo + hi) / 2 for lo, hi in zip(rest_low, rest_high)])
extent = unreal.Vector(*[(hi - lo) / 2 for lo, hi in zip(rest_low, rest_high)])
expected = CONTRACT['fbx_rest_bounds_cm']
for i, axis in enumerate(('x', 'y', 'z')):
    expected_extent = (expected['max'][i] - expected['min'][i]) / 2
    expected_center = (expected['max'][i] + expected['min'][i]) / 2
    assert abs(getattr(extent, axis) - expected_extent) < 0.15, (axis, getattr(extent, axis), expected_extent)
    assert abs(getattr(origin, axis) - expected_center) < 0.15, (axis, getattr(origin, axis), expected_center)
low = META['lods'][0]['animation_bounds_cm']['min']
high = META['lods'][0]['animation_bounds_cm']['max']
# The native chassis length spans the complete stance, not the shorter rest pose.
# Independent FBX readback has already checked these centimetre-space axes.
actual_length = high[1] - low[1]
assert abs(actual_length - CONTRACT['body_length_cm']) < 0.5, (actual_length, CONTRACT['body_length_cm'])
# WPO bounds must cover both source clips; scale comes from the full stance.
origin_components = (origin.x, origin.y, origin.z)
positive = [max(rest_high[i], high[i]) - origin_components[i] + 5 for i in range(3)]
negative = [origin_components[i] - min(rest_low[i], low[i]) + 5 for i in range(3)]
mesh.set_editor_property('positive_bounds_extension', unreal.Vector(*positive))
mesh.set_editor_property('negative_bounds_extension', unreal.Vector(*negative))
save(mesh)
body = mesh.get_bounds()
scale = 591.6596 / actual_length
walk_rate = 900 / (CONTRACT['source_stance_speed_cm_per_second'] * scale)
CONTRACT.update(presentation_scale=scale, imported_body_length_cm=actual_length, default_walk_playback_rate=walk_rate,
    scaled_stance_speed_cm_per_second=CONTRACT['source_stance_speed_cm_per_second'] * scale,
    ue_asset=mesh.get_path_name(), ue_material_function=fn.get_path_name(), ue_import_readback_completed=True)
(ART / 'runtime_contract.json').write_text(json.dumps(CONTRACT, ensure_ascii=False, indent=2), encoding='utf8')
REPORT.update(success=True, animation_bounds_cm=META['lods'][0]['animation_bounds_cm'], imported_bounds=str(body),
    imported_length_cm=actual_length, presentation_scale=scale, walk_playback_rate=walk_rate,
    lods=[{'lod': i, 'triangles': mesh.get_num_triangles(i), 'vertices': mesh.get_num_vertices(i), 'sections': mesh.get_num_sections(i),
        'full_precision_uvs': sub.get_lod_build_settings(mesh, i).get_editor_property('use_full_precision_u_vs'),
        'section_material_indices': [sub.get_lod_material_slot(mesh, i, j) for j in range(mesh.get_num_sections(i))]} for i in range(3)],
    material_count=len(META['materials']), runtime_skeleton=False, gameplay_speed_cm_per_second=900)
(ART / 'Reports' / 'ue_vat_import.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(REPORT, ensure_ascii=False))

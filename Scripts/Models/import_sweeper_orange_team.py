"""Promote the explicitly released Sweeper B_v1 UV mask, preserving the native mesh.

Run in the live editor through commander_editor_python.py. No mesh reimport,
vertex painting, LOD regeneration, native compilation or gameplay launch.
"""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/SweeperTeamColor_v1_20261008'
SOURCE = OUT / 'Masks/T_Sweeper_OrangeTeamMask.png'
BLEND = OUT / 'Sweeper_OrangeTeamColor_B_v1.blend'
EXPECTED_BLEND = '6ff9ea43b21702b6a820bafbca0e69e5e715ec5302c28128086af431ba938144'
MASK_ROOT = '/Game/GuLiStrike/Models/TeamColor_v1/SweeperSummon/Textures'
MASK_PATH = MASK_ROOT + '/T_Sweeper_OrangeTeamMask'
MARKER = 'GuLiSweeperOrangeTeamPalette'
lib = unreal.MaterialEditingLibrary


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_report(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')


def connect(source, output, target, pin):
    if not lib.connect_material_expressions(source, output, target, pin):
        raise RuntimeError('Rejected material connection: ' + target.get_name() + '/' + pin)


def expressions(material, cls=unreal.MaterialExpression):
    return [e for e in unreal.ObjectIterator(cls) if e.get_outer() == material]


def parameters(material):
    values = []
    for cls, vector in [(unreal.MaterialExpressionVectorParameter, True),
                        (unreal.MaterialExpressionScalarParameter, False)]:
        for e in expressions(material, cls):
            value = e.get_editor_property('default_value')
            values.append({'name': str(e.get_editor_property('parameter_name')),
                           'value': list(value.to_tuple()) if vector else float(value),
                           'type': 'Vector' if vector else 'Scalar',
                           'cpd': bool(e.get_editor_property('use_custom_primitive_data')),
                           'index': int(e.get_editor_property('primitive_data_index'))})
    return sorted(values, key=lambda r: (r['type'], r['name']))


def run():
    if sha(BLEND) != EXPECTED_BLEND:
        raise RuntimeError('The released Blender candidate has changed; stop before formal writes.')
    original = json.loads((OUT / 'original-source-readback.json').read_text(encoding='utf8'))
    mesh = unreal.load_object(None, original['model']['ResourcePath'])
    material = unreal.load_object(None, original['materials'][0]['parent'])
    before = json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
    if before != original['snapshot']:
        raise RuntimeError('Native source mesh changed since the B candidate; no asset writes performed.')
    tone = next(e for e in expressions(material, unreal.MaterialExpressionCustom)
                if [str(i.get_editor_property('input_name')) for i in e.get_editor_property('inputs')] == ['Base', 'N', 'L', 'Ink'])
    tone_code = tone.get_editor_property('code')
    if tone_code != original['materials'][0]['custom'][0]['code']:
        raise RuntimeError('Original Sweeper tone shader changed; inspect before promotion.')
    previous = lib.get_inputs_for_material_expression(material, tone)[0]
    existing = next((e for e in expressions(material, unreal.MaterialExpressionCustom)
                     if e.get_editor_property('desc') == MARKER), None)
    old_base = lib.get_inputs_for_material_expression(material, existing)[0] if existing else previous
    if not isinstance(old_base, unreal.MaterialExpressionTextureSample):
        raise RuntimeError('Expected original base-color atlas connection.')
    base_output = 'RGB'
    wpo = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    protected_parameters = [p for p in parameters(material) if not p['name'].startswith('GuLi_Team')]
    authorization = {'date': '2026-10-09', 'user_instruction': '导入 UE、接入自动改色。',
                     'released_candidate': str(BLEND.relative_to(ROOT)), 'blend_sha256': sha(BLEND),
                     'mask_sha256': sha(SOURCE), 'model_id': 1005, 'unit_type_id': 5,
                     'scope': 'Original orange regions only; shared native mesh and original fixed colors',
                     'excluded_models': [2004, 2007],
                     'decision': 'User explicitly released this displayed B_v1 for formal import and automatic team colors'}
    save_report('formal-import-authorization.json', authorization)
    save_report('pre-import-native-invariants.json', before)
    texture = unreal.load_asset(MASK_PATH)
    if texture and unreal.EditorAssetLibrary.get_metadata_tag(texture, 'GuLi.SourceSHA256') != sha(SOURCE):
        raise RuntimeError('Destination mask is not owned by this exact candidate.')
    if not texture:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        enabled = unreal.SystemLibrary.get_console_variable_bool_value('Interchange.FeatureFlags.Import.Enable')
        try:
            unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.Enable 0')
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', str(SOURCE))
            task.set_editor_property('destination_path', MASK_ROOT)
            task.set_editor_property('destination_name', 'T_Sweeper_OrangeTeamMask')
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', False)
            task.set_editor_property('save', False)
            task.set_editor_property('factory', unreal.TextureFactory())
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        finally:
            unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.Enable ' + ('1' if enabled else '0'))
        texture = unreal.load_asset(MASK_PATH)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError('Mask import did not create the expected Texture2D.')
    texture.set_editor_property('srgb', False)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_GRAYSCALE)
    texture.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    unreal.EditorAssetLibrary.set_metadata_tag(texture, 'GuLi.SourceSHA256', sha(SOURCE))
    unreal.EditorAssetLibrary.set_metadata_tag(texture, 'GuLi.ModelId', '1005')
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture, False):
        raise RuntimeError('Unable to save the approved mask texture.')
    added = []
    try:
        if not existing:
            def node(cls, x, y):
                e = lib.create_material_expression(material, cls, x, y)
                added.append(e)
                return e
            mask = node(unreal.MaterialExpressionTextureSample, -1000, 900)
            mask.set_editor_property('texture', texture)
            mask.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
            # Texture sample defaults to UV0. Use the same coordinates as the original atlas.
            coords = lib.get_inputs_for_material_expression(material, old_base)
            if coords:
                connect(coords[0], '', mask, 'UVs')
            primary = node(unreal.MaterialExpressionVectorParameter, -1000, 1200)
            primary.set_editor_property('parameter_name', 'GuLi_TeamPrimary')
            primary.set_editor_property('use_custom_primitive_data', True)
            primary.set_editor_property('primitive_data_index', 8)
            primary.set_editor_property('default_value', unreal.LinearColor(.144128470858, .371237680474, .514917665377, 1))
            enabled_param = node(unreal.MaterialExpressionScalarParameter, -1000, 1450)
            enabled_param.set_editor_property('parameter_name', 'GuLi_TeamEnabled')
            enabled_param.set_editor_property('use_custom_primitive_data', True)
            enabled_param.set_editor_property('primitive_data_index', 16)
            enabled_param.set_editor_property('default_value', 0)
            palette = node(unreal.MaterialExpressionCustom, -550, 850)
            palette.set_editor_property('desc', MARKER)
            palette.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            inputs = []
            for name in ['Base', 'Mask', 'Primary', 'Enabled']:
                value = unreal.CustomInput()
                value.set_editor_property('input_name', name)
                inputs.append(value)
            palette.set_editor_property('inputs', inputs)
            palette.set_editor_property('code', 'float3 gray=float3(.02518686,.03820437,.03560131); float3 team=Enabled>.5?Primary.rgb:gray; return lerp(Base.rgb,team,saturate(Mask));')
            connect(old_base, base_output, palette, 'Base')
            connect(mask, 'R', palette, 'Mask')
            connect(primary, '', palette, 'Primary')
            connect(enabled_param, '', palette, 'Enabled')
            connect(palette, '', tone, 'Base')
        if tone.get_editor_property('code') != tone_code:
            raise RuntimeError('The original three-tone code was modified.')
        if lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET) != wpo:
            raise RuntimeError('Original rigid animation WPO was modified.')
        if [p for p in parameters(material) if not p['name'].startswith('GuLi_Team')] != protected_parameters:
            raise RuntimeError('An original rigid/color parameter changed.')
        lib.recompile_material(material)
        c = unreal.new_object(unreal.StaticMeshComponent)
        c.set_static_mesh(mesh)
        indices = {'GuLi_TeamPrimary': c.get_custom_primitive_data_index_for_vector_parameter('GuLi_TeamPrimary'),
                   'GuLi_TeamEnabled': c.get_custom_primitive_data_index_for_scalar_parameter('GuLi_TeamEnabled')}
        if indices != {'GuLi_TeamPrimary': 8, 'GuLi_TeamEnabled': 16}:
            raise RuntimeError('Material CPD bindings did not resolve: ' + str(indices))
        after = json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
        if after != before:
            raise RuntimeError('Native source/render mesh attributes changed.')
        for key, value in [('GuLi.ModelId', '1005'), ('GuLi.ModelInterface', 'SweeperUV0Orange_v1'),
                           ('GuLi.ModelApproved', '1'), ('GuLi.SourceSHA256', sha(BLEND)),
                           ('GuLi.TeamMask', texture.get_path_name())]:
            unreal.EditorAssetLibrary.set_metadata_tag(material, key, value)
        if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
            raise RuntimeError('Unable to save the formal material.')
        report = {'success': True, 'model_id': 1005, 'mesh': mesh.get_path_name(),
                  'material': material.get_path_name(), 'mask': texture.get_path_name(),
                  'mask_sha256': sha(SOURCE), 'blend_sha256': sha(BLEND),
                  'mesh_snapshot_before': before, 'mesh_snapshot_after': after,
                  'all_native_mesh_attributes_exact': before == after,
                  'cpd': indices, 'tone_code': tone_code, 'tone_code_unchanged': True,
                  'wpo_unchanged': True, 'original_parameters_unchanged': True,
                  'base_atlas': old_base.get_editor_property('texture').get_path_name(),
                  'parameters_after': parameters(material),
                  'texture_srgb': texture.get_editor_property('srgb'),
                  'texture_compression': str(texture.get_editor_property('compression_settings')),
                  'mesh_reimported': False, 'native_compile': False,
                  'saved_packages': [texture.get_path_name(), material.get_path_name()]}
        save_report('formal-ue-import.json', report)
        return {'success': True, 'mask': report['mask'], 'cpd': indices,
                'native_lod_triangles': [x['triangles'] for x in after['render_lods']],
                'all_native_mesh_attributes_exact': True, 'original_shader_and_wpo_preserved': True}
    except Exception:
        if added:
            connect(old_base, base_output, tone, 'Base')
            for e in reversed(added):
                lib.delete_material_expression(material, e)
            lib.recompile_material(material)
        raise


unreal.MCPythonHelper.submit_result(json.dumps(run()))

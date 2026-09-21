"""Author only RocketJump's DataTable, input, mesh material and Blueprint wiring in UE."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'TestResults/GroundMech/RocketJump'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/GuLiStrike/GroundMech'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
REPORT = {'success': False, 'assets': []}


def load(path):
    return unreal.load_object(None, path + '.' + path.rsplit('/', 1)[-1])


def save(asset):
    assert asset.get_path_name().startswith('/Game/GuLiStrike/')
    if asset.get_path_name().startswith(BASE + '/'):
        LIB.set_metadata_tag(asset, 'GuLi.Owner', 'GuLi.GroundMech.v1')
    assert LIB.save_loaded_asset(asset, False), asset.get_path_name()
    REPORT['assets'].append(asset.get_path_name())


def create(path, cls, factory):
    existing = load(path)
    if existing:
        return existing
    folder, name = path.rsplit('/', 1)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)


def author_material():
    mat = create(BASE + '/UI/M_RocketFuelArc', unreal.Material, unreal.MaterialFactoryNew())
    mat.modify()
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('disable_depth_test', False)
    EDIT.delete_all_material_expressions(mat)
    uv = EDIT.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -600, -150)
    params = {}
    for index, (name, value) in enumerate([('FuelRatio', 1.), ('Opacity', 1.)]):
        p = EDIT.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, index*150)
        p.set_editor_property('parameter_name', name)
        p.set_editor_property('default_value', value)
        params[name] = p
    color = EDIT.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 300)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(.18, 1., .08, 1.))
    custom = EDIT.create_material_expression(mat, unreal.MaterialExpressionCustom, -220, 0)
    inputs = []
    for name in ['UV', 'FuelRatio', 'Opacity', 'Color']:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        inputs.append(pin)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description', '20-segment RocketJump arc; bottom-to-top remaining capacity')
    custom.set_editor_property('code', '''
float2 p = float2(UV.x + 0.27, 0.5 - UV.y);
float radius = length(p);
float angle = atan2(p.y, p.x);
float halfAngle = 0.9599311;
float t = saturate((angle + halfAngle) / (2.0 * halfAngle));
float aa = max(fwidth(radius), 0.0015);
float edgeDistance = abs(radius - 0.59);
float arc = (1.0 - smoothstep(0.025-aa, 0.025+aa, edgeDistance)) *
            (1.0 - smoothstep(halfAngle-0.012, halfAngle+0.012, abs(angle)));
float rim = smoothstep(0.016, 0.021, edgeDistance);
float ticks = 1.0 - smoothstep(0.79, 0.90, frac(t * 20.0));
float filled = step(t, saturate(FuelRatio)) * step(0.00001, FuelRatio);
float3 dark = float3(0.012, 0.022, 0.024);
float3 body = lerp(dark, Color.rgb * 1.35, filled * ticks);
float3 rgb = lerp(body, Color.rgb * 0.7 + 0.2, rim);
return float4(rgb, arc * saturate(Opacity) * 0.95);
''')
    for source, name in [(uv, 'UV'), (params['FuelRatio'], 'FuelRatio'), (params['Opacity'], 'Opacity'), (color, 'Color')]:
        assert EDIT.connect_material_expressions(source, '', custom, name)
    rgb = EDIT.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 0)
    alpha = EDIT.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 150)
    for node, channels in [(rgb, ('r','g','b')), (alpha, ('a',))]:
        for channel in ('r','g','b','a'):
            node.set_editor_property(channel, channel in channels)
        assert EDIT.connect_material_expressions(custom, '', node, '')
    assert EDIT.connect_material_property(rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert EDIT.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    EDIT.recompile_material(mat)
    save(mat)
    REPORT['material'] = {'scalar_parameters': [str(x) for x in EDIT.get_scalar_parameter_names(mat)],
                          'vector_parameters': [str(x) for x in EDIT.get_vector_parameter_names(mat)]}


def import_skill_table():
    source = (ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    marker = 'report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}'
    assert marker in source
    namespace = {'__name__': 'rocket_jump_import'}
    exec(compile(source.split(marker, 1)[0], 'import_data_to_engine.py', 'exec'), namespace)
    namespace['PROGRESS'] = str(OUT / 'data-import.log')
    original = namespace['write_csv_sidecar']
    def utf8_sidecar(rows, path):
        original(rows, path)
        file = Path(path)
        file.write_text(file.read_text(encoding='utf-8'), encoding='utf-8-sig')
    namespace['write_csv_sidecar'] = utf8_sidecar
    manifest = json.loads((ROOT / 'Data/Json/manifest.json').read_text(encoding='utf-8'))
    name = 'DT_GuLiStrikeMech_Skills'
    result = namespace['import_table'](name, manifest['tables'][name])
    REPORT['table_import'] = result
    assert result.get('imported'), result


def wire_input():
    action = create(BASE + '/Input/IA_Ground_RocketJump', unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
    action.set_editor_property('consume_input', True)
    save(action)
    context = load(BASE + '/Input/IMC_GroundMech')
    assert context
    context.modify()
    data = context.get_editor_property('default_key_mappings')
    rows = list(data.get_editor_property('mappings'))
    rows = [r for r in rows if r.get_editor_property('action') != action]
    key = unreal.Key()
    key.set_editor_property('key_name', 'SpaceBar')
    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property('action', action)
    mapping.set_editor_property('key', key)
    rows.append(mapping)
    data.set_editor_property('mappings', rows)
    context.set_editor_property('default_key_mappings', data)
    save(context)
    for path in [BASE + '/BP_GroundMech_Light', BASE + '/Review/BP_GroundMech_FireReview']:
        bp = load(path)
        if not bp:
            continue
        bp.modify()
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        cdo = unreal.get_default_object(bp.generated_class())
        cdo.modify()
        cdo.set_editor_property('rocket_jump_action', action)
        rocket = cdo.get_editor_property('rocket_jump')
        rocket.modify()
        rocket.set_editor_property('skill_table', load('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills'))
        # A previously replaced native movement subobject can retain its old class defaults.
        # Keep the established ground-mech locomotion values, including normal gravity.
        movement = cdo.character_movement
        movement.modify()
        native = unreal.get_default_object(unreal.GuLiGroundMechCharacter).character_movement
        for field in ['gravity_scale', 'max_walk_speed', 'max_acceleration', 'braking_deceleration_walking',
                      'max_step_height', 'rotation_rate', 'orient_rotation_to_movement', 'min_analog_walk_speed']:
            movement.set_editor_property(field, native.get_editor_property(field))
        movement.set_walkable_floor_angle(native.get_walkable_floor_angle())
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        save(bp)
    REPORT['space_bindings'] = sum(str(r.get_editor_property('key').get_editor_property('key_name')) == 'SpaceBar' for r in rows)


try:
    author_material()
    import_skill_table()
    wire_input()
    REPORT['success'] = True
except Exception:
    REPORT['error'] = traceback.format_exc()
(OUT / 'asset-authoring.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(REPORT, ensure_ascii=True))

"""Read real editor assets and author only the isolated final-UI silhouette material."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/LocalTeamColorReview_20261008'
OUT.mkdir(parents=True, exist_ok=True)
EDIT = unreal.MaterialEditingLibrary
LIB = unreal.EditorAssetLibrary
report = {'success': False, 'native_build_executed': False, 'gameplay_started': False, 'models': []}

def exprs(owner):
    return [node for node in unreal.ObjectIterator(unreal.MaterialExpression) if node.get_outer() == owner]

def field(obj, name):
    try:
        value = obj.get_editor_property(name)
        if isinstance(value, (str, int, float, bool)):
            return value
        if isinstance(value, unreal.Object):
            return value.get_path_name()
        if hasattr(value, 'to_tuple'):
            return list(value.to_tuple())
        return str(value)
    except Exception:
        return None

def material_info(material):
    info = {'path': material.get_path_name(), 'class': material.get_class().get_name()}
    parent = material
    while isinstance(parent, unreal.MaterialInstance):
        info.setdefault('instance_parameters', []).append({
            'path': parent.get_path_name(),
            'vectors': str(parent.get_editor_property('vector_parameter_values')),
            'textures': str(parent.get_editor_property('texture_parameter_values'))})
        parent = parent.get_editor_property('parent')
    info['base'] = parent.get_path_name()
    info['properties'] = {name: field(parent, name) for name in ('shading_model', 'blend_mode', 'two_sided')}
    info['expressions'] = []
    for node in exprs(parent):
        entry = {'name': node.get_name(), 'class': node.get_class().get_name()}
        for name in ('parameter_name', 'default_value', 'constant', 'texture', 'coordinate_index',
                     'code', 'desc', 'description', 'data_index'):
            value = field(node, name)
            if value is not None:
                entry[name] = value
        info['expressions'].append(entry)
    return info

def add_model(name, mesh_path, group, **extra):
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        raise RuntimeError('Missing source model: ' + mesh_path)
    prop = 'static_materials' if isinstance(mesh, unreal.StaticMesh) else 'materials'
    slots = []
    for i, slot in enumerate(mesh.get_editor_property(prop)):
        material = slot.get_editor_property('material_interface')
        slots.append({'index': i, 'name': str(slot.get_editor_property('material_slot_name')),
                      'material': material_info(material) if material else None})
    bounds = mesh.get_bounds() if isinstance(mesh, unreal.StaticMesh) else 'Read through the preview component bounds'
    report['models'].append({'name': name, 'group': group, 'mesh': mesh.get_path_name(),
                             'kind': mesh.get_class().get_name(), 'bounds': str(bounds), 'slots': slots, **extra})

def author_outline():
    folder = '/Game/GuLiStrike/UI/SceneOverlay'
    path = folder + '/M_SceneUIEnemyOutline'
    material = unreal.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_SceneUIEnemyOutline', folder, unreal.Material, unreal.MaterialFactoryNew())
    for node in list(exprs(material)):
        EDIT.delete_material_expression(material, node)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate)
    size = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureProperty)
    size.set_editor_property('property', unreal.MaterialExposedTextureProperty.TMTM_TEXEL_SIZE)
    obj = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter)
    obj.set_editor_property('parameter_name', 'Silhouette')
    obj.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    obj.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    assert EDIT.connect_material_expressions(obj, '', size, '')
    custom = EDIT.create_material_expression(material, unreal.MaterialExpressionCustom)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for name in ('Silhouette', 'UV', 'Texel', 'SolidFill'):
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    custom.set_editor_property('inputs', pins)
    custom.set_editor_property('code', '''
float center = 1.0 - Texture2DSample(Silhouette, SilhouetteSampler, UV).a;
float neighbor = 0.0;
const float2 offsets[8] = {float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),
    float2(.707,.707),float2(-.707,.707),float2(.707,-.707),float2(-.707,-.707)};
[unroll] for (int i=0; i<8; ++i)
    neighbor = max(neighbor, 1.0 - Texture2DSample(Silhouette, SilhouetteSampler,
        clamp(UV + offsets[i] * Texel * 2.0, Texel * .5, 1.0 - Texel * .5)).a);
return lerp(step(.5, neighbor) * (1.0 - step(.5, center)), step(.5,center), saturate(SolidFill));
''')
    for source, name in ((obj, 'Silhouette'), (uv, 'UV'), (size, 'Texel')):
        assert EDIT.connect_material_expressions(source, '', custom, name)
    fill = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    fill.set_editor_property('parameter_name', 'SolidFill')
    fill.set_editor_property('default_value', 0.0)
    assert EDIT.connect_material_expressions(fill, '', custom, 'SolidFill')
    color = EDIT.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name', 'TintColor')
    color.set_editor_property('default_value', unreal.LinearColor(1.0, .04, .03, 1.0))
    assert EDIT.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert EDIT.connect_material_property(custom, '', unreal.MaterialProperty.MP_OPACITY)
    EDIT.recompile_material(material)
    LIB.set_metadata_tag(material, 'GuLi.SceneUI', 'LocalViewOpaqueOutline.v1')
    assert LIB.save_loaded_asset(material, False)
    report['outline_material'] = path

    for name, is_ui in (('M_SceneUIWidgetSurface', True), ('M_SceneUIPlacementMask', False)):
        path = folder+'/'+name
        mat = unreal.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name,folder,unreal.Material,unreal.MaterialFactoryNew())
        for node in list(exprs(mat)):
            EDIT.delete_material_expression(mat,node)
        if is_ui:
            mat.set_editor_property('material_domain',unreal.MaterialDomain.MD_UI)
            mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
            texture = EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureSampleParameter2D)
            texture.set_editor_property('parameter_name','WidgetTexture')
            texture.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
            opacity = EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom)
            opacity.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
            pin = unreal.CustomInput(); pin.set_editor_property('input_name','Alpha')
            opacity.set_editor_property('inputs',[pin])
            opacity.set_editor_property('code','return step(1.0/255.0,Alpha);')
            assert EDIT.connect_material_expressions(texture,'A',opacity,'Alpha')
            assert EDIT.connect_material_property(texture,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            assert EDIT.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
        else:
            mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
            mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
            mat.set_editor_property('used_with_skeletal_mesh',True)
            constant = EDIT.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
            constant.set_editor_property('constant',unreal.LinearColor(1,1,1,1))
            assert EDIT.connect_material_property(constant,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        EDIT.recompile_material(mat)
        LIB.set_metadata_tag(mat,'GuLi.SceneUI','FinalSlate.v1')
        assert LIB.save_loaded_asset(mat,False)
    report['scene_ui_materials'] = [folder+'/'+name for name in
        ('M_SceneUIEnemyOutline','M_SceneUIWidgetSurface','M_SceneUIPlacementMask')]

try:
    assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None
    author_outline()
    rows = json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8-sig'))
    import sys
    sys.path.insert(0,str(ROOT/'Scripts'))
    from Models import model_catalog
    for row in rows:
        if row['Id'] in (1, 2, 5, 6):
            add_model(row['Name'], model_catalog.resource(row['ModelId']), 'Mass', unit_id=row['Id'], display_name=row['DisplayName'])
    rows = json.loads((ROOT/'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf-8-sig'))
    for row in rows:
        if model_catalog.definition(row['ModelId'])['ResourceType']!='PresentationClass':
            add_model(row['Name'], model_catalog.resource(row['ModelId']), 'Building', definition_id=row['Id'], display_name=row['DisplayName'], mesh_scale=row['MeshScale'])
    delivery = json.loads((ROOT/'ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/formal_delivery.json').read_text(encoding='utf-8'))
    entries = delivery['assets'] if 'assets' in delivery else delivery['models']
    for entry in entries:
        if entry['team'] == 'Blue':
            add_model('SSF_' + entry['building'], entry['path'], 'SSF', approved_palette='SSF_TeamPalette_B_v2')
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
(OUT/'source-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log(json.dumps({key: value for key, value in report.items() if key not in ('models',)}, ensure_ascii=False))

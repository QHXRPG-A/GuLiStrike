"""Build capture-only derivatives; never alter a source mesh/material or start PIE.

Run through commander_editor_python in the open editor. The generated binding
manifest is consumed by GuLiGroundSceneUISettings; source MIC static permutations,
WPO, opacity clipping and parameter names are retained.
"""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = Path(r'D:/UE5.7/PerformanceCaptures/GuLiStrike/20261106-ground-ui-feedback')
FOLDER = '/Game/GuLiStrike/UI/SceneOverlay/GroundFeedback'
EDIT = unreal.MaterialEditingLibrary
LIB = unreal.EditorAssetLibrary
assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Finish user play before authoring'


def nodes(material):
    return [x for x in unreal.ObjectIterator(unreal.MaterialExpression) if x.get_outer() == material]


def make(material, cls, **values):
    result = EDIT.create_material_expression(material, cls)
    for key, value in values.items():
        result.set_editor_property(key, value)
    result.set_editor_property('desc', 'GuLiGroundFeedback.v1')
    return result


def connect(source, output, target, pin):
    assert EDIT.connect_material_expressions(source, output, target, pin)


bindings = {}
report = {'bindings': [], 'skipped': [], 'gameplay_started': False}


def derive(source):
    path = source.get_path_name()
    if path in bindings:
        return bindings[path]
    suffix = hashlib.sha256(path.encode()).hexdigest()[:10]
    name = ('MI_' if isinstance(source, unreal.MaterialInstance) else 'M_') + 'GroundMask_' + suffix
    target_path = FOLDER + '/Masks/' + name
    assert not LIB.does_asset_exist(target_path) or LIB.get_metadata_tag(LIB.load_asset(target_path), 'GuLi.MaskSource') == path
    # Existing derivatives keep their preserved source graph; schema nodes are replaced idempotently.
    if LIB.does_asset_exist(target_path):
        result = LIB.load_asset(target_path)
    else:
        result = LIB.duplicate_asset(path, target_path)
    assert result
    LIB.set_metadata_tag(result, 'GuLi.MaskSource', path)
    if isinstance(source, unreal.MaterialInstance):
        parent = derive(source.get_editor_property('parent'))
        EDIT.set_material_instance_parent(result, parent)
        # Duplicate preserves the complete static parameter set and base overrides.
        EDIT.update_material_instance(result)
    else:
        assert not source.get_editor_property('use_material_attributes'), path
        for node in nodes(result):
            if node.get_editor_property('desc') == 'GuLiGroundFeedback.v1':
                EDIT.delete_material_expression(result, node)
        result.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        result.set_editor_property('used_with_instanced_static_meshes', True)
        result.set_editor_property('used_with_skeletal_mesh', source.get_editor_property('used_with_skeletal_mesh'))
        # R is object opacity, G is coverage; source WPO and opacity mask remain connected.
        scalar = make(result, unreal.MaterialExpressionScalarParameter,
                      parameter_name='GuLiSceneUIOpacity', default_value=1.0)
        instanced = make(result, unreal.MaterialExpressionScalarParameter,
                         parameter_name='GuLiSceneUIInstanced', default_value=0.0)
        custom = make(result, unreal.MaterialExpressionPerInstanceCustomData, data_index=63, const_default_value=1.0)
        vertex = make(result, unreal.MaterialExpressionVertexInterpolator)
        connect(custom, '', vertex, '')
        alpha = make(result, unreal.MaterialExpressionLinearInterpolate)
        connect(scalar, '', alpha, 'A')
        connect(vertex, '', alpha, 'B')
        connect(instanced, '', alpha, 'Alpha')
        coverage = make(result, unreal.MaterialExpressionConstant2Vector, r=1.0, g=0.0)
        channels = make(result, unreal.MaterialExpressionAppendVector)
        connect(alpha, '', channels, 'A')
        connect(coverage, '', channels, 'B')
        assert EDIT.connect_material_property(channels, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        # Remove irrelevant lighting branches, preserving only the geometry/clip contract.
        EDIT.recompile_material(result)
        for prop in (unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, unreal.MaterialProperty.MP_OPACITY_MASK,
                     unreal.MaterialProperty.MP_OPACITY):
            before = EDIT.get_material_property_input_node(source, prop)
            after = EDIT.get_material_property_input_node(result, prop)
            assert (before is None) == (after is None), (path, str(prop))
            if before:
                assert before.get_name() == after.get_name(), (path, str(prop))
    LIB.set_metadata_tag(result, 'GuLi.MaskSource', path)
    LIB.set_metadata_tag(result, 'GuLi.SceneUI', 'GroundObjectOpacity.v1')
    assert LIB.save_loaded_asset(result, False)
    bindings[path] = result
    report['bindings'].append({'source': path, 'mask': result.get_path_name(),
                               'class': source.get_class().get_name()})
    return result


if (OUT/'ground-mask-materials.json').exists():
    previous=json.loads((OUT/'ground-mask-materials.json').read_text(encoding='utf-8'))
    for row in previous['bindings']:
        asset=unreal.load_asset(row['mask'])
        assert LIB.get_metadata_tag(asset,'GuLi.MaskSource')==row['source']
        bindings[row['source']]=asset
        report['bindings'].append(row)

catalog = json.loads((OUT/'material-source-catalog.json').read_text(encoding='utf-8'))
baseline = json.loads((OUT/'baseline-a/presentation-census.json').read_text(encoding='utf-8'))
sources = set(catalog['bases']) | set(baseline['mask_sources'])
for asset in catalog['assets']:
    if asset['class'] == 'MaterialInstanceConstant':
        sources.add(asset['package'])
# Runtime substitutions and player-ship materials are outside some model mesh slots.
import re
config = (ROOT/'Config/DefaultGame.ini').read_text(encoding='utf-8-sig')
sources.update(re.findall(r'Variant="([^"]+)"', config))
sources.update(json.loads((OUT/'ship-material-sources.json').read_text(encoding='utf-8'))['materials'])
ar=unreal.AssetRegistryHelpers.get_asset_registry()
for asset in ar.get_assets_by_path('/Game/GuLiStrike/Ship/Stylized/Materials',recursive=False):
    if str(asset.asset_class_path.asset_name) in ('Material','MaterialInstanceConstant'):
        sources.add(asset.get_asset().get_path_name())
for path in sorted(sources):
    # Resource nodes are not enemy units; these material-attribute graphs are not captured.
    if '/Resources/Ores/' in path or path.endswith('M_CommanderUnitRing.M_CommanderUnitRing') or '/M_Pioneer_Hit.' in path or '/MatineeCam_mat.' in path:
        report['skipped'].append(path)
        continue
    source = unreal.load_asset(path)
    assert isinstance(source, unreal.MaterialInterface), path
    derive(source)

name = 'M_GroundEnemyOutline'
path = FOLDER + '/' + name
material = LIB.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
assert material
for node in nodes(material):
    EDIT.delete_material_expression(material, node)
material.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
uv = make(material, unreal.MaterialExpressionTextureCoordinate)
size = make(material, unreal.MaterialExpressionTextureProperty,
            property=unreal.MaterialExposedTextureProperty.TMTM_TEXEL_SIZE)
texture = make(material, unreal.MaterialExpressionTextureObjectParameter,
               parameter_name='Silhouette', texture=unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'),
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
connect(texture, '', size, '')
pins = []
for name in ('Silhouette', 'UV', 'Texel'):
    pin = unreal.CustomInput()
    pin.set_editor_property('input_name', name)
    pins.append(pin)
edge = make(material, unreal.MaterialExpressionCustom, inputs=pins,
            output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
            code='''
float2 center = Texture2DSample(Silhouette, SilhouetteSampler, UV).rg;
float opacity = 0.0;
const float2 offsets[8] = {float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),
    float2(.707,.707),float2(-.707,.707),float2(.707,-.707),float2(-.707,-.707)};
[unroll] for (int i=0; i<8; ++i) {
    float2 neighbor = Texture2DSample(Silhouette, SilhouetteSampler,
        clamp(UV + offsets[i] * Texel * 2.0, Texel * .5, 1.0 - Texel * .5)).rg;
    opacity = max(opacity, saturate(neighbor.r) * step(.5, neighbor.g));
}
return opacity * (1.0 - step(.5, center.g));
''')
for source, pin in ((texture, 'Silhouette'), (uv, 'UV'), (size, 'Texel')):
    connect(source, '', edge, pin)
color = make(material, unreal.MaterialExpressionVectorParameter, parameter_name='TintColor',
             default_value=unreal.LinearColor(1.0, .04, .03, 1.0))
assert EDIT.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
assert EDIT.connect_material_property(edge, '', unreal.MaterialProperty.MP_OPACITY)
EDIT.recompile_material(material)
LIB.set_metadata_tag(material, 'GuLi.SceneUI', 'GroundObjectOpacity.v1')
assert LIB.save_loaded_asset(material, False)
report['outline'] = material.get_path_name()
report['success'] = True
(OUT/'ground-mask-materials.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'bindings': len(bindings), 'outline': report['outline']}))

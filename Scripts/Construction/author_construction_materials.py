"""Create only construction-owned materials. Run in the normal editor through ue_exec.py."""
import gc
import json
from pathlib import Path
import unreal

BASE = '/Game/GuLiStrike/Buildings/Construction'
OWNER = 'GuLiStrike.Construction.20260922'
OUT = Path('D:/UE5.7/test1/outputs/construction-20260922')
M = unreal.MaterialEditingLibrary


def node(material, kind, **properties):
    result = M.create_material_expression(material, getattr(unreal, 'MaterialExpression' + kind), 0, 0)
    assert result, kind
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def wire(source, target, pin, output=''):
    assert M.connect_material_expressions(source, output, target, pin), pin


def output(source, material, prop):
    assert M.connect_material_property(source, '', getattr(unreal.MaterialProperty, 'MP_' + prop)), prop


def scalar(material, name, value):
    return node(material, 'ScalarParameter', parameter_name=name, default_value=value)


def vector(material, color):
    return node(material, 'VectorParameter', parameter_name='TintColor', default_value=unreal.LinearColor(*color))


def multiply(material, a, b):
    result = node(material, 'Multiply')
    wire(a, result, 'A'); wire(b, result, 'B')
    return result


def material(name, decal=False):
    path = BASE + '/' + name
    asset = unreal.load_asset(path)
    if asset:
        assert unreal.EditorAssetLibrary.get_metadata_tag(asset, 'GuLi.Construction.Owner') == OWNER, path
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, BASE, unreal.Material, unreal.MaterialFactoryNew())
    assert asset, path
    M.delete_all_material_expressions(asset)
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    asset.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL if decal else unreal.MaterialDomain.MD_SURFACE)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('two_sided', False)
    if not decal:
        asset.set_editor_property('used_with_skeletal_mesh', True)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, 'GuLi.Construction.Owner', OWNER)
    return asset


def build_hologram():
    asset = material('M_ConstructionHologram')
    tint = vector(asset, (0.025, 0.25, 1.0, 1.0))
    alpha = scalar(asset, 'EffectAlpha', 1.0)
    fresnel = node(asset, 'Fresnel', exponent=3.0, base_reflect_fraction=0.0)
    edge = multiply(asset, fresnel, scalar(asset, 'EdgeOpacity', 0.28))
    opacity = node(asset, 'Add')
    wire(edge, opacity, 'A'); wire(scalar(asset, 'BodyOpacity', 0.14), opacity, 'B')
    output(multiply(asset, opacity, alpha), asset, 'OPACITY')
    output(multiply(asset, tint, scalar(asset, 'Intensity', 1.6)), asset, 'EMISSIVE_COLOR')
    return asset


def build_finish():
    asset = material('M_ConstructionFinishGlow')
    tint = vector(asset, (0.3, 0.65, 1.0, 1.0))
    alpha = scalar(asset, 'EffectAlpha', 1.0)
    fresnel = node(asset, 'Fresnel', exponent=3.0, base_reflect_fraction=0.0)
    output(multiply(asset, fresnel, alpha), asset, 'OPACITY')
    output(multiply(asset, tint, scalar(asset, 'Intensity', 2.5)), asset, 'EMISSIVE_COLOR')
    unreal.EditorAssetLibrary.set_metadata_tag(asset, 'GuLi.Construction.Source',
        '/Game/Assets/ExternalPacks/BuildingGrowthBlueprint/Material/外发光: Fresnel edge with fading opacity')
    return asset


def build_grid():
    asset = material('M_BuildingPlacementGrid', True)
    tint = vector(asset, (0.04, 1.0, 0.12, 1.0))
    position = node(asset, 'WorldPosition')
    size = scalar(asset, 'GridSize', 100.0)
    custom = node(asset, 'Custom')
    inputs = []
    for name in ['P', 'Size']:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property('code', '''float2 cell = P.xy / max(Size, 1.0);
float2 edge = abs(frac(cell + 0.5) - 0.5);
#if PIXELSHADER
float2 aa = max(fwidth(cell), float2(0.0001, 0.0001));
float3 normal = cross(ddx(P), ddy(P));
float ground = saturate((abs(normal.z) / max(length(normal), 0.00001) - 0.6) / 0.2);
#else
float2 aa = float2(0.001, 0.001);
float ground = 1.0;
#endif
float2 gridLine = 1.0 - smoothstep(float2(0.012, 0.012), float2(0.012, 0.012) + aa, edge);
return (0.08 + 0.72 * max(gridLine.x, gridLine.y)) * ground;''')
    wire(position, custom, 'P'); wire(size, custom, 'Size')
    output(custom, asset, 'OPACITY')
    output(tint, asset, 'EMISSIVE_COLOR')
    return asset


def run():
    assert hasattr(unreal.MaterialService, 'get_material_info')
    assert hasattr(unreal.MaterialNodeService, 'get_material_diagnostics')
    unreal.EditorAssetLibrary.make_directory(BASE)
    created = [build_hologram(), build_finish(), build_grid()]
    # The existing placement material now also draws the factory's skeletal door.
    preview = unreal.load_asset('/Game/GuLiStrike/Buildings/Materials/M_BuildingPlacementPreview')
    assert preview
    preview.set_editor_property('used_with_skeletal_mesh', True)
    created.append(preview)
    report = []
    for asset in created:
        M.layout_material_expressions(asset)
        M.recompile_material(asset)
        path = asset.get_path_name()
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False), path
        report.append({'path': path, 'domain': str(asset.get_editor_property('material_domain')),
            'blend': str(asset.get_editor_property('blend_mode')),
            'skeletal': asset.get_editor_property('used_with_skeletal_mesh'),
            'diagnostics': str(unreal.MaterialNodeService.get_material_diagnostics(path))})
    return {'saved_materials': report}


def main():
    try:
        result = run()
        result['success'] = True
    except Exception as error:
        result = {'success': False, 'error': str(error)}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'materials.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=True))


main()
gc.collect()

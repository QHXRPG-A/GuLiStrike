"""Author the shared, UV-independent rust/charred-steel material for destroyed units."""
import json
from pathlib import Path
import unreal

DEST = '/Game/GuLiStrike/FX/UnitFeedback'
PATH = DEST + '/M_UnitWreckRust'
OUT = Path('D:/UE5.7/test1/Artifacts/UnitWreck')
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
MAT = unreal.MaterialEditingLibrary


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def main():
    require(not unreal.WidgetService.is_pie_running(), 'Stop PIE before material authoring')
    OUT.mkdir(parents=True, exist_ok=True)
    existed = ASSETS.does_asset_exist(PATH)
    if existed:
        graph = json.loads(unreal.MaterialNodeService.export_material_graph(PATH))
        (OUT / 'material-before.json').write_text(json.dumps(graph, indent=2), encoding='utf-8')
        # The build is idempotent. Changes to an existing material require inspecting its exported graph.
        obj = ASSETS.load_asset(PATH)
        require(unreal.EditorAssetLibrary.get_metadata_tag(obj, 'GuLi.WreckMaterial.Version') in ('1', '2', '3', 'building-v1') or not graph['expressions'],
                'Existing material is not owned by this builder; inspect material-before.json')
    else:
        require(unreal.MaterialService.create_material('M_UnitWreckRust', DEST).success, 'Create wreck material')
        obj = ASSETS.load_asset(PATH)
    complete = unreal.EditorAssetLibrary.get_metadata_tag(obj, 'GuLi.WreckMaterial.Version') == '3'
    if not complete:
        unreal.EditorAssetLibrary.set_metadata_tag(obj, 'GuLi.WreckMaterial.Version', 'building-v1')
        for expression in list(unreal.ObjectIterator(unreal.MaterialExpression)):
            if expression.get_outer() == obj:
                MAT.delete_material_expression(obj, expression)
        obj.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
        obj.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
        MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)

        def node(cls):
            return MAT.create_material_expression(obj, cls, 0, 0)

        def connect(src, dst, pin):
            require(MAT.connect_material_expressions(src, '', dst, pin), 'Wire ' + pin)

        def scalar(name, value):
            n = node(unreal.MaterialExpressionScalarParameter)
            n.set_editor_property('parameter_name', name)
            n.set_editor_property('default_value', value)
            return n

        def color(name, rgb):
            n = node(unreal.MaterialExpressionVectorParameter)
            n.set_editor_property('parameter_name', name)
            n.set_editor_property('default_value', unreal.LinearColor(*rgb, 1.0))
            return n

        def lerp(a, b, alpha):
            n = node(unreal.MaterialExpressionLinearInterpolate)
            connect(a, n, 'A'); connect(b, n, 'B'); connect(alpha, n, 'Alpha')
            return n

        position = node(unreal.MaterialExpressionPreSkinnedPosition)
        varying = node(unreal.MaterialExpressionVertexInterpolator)
        connect(position, varying, '')
        scale = node(unreal.MaterialExpressionMultiply)
        connect(varying, scale, 'A')
        connect(scalar('RustPatternScale', 0.006), scale, 'B')
        noise = node(unreal.MaterialExpressionCustom)
        inp = unreal.CustomInput()
        inp.set_editor_property('input_name', 'P')
        noise.set_editor_property('inputs', [inp])
        noise.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        noise.set_editor_property('description', 'Local-space iron oxide, pits and soot; no moving world projection')
        noise.set_editor_property('code', '''
struct RustNoise {
    float hash(float3 p) {
        p = frac(p * 0.1031);
        p += dot(p, p.yzx + 33.33);
        return frac((p.x + p.y) * p.z);
    }
    float value(float3 p) {
        float3 i = floor(p), f = frac(p);
        f = f*f*(3.0-2.0*f);
        return lerp(lerp(lerp(hash(i), hash(i+float3(1,0,0)), f.x),
                         lerp(hash(i+float3(0,1,0)), hash(i+float3(1,1,0)), f.x), f.y),
                    lerp(lerp(hash(i+float3(0,0,1)), hash(i+float3(1,0,1)), f.x),
                         lerp(hash(i+float3(0,1,1)), hash(i+float3(1,1,1)), f.x), f.y), f.z);
    }
};
RustNoise N;
float broad = N.value(P * 0.65);
float chips = N.value(P * 3.7 + 7.3);
float grain = N.value(P * 22.0 + 19.1);
float rust = smoothstep(0.25, 0.64, broad * 0.68 + chips * 0.32);
float soot = smoothstep(0.45, 0.70, N.value(P * 0.28 + 31.7));
return float3(rust, saturate(chips * 0.75 + grain * 0.25), soot);
''')
        connect(scale, noise, 'P')
        masks = []
        for channel in ('r', 'g', 'b'):
            n = node(unreal.MaterialExpressionComponentMask)
            for flag in ('r', 'g', 'b', 'a'):
                n.set_editor_property(flag, flag == channel)
            connect(noise, n, '')
            masks.append(n)
        rust, grain, soot = masks
        oxide = lerp(color('DeepOxide', (0.030, 0.018, 0.011)),
                     color('FlakedRust', (0.095, 0.043, 0.021)), grain)
        steel_rust = lerp(color('ExposedSteel', (0.034, 0.035, 0.032)), oxide, rust)
        base = lerp(steel_rust, color('Charcoal', (0.008, 0.009, 0.010)), soot)
        rough = lerp(scalar('SteelRoughness', 0.75), scalar('RustRoughness', 0.94), rust)
        metal = lerp(scalar('SteelMetallic', 0.8), scalar('OxideMetallic', 0.02), rust)
        metal = lerp(metal, scalar('SootMetallic', 0.0), soot)
        for expr, output in [(base, unreal.MaterialProperty.MP_BASE_COLOR),
                             (rough, unreal.MaterialProperty.MP_ROUGHNESS),
                             (metal, unreal.MaterialProperty.MP_METALLIC),
                             (scalar('WreckEmissive', 0.0), unreal.MaterialProperty.MP_EMISSIVE_COLOR)]:
            require(MAT.connect_material_property(expr, '', output), 'Material output ' + str(output))
        MAT.layout_material_expressions(obj)
        MAT.recompile_material(obj)

    diagnostics = unreal.MaterialNodeService.get_material_diagnostics(PATH)
    require(diagnostics.success and diagnostics.is_compiled_ok and not diagnostics.compile_errors, str(diagnostics))
    if not complete:
        unreal.EditorAssetLibrary.set_metadata_tag(obj, 'GuLi.WreckMaterial.Version', '3')
        require(ASSETS.save_loaded_asset(obj), 'Save material')
    graph = json.loads(unreal.MaterialNodeService.export_material_graph(PATH))
    report = {'path': PATH, 'saved': [] if complete else [PATH], 'diagnostics': str(diagnostics),
              'graph': graph, 'description': 'Opaque lit rust/steel/soot, zero emission, mesh-local pattern; shared by ISM and frozen skeletal poses.'}
    (OUT / 'material.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'path': PATH, 'diagnostics': str(diagnostics), 'saved': report['saved']}))


main()

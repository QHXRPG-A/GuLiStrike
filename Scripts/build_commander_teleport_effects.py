"""Author only the project's CommanderTeleport assets; run via ue_exec.py or editor commandlet."""
import json
from pathlib import Path
import unreal

ROOT = '/Game/GuLiStrike/FX/CommanderTeleport'
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
report = {'assets': [], 'diagnostics': {}}

def node(mat, kind):
    return edit.create_material_expression(mat, kind, 0, 0)

def custom(mat, inputs, code, output):
    expr = node(mat, unreal.MaterialExpressionCustom)
    expr.set_editor_property('output_type', output)
    data = []
    for name, source in inputs.items():
        item = unreal.CustomInput(); item.set_editor_property('input_name', name); data.append(item)
    expr.set_editor_property('inputs', data)
    expr.set_editor_property('code', code)
    for name, source in inputs.items():
        assert edit.connect_material_expressions(source, '', expr, name)
    return expr

def scalar(mat, name, value):
    expr = node(mat, unreal.MaterialExpressionScalarParameter)
    expr.set_editor_property('parameter_name', name)
    expr.set_editor_property('default_value', value)
    return expr

for name, ground in [('M_TeleportGround', True), ('M_TeleportBeam', False)]:
    path = ROOT + '/' + name
    mat = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    assert mat
    edit.delete_all_material_expressions(mat)
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL if ground else unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate)
    opacity = scalar(mat, 'Opacity', 1)
    tint = node(mat, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(0, .7, 1, 1))
    if ground:
        progress = scalar(mat, 'Progress', 1)
        # For the downward decal, +V is world north (twelve) and -U is east (three).
        common = 'float2 p=UV*2-1; float r=length(p); float a=frac(atan2(-p.x,p.y)/6.28318530718+1); float core=exp(-pow((r-.94)/.012,2)); float glow=exp(-abs(r-.94)*42); float filled=step(a,Progress)*step(.00001,Progress); '
        mask = custom(mat, {'UV': uv, 'Progress': progress, 'Opacity': opacity}, common + 'return saturate((core*(.18+.82*filled)+glow*.24+step(r,.94)*.045)*Opacity);', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        color = custom(mat, {'UV': uv, 'Progress': progress, 'Tint': tint}, common + 'return Tint.rgb*(.55+glow*3+core*filled*5)+float3(1,1,1)*core*filled*5;', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    else:
        normal = node(mat, unreal.MaterialExpressionPixelNormalWS)
        camera = node(mat, unreal.MaterialExpressionCameraVectorWS)
        softness = scalar(mat, 'EdgeSoftness', .32)
        mask = custom(mat, {'UV': uv, 'Normal': normal, 'Camera': camera, 'Opacity': opacity, 'EdgeSoftness': softness}, 'float side=1-smoothstep(.72,.92,abs(Normal.z)); float facing=saturate(abs(dot(normalize(Normal),normalize(Camera)))); float softEdge=smoothstep(0,max(.04,EdgeSoftness),facing); float vertical=smoothstep(0,.08,UV.y)*(.16+.84*pow(saturate(1-UV.y),.5)); return side*softEdge*(.035+.24*facing)*Opacity*vertical;', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        color = custom(mat, {'Tint': tint}, 'return Tint.rgb*8;', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    assert edit.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert edit.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY)
    edit.layout_material_expressions(mat)
    edit.recompile_material(mat)
    assert assets.save_asset(path, only_if_is_dirty=False)
    report['assets'].append(path)
    report['diagnostics'][path] = str(unreal.MaterialNodeService.get_material_diagnostics(path))

body_path = ROOT + '/M_TeleportBody'
body = assets.load_asset(body_path) if assets.does_asset_exist(body_path) else tools.create_asset('M_TeleportBody', ROOT, unreal.Material, unreal.MaterialFactoryNew())
edit.delete_all_material_expressions(body)
body.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
body.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
body.set_editor_property('two_sided', False)
edit.set_material_usage(body, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
edit.set_material_usage(body, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
normal = node(body, unreal.MaterialExpressionPixelNormalWS)
camera = node(body, unreal.MaterialExpressionCameraVectorWS)
mask = custom(body, {'N': normal, 'V': camera}, 'return .24+.28*pow(1-abs(dot(N,V)),3);', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
color = custom(body, {'N': normal, 'V': camera}, 'float f=pow(1-abs(dot(N,V)),3); return float3(.01,.4,.95)*(1.4+f*3);', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
assert edit.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY)
assert edit.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.layout_material_expressions(body); edit.recompile_material(body)
assert assets.save_asset(body_path, only_if_is_dirty=False)
report['assets'].append(body_path)
report['diagnostics'][body_path] = str(unreal.MaterialNodeService.get_material_diagnostics(body_path))

mesh_path = ROOT + '/SM_TeleportCylinder'
if not assets.does_asset_exist(mesh_path):
    assert assets.duplicate_asset('/Engine/BasicShapes/Cylinder', mesh_path)
assert assets.save_asset(mesh_path, only_if_is_dirty=False)
report['assets'].append(mesh_path)
out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'outputs/teleport'
out.mkdir(parents=True, exist_ok=True)
(out/'effect_authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

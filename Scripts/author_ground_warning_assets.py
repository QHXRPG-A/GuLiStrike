"""Author the reusable ground-warning material/style; never changes combat references."""
import json
from pathlib import Path
import unreal

import sys
from pathlib import Path
sys.path.insert(0, str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "Scripts/Vfx"))
from vfx_registry import vfx_id, resource as vfx_resource, scale as vfx_scale, require_id, visual_variant

ROOT = '/Game/GuLiStrike/FX/GroundWarning'
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
edit = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
path = ROOT + '/M_GroundWarning_Circle'
existing = [str(p) for p in assets.list_assets(ROOT, recursive=True, include_folder=False)]
if not assets.does_asset_exist(path):
    assert unreal.MaterialService.create_material('M_GroundWarning_Circle', ROOT).success
mat = assets.load_asset(path)
assert mat
edit.delete_all_material_expressions(mat)
mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('two_sided', True)

def node(cls):
    return edit.create_material_expression(mat, cls, 0, 0)

def scalar(name, value):
    result = node(unreal.MaterialExpressionScalarParameter)
    result.set_editor_property('parameter_name', name)
    result.set_editor_property('default_value', value)
    return result

def custom(inputs, code, output):
    result = node(unreal.MaterialExpressionCustom)
    result.set_editor_property('output_type', output)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    result.set_editor_property('inputs', pins)
    result.set_editor_property('code', code)
    for name, source in inputs.items():
        output_pin = 'RGBA' if isinstance(source, unreal.MaterialExpressionVectorParameter) else ''
        assert edit.connect_material_expressions(source, output_pin, result, name)
    return result

uv = node(unreal.MaterialExpressionTextureCoordinate)
tint = node(unreal.MaterialExpressionVectorParameter)
tint.set_editor_property('parameter_name', 'Tint')
tint.set_editor_property('default_value', unreal.LinearColor(1, .025, .015, 1))
age = scalar('Age', 0)
period = scalar('WavePeriod', .8)
width = scalar('RingWidth', .025)
opacity = scalar('Opacity', .85)
mask = custom({'UV': uv, 'Age': age, 'WavePeriod': period, 'RingWidth': width,
               'Opacity': opacity, 'Tint': tint}, '''
float r = length(UV * 2 - 1) / .96;
float aa = max(fwidth(r), .001);
float inside = 1 - smoothstep(1-aa, 1+aa, r);
float ring = smoothstep(1-RingWidth-aa, 1-RingWidth+aa, r) * inside;
float phase = Age / max(.05, WavePeriod);
float p0 = frac(phase), p1 = frac(phase + .5);
float w0 = (1-smoothstep(.012, .025+aa, abs(r-p0))) * smoothstep(0,.12,p0) * (1-smoothstep(.68,1,p0));
float w1 = (1-smoothstep(.012, .025+aa, abs(r-p1))) * smoothstep(0,.12,p1) * (1-smoothstep(.68,1,p1));
return saturate((ring + inside*(.045 + .42*(w0+w1))) * Opacity * Tint.a);
''', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
color = custom({'Tint': tint}, 'return Tint.rgb * 1.3;', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
assert edit.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY)
assert edit.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.layout_material_expressions(mat)
assert unreal.MaterialService.compile_material(path)
diagnostics = unreal.MaterialNodeService.get_material_diagnostics(path)
assert diagnostics.success and diagnostics.is_compiled_ok, str(diagnostics)
assert unreal.MaterialService.save_material(path)

style_path = ROOT + '/DA_GroundWarning_Red'
style = assets.load_asset(style_path) if assets.does_asset_exist(style_path) else tools.create_asset(
    'DA_GroundWarning_Red', ROOT, unreal.GuLiGroundWarningStyle, unreal.DataAssetFactory())
assert style
for name, value in [('material_vfx_id', vfx_id('GroundWarning')), ('wave_period', .8), ('ring_width', .025),
                    ('opacity', .85), ('projection_depth', 80.)]:
    style.set_editor_property(name, value)
assert assets.save_asset(style_path, only_if_is_dirty=False)
report = {'existing_assets': existing, 'material': path, 'style': style_path,
          'diagnostics': str(diagnostics), 'material_info': str(unreal.MaterialService.get_material_info(path)),
          'style_readback': {k: str(style.get_editor_property(k)) for k in ['material_vfx_id', 'wave_period', 'ring_width', 'opacity', 'projection_depth']},
          'combat_reference_changed': False, 'reference_approval': 'A1 approved by user 2026-09-17'}
out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'outputs/wm01_q'
out.mkdir(parents=True, exist_ok=True)
(out / 'ground-warning-authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

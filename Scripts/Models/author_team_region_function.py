"""Create the independent common team-region function; no formal model/material writes."""
import json
import sys
from pathlib import Path
import unreal

DEST = globals().pop('GULI_TEAM_FUNCTION_DEST', '/Game/GuLiStrike/Review/ModelInterface/Shared')
NAME = globals().pop('GULI_TEAM_FUNCTION_NAME', 'MF_GuLiTeamRegion')
PATH = DEST + '/' + NAME
lib = unreal.MaterialEditingLibrary
sys.path.insert(0,str(Path(r'D:/UE5.7/test1/Scripts')))
from Models import model_catalog
defaults={b['ParameterName']:b for b in model_catalog.rows('MaterialParameters') if b['ModelId']==1001 and b['Driver']=='CPD'}
function = unreal.load_asset(PATH)
if not function:
    function = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DEST, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
else:
    if unreal.EditorAssetLibrary.get_metadata_tag(function,'GuLi.ModelApproved')=='1':
        raise RuntimeError('Approved shared function is frozen; author a new interface version')
# UE5.7's helper removes entries while iterating the same array and can leave
# every other node behind. Repeat until empty so pin names never become ambiguous.
while lib.get_num_material_expressions_in_function(function):
    before = lib.get_num_material_expressions_in_function(function)
    lib.delete_all_material_expressions_in_function(function)
    if lib.get_num_material_expressions_in_function(function) >= before:
        raise RuntimeError('Unable to clear old function expressions safely')

def connect(source, output, target, input_name):
    if not lib.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError('Rejected function connection: '+target.get_name()+'/'+input_name)

def node(cls, x, y):
    return lib.create_material_expression_in_function(function, cls, x, y)

base = node(unreal.MaterialExpressionFunctionInput, -900, -100)
base.set_editor_property('input_name', 'FixedPalette')
base.set_editor_property('input_type', unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3)
alpha = node(unreal.MaterialExpressionFunctionInput, -900, 80)
alpha.set_editor_property('input_name', 'RoleAlpha')
alpha.set_editor_property('input_type', unreal.FunctionInputType.FUNCTION_INPUT_SCALAR)
params = []
for name, vector, index, y in [('GuLi_TeamPrimary', True, 8, 220), ('GuLi_TeamSecondary', True, 12, 380), ('GuLi_TeamEnabled', False, 16, 540), ('GuLi_TeamLightStrength', False, 17, 700)]:
    p = node(unreal.MaterialExpressionVectorParameter if vector else unreal.MaterialExpressionScalarParameter, -900, y)
    p.set_editor_property('parameter_name', name)
    p.set_editor_property('use_custom_primitive_data', True)
    p.set_editor_property('primitive_data_index', index)
    d=defaults[name]
    p.set_editor_property('default_value',unreal.LinearColor(d['DefaultR'],d['DefaultG'],d['DefaultB'],d['DefaultA']) if vector else d['DefaultScalar'])
    params.append(p)
inputs = [('Base', base, ''), ('RoleAlpha', alpha, ''), ('Primary', params[0], ''), ('Secondary', params[1], ''), ('Enabled', params[2], ''), ('LightStrength', params[3], '')]
def custom_input(name):
    value = unreal.CustomInput()
    value.set_editor_property('input_name', name)
    return value
palette = node(unreal.MaterialExpressionCustom, -400, 0)
palette.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
palette.set_editor_property('inputs', [custom_input(n) for n, _, _ in inputs])
palette.set_editor_property('code', '''float role=floor(saturate(RoleAlpha)*255.0+0.5);
float primary=step(abs(role-3.0),0.1)+step(abs(role-7.0),0.1);
float secondary=step(abs(role-4.0),0.1);
float3 gray=float3(0.02518686,0.03820437,0.03560131);
float3 team=primary>0.5?Primary.rgb:Secondary.rgb;
// B_v1 preserves source RGB as data; its face roles select the approved paint.
// The source RGB is already linear and must never undergo another sRGB decode.
float3 fixedLinear=Base.rgb;
if(abs(role-0.0)<0.1) fixedLinear=float3(0.9911020971,0.7758222183,0.6938717613);
else if(abs(role-1.0)<0.1) fixedLinear=float3(0.6653872983,0.5271151257,0.3324515363);
else if(abs(role-2.0)<0.1) fixedLinear=gray;
else if(abs(role-5.0)<0.1) fixedLinear=float3(0.8549926081,0.3371636150,0.0975873471);
else if(abs(role-6.0)<0.1) fixedLinear=float3(0.0040247170,0.2788942635,0.3185468018);
return (primary+secondary)>0.5?(Enabled>0.5?team:gray):fixedLinear;''')
for n, source, output in inputs: connect(source, output, palette, n)
lamp = node(unreal.MaterialExpressionCustom, -400, 500)
lamp.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
lamp.set_editor_property('inputs', [custom_input(n) for n in ['RoleAlpha', 'Enabled', 'Strength']])
lamp.set_editor_property('code', 'float role=floor(saturate(RoleAlpha)*255.0+0.5);return step(abs(role-7.0),0.1)*step(0.5,Enabled)*max(0.0,Strength);')
for name, source in [('RoleAlpha', alpha), ('Enabled', params[2]), ('Strength', params[3])]: connect(source, '', lamp, name)
for name, source, y in [('PaletteBeforeThreeTone', palette, 0), ('TeamLampStrength', lamp, 300)]:
    output = node(unreal.MaterialExpressionFunctionOutput, 200, y)
    output.set_editor_property('output_name', name)
    # FunctionOutput has an unnamed input, unlike Add/Multiply's A input.
    if not lib.connect_material_expressions(source, '', output, ''):
        raise RuntimeError('Missing material-function output connection: '+name)
lib.update_material_function(function)
unreal.EditorAssetLibrary.save_loaded_asset(function)
loaded = unreal.load_asset(PATH)
report = {'function': loaded.get_path_name(), 'cpd': [8, 12, 16, 17], 'native_compile': False,
          'formal_assets_modified': False, 'shader_stage': 'palette BEFORE existing three-tone/line work; lamp strength separate',
          'roles': {'3':'team primary', '4':'team secondary', '7':'team primary lamp'}, 'status':'saved and read back'}
out = Path(r'D:/UE5.7/test1/ArtSource/ModelInterface_B_20261008')
out.mkdir(parents=True,exist_ok=True)
(out / 'material-function-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report))

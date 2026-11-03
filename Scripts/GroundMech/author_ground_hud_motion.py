"""Import only the approved HUD short atlases, two UI materials and HUD table."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE=ROOT/'ArtSource/UI/GroundMech/HUD_Motion_20261102'
OUT=ROOT/'data/GroundHUDValidation/Motion20261102'
OUT.mkdir(parents=True,exist_ok=True)
assert not unreal.WidgetService.is_pie_running(), 'Stop PIE before authoring HUD assets'
manifest=json.loads((SOURCE/'manifest.json').read_text(encoding='utf-8'))
M=unreal.MaterialEditingLibrary
report={'textures':[], 'materials':[], 'visual_review':'not_performed'}

for clip in manifest['clips']:
    path=clip['asset'];task=unreal.AssetImportTask()
    task.filename=str(ROOT/clip['png']);task.destination_path=path.rsplit('/',1)[0]
    task.destination_name=path.rsplit('/',1)[1];task.automated=True;task.replace_existing=True;task.save=False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture=unreal.load_asset(path);assert texture
    texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('srgb',True)
    texture.set_editor_property('filter',unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property('never_stream',True)
    texture.set_editor_property('max_texture_size',8192)
    texture.set_editor_property('lod_bias',0)
    assert unreal.EditorAssetLibrary.save_loaded_asset(texture)
    size=[texture.blueprint_get_size_x(),texture.blueprint_get_size_y()]
    assert size==clip['atlas_size'],(path,size,clip['atlas_size'])
    report['textures'].append({'asset':texture.get_path_name(),'size':size,'rgba_bytes':clip['rgba_bytes']})

def material(name,code,defaults,texture_key):
    path=f'/Game/GuLiStrike/GroundMech/UI/Motion/Materials/M_GroundHUD_{name}'
    asset=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit('/',1)[1],path.rsplit('/',1)[0],unreal.Material,unreal.MaterialFactoryNew())
    assert asset
    asset.modify();asset.set_editor_property('material_domain',unreal.MaterialDomain.MD_UI)
    asset.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    M.delete_all_material_expressions(asset)
    def node(cls,**props):
        n=M.create_material_expression(asset,cls,-600,0)
        for k,v in props.items():n.set_editor_property(k,v)
        return n
    inputs={'UV':node(unreal.MaterialExpressionTextureCoordinate)}
    texture=unreal.load_asset(next(c['asset'] for c in manifest['clips'] if c['key']==texture_key))
    inputs['Atlas']=node(unreal.MaterialExpressionTextureObjectParameter,parameter_name='Atlas',texture=texture)
    inputs['Tint']=node(unreal.MaterialExpressionVectorParameter,parameter_name='Tint',default_value=unreal.LinearColor(.004,.279,.319,1))
    inputs['Panel']=node(unreal.MaterialExpressionVectorParameter,parameter_name='Panel',default_value=unreal.LinearColor(.0006,.0086,.008,1))
    for key,value in defaults.items():inputs[key]=node(unreal.MaterialExpressionScalarParameter,parameter_name=key,default_value=value)
    custom=node(unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,description='Ground HUD source-sequence playback; no material Time node')
    pins=[]
    for name in inputs:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    for key,n in inputs.items():assert M.connect_material_expressions(n,'',custom,key)
    for channels,prop in [(('r','g','b'),unreal.MaterialProperty.MP_EMISSIVE_COLOR),(('a',),unreal.MaterialProperty.MP_OPACITY)]:
        mask=node(unreal.MaterialExpressionComponentMask)
        for channel in ('r','g','b','a'):mask.set_editor_property(channel,channel in channels)
        assert M.connect_material_expressions(custom,'',mask,'')
        assert M.connect_material_property(mask,'',prop)
    M.recompile_material(asset)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
    report['materials'].append({'asset':asset.get_path_name(),'domain':str(asset.get_editor_property('material_domain')),
        'blend':str(asset.get_editor_property('blend_mode')),'scalars':list(map(str,M.get_scalar_parameter_names(asset))),
        'textures':list(map(str,M.get_texture_parameter_names(asset)))})

material('Atlas',r'''
float f=clamp(Frame,0,LastFrame),a=floor(f),b=min(a+1,LastFrame);
float2 grid=float2(Columns,Rows),pad=float2(PaddingX,PaddingY);
float2 local=pad+saturate(UV)*(1-2*pad);
float2 u=(float2(fmod(a,Columns),floor(a/Columns))+local)/grid;
float2 v=(float2(fmod(b,Columns),floor(b/Columns))+local)/grid;
float4 A=Texture2DSample(Atlas,AtlasSampler,u),B=Texture2DSample(Atlas,AtlasSampler,v);
float alpha=lerp(A.a,B.a,frac(f));
float3 rgb=lerp(A.rgb*A.a,B.rgb*B.a,frac(f))/max(alpha,0.00001);
return float4(rgb*Tint.rgb,alpha*saturate(Opacity));
''',{'Frame':0,'LastFrame':18,'Columns':4,'Rows':5,'PaddingX':2/1028,'PaddingY':2/580,'Opacity':1},'Intro')
material('Cooldown',r'''
float2 local=float2(PaddingX,PaddingY)+UV*(1-2*float2(PaddingX,PaddingY));
float ring=Texture2DSample(Atlas,AtlasSampler,local).a;
float2 d=UV-0.5;
float angle=frac(atan2(d.x,-d.y)/6.28318530718+1);
float completed=step(angle,saturate(Progress));
float shade=(1-completed)*0.72;
float edge=ring*lerp(0.22,1.0,completed);
float alpha=edge+shade*(1-edge);
float3 rgb=(Tint.rgb*edge+Panel.rgb*shade*(1-edge))/max(alpha,0.00001);
return float4(rgb,alpha*saturate(Opacity));
''',{'Progress':0,'PaddingX':2/260,'PaddingY':2/260,'Opacity':1},'Cooldown')

table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements')
raw=(ROOT/'data/Json/DT_GuLiStrikeGroundHUD_Elements.json').read_text(encoding='utf-8-sig')
problems=unreal.GuLiDataTableImportLibrary.update_from_json(table,raw)
assert not problems, list(problems)
assert unreal.EditorAssetLibrary.save_loaded_asset(table)
actual=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
expected=json.loads(raw);assert len(actual)==len(expected)
import math
for a,b in zip(actual,expected):
    for key in b:
        if key=='Texture':assert (a[key] if a[key]!='None' else '')==b[key],(a['Name'],key)
        elif key=='Value':assert math.isclose(a[key],b[key],rel_tol=1e-5,abs_tol=1e-7),(a['Name'],key)
        else:assert a[key]==b[key],(a['Name'],key)
report['table_rows']=len(actual);report['rgba_mib']=manifest['rgba_mib']
report['saved']=True
(OUT/'asset-import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

"""Import generated icons and source-derived loops. No screenshots or visual QA."""
import json
import importlib
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE=ROOT/'ArtSource/UI/GroundMech/HUD_Revision_20261103'
sys.path.insert(0,str(ROOT/'Scripts/GroundMech'))
import_catalog=importlib.reload(importlib.import_module('icon_assets')).import_catalog
OUT=ROOT/'Artifacts/GroundHUDRevision_20261103'
OUT.mkdir(parents=True,exist_ok=True)
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
loops=json.loads((SOURCE/'loop_manifest.json').read_text(encoding='utf-8'))['clips']
report={'textures':import_catalog(ROOT),'visual_acceptance':'pending_user'}
for entry in loops:
    path=entry['asset'];assert path not in dirty,'Preserve unsaved edits: '+path
    task=unreal.AssetImportTask();task.filename=str(ROOT/entry['png']);task.destination_path=path.rsplit('/',1)[0]
    task.destination_name=path.rsplit('/',1)[1];task.automated=True;task.replace_existing=True;task.save=False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture=assets.load_asset(path);assert isinstance(texture,unreal.Texture2D),path
    for prop,value in [('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON),('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI),
            ('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS),('srgb',True),('filter',unreal.TextureFilter.TF_BILINEAR),
            ('address_x',unreal.TextureAddress.TA_CLAMP),('address_y',unreal.TextureAddress.TA_CLAMP),('never_stream',True),
            ('max_texture_size',256 if entry.get('icon') else 2048),('lod_bias',0)]:texture.set_editor_property(prop,value)
    assert assets.save_loaded_asset(texture)
    report['textures'].append({'path':texture.get_path_name(),'size':[texture.blueprint_get_size_x(),texture.blueprint_get_size_y()],
        'max_size':texture.get_editor_property('max_texture_size'),'source':entry['png']})

path='/Game/GuLiStrike/GroundMech/UI/Motion/Materials/M_GroundHUD_Ambient'
assert path not in dirty
material=assets.load_asset(path) or unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_GroundHUD_Ambient',path.rsplit('/',1)[0],unreal.Material,unreal.MaterialFactoryNew())
M=unreal.MaterialEditingLibrary;material.modify();material.set_editor_property('material_domain',unreal.MaterialDomain.MD_UI)
material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);M.delete_all_material_expressions(material)
def node(cls,**props):
    n=M.create_material_expression(material,cls,-600,0)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
inputs={'UV':node(unreal.MaterialExpressionTextureCoordinate),'Time':node(unreal.MaterialExpressionTime,ignore_pause=False),
        'Atlas':node(unreal.MaterialExpressionTextureObjectParameter,parameter_name='Atlas',texture=assets.load_asset(loops[0]['asset'])),
        'Tint':node(unreal.MaterialExpressionVectorParameter,parameter_name='Tint',default_value=unreal.LinearColor(.004,.279,.319,1))}
defaults={'LastFrame':47,'Columns':4,'Rows':12,'PaddingX':.02,'PaddingY':.02,'Opacity':.18,'Period':4,'SeamSeconds':.25,
          'Run':0,'StartTime':0,'PausedPhase':0,'PhaseOffset':0}
for name,value in defaults.items():inputs[name]=node(unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
code='''float period=max(Period,1.0);
float t=frac((PausedPhase+max(Time-StartTime,0.0)*Run+PhaseOffset)/period)*period;
float f=(t/period)*LastFrame,a=floor(f),b=min(a+1,LastFrame);
float2 grid=float2(Columns,Rows),pad=float2(PaddingX,PaddingY);
float2 local=pad+saturate(UV)*(1-2*pad);
float2 u=(float2(fmod(a,Columns),floor(a/Columns))+local)/grid;
float2 v=(float2(fmod(b,Columns),floor(b/Columns))+local)/grid;
float4 A=Texture2DSample(Atlas,AtlasSampler,u),B=Texture2DSample(Atlas,AtlasSampler,v);
float4 H=Texture2DSample(Atlas,AtlasSampler,local/grid);
A.rgb*=A.a;B.rgb*=B.a;H.rgb*=H.a;
float4 C=lerp(A,B,frac(f));float blend=smoothstep(period-max(SeamSeconds,.01),period,t);
C=lerp(C,H,blend);
float corner=(1-smoothstep(.46,.49,UV.x))*(1-smoothstep(.46,.49,UV.y));
return float4(C.rgb/max(C.a,.00001)*Tint.rgb,C.a*saturate(Opacity)*corner);'''
custom=node(unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,description='HUDuiPRO source loop; bounded corner decoration; clock updates only on pause/resume')
pins=[]
for name in inputs:
    pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
custom.set_editor_property('inputs',pins)
for name,n in inputs.items():assert M.connect_material_expressions(n,'',custom,name)
for channels,prop in [(('r','g','b'),unreal.MaterialProperty.MP_EMISSIVE_COLOR),(('a',),unreal.MaterialProperty.MP_OPACITY)]:
    mask=node(unreal.MaterialExpressionComponentMask)
    for c in ('r','g','b','a'):mask.set_editor_property(c,c in channels)
    assert M.connect_material_expressions(custom,'',mask,'') and M.connect_material_property(mask,'',prop)
M.recompile_material(material)
diagnostic=unreal.MaterialNodeService.get_material_diagnostics(path)
assert diagnostic.success and diagnostic.is_compiled_ok,str(diagnostic)
assert assets.save_loaded_asset(material)
report['material']={'path':material.get_path_name(),'diagnostics':str(diagnostic),'scalars':list(map(str,M.get_scalar_parameter_names(material)))}
(OUT/'hud_asset_import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'textures':len(report['textures']),'report':str(OUT/'hud_asset_import.json')}))

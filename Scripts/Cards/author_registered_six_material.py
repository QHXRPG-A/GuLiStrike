"""Create the shared catalog-owned six-layer, rigid XY parallax material in UE."""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
OUT=ROOT/'Artifacts/CardArtworkPipeline/GroundMechFourCards_20261117'
DEST='/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardArtwork_RegisteredSix_v1'


def main(destination=None, normal_draw_order=None):
    dest=destination or DEST
    order=list(normal_draw_order or [6,5,4,3,2,1])
    assert sorted(order)==list(range(1,7)), 'Expected each of the six layers once'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert dest not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    lib,edit=unreal.EditorAssetLibrary,unreal.MaterialEditingLibrary
    material=unreal.load_asset(dest)
    if material:
        assert lib.get_metadata_tag(material,'GuLi.CardArtwork.Template')=='RegisteredSixRigidLayer3Mask'
    else:
        material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(dest.rsplit('/',1)[1],dest.rsplit('/',1)[0],unreal.Material,unreal.MaterialFactoryNew())
    assert material
    lib.set_metadata_tag(material,'GuLi.CardArtwork.Template','RegisteredSixRigidLayer3Mask')
    edit.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided',False)
    nodes=[]
    def node(cls,**props):
        n=edit.create_material_expression(material,cls,-1800+(len(nodes)%4)*350,(len(nodes)//4)*130)
        for key,value in props.items(): n.set_editor_property(key,value)
        nodes.append(n);return n
    def scalar(name,value):return node(unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
    def vector(name,value):return node(unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*value))
    white=unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
    assert white
    uv=node(unreal.MaterialExpressionTextureCoordinate)
    camera=node(unreal.MaterialExpressionCameraPositionWS)
    pivot=node(unreal.MaterialExpressionActorPositionWS)
    delta=node(unreal.MaterialExpressionSubtract)
    assert edit.connect_material_expressions(camera,'',delta,'A') and edit.connect_material_expressions(pivot,'',delta,'B')
    tangent=node(unreal.MaterialExpressionTransform,transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    assert edit.connect_material_expressions(delta,'',tangent,'Input') or edit.connect_material_expressions(delta,'',tangent,'')
    inputs={'UV':uv,'Camera':tangent,'Strength':scalar('Layers global depth',.75),
        'Travel':vector('Parallax Travel',(.02,.02,0,0)), 'TiltTangent':scalar('Parallax Tilt Tangent',.2867453858),
        'Aperture':vector('Artwork Aperture',(0,0,1,1)), 'Split':scalar('Layer 3 Split Enabled',0)}
    for i in range(1,7):
        inputs[f'T{i}']=node(unreal.MaterialExpressionTextureObjectParameter,parameter_name=f'Layer {i} Map',texture=white)
        inputs[f'D{i}']=scalar(f'Layer {i} Depth',0)
        inputs[f'O{i}']=scalar(f'Layer {i} Opacity',1)
        inputs[f'S{i}']=vector(f'Layer {i} Scale',(1,1,1,1))
        inputs[f'B{i}']=vector(f'Layer {i} Offset',(0,0,0,0))
    mask_path='/Game/GuLiStrike/CardSystem/RevealDemo/Textures/T_CardArtwork_WhiteMask'
    white_mask=unreal.load_asset(mask_path) or lib.duplicate_asset(white.get_path_name(),mask_path)
    assert white_mask
    white_mask.set_editor_property('srgb',False)
    white_mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    assert lib.save_loaded_asset(white_mask,False)
    inputs['Mask']=node(unreal.MaterialExpressionTextureObjectParameter,parameter_name='Foreground Front Mask',texture=white_mask,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    full_aperture=node(unreal.MaterialExpressionAppendVector)
    assert edit.connect_material_expressions(inputs['Aperture'],'',full_aperture,'A')
    assert edit.connect_material_expressions(inputs['Aperture'],'A',full_aperture,'B')
    inputs['Aperture']=full_aperture
    code=['float2 motion=clamp(Camera.xy/max(abs(Camera.z),0.05)/max(TiltTangent,0.001),-1.0,1.0)*Travel.xy*Strength;']
    for i in range(1,7):
        code += [f'float2 p{i}=(UV.xy-0.5-B{i}.xy)/S{i}.xy+0.5-motion*D{i};',
            f'float4 c{i}=Texture2DSample(T{i},T{i}Sampler,saturate(p{i}));',
            f'c{i}.a*=O{i}*step(0,p{i}.x)*step(p{i}.x,1)*step(0,p{i}.y)*step(p{i}.y,1);']
    code += ['float m=saturate(Texture2DSample(Mask,MaskSampler,saturate(p3)).r);', 'float3 normal=float3(0,0,0);']
    for i in order:
        code.append(f'normal=lerp(normal,c{i}.rgb,c{i}.a);')
    code += [
        'float3 split=c6.rgb*c6.a;', 'split=lerp(split,c3.rgb,c3.a*(1-m));',
        'split=lerp(split,c4.rgb,c4.a);','split=lerp(split,c5.rgb,c5.a);','split=lerp(split,c3.rgb,c3.a*m);',
        'split=lerp(split,c2.rgb,c2.a);','split=lerp(split,c1.rgb,c1.a);',
        'float3 color=lerp(normal,split,saturate(Split));',
        'float clip=step(Aperture.x,UV.x)*step(UV.x,Aperture.z)*step(Aperture.y,UV.y)*step(UV.y,Aperture.w);',
        'return float4(color,clip);']
    entries=[]
    for name in inputs:
        value=unreal.CustomInput();value.set_editor_property('input_name',name);entries.append(value)
    custom=node(unreal.MaterialExpressionCustom,desc='CATALOG_REGISTERED_SIX_COMPOSITE',inputs=entries,
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,code='\n'.join(code))
    for name,source in inputs.items():assert edit.connect_material_expressions(source,'',custom,name),name
    rgb=node(unreal.MaterialExpressionComponentMask,r=True,g=True,b=True,a=False)
    alpha=node(unreal.MaterialExpressionComponentMask,r=False,g=False,b=False,a=True)
    assert edit.connect_material_expressions(custom,'',rgb,'Input') or edit.connect_material_expressions(custom,'',rgb,'')
    assert edit.connect_material_expressions(custom,'',alpha,'Input') or edit.connect_material_expressions(custom,'',alpha,'')
    assert edit.connect_material_property(rgb,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert edit.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    edit.recompile_material(material)
    lib.set_metadata_tag(material,'GuLi.CardArtwork.Template','RegisteredSixRigidLayer3Mask')
    lib.set_metadata_tag(material,'GuLi.CardArtwork.DrawOrder',json.dumps(order))
    assert lib.save_loaded_asset(material,False)
    return {'success':True,'material':dest,'draw_order':order,'diagnostics':str(unreal.MaterialNodeService.get_material_diagnostics(dest)),
        'scalar_parameters':list(map(str,edit.get_scalar_parameter_names(material))),
        'vector_parameters':list(map(str,edit.get_vector_parameter_names(material))),
        'texture_parameters':list(map(str,edit.get_texture_parameter_names(material))),
        'native_build':False,'assistant_visual_test':False}


if __name__=='__main__':
    try: report=main()
    except Exception: report={'success':False,'error':traceback.format_exc()}
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'material-authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))

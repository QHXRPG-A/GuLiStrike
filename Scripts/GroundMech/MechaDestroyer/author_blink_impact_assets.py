"""Approved A_v1 -> editable B1 assets. Editor authoring/readback only, no PIE."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/BlinkImpact_B1_20261026'
BASE='/Game/GuLiStrike/FX/GroundMech/BlinkImpact_B1'
LIB=unreal.EditorAssetLibrary
EDIT=unreal.MaterialEditingLibrary

def color(value):
    c=[int(value[i:i+2],16)/255 for i in (0,2,4)]
    return unreal.LinearColor(*[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c],1)

def make_material(name,kind):
    path=BASE+'/'+name
    mat=unreal.load_asset(path) or unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,BASE,unreal.Material,unreal.MaterialFactoryNew())
    mat.modify()
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if kind=='ghost' else unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('disable_depth_test',False)
    mat.set_editor_property('used_with_skeletal_mesh',kind=='ghost')
    mat.set_editor_property('used_with_niagara_sprites',kind=='impact')
    EDIT.delete_all_material_expressions(mat)
    params={}
    for i,(key,hexcode) in enumerate([('Tint','6AA4BE')] if kind=='ghost' else [('Heat','EE9D58'),('Core','FEE4D9')]):
        node=EDIT.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-600,150*i)
        node.set_editor_property('parameter_name',key); node.set_editor_property('default_value',color(hexcode)); params[key]=node
    if kind=='ghost':
        node=EDIT.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-600,300)
        node.set_editor_property('parameter_name','Opacity'); node.set_editor_property('default_value',.55); params['Opacity']=node
        params['Rim']=EDIT.create_material_expression(mat,unreal.MaterialExpressionFresnel,-600,450)
        code='float rim=saturate(Rim); return float4(Tint.rgb*(0.7+rim*2.0), saturate(Opacity)*(0.30+rim*0.55));'
    else:
        params['UV']=EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate,-600,350)
        params['Age']=EDIT.create_material_expression(mat,unreal.MaterialExpressionParticleRelativeTime,-600,500)
        code='''
float t=saturate(Age);
float2 p=(UV-0.5)*2.0;
float r=length(p), a=atan2(p.y,p.x);
float expansion=lerp(0.65,1.0,saturate(t*4.0));
float teeth=pow(saturate(cos(a*7.0+0.7)),10.0);
float radius=(0.24+0.43*teeth)*expansion;
float aa=max(fwidth(r),0.002);
float burst=1.0-smoothstep(radius-aa,radius+aa,r);
float core=1.0-smoothstep(radius*0.42-aa,radius*0.42+aa,r);
float ringRadius=lerp(0.22,0.88,t);
float ring=(1.0-smoothstep(0.018,0.035,abs(r-ringRadius)))*(1.0-t)*0.35;
float chips=0.0;
for(int i=0;i<6;i++) {
 float angle=i*1.0472+0.26; float2 dir=float2(cos(angle),sin(angle));
 float2 pos=dir*lerp(0.46,0.91,t);
 float2 v=p-pos; float d=max(abs(dot(v,dir))*0.62,abs(dot(v,float2(-dir.y,dir.x))));
 chips=max(chips,1.0-smoothstep(0.021,0.028,d));
}
float fade=1.0-smoothstep(0.0,1.0,t);
float mask=saturate(burst+ring+chips*0.65)*fade;
return float4((Heat.rgb*(burst+ring+chips)+Core.rgb*core*1.35)*5.0,mask);
'''
    custom=EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom,-150,0)
    pins=[]
    for key in params:
        pin=unreal.CustomInput(); pin.set_editor_property('input_name',key); pins.append(pin)
    custom.set_editor_property('inputs',pins); custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description','Ground BlinkImpact B1: frozen ghost' if kind=='ghost' else 'Ground orange impact: normalized lifetime fade, angular burst and sparse chips')
    custom.set_editor_property('code',code)
    for key,node in params.items(): assert EDIT.connect_material_expressions(node,'',custom,key)
    for channels,prop,y in [(('r','g','b'),unreal.MaterialProperty.MP_EMISSIVE_COLOR,0),(('a',),unreal.MaterialProperty.MP_OPACITY,180)]:
        mask=EDIT.create_material_expression(mat,unreal.MaterialExpressionComponentMask,160,y)
        for c in 'rgba': mask.set_editor_property(c,c in channels)
        assert EDIT.connect_material_expressions(custom,'',mask,'') and EDIT.connect_material_property(mask,'',prop)
    EDIT.recompile_material(mat); assert LIB.save_loaded_asset(mat,False)
    (OUT/(name+'.hlsl')).write_text(code,encoding='utf8')
    return mat

def author(diameter):
    raise RuntimeError('Superseded: user requires the original impact shape. Run author_preserved_impact_assets.py; ghost material remains valid.')
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    OUT.mkdir(parents=True,exist_ok=True)
    ghost=make_material('M_GroundMech_Afterimage_B1','ghost')
    impact=make_material('M_GroundMech_Impact_B1','impact')
    path=BASE+'/NS_GroundMech_Impact_B1'
    system=unreal.load_asset(path) or LIB.duplicate_asset('/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Bullet',path)
    assert system
    for module in ('SphereLocation','ScaleColor','ScaleSpriteSize'):
        if any(m.module_name==module for m in unreal.NiagaraEmitterService.list_modules(path,'NE_forward_simple_bullet')):
            assert unreal.NiagaraEmitterService.remove_module(path,'NE_forward_simple_bullet',module)
    config=json.loads(unreal.GuLiGroundMechFireEditorLibrary.configure_impact(system,impact,diameter,.20))
    assert config['success'],config
    compiled=unreal.NiagaraService.compile_with_results(path); assert compiled.success,str(compiled)
    assert unreal.NiagaraService.save_system(path)
    # Extend only this existing node's bindings. Preserve all authored pose chains.
    abp=unreal.load_asset('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/ABP_GroundMech_A4B2')
    graph=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.bind_arm_recoil(abp)); assert graph['success'],graph
    assert LIB.save_loaded_asset(abp,False)
    result={'success':True,'impact':config,'graph':graph,'assets':[system.get_path_name(),impact.get_path_name(),ghost.get_path_name()],
            'verification':'Asset structure/compile only. Player visual acceptance pending; no PIE.'}
    (OUT/'author_assets.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    return result

if __name__=='__main__':
    # Caller supplies the baseline impact emitter diameter after reading its saved graph.
    unreal.MCPythonHelper.submit_result(json.dumps(author(GULI_IMPACT_BASE_DIAMETER)))

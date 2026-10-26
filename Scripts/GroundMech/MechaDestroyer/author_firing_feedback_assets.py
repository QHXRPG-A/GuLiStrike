"""Build the approved A_v3 single-curve VFX in project-owned copies, without PIE."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/FiringFeedback_UE_20261025'
BASE = '/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A3'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService


def linear(hex_rgb):
    rgb = [int(hex_rgb[i:i+2], 16)/255. for i in (0, 2, 4)]
    return unreal.LinearColor(*[v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in rgb], 1.)


def material():
    path = BASE+'/M_GroundMech_SingleCurve_A3'
    mat = unreal.load_asset(path)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit('/',1)[1],BASE,unreal.Material,unreal.MaterialFactoryNew())
    assert mat
    mat.modify()
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('disable_depth_test',False)
    mat.set_editor_property('used_with_niagara_sprites',True)
    EDIT.delete_all_material_expressions(mat)
    uv = EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate,-600,-200)
    params = {'UV':uv}
    for i,(name,color) in enumerate([('TailColor','522C5C'),('HeadColor','EE9D58'),('CoreColor','FEE4D9')]):
        p = EDIT.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-600,i*150)
        p.set_editor_property('parameter_name',name)
        p.set_editor_property('default_value',linear(color))
        params[name] = p
    for i,(name,value) in enumerate([('Bend',.22),('Emission',8.)]):
        p = EDIT.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-600,500+i*150)
        p.set_editor_property('parameter_name',name)
        p.set_editor_property('default_value',value)
        params[name] = p
    custom = EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom,-200,0)
    pins = []
    for name in params:
        pin = unreal.CustomInput(); pin.set_editor_property('input_name',name); pins.append(pin)
    custom.set_editor_property('inputs',pins)
    custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description','A_v3: one connected tapered curve; UV.y=0 is the physical projectile tip; no forks/no noise')
    code = '''
float t = saturate(UV.y);
float center = 0.5 + Bend*sin(6.2831853*t)*sin(3.14159265*t)*smoothstep(0.12,0.3,t);
float d = abs(UV.x-center);
float aa = max(fwidth(d),0.0008);
float head = (1.0/6.0)*sqrt(saturate(1.0-pow((t-0.09)/0.09,2.0)));
float stroke = 0.028*pow(saturate(1.0-t),0.7)*smoothstep(0.0,0.07,t);
float width = max(head,stroke);
float fade = smoothstep(0.0,0.012,t)*(1.0-smoothstep(0.84,1.0,t));
float body = (1.0-smoothstep(width-aa,width+aa,d))*fade;
float core = (1.0-smoothstep(width*0.28-aa,width*0.28+aa,d))*fade;
float glow = exp(-pow(d/max(width+0.022,0.001),2.0)*3.5)*fade*0.18;
float3 color = lerp(HeadColor.rgb,TailColor.rgb,smoothstep(0.08,0.65,t));
float3 rgb = Emission*(color*(body+glow)+CoreColor.rgb*core*lerp(0.65,0.12,t));
return float4(rgb,saturate(body+glow));
'''
    custom.set_editor_property('code',code)
    for name,node in params.items():
        assert EDIT.connect_material_expressions(node,'',custom,name),name
    for channels,prop,y in [(('r','g','b'),unreal.MaterialProperty.MP_EMISSIVE_COLOR,0),(('a',),unreal.MaterialProperty.MP_OPACITY,180)]:
        mask = EDIT.create_material_expression(mat,unreal.MaterialExpressionComponentMask,120,y)
        for c in ('r','g','b','a'): mask.set_editor_property(c,c in channels)
        assert EDIT.connect_material_expressions(custom,'',mask,'')
        assert EDIT.connect_material_property(mask,'',prop)
    EDIT.recompile_material(mat)
    assert LIB.save_loaded_asset(mat,False)
    (OUT/'single_curve.hlsl').write_text(code,encoding='utf8')
    return mat


def copy_system(kind,name):
    path = BASE+'/'+name
    system = unreal.load_asset(path)
    if not system:
        system = LIB.duplicate_asset('/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_'+kind,path)
    assert system and system.get_path_name().startswith(BASE+'/')
    return system


def sync_dimensions(bullet,mat):
    rows = json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf8'))
    scale = next(r['Scale'] for r in rows if r['Name']=='PlayerBullet')
    skills = json.loads((ROOT/'Data/Json/DT_GuLiStrikeMech_Skills.json').read_text(encoding='utf8'))
    life = next(r['ProjectileLifetime'] for r in skills if r['Name']=='BasicMachinegun')
    report = json.loads(unreal.GuLiGroundMechFireEditorLibrary.configure_bullet(bullet,mat,unreal.Vector(**{k.lower():v for k,v in scale.items()}),life,True))
    assert report['success'],report
    return report


def author():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    OUT.mkdir(parents=True,exist_ok=True)
    mat = material()
    bullet = copy_system('Bullet','NS_GroundMech_Bullet_SingleCurve_A3')
    muzzle = copy_system('Muzzle','NS_GroundMech_Muzzle_A3')
    p = bullet.get_path_name(); e = 'NE_forward_simple_bullet'
    for module in ('SphereLocation','ScaleColor','ScaleSpriteSize'):
        if any(m.module_name==module for m in EM.list_modules(p,e)):
            assert EM.remove_module(p,e,module),module
    bullet_data = sync_dimensions(bullet,mat)
    assert unreal.GuLiGroundMechFireEditorLibrary.configure_muzzle(muzzle)
    scale = next(r['Scale']['X'] for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text()) if r['Name']=='PlayerMuzzle')
    assert NS.set_parameter(muzzle.get_path_name(),'User.Size',str(scale))
    systems = {}
    for system in (bullet,muzzle):
        result = NS.compile_with_results(system.get_path_name())
        assert result.success,str(result)
        LIB.set_metadata_tag(system,'GroundFiringFeedback','A_v3_single_curve_20261025')
        assert NS.save_system(system.get_path_name())
        systems[system.get_name()] = {'compile':str(result),'emitters':[
            {'name':x.emitter_name,'properties':str(EM.get_emitter_properties(system.get_path_name(),x.emitter_name)),
             'renderers':[str(EM.get_renderer_details(system.get_path_name(),x.emitter_name,r.renderer_index)) for r in EM.list_renderers(system.get_path_name(),x.emitter_name)]}
            for x in NS.list_emitters(system.get_path_name())]}
    report = {'success':True,'bullet':bullet_data,'systems':systems,'material':mat.get_path_name(),
              'material_diagnostics':unreal.MaterialNodeService.get_material_diagnostics(mat.get_path_name()),
              'visual_test':'not_run; user acceptance pending','pie_started':False}
    (OUT/'asset_authoring.json').write_text(json.dumps(report,indent=2,default=str),encoding='utf8')
    return report


if __name__=='__main__':
    unreal.MCPythonHelper.submit_result(json.dumps(author(),default=str))

"""Ground A4: one continuous long trace with distance-based aging, no round bullet head."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/PersistentTrail_20261025'
BASE = '/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4'
SYSTEM = BASE+'/NS_GroundMech_PersistentCurve_A4'
MATERIAL = BASE+'/M_GroundMech_PersistentCurve_A4'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary

CODE = '''
float lengthCm = max(Length,0.001);
float along = ChunkOffset+(1.0-saturate(UV.y))*lengthCm;
float behind = HeadDistance-along;
float pointAge = max(behind,0.0)/max(Speed,1.0)+max(EndAge,0.0);
float life = 1.0-smoothstep(Hold,Hold+max(Fade,0.01),pointAge);
// Absolute distance from launch holds the wave in place as the visible window moves.
float curl = Bend*sin(6.2831853*along/max(WaveLength,100.0));
curl *= smoothstep(0.0,100.0,along);
float d = abs((UV.x-0.5)*max(Canvas,1.0)-curl);
float aa = max(fwidth(d),0.12);
float radius = Width*0.5*lerp(0.35,1.0,life);
float body = 1.0-smoothstep(radius-aa,radius+aa,d);
float core = 1.0-smoothstep(radius*0.24-aa,radius*0.24+aa,d);
float glow = exp(-pow(d/max(radius*1.5,0.1),2.0)*3.0)*0.13;
float edge = smoothstep(0.0,12.0,behind)*smoothstep(0.0,150.0,WaveLength-behind)*smoothstep(0.0,80.0,along);
float heat = 1.0-smoothstep(0.0,0.06,pointAge);
float3 color = lerp(TailColor.rgb,HeadColor.rgb,heat);
float3 rgb = Emission*(color*(body+glow)+CoreColor.rgb*core*lerp(0.22,0.6,heat));
// There is deliberately no ellipse/bulb. Every pixel belongs to the same thin stroke.
return float4(rgb,saturate(body+glow)*life*edge*step(1.0,Length));
'''


def linear(value):
    srgb = [int(value[i:i+2],16)/255. for i in (0,2,4)]
    return unreal.LinearColor(*[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in srgb],1.)


def make_material():
    mat = unreal.load_asset(MATERIAL)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MATERIAL.rsplit('/',1)[1],BASE,unreal.Material,unreal.MaterialFactoryNew())
    assert mat
    mat.modify()
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('disable_depth_test',False)
    mat.set_editor_property('used_with_niagara_sprites',True)
    EDIT.delete_all_material_expressions(mat)
    params = {'UV':(EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate,-600,-200),'')}
    for i,(name,value) in enumerate([('TailColor','522C5C'),('HeadColor','EE9D58'),('CoreColor','FEE4D9')]):
        node=EDIT.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-600,i*140)
        node.set_editor_property('parameter_name',name); node.set_editor_property('default_value',linear(value))
        params[name]=(node,'')
    node=EDIT.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-600,420)
    node.set_editor_property('parameter_name','Emission');node.set_editor_property('default_value',6.)
    params['Emission']=(node,'')
    for index,names in enumerate([['Length','HeadDistance','EndAge','Speed'],['Width','Bend','Hold','Fade'],['WaveLength','Canvas','ChunkOffset','UnusedB']]):
        node=EDIT.create_material_expression(mat,unreal.MaterialExpressionDynamicParameter,-600,600+index*180)
        node.set_editor_property('parameter_index',index); node.set_editor_property('param_names',names)
        node.set_editor_property('default_value',unreal.LinearColor(0,0,0,0))
        for name in names:
            if not name.startswith('Unused'):params[name]=(node,name)
    custom=EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom,-100,0)
    pins=[]
    for name in params:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description','A4: persistent single stroke, absolute path distance, per-point hold/fade, no head ellipse')
    custom.set_editor_property('code',CODE)
    for name,(node,output) in params.items():
        connected=EDIT.connect_material_expressions(node,output,custom,name)
        if not connected and isinstance(node,unreal.MaterialExpressionDynamicParameter):
            # MaterialEditingLibrary reads Outputs directly; a node without an
            # open graph can still expose the original channel mask names.
            channel='RGBA'[list(node.get_editor_property('param_names')).index(name)]
            connected=EDIT.connect_material_expressions(node,channel,custom,name)
        assert connected,name
    for channels,prop,y in [(('r','g','b'),unreal.MaterialProperty.MP_EMISSIVE_COLOR,0),(('a',),unreal.MaterialProperty.MP_OPACITY,180)]:
        mask=EDIT.create_material_expression(mat,unreal.MaterialExpressionComponentMask,170,y)
        for c in ('r','g','b','a'):mask.set_editor_property(c,c in channels)
        assert EDIT.connect_material_expressions(custom,'',mask,'')
        assert EDIT.connect_material_property(mask,'',prop)
    EDIT.recompile_material(mat)
    diag=unreal.MaterialNodeService.get_material_diagnostics(mat.get_path_name())
    assert diag.is_compiled_ok,str(diag)
    assert LIB.save_loaded_asset(mat,False)
    return mat,diag


def sync_dimensions(system=None,material=None,apply=True):
    rows=json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf8'))
    row=next(r for r in rows if r['Name']=='PlayerBullet')
    skills=json.loads((ROOT/'Data/Json/DT_GuLiStrikeMech_Skills.json').read_text(encoding='utf8'))
    life=next(r['ProjectileLifetime'] for r in skills if r['Name']=='BasicMachinegun')
    result=json.loads(unreal.GuLiGroundMechFireEditorLibrary.configure_persistent_trail(
        system or unreal.load_asset(SYSTEM),material or unreal.load_asset(MATERIAL),
        unreal.Vector(12*row['Scale']['Y'],row['TrailLengthCm'],row['TrailBendCm']),
        row['TrailHoldSeconds'],row['TrailFadeSeconds'],life,apply))
    assert result['success'],result
    return result


def author():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    OUT.mkdir(parents=True,exist_ok=True)
    mat,diag=make_material()
    system=unreal.load_asset(SYSTEM)
    if not system:
        system=LIB.duplicate_asset('/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A3/NS_GroundMech_Bullet_SingleCurve_A3',SYSTEM)
    assert system
    data=sync_dimensions(system,mat)
    compiled=unreal.NiagaraService.compile_with_results(SYSTEM)
    assert compiled.success,str(compiled)
    LIB.set_metadata_tag(system,'GroundPersistentTrail','A4_distance_aging_20261025')
    assert unreal.NiagaraService.save_system(SYSTEM)
    data.update(success=True,system=SYSTEM,material=MATERIAL,compile=str(compiled),material_diagnostics=str(diag),
                pie_started=False,visual_acceptance='pending_user')
    (OUT/'persistent_curve.hlsl').write_text(CODE,encoding='utf8')
    (OUT/'asset_authoring.json').write_text(json.dumps(data,indent=2),encoding='utf8')
    return data


if __name__=='__main__':
    unreal.MCPythonHelper.submit_result(json.dumps(author()))

"""Install view-distance shaders on the project-owned bullet and laser materials."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/FX/DistanceLOD_20261127'
LIB=unreal.MaterialEditingLibrary
PREFIX='// GuLi camera-distance LOD'
BASE='/Game/GuLiStrike/FX/'
MATS={'mass':BASE+'BulletTrails/Review_A1/M_MassBulletTrail',
      'ground':BASE+'BulletTrails/Review_A1/M_GroundBulletTrail_Orange',
      'burning':BASE+'GroundMech/Burning/M_GroundBurningBullet',
      'laser':BASE+'WingmanWeapons/M_WingmanLaser'}
HOVER=BASE+'WarMachineHover/NS_WarMachineHoverPool'

def cheap_trail(role):
    mass=role=='mass'
    along='StartDistance' if mass else 'ChunkOffset'
    color='Tint.rgb' if mass else 'lerp(TailColor.rgb,HeadColor.rgb,saturate(1-age/.06))'
    alpha='Tint.a' if mass else '1.0'
    cutoff='*smoothstep(0,150,WaveLength-behind)' if role=='burning' else ''
    return f'''{PREFIX}
[branch] if (ViewDistance >= 8000.0)
{{
    float along = {along} + (1-saturate(UV.y))*Length;
    float behind = HeadDistance-along;
    float age = max(0,behind)/max(Speed,1)+max(EndAge,0);
    float life = 1-smoothstep(Hold,Hold+max(Fade,.01),age);
    float d = (UV.x-.5)*max(Canvas,1);
    float3 tint = {color};
    float edge = saturate(behind/8)*saturate(along/15){cutoff};
    [branch] if (ViewDistance >= 15000.0)
    {{
        float coverage = saturate((Width*.5-abs(d))/max(fwidth(d),.25)+.5);
        return float4(Emission*tint,coverage*life*edge*{alpha}*step(.01,Length));
    }}
    // The medium stroke keeps the wave but omits the exponential halo.
    d -= Bend*sin(6.2831853*along/max(WaveLength,100))*saturate(along/100);
    float coverage=saturate((Width*.5-abs(d))/max(fwidth(d),.25)+.5);
    float core=saturate(1-abs(d)/max(.2,Width*.3));
    return float4(Emission*lerp(tint,CoreColor.rgb,core*.28),coverage*life*edge*{alpha}*step(.01,Length));
}}
'''

def material(role,path):
    asset=unreal.load_asset(path)
    customs=[e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer()==asset]
    assert len(customs)==(2 if role=='laser' else 1),(path,len(customs))
    dist=next((e for e in unreal.ObjectIterator(unreal.MaterialExpressionDistance)
               if e.get_outer()==asset and e.get_editor_property('desc')=='GuLi FX view distance'),None)
    if not dist:
        w=LIB.create_material_expression(asset,unreal.MaterialExpressionWorldPosition,-1100,930)
        c=LIB.create_material_expression(asset,unreal.MaterialExpressionCameraPositionWS,-1100,1060)
        dist=LIB.create_material_expression(asset,unreal.MaterialExpressionDistance,-820,995)
        dist.set_editor_property('desc','GuLi FX view distance')
        assert LIB.connect_material_expressions(w,'',dist,'A') and LIB.connect_material_expressions(c,'',dist,'B')
    for n in customs:
        inputs=list(n.get_editor_property('inputs')); names=[str(i.get_editor_property('input_name')) for i in inputs]
        if 'ViewDistance' not in names:
            inp=unreal.CustomInput(); inp.set_editor_property('input_name','ViewDistance')
            n.modify();n.set_editor_property('inputs',inputs+[inp])
        assert LIB.connect_material_expressions(dist,'',n,'ViewDistance')
        code=n.get_editor_property('code')
        if PREFIX not in code:
            if role!='laser':prefix=cheap_trail(role)
            elif 'Tint' in names:prefix=PREFIX+'\n[branch] if(ViewDistance>=15000) return Tint;\n[branch] if(ViewDistance>=8000) return Tint*(1+.35*saturate(1-abs(2*UV.x-1)));\n'
            else:prefix=PREFIX+'\n[branch] if(ViewDistance>=15000) return saturate(1-abs(2*UV.x-1))*Alpha;\n[branch] if(ViewDistance>=8000) return saturate(1-abs(2*UV.x-1))*saturate((1-abs(2*UV.y-1))*8)*Alpha;\n'
            n.modify();n.set_editor_property('code',prefix+code)
    LIB.recompile_material(asset)
    diag=unreal.MaterialNodeService.get_material_diagnostics(path)
    assert diag.success and diag.is_compiled_ok and not diag.compile_errors,(path,str(diag))
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False),path
    return {'path':path,'custom_expressions':len(customs),'thresholds_cm':[8000,15000],'errors':[]}

def hover():
    sp=unreal.NiagaraScratchPadService;e='LaserMuzzles'
    mod=next(str(m) for m in sp.list_scratch_modules(HOVER,e) if str(m).startswith('AdvanceReferenceHistory'))
    n=next(str(i.node_id) for i in sp.list_nodes(HOVER,e,mod) if 'customhlsl' in str(i.node_type).lower())
    code=sp.get_custom_hlsl_code(HOVER,e,mod,n)
    if 'int historyStride' not in code:
        anchor='bool moving=active && speed>5.0;'
        assert anchor in code and 'SpanOut=spacing;' in code and 'SizeOut=float2(width,span)' in code
        # Inactive / suppressed lanes still publish complete state; never let a prior
        # generation reappear when the nozzle comes back into the near band.
        code=code.replace(anchor,'''int historyStride=Meta.a>0.5 ? max(1,(int)round(Meta.a)) : 1;
if(active && Lane>0 && ((Lane-1)%historyStride)!=0)
{
    BornOut=-100000.0; WasMovingOut=0; LengthOut=0; SpanOut=0;
    SampleDistanceOut=-100000.0; ViewFadeOut=0; MoveAgeOut=0;
    PositionOut=NozzlePosition; AlignmentOut=SavedHeading; SizeOut=float2(0,0); ColorOut=float4(0,0,0,0);
    return;
}
'''+anchor).replace('SpanOut=spacing;','SpanOut=spacing*historyStride;').replace('SpanOut=spacing*1.65;','SpanOut=spacing*1.65*historyStride;')
        assert sp.set_custom_hlsl_code(HOVER,e,mod,n,code) and sp.apply_changes(HOVER)
    c=unreal.NiagaraService.compile_with_results(HOVER)
    assert c.success and not c.errors,(HOVER,str(c))
    assert unreal.NiagaraService.save_system(HOVER)
    return {'path':HOVER,'history_lanes':[9,3,1],'head_lanes':1,'errors':list(c.errors)}

def run():
    results=[material(k,p) for k,p in MATS.items()]
    results.append(hover())
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'material-and-pool-lod.json').write_text(json.dumps(results,indent=2),encoding='utf8')
    return results

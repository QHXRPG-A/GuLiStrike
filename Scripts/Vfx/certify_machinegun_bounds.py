"""Editor-side audit of the frozen six-emitter machine-gun candidates.

The envelope is metadata for runtime pre-spawn admission; dynamic Niagara
simulation bounds and all renderer/module settings remain intact.
"""
import json, re, math
from pathlib import Path
import unreal

def curve_range(text):
    body=text.split('Keys=(',1)[1].split('),DefaultValue=',1)[0]
    keys=[]
    for raw in re.findall(r'\(([^()]*)\)',body):
        fields=dict(re.findall(r'(\w+)=([^,]+)',raw))
        keys.append({k:float(fields.get(k,0)) for k in ['Time','Value','ArriveTangent','LeaveTangent']})
    bounds=[x['Value'] for x in keys]
    for left,right in zip(keys,keys[1:]):
        dt=right['Time']-left['Time']
        if dt<=0:continue
        # UE rich curves use unweighted cubic Hermite tangents. The captured
        # size curves have no weighted segments; include all analytic extrema.
        p,q=left['Value'],right['Value'];m=left['LeaveTangent']*dt;n=right['ArriveTangent']*dt
        a=2*p-2*q+m+n;b=-3*p+3*q-2*m-n;c=m
        roots=[]
        if abs(a)<1e-12:
            if abs(b)>1e-12:roots=[-c/(2*b)]
        else:
            discriminant=4*b*b-12*a*c
            if discriminant>=0:roots=[(-2*b+sign*math.sqrt(discriminant))/(6*a) for sign in [-1,1]]
        bounds.extend(((a*t+b)*t+c)*t+p for t in roots if 0<t<1)
    return [min(bounds),max(bounds)]

def certify(path):
    assert path.startswith('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun')
    asset=unreal.load_asset(path)
    audit=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_machine_gun_bounds_inputs(asset))
    assert {x['emitter'] for x in audit['emitters']}=={'Glow','Flash','RibbonCore','RIbbonTrailFollower','Sparks','Debris'}
    numeric={x['parameter']:x['value'] for x in audit['numeric_inputs']}
    assert max(v[0] for k,v in numeric.items() if 'UniformRangedFloat002.Maximum' in k)<=2000
    assert max(abs(x) for k,v in numeric.items() if '.GravityForce.Gravity' in k for x in v)<=800
    assert max(v[0] for k,v in numeric.items() if '.Lifetime Max' in k)<=1.000001
    assert numeric['Constants.RIbbonTrailFollower.InitializeRibbon.Lifetime'][0]<=.25
    assert max(max(v) for k,v in numeric.items() if '.Lerp_Vector2.' in k)<=35
    assert max(max(v) for k,v in numeric.items() if '.Sprite Size' in k)<=125
    size_curves=[]
    for interface in audit['curve_interfaces']:
        for key in ['XCurve','YCurve']:
            if key not in interface:continue
            bounds=curve_range(interface[key]);assert max(abs(x) for x in bounds)<=2
            size_curves.append({'path':interface['path'],'axis':key,'range':bounds})
    materials=[]
    for emitter in audit['emitters']:
        for renderer in unreal.NiagaraEmitterService.list_renderers(path,emitter['emitter']):
            details=unreal.NiagaraEmitterService.get_renderer_details(path,emitter['emitter'],renderer.renderer_index)
            if not details.has_material:continue
            material=unreal.load_asset(details.material_path);chain=[]
            while isinstance(material,unreal.MaterialInstance):
                chain.append(material.get_path_name());material=material.get_editor_property('parent')
            chain.append(material.get_path_name())
            graph=json.loads(unreal.MaterialNodeService.export_material_graph(material.get_path_name()))
            outputs=[x['property'] for x in graph['output_connections']]
            # The frozen renderer materials expose only emissive and opacity.
            # Any future WPO/attributes displacement needs a new envelope audit.
            assert set(outputs)<= {'EmissiveColor','Opacity'},(chain,outputs)
            materials.append({'emitter':emitter['emitter'],'chain':chain,'output_properties':outputs})
    # Combined producer/follower lifetime <=1.25 s. Ignoring positive drag
    # and bounding gravity with a*t^2 covers the semi-implicit update. Add
    # 250 cm for size-curve extent, 15 cm camera offset and 2 cm spawn radius.
    motion=2000*1.25+800*1.25**2
    conservative=motion+250+15+2
    assert conservative<4096
    error=unreal.GuLiCombatEffectAuthoringLibrary.configure_machine_gun_bounds_envelope(asset,unreal.Vector(4096,4096,4096))
    assert error is not None,error
    assert unreal.NiagaraService.save_system(path)
    info=unreal.NiagaraService.get_parameter(path,'User.GuLiPreSpawnBoundsExtent')
    assert info is not None
    return {'path':path,'extent_unscaled_cm':[4096]*3,'conservative_formula_cm':conservative,
        'size_curve_extrema':size_curves,'materials_without_vertex_displacement':materials,'readback':str(info),'audit':audit,
        'scope':'Frozen vendor modules/inputs, scale applied once by consumer. Re-audit after any authored motion/size change. Dynamic simulation bounds retained.'}

if __name__=='__main__':
    paths=['/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_Optimized',
        '/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Optimized',
        '/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_Optimized_NoCollision']
    reports=[certify(path) for path in paths]
    out=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-implementation/flash-certified-envelopes.json'
    out.write_text(json.dumps(reports,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved':paths}))

"""Author two emissive layers on the existing certified muzzle NDC contract.
Run after user-authorized PIE stop. No dynamic lights, screenshots or runtime tests.
"""
import unreal,json,ast,math
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/CommanderRecoil_20261114'
BASE='/Game/GuLiStrike/FX/CommanderWeapons';DEST=BASE+'/RecoilGlow_v1'
AS=unreal.EditorAssetLibrary;NS=unreal.NiagaraService;EM=unreal.NiagaraEmitterService;SP=unreal.NiagaraScratchPadService
EDIT=unreal.MaterialEditingLibrary;NATIVE=unreal.GuLiCombatEffectAuthoringLibrary
REPORT={'success':False,'systems':[],'dynamic_lights':0,'visual_review':'Pending user','performance_measurement':'Not run'}

def need(value,what):
    if not value:raise RuntimeError(what)
    return value

def linear(hex):
    v=[int(hex[i:i+2],16)/255 for i in (0,2,4)]
    return [x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in v]

def material(name,halo=False,combined=False):
    path=DEST+'/M_'+name
    a=unreal.load_asset(path) if AS.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_'+name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(a)
    for k,v in dict(blend_mode=unreal.BlendMode.BLEND_ADDITIVE,shading_model=unreal.MaterialShadingModel.MSM_UNLIT,two_sided=True).items():a.set_editor_property(k,v)
    EDIT.set_material_usage(a,unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv=EDIT.create_material_expression(a,unreal.MaterialExpressionTextureCoordinate)
    age=EDIT.create_material_expression(a,unreal.MaterialExpressionParticleRelativeTime)
    custom=EDIT.create_material_expression(a,unreal.MaterialExpressionCustom)
    core=','.join(str(x) for x in linear('FEE4D9'));warm=','.join(str(x) for x in linear('EE9D58'))
    color=f'float3({warm})*pow(saturate(1-r),2)*9' if halo else f'float3({core})*(1-smoothstep(.18,.5,r))*18'
    if combined:color=f'float3({core})*(1-smoothstep(.08,.22,r))*18+float3({warm})*pow(saturate(1-r),2)*7'
    custom.set_editor_property('code',f'float r=length(UV*2-1); float fade=pow(saturate(1-Age),2); return ({color})*fade;')
    custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins=[]
    for key in ('UV','Age'):
        pin=unreal.CustomInput();pin.set_editor_property('input_name',key);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    need(EDIT.connect_material_expressions(uv,'',custom,'UV'),'UV');need(EDIT.connect_material_expressions(age,'',custom,'Age'),'Age')
    need(EDIT.connect_material_property(custom,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'Emissive')
    one=EDIT.create_material_expression(a,unreal.MaterialExpressionConstant);one.set_editor_property('r',1.)
    need(EDIT.connect_material_property(one,'',unreal.MaterialProperty.MP_OPACITY),'Opacity')
    EDIT.recompile_material(a);need(AS.save_loaded_asset(a,False),path);return a

def main():
    need(not unreal.EditorLevelLibrary.get_pie_worlds(True),'User-authorized PIE stop required')
    # This probe is introspection-only and is deliberately run before first write.
    exec(compile((ROOT/'.agents/skills/ue5-vfx-production/scripts/probe_vfx_capabilities.py').read_text(encoding='utf8'),'vfx_capabilities','exec'),{'__name__':'recoil_vfx_probe'})
    AS.make_directory(DEST)
    mats={'Flash':material('CommanderMuzzleCore'),'Glow':material('CommanderMuzzleHalo',True),'Combined':material('CommanderMuzzleCombined',combined=True)}
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf8'))
    definition=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    scope={'SP':SP,'ASSETS':AS,'unreal':unreal,'require':need,'prop':lambda o,k:o.get_editor_property(k)}
    exec(compile(ast.Module(body=[definition],type_ignores=[]),'muzzle_glow_scratch','exec'),scope)
    for batch in (False,True):
        source=BASE+('/NS_MachineGunMuzzle_Batch' if batch else '/NS_MachineGunMuzzle_AllOptimizations')
        need(unreal.load_asset(source),'Missing formal source '+source)
        for level,suffix in enumerate(('', '_Reduced','_Minimal')):
            path=DEST+'/NS_CommanderMuzzleGlow'+('_Batch' if batch else '')+suffix
            if not AS.does_asset_exist(path):need(AS.duplicate_asset(source,path),'Duplicate '+path)
            system=unreal.load_asset(path);emitters=list(NS.list_emitters(path));need(emitters,'Emitter service unavailable')
            keep={'Flash'} if level==2 else {'Flash','Glow'}
            for e in emitters:
                name=str(e.emitter_name)
                if name not in keep:need(NS.remove_emitter(path,name),'Remove unused layer '+name)
            actual={str(e.emitter_name) for e in NS.list_emitters(path)};need(actual==keep,'Required flash layers '+str(actual))
            for emitter in sorted(actual):
                modules=list(EM.list_modules(path,emitter));need(modules,'Read stack')
                # Retain spawn/lifecycle/channel inputs; replace only cosmetic motion.
                for m in modules:
                    name=str(m.module_name)
                    if name.startswith('GuLiMuzzle') or name.startswith('EmitterState') or name.startswith('InitializeParticle') or name.startswith('ParticleState') or name.startswith('SpawnBurst') or name.startswith('GuLiCommanderGlow'):continue
                    need(EM.enable_module(path,emitter,name,False),'Disable old cosmetic module '+name)
                if not batch:
                    bursts=[str(m.module_name) for m in modules if str(m.module_name).startswith('SpawnBurst')];need(bursts,'Missing single spawn')
                    for name in bursts:need(EM.set_module_input(path,emitter,name,'Spawn Count','1'),'One particle per layer')
                diameter=64 if level==2 else (36 if emitter=='Flash' else 76)
                life=.12 if emitter=='Flash' and level<2 else .18
                scratch_name='GuLiCommanderGlowInitialize'
                for m in modules:
                    if str(m.module_name).startswith(scratch_name):
                        need(EM.remove_module(path,emitter,str(m.module_name)),'Replace authored glow initializer')
                authored=scope['scratch'](path,emitter,'ParticleSpawn',scratch_name,[],
                        [('Position','Position','Particles.Position'),('Velocity','Vector','Particles.Velocity'),('Size','vec2','Particles.SpriteSize'),
                         ('Life','float','Particles.Lifetime'),('Tint','Color','Particles.Color'),('Rotation','float','Particles.SpriteRotation')],
                        f'Position=float3(0,0,0);Velocity=float3(0,0,0);Size=float2({diameter},{diameter});Life={life};Tint=float4(1,1,1,1);Rotation=0;')
                module=next(m for m in EM.list_modules(path,emitter) if str(m.module_name)==authored)
                script=unreal.load_object(None,str(module.script_asset_path));need(NATIVE.move_client_effect_module(system,emitter,script,'ParticleSpawn',-1) is not None,'Set authored size/lifetime')
                if batch:
                    # Render after initialization, retaining original slot/generation,
                    # F+1 birth gate and per-event local-to-world pose input modules.
                    render=next(m for m in EM.list_modules(path,emitter) if str(m.module_name)=='GuLiMuzzleBirthRender')
                    need(NATIVE.move_client_effect_module(system,emitter,unreal.load_object(None,str(render.script_asset_path)),'ParticleSpawn',-1) is not None,'Event render last')
                renderers=list(EM.list_renderers(path,emitter));need(len(renderers)==1,'One sprite renderer per layer')
                need(str(renderers[0].renderer_type)=='Sprite','Unexpected renderer')
                mat=mats['Combined' if level==2 else emitter]
                need(EM.set_renderer_property(path,emitter,0,'Material',mat.get_path_name()),'Material')
                need(EM.set_renderer_property(path,emitter,0,'FacingMode','FaceCamera'),'Camera facing glow')
            system.set_editor_property('max_pool_size',4 if batch else 64);system.set_editor_property('pool_prime_size',0)
            # NiagaraService vector inputs use comma-separated components. Unreal
            # export-text syntax returns success but silently writes a zero vector.
            need(NS.set_parameter(path,'User.GuLiPreSpawnBoundsExtent','60,60,60'),'Known glow bounds')
            bounds=NS.get_parameter(path,'User.GuLiPreSpawnBoundsExtent').current_value
            need(all(float(part.split('=')[1])==60 for part in bounds.strip('()').split(',')), 'Read back nonzero glow bounds: '+bounds)
            if batch:
                need(NS.set_parameter(path,'User.GuLiMuzzleMaximumLifetime','0.18'),'Glow lifetime contract')
                # Inherited channel, slot, generation, birth and retire wiring is
                # unchanged; this is structural reuse, not a new runtime certificate.
                need(float(NS.get_parameter(path,'User.GuLiMuzzleInputVersion').current_value)==1,'Inherited input version')
            result=NS.compile_with_results(path);need(result.success and result.error_count==0,str(result))
            need(NS.save_system(path),'Save '+path)
            rows=[{'emitter':str(e.emitter_name),'renderers':[str(x.renderer_type) for x in EM.list_renderers(path,str(e.emitter_name))]} for e in NS.list_emitters(path)]
            need(all(x['renderers']==['Sprite'] for x in rows),'No light renderers')
            REPORT['systems'].append({'path':path,'batch':batch,'lod':level,'layers':rows,'lifetime':.18,'bounds':bounds,'compile_errors':result.error_count})
    REPORT['success']=True;(OUT/'muzzle-glow-readback.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps(REPORT,ensure_ascii=False))

if __name__=='__main__':main()

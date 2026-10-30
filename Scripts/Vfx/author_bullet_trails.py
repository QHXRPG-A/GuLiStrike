"""Owned bullet candidates. Stages: materials, systems, scene, readback. Never starts play."""
import ast
import json
import sys
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/FX/BulletTrails_20261028'
BASE = '/Game/GuLiStrike/FX/BulletTrails/Review_A1'
MASS = BASE + '/NS_MassBulletTrail_GPU'
LIGHTS = BASE + '/NS_BulletLights_CPU'
GROUND = BASE + '/NS_GroundBulletTrail_Orange'
MASS_MATERIAL = BASE + '/M_MassBulletTrail'
GROUND_MATERIAL = BASE + '/M_GroundBulletTrail_Orange'
SOURCE_POOL = '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches'
SOURCE_GROUND = '/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4/NS_GroundMech_PersistentCurve_A4'
SOURCE_GROUND_MATERIAL = '/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4/M_GroundMech_PersistentCurve_A4'
LIB, EDIT = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
NS, EM, SP = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService

MATERIAL_CODE = '''
float along = StartDistance + (1.0-saturate(UV.y))*Length;
float age = max(0.0,HeadDistance-along)/max(Speed,1.0)+max(EndAge,0.0);
float life = 1.0-smoothstep(Hold,Hold+max(Fade,0.01),age);
float curve = Bend*sin(6.2831853*along/max(WaveLength,100.0))*smoothstep(0.0,100.0,along);
float d = abs((UV.x-0.5)*Canvas-curve);
float aa = max(fwidth(d),0.12);
float radius = Width*0.5*lerp(0.35,1.0,life);
float body = 1.0-smoothstep(radius-aa,radius+aa,d);
float core = 1.0-smoothstep(radius*0.24-aa,radius*0.24+aa,d);
float halo = exp(-pow(d/max(radius*1.5,0.1),2.0)*3.0)*0.13;
float edge = smoothstep(0.0,4.0,HeadDistance-along)*smoothstep(0.0,12.0,along);
float3 rgb = Emission*(Tint.rgb*(body+halo)+lerp(Tint.rgb,CoreColor.rgb,0.45)*core*0.45);
return float4(rgb,saturate(body+halo)*life*edge*Tint.a*step(0.01,Length));
'''

EVALUATE_CODE = '''
float terminal = Times.z > -1.e19 ? 1.0 : 0.0;
float endAt = terminal > 0.0 ? Times.z : Times.y;
float headDistance = terminal > 0.0 ? Times.w : Speed*clamp(Now-Times.x,0.0,max(0.0,Times.y-Times.x));
float endAge = max(0.0,Now-endAt);
float recentLength = min(MaximumLength,Speed*max(0.0,Style.z+Style.w-endAge));
float startDistance = max(0.0,headDistance-recentLength);
float length = max(0.0,headDistance-startDistance);
float canvas = 2.0*(Style.x+Style.y);
Position = Origin + Direction*((startDistance+headDistance)*0.5);
Alignment = Direction;
Facing = Plane;
Size = float2(canvas,length);
Color = Tint;
Color.a *= Generation != 0 && Now >= Times.x && length > 0.01 && endAge < Style.z+Style.w ? 1.0 : 0.0;
Data = float4(startDistance,headDistance,endAge,Speed);
StrokeStyle = Style;
Shape = float4(MaximumLength,canvas,length,Emission);
'''


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def prop(value, key):
    return value.get_editor_property(key)


def linear(rgb):
    values = [int(rgb[i:i+2],16)/255. for i in (0,2,4)]
    return unreal.LinearColor(*[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values],1.)


def copy(source, target):
    require(target.startswith(BASE+'/'), 'Unexpected destination')
    return require(unreal.load_asset(target) or LIB.duplicate_asset(source,target), 'Copy '+target)


def save_report(name, value):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2,default=str),encoding='utf8')
    return value


def outputs(mat, node):
    for channels,target,y in [(('r','g','b'),unreal.MaterialProperty.MP_EMISSIVE_COLOR,0),(('a',),unreal.MaterialProperty.MP_OPACITY,180)]:
        mask=EDIT.create_material_expression(mat,unreal.MaterialExpressionComponentMask,240,y)
        for channel in 'rgba': mask.set_editor_property(channel,channel in channels)
        require(EDIT.connect_material_expressions(node,'',mask,''),'Output mask')
        require(EDIT.connect_material_property(mask,'',target),'Material output')


def materials():
    ground=copy(SOURCE_GROUND_MATERIAL,GROUND_MATERIAL)
    values={'TailColor':linear('EE9D58'),'HeadColor':linear('EE9D58'),'CoreColor':linear('FEE4D9'),'Emission':8.}
    for cls in (unreal.MaterialExpressionVectorParameter,unreal.MaterialExpressionScalarParameter):
        for node in unreal.ObjectIterator(cls):
            if node.get_outer()==ground and str(prop(node,'parameter_name')) in values:
                node.modify();node.set_editor_property('default_value',values[str(prop(node,'parameter_name'))])
    ground.modify(); EDIT.recompile_material(ground)
    mat=unreal.load_asset(MASS_MATERIAL) or unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MASS_MATERIAL.rsplit('/',1)[1],BASE,unreal.Material,unreal.MaterialFactoryNew())
    require(mat,'Create Mass material');mat.modify();EDIT.delete_all_material_expressions(mat)
    for key,value in [('blend_mode',unreal.BlendMode.BLEND_ADDITIVE),('shading_model',unreal.MaterialShadingModel.MSM_UNLIT),
                      ('two_sided',True),('disable_depth_test',False),('used_with_niagara_sprites',True)]: mat.set_editor_property(key,value)
    params={'UV':(EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate,-800,-200),'')}
    params['Tint']=(EDIT.create_material_expression(mat,unreal.MaterialExpressionParticleColor,-800,0),'')
    core=EDIT.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-800,180)
    core.set_editor_property('parameter_name','CoreColor');core.set_editor_property('default_value',linear('FEE4D9'))
    params['CoreColor']=(core,'')
    for index,names in enumerate([['StartDistance','HeadDistance','EndAge','Speed'],['Width','Bend','Hold','Fade'],['WaveLength','Canvas','Length','Emission']]):
        node=EDIT.create_material_expression(mat,unreal.MaterialExpressionDynamicParameter,-800,400+index*180)
        node.set_editor_property('parameter_index',index);node.set_editor_property('param_names',names)
        node.set_editor_property('default_value',unreal.LinearColor(0,0,0,0))
        for name in names: params[name]=(node,name)
    custom=EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom,-200,0)
    pins=[]
    for name in params:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins);custom.set_editor_property('code',MATERIAL_CODE)
    custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description','Fixed world-space curve; per-point age; bounded visible window; no textures')
    for name,(node,output) in params.items():
        linked=EDIT.connect_material_expressions(node,output,custom,name)
        if not linked and isinstance(node,unreal.MaterialExpressionDynamicParameter):
            linked=EDIT.connect_material_expressions(node,'RGBA'[list(prop(node,'param_names')).index(name)],custom,name)
        require(linked,'Material input '+name)
    outputs(mat,custom);EDIT.recompile_material(mat)
    reports=[]
    for item in (ground,mat):
        diag=unreal.MaterialNodeService.get_material_diagnostics(item.get_path_name())
        require(diag.success and diag.is_compiled_ok and not diag.compile_errors,str(diag))
        require(LIB.save_loaded_asset(item,False),'Save material')
        reports.append({'path':item.get_path_name(),'diagnostics':str(diag)})
    return save_report('materials.json',{'success':True,'materials':reports,'visual_acceptance':'pending_user','PIE':False})


def scratch_helper():
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf8'))
    function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    scope={'unreal':unreal,'SP':SP,'ASSETS':unreal.get_editor_subsystem(unreal.EditorAssetSubsystem),'require':require,'prop':prop}
    exec(compile(ast.Module(body=[function],type_ignores=[]),'owned_scratch_helper','exec'),scope)
    return scope['scratch']


def strip_legacy_parameters(path):
    names=[str(n) for n in NS.summarize(path).user_parameter_names if str(n).startswith(('User.Laser','User.Muzzle'))]
    for name in names: require(NS.remove_user_parameter(path,name),'Remove unused template parameter '+name)
    return names


def clean_parameters():
    result=[]
    for path in (MASS,LIGHTS):
        removed=strip_legacy_parameters(path)
        compiled=NS.compile_with_results(path)
        require(compiled.success and compiled.error_count==0,str(compiled))
        diagnostics=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(unreal.load_asset(path))
        require('VM ERROR:' not in diagnostics and 'GPU ERROR:' not in diagnostics,diagnostics)
        require(NS.save_system(path),'Save parameter cleanup')
        result.append({'path':path,'removed':removed,'compile':str(compiled),'diagnostics':diagnostics})
    return save_report('parameter_cleanup.json',{'success':True,'systems':result,'PIE':False})


def configure_slot_pool(path, scratch):
    # Keep the emitter alive, but allocate its fixed particle slots only once.
    require(EM.set_module_input(path,'LaserBolts','EmitterState','Loop Behavior','NewEnumerator0'),'Infinite slot pool')
    require(EM.get_emitter_properties(path,'LaserBolts').loop_behavior=='Infinite','Read back slot pool loop')
    require(EM.set_module_input(path,'LaserBolts','SpawnBurst_Instantaneous','UseLoopCountLimit','true'),'One allocation per activation')
    require(EM.set_module_input(path,'LaserBolts','SpawnBurst_Instantaneous','Loop Count Limit','1'),'First loop only')
    require(EM.get_module_input(path,'LaserBolts','SpawnBurst_Instantaneous','UseLoopCountLimit').lower()=='true','Read back burst limit')
    limits=[p.current_value for p in NS.get_all_editable_settings(path).rapid_iteration_parameters
            if str(p.setting_path).endswith('.SpawnBurst_Instantaneous.Loop Count Limit')]
    require(limits and all(int(v)==1 for v in limits),'Read back first-loop limit')
    for name in list(map(str,SP.list_scratch_modules(path,'LaserBolts'))):
        if name.startswith('InitializeBulletTrailSlot'):
            require(EM.remove_module(path,'LaserBolts',name),'Replace slot initializer')
    # Use the Niagara execution-index intrinsic; a named MapGet can read a zero default.
    scratch(path,'LaserBolts','ParticleSpawn','InitializeBulletTrailSlot',
        [],
        [('Slot','int','Particles.TrailSlot'),('Lifetime','float','Particles.Lifetime'),('Age','float','Particles.Age'),
         ('Alive','bool','Particles.Alive'),('Position','Position','Particles.Position'),('Size','vec2','Particles.SpriteSize'),
         ('Color','Color','Particles.Color'),('LightEnabled','bool','Particles.BulletLightEnabled'),('Scattering','float','Particles.BulletLightScattering')],
        'Slot=ExecIndex();Lifetime=100000000.0;Age=0.0;Alive=true;Position=float3(0,0,0);Size=float2(0,0);Color=float4(0,0,0,0);LightEnabled=false;Scattering=0.0;')


def repair_slot_pools():
    scratch=scratch_helper()
    reports=[]
    for path in (MASS,LIGHTS):
        configure_slot_pool(path,scratch)
        compiled=NS.compile_with_results(path)
        require(compiled.success and compiled.error_count==0,str(compiled))
        diagnostics=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(unreal.load_asset(path))
        require('VM ERROR:' not in diagnostics and 'GPU ERROR:' not in diagnostics,diagnostics)
        require(NS.save_system(path),'Save fixed slot pool')
        reports.append({'path':path,'compile':str(compiled),'diagnostics':diagnostics,
            'loop':'Infinite','allocation_loops':1,'slot_index':'ExecIndex()'})
    return save_report('slot_pool_repair.json',{'success':True,'systems':reports,'visual_acceptance':'pending_user'})


def systems():
    require(hasattr(unreal.GuLiCombatEffectAuthoringLibrary,'configure_bullet_trail_system'),
            'Native bullet authoring API is not loaded; compile authorization and reload are required')
    ground=copy(SOURCE_GROUND,GROUND)
    require(EM.set_renderer_property(GROUND,'NE_forward_simple_bullet',0,'Material',GROUND_MATERIAL),'Ground candidate material')
    scratch=scratch_helper()
    reports=[]
    for path,lights in ((MASS,False),(LIGHTS,True)):
        system=copy(SOURCE_POOL,path)
        for emitter in list(NS.list_emitters(path)):
            if str(emitter.emitter_name)!='LaserBolts': require(NS.remove_emitter(path,str(emitter.emitter_name)),'Remove unused muzzle emitter')
        for module in list(EM.list_modules(path,'LaserBolts')):
            if str(module.module_name) not in ('EmitterState','ParticleState','SpawnBurst_Instantaneous'):
                require(EM.remove_module(path,'LaserBolts',str(module.module_name)),'Strip old slot module')
        for renderer in reversed(list(EM.list_renderers(path,'LaserBolts'))):
            if str(renderer.renderer_type)!=('Light' if lights else 'Sprite'):
                require(EM.remove_renderer(path,'LaserBolts',renderer.renderer_index),'Remove unrelated renderer')
        require(unreal.GuLiCombatEffectAuthoringLibrary.configure_bullet_trail_system(system,lights) is not None,'Configure candidate')
        configure_slot_pool(path,scratch)
        result=SP.create_scratch_module(path,'LaserBolts','ParticleUpdate','ReadBulletTrailPool')
        require(result.success,'Create array reader')
        script=unreal.load_object(None,str(SP.get_scratch_script_path(path,'LaserBolts',str(result.module_name))))
        require(script,'Load owned reader script')
        require(unreal.GuLiCombatEffectAuthoringLibrary.wire_bullet_trail_reader(system,script,lights) is not None,'Wire arrays')
        require(SP.apply_changes(path),'Apply array reader')
        if not lights:
            scratch(path,'LaserBolts','ParticleUpdate','EvaluateBulletTrail',
                [('Origin','Position','Particles.TrailOrigin'),('Direction','Vector','Particles.TrailDirection'),('Plane','Vector','Particles.TrailFacing'),
                 ('Times','vec4','Particles.TrailTimes'),('Speed','float','Particles.TrailSpeed'),('Tint','Color','Particles.TrailTint'),
                 ('Generation','int','Particles.TrailGeneration'),('Now','float','User.TrailNow'),('Style','vec4','User.TrailStyle'),
                 ('MaximumLength','float','User.TrailMaximumLength'),('Emission','float','User.TrailEmission')],
                [('Position','Position','Particles.Position'),('Alignment','Vector','Particles.SpriteAlignment'),('Facing','Vector','Particles.SpriteFacing'),
                 ('Size','vec2','Particles.SpriteSize'),('Color','Color','Particles.Color'),('Data','vec4','Particles.DynamicMaterialParameter'),
                 ('StrokeStyle','vec4','Particles.DynamicMaterialParameter1'),('Shape','vec4','Particles.DynamicMaterialParameter2')],EVALUATE_CODE)
            require(EM.set_renderer_property(path,'LaserBolts',0,'Material',MASS_MATERIAL),'Mass material')
        strip_legacy_parameters(path)
        compiled=NS.compile_with_results(path)
        require(compiled.success and compiled.error_count==0,str(compiled))
        diagnostics=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
        require('VM ERROR:' not in diagnostics and 'GPU ERROR:' not in diagnostics,diagnostics)
        if not lights:
            require('GPU finished=1 complete=1' in diagnostics,diagnostics)
        require(NS.save_system(path),'Save system')
        reports.append({'path':path,'compile':str(compiled),'diagnostics':diagnostics,'gpu':bool(NS.summarize(path).has_gpu_emitters)})
    compiled=NS.compile_with_results(GROUND)
    require(compiled.success and compiled.error_count==0,str(compiled));require(NS.save_system(GROUND),'Save Ground candidate')
    return save_report('systems.json',{'success':True,'systems':reports,'ground':GROUND,'PIE':False,'formal_references_changed':False})


def scene():
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype','Open existing CommanderMassPrototype first')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed=[]
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label()=='BulletTrails_Review_OptIn':
            removed.append(actor.get_path_name())
            require(actors.destroy_actor(actor),'Remove retired override actor')
    if removed:
        require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(),'Save existing map')
    require(not any(a.get_actor_label()=='BulletTrails_Review_OptIn' for a in actors.get_all_level_actors()),'Retired actor remains')
    return save_report('production_scene.json',{'success':True,'map':world.get_path_name(),
        'removed_override_actors':removed,'activation':'normal Ground and Mass weapon fire','extra_switch':False,'PIE':False})


def readback():
    result={'success':True,'PIE':False,'systems':[]}
    for path in (MASS,LIGHTS,GROUND):
        summary=NS.summarize(path)
        result['systems'].append({'path':path,'gpu':bool(summary.has_gpu_emitters),'parameters':list(map(str,summary.user_parameter_names)),
            'emitters':[{'name':str(e.emitter_name),'properties':str(EM.get_emitter_properties(path,str(e.emitter_name))),
                'renderers':[str(EM.get_renderer_details(path,str(e.emitter_name),r.renderer_index)) for r in EM.list_renderers(path,str(e.emitter_name))]}
                for e in NS.list_emitters(path)]})
    require(result['systems'][0]['gpu'] and not result['systems'][1]['gpu'] and not result['systems'][2]['gpu'],'Simulation targets')
    for system in result['systems']:
        require(len(system['emitters'])==1 and len(system['emitters'][0]['renderers'])==1,'One emitter/renderer per candidate')
    for system in result['systems'][:2]:
        require(not any(n.startswith(('User.Laser','User.Muzzle')) for n in system['parameters']),'Unused template interfaces remain')
    renderer=next(r for r in unreal.ObjectIterator(unreal.NiagaraLightRendererProperties) if r.get_path_name().startswith(LIGHTS+'.'))
    light_keys=('bUseInverseSquaredFalloff','bAlphaScalesBrightness','bAffectsTranslucency','bAllowMegaLights','bMegaLightsCastShadows','RadiusScale','DefaultExponent','SpecularScale','DiffuseScale')
    result['light_settings']={k:prop(renderer,k) for k in light_keys}
    require(not result['light_settings']['bAffectsTranslucency'] and not result['light_settings']['bAllowMegaLights']
            and not result['light_settings']['bMegaLightsCastShadows'],'Light budget features')
    result['light_bindings']={k:prop(renderer,k).export_text() for k in ('PositionBinding','ColorBinding','RadiusBinding','LightRenderingEnabledBinding','VolumetricScatteringBinding')}
    result['defaults']={}
    for path in (MASS,LIGHTS):
        settings=NS.get_all_editable_settings(path)
        result['defaults'][path]={str(p.setting_path):str(p.current_value) for p in settings.user_parameters}
    ground=unreal.load_asset(GROUND_MATERIAL)
    result['ground_material_parameters']={}
    for cls in (unreal.MaterialExpressionVectorParameter,unreal.MaterialExpressionScalarParameter):
        for node in unreal.ObjectIterator(cls):
            if node.get_outer()==ground:
                name=str(prop(node,'parameter_name'));value=prop(node,'default_value')
                result['ground_material_parameters'][name]=[value.r,value.g,value.b,value.a] if isinstance(value,unreal.LinearColor) else value
    require(result['ground_material_parameters']['Emission']==8.,'Ground emission')
    require(result['ground_material_parameters']['HeadColor']==result['ground_material_parameters']['TailColor'],'Purple tail remains')
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    result['formal_references']={r['Name']:r['ResourcePath'] for r in rows if r['Name'] in ('PlayerBullet','GroundMachineGunTracer')}
    require(result['formal_references']['PlayerBullet']==GROUND+'.NS_GroundBulletTrail_Orange' and result['formal_references']['GroundMachineGunTracer']==MASS+'.NS_MassBulletTrail_GPU','Formal references are not installed')
    result['retired_override_class_absent']=not hasattr(unreal,'GuLiBulletTrailReviewActor')
    require(result['retired_override_class_absent'],'Retired class is still loaded')
    return save_report('production_readback.json',result)


if __name__=='__main__':
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Do not interrupt a running play session')
    stage=globals().get('BULLET_TRAIL_STAGE','materials')
    result={'materials':materials,'systems':systems,'scene':scene,'readback':readback,'clean_parameters':clean_parameters,'repair_slot_pools':repair_slot_pools}[stage]()
    unreal.MCPythonHelper.submit_result(json.dumps(result,default=str))

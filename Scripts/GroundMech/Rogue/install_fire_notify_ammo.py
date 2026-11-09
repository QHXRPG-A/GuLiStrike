"""Scoped alternating loop and supplied missile adaptation; no marketplace edits."""
import ast,json,math,re,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundAlternatingLoop_20261108'
ANIMS='/Game/GuLiStrike/GroundMech/Animations/A4B2'
FLIGHT='/Game/GuLiStrike/FX/GroundMech/ExplosivePackage/NS_GroundExplosiveFlight'
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService

def require(v,message):
    if not v:raise RuntimeError(message)
    return v

def prop(obj,name):return obj.get_editor_property(name)

def animation():
    bp=require(ASSETS.load_asset(ANIMS+'/ABP_GroundMech_A4B2'),'Formal animation blueprint')
    source=require(ASSETS.load_asset(ANIMS+'/Anim_Mecha_Shoot'),'Formal firing source')
    skeleton=source.get_editor_property('skeleton')
    loop=ASSETS.load_asset(ANIMS+'/Anim_GroundFire_AlternatingLoop')
    if loop is None:loop=require(ASSETS.duplicate_asset(source.get_path_name(),ANIMS+'/Anim_GroundFire_AlternatingLoop'),'Duplicate loop')
    name='AM_GroundFire_AlternatingLoop';montage=ASSETS.load_asset(ANIMS+'/'+name)
    if montage is None:
        factory=unreal.AnimMontageFactory();factory.set_editor_property('target_skeleton',skeleton);factory.set_editor_property('source_animation',loop)
        montage=require(unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,ANIMS,unreal.AnimMontage,factory),'Create loop montage')
    result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_fire_animation(bp,source,loop,montage))
    require(result['success'],'Author alternating loop: '+str(result))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    component=unreal.new_object(unreal.SkeletalMeshComponent)
    instance=unreal.new_object(bp.generated_class(),outer=component)
    require(instance.get_editor_property('FireLoopMontage')==montage,'Fresh animation instance inherits loop')
    for obj in [skeleton,loop,montage,bp]:require(unreal.EditorAssetLibrary.save_loaded_asset(obj,False),'Save '+obj.get_path_name())
    result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.read_fire_animation(bp))
    require(result['success'],'Saved alternating loop')
    return result

def move_module(emitter,module,stage,index):
    info=next(m for m in EM.list_modules(FLIGHT,emitter) if str(m.module_name)==module)
    script=unreal.load_object(None,str(info.script_asset_path))
    require(unreal.GuLiCombatEffectAuthoringLibrary.move_client_effect_module(ASSETS.load_asset(FLIGHT),unreal.Name(emitter),script,stage,index) is not None,'Order '+module)

def missile():
    flight=require(ASSETS.load_asset(FLIGHT),'Project missile copy')
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf8'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'shared_scratch_helper','exec'),globals())
    row=next(r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf8')) if r['Id']==63)
    skill=next(r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeMech_Skills.json').read_text(encoding='utf8')) if r['Name']=='BasicMachinegun')
    effect=next(r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeRogueCards_GroundWeaponEffects.json').read_text(encoding='utf8')) if r['Id']==3)
    speed=skill['ProjectileSpeed']*effect['ExplosiveSpeedMultiplier'];life=skill['ProjectileLifetime']/effect['ExplosiveSpeedMultiplier']
    for name,kind,value in [('FlightActive','Float','1'),('FlightScale','Float',str(row['Scale']['X'])),('FlightVelocity','Vector',f'({speed},0,0)'),('FlightLifetime','Float',str(life)),('TrailHoldSeconds','Float',str(row['TrailHoldSeconds'])),('TrailFadeSeconds','Float',str(row['TrailFadeSeconds']))]:
        if not NS.parameter_exists(FLIGHT,'User.'+name):require(NS.add_user_parameter(FLIGHT,name,kind,value),'Add '+name)
        require(NS.set_parameter(FLIGHT,'User.'+name,value),'Default '+name)
    body='GuLiMissileBody';head='NE_spread_spark'
    if body not in [str(e.emitter_name) for e in NS.list_emitters(FLIGHT)]:require(NS.copy_emitter('/Game/Assets/Missile_Cluster/飞弹','fa001',FLIGHT,body),'Copy Missile_Cluster carrier')
    tails=['NE_FlipBookPointLocation002','NE_FlipBooks001','NE_FlipBookPointLocation004','RIbbonTrailFollower_14'];enabled=[head,body,*tails]
    for emitter in NS.list_emitters(FLIGHT):
        name=str(emitter.emitter_name);require(NS.enable_emitter(FLIGHT,name,name in enabled),'Enable flight layer '+name)
    for name in enabled:
        for m in list(EM.list_modules(FLIGHT,name)):
            if str(m.module_name).startswith(('GuLiCompanion','GuLiHeadScale','GuLiFlight','GuLiTrail','GuLiRender')):require(EM.remove_module(FLIGHT,name,str(m.module_name)),'Refresh '+str(m.module_name))
        for m in list(EM.list_modules(FLIGHT,name)):
            n=str(m.module_name);path=str(m.script_asset_path)
            disabled=any(k in path for k in ('SolveForcesAndVelocity','/Forces/','/Collision/','/Audio/','GenerateDeathEvent','GenerateCollisionEvent'))
            if name==body:disabled|=n not in ('EmitterState','InitializeParticle','UpdateAge','ParticleState','SpawnBurst_Instantaneous','UpdateMeshOrientation') and '/DynamicInputs/' not in path
            elif name in tails:disabled|=n.startswith(('SpawnRate','SpawnBurst','AddVelocity','ScaleSpriteSizeBySpeed','SpriteSizeScaleBySpeed'))
            if disabled and m.is_enabled:require(EM.enable_module(FLIGHT,name,n,False),'Disable autonomous '+name+'/'+n)
        if name in (head,body):
            require(EM.enable_module(FLIGHT,name,'SpawnBurst_Instantaneous',True),'Enable one carrier particle')
            require(NS.set_rapid_iteration_param_by_stage(FLIGHT,name,'EmitterUpdate',f'Constants.{name}.SpawnBurst_Instantaneous.Spawn Count','1'),'Single carrier burst')
            for stage,suffix in [('ParticleSpawn','Spawn'),('ParticleUpdate','Update')]:
                scratch(FLIGHT,name,stage,'GuLiFlight'+suffix,[('Owner','Position','Engine.Owner.Position'),('Velocity','Vector','User.FlightVelocity'),('Life','float','User.FlightLifetime'),('Active','float','User.FlightActive')],[('PositionOut','Position','Particles.Position'),('VelocityOut','Vector','Particles.Velocity'),('LifeOut','float','Particles.Lifetime'),('AliveOut','bool','Particles.Alive')],'PositionOut=Owner;VelocityOut=Velocity;LifeOut=Life;AliveOut=Active>0.5;')
            require(EM.enable_module(FLIGHT,name,'GenerateLocationEvent',name==body),'Carrier location stream')
            if name==body:move_module(name,'GenerateLocationEvent','ParticleUpdate',-1)
        else:
            # World-space event positions remain behind; tail particles never integrate carrier velocity.
            for stage,suffix in [('ParticleSpawn','Spawn'),('ParticleUpdate','Update')]:
                module=scratch(FLIGHT,name,stage,'GuLiTrail'+suffix,[('Hold','float','User.TrailHoldSeconds'),('Fade','float','User.TrailFadeSeconds')],[('LifeOut','float','Particles.Lifetime'),('VelocityOut','Vector','Particles.Velocity')],'LifeOut=Hold+Fade;VelocityOut=float3(0,0,0);')
                if stage=='ParticleUpdate':move_module(name,module,stage,0)
            scratch(FLIGHT,name,'ParticleUpdate','GuLiTrailFade',[('Tint','Color','Particles.Color'),('Age','float','Particles.Age'),('Hold','float','User.TrailHoldSeconds'),('Fade','float','User.TrailFadeSeconds')],[('TintOut','Color','Particles.Color')],'TintOut=float4(Tint.rgb,Tint.a*(1-saturate((Age-Hold)/max(Fade,0.001))));')
        types=[str(r.renderer_type) for r in EM.list_renderers(FLIGHT,name)]
        ins=[('Scale','float','User.FlightScale')];outs=[];code=''
        for token,variable,kind,alias,target in [('Sprite','SpriteSize','vec2','Sprite','SpriteSize'),('Mesh','Scale','Vector','Mesh','MeshScale'),('Ribbon','RibbonWidth','float','Ribbon','RibbonWidth')]:
            if any(token in t for t in types):
                ins.append(('Base'+alias,kind,'Particles.'+variable));outs.append(('Scaled'+alias,kind,'Particles.GuLiScaled'+target));code+='Scaled'+alias+'=Base'+alias+'*Scale;'
        for stage,suffix in [('ParticleSpawn','Spawn'),('ParticleUpdate','Update')]:scratch(FLIGHT,name,stage,'GuLiRender'+suffix,ins,outs,code)
    binding=json.loads(unreal.GuLiGroundMechFireEditorLibrary.configure_explosive_head(flight,True));require(binding['success'],'Renderer binding '+str(binding))
    result=NS.compile_with_results(FLIGHT);require(result.success,'Missile compile: '+str(result));require(NS.save_system(FLIGHT),'Save missile')
    return {'enabled_emitters':enabled,'binding':binding,'speed':speed,'lifetime':life,'scale':row['Scale'],'tail_hold':row['TrailHoldSeconds'],'tail_fade':row['TrailFadeSeconds'],'compile_errors':list(map(str,result.errors)),'source_carrier':'/Game/Assets/Missile_Cluster/飞弹','flight_source':'/Game/Assets/VFX/WeaponBulletVFX/NS/VFX_Missile','impact_source':'/Game/_VFXResources/Niagara_System/NS_Explosion_19'}

def tables():
    names=['DT_GuLiStrikeMech_Gait','DT_GuLiStrikeRogueCards_Effects','DT_GuLiStrikeRogueCards_GroundWeaponEffects','DT_GuLiStrikeVfx_Effects']
    metadata=json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf8'))['tables'];report=[]
    for name in names:
        source=(ROOT/'Data/Json'/f'{name}.json').read_text(encoding='utf-8-sig');table=require(unreal.load_asset('/Game/GuLiStrike/Data/'+name),'Existing '+name)
        require(table.get_editor_property('row_struct')==unreal.find_object(None,metadata[name]['struct']),'Loaded schema '+name)
        require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,source),'Import '+name);require(unreal.EditorAssetLibrary.save_loaded_asset(table,False),'Save '+name)
        expected=json.loads(source);actual=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table));by_key={r['Name']:r for r in actual}
        require(set(by_key)=={r['Name'] for r in expected},'Row identity '+name)
        for row in expected:
            for key,value in row.items():
                got=by_key[row['Name']].get(key)
                # UE's table JSON writer emits six significant digits for float properties.
                if isinstance(value,float):require(isinstance(got,(int,float)) and math.isclose(value,got,rel_tol=5e-6,abs_tol=1e-5),'Number '+key)
                elif isinstance(value,dict):
                    parts=dict(re.findall(r'(\w+)=([^,)]+)',str(got)));require(set(parts)==set(value) and all(math.isclose(float(parts[k]),v,rel_tol=1e-6,abs_tol=1e-5) for k,v in value.items()),'Struct '+key)
                else:
                    if isinstance(got,str) and "'" in got and got.endswith("'"):got=got.split("'",1)[1][:-1]
                    if got=='None':got=''
                    require(got==value,'Field '+name+'/'+row['Name']+'.'+key)
        report.append({'table':name,'rows':len(actual),'saved_readback':True})
    return report

def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active play')
    require(not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() or globals().get('GULI_FIRE_OWNED_RETRY',False),'Preserve unsaved content')
    stage=globals().get('GULI_FIRE_STAGE','all');report={'success':True,'stage':stage,'assistant_visual_test':False}
    for name,fn in [('animation',animation),('missile',missile),('tables',tables)]:
        if stage in ('all',name):report[name]=fn()
    return report
try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/('asset_install_'+globals().get('GULI_FIRE_STAGE','all')+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))

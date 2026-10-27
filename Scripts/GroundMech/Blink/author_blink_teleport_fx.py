"""Finite endpoint-teleport VFX. No skeletal DI, model material override, or moving Loop."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/FX/GroundMech/Blink_20261027/Shape_E_v1'
DEST='/Game/GuLiStrike/FX/GroundMech/Blink'
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
AS=unreal.EditorAssetLibrary
MAT=unreal.MaterialEditingLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
TEMPLATE='/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain'
CFG=next(r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeMech_Skills.json').read_text(encoding='utf8')) if r['Name']=='Blink')
REPORT={'saved':[],'systems':[],'model_material_overrides':False}


def need(v,reason):
    if not v:raise RuntimeError(reason)
    return v


def save(obj):
    p=obj if isinstance(obj,str) else obj.get_path_name()
    need(p.startswith(DEST+'/'),'Out of scope save')
    need(AS.save_asset(p,only_if_is_dirty=True),'Save '+p)
    REPORT['saved'].append(p)


def material(name,ribbon):
    path=DEST+'/'+name
    m=AS.load_asset(path) if AS.does_asset_exist(path) else TOOLS.create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    MAT.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided',True)
    MAT.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_NIAGARA_RIBBONS if ribbon else unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv=MAT.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate,-650,180)
    color=MAT.create_material_expression(m,unreal.MaterialExpressionParticleColor,-650,-100)
    mask=MAT.create_material_expression(m,unreal.MaterialExpressionCustom,-400,180)
    mask.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    param=unreal.CustomInput();param.set_editor_property('input_name','UV');mask.set_editor_property('inputs',[param])
    code=('float x=abs(UV.y*2-1); return pow(saturate(1-x),1.5);' if ribbon else
          'float2 p=UV*2-1; float d=abs(p.x)*1.6+abs(p.y)*.65; return step(d,1.0)*pow(saturate(1-d),.25);')
    mask.set_editor_property('code',code);need(MAT.connect_material_expressions(uv,'',mask,'UV'),'Mask UV')
    opacity=MAT.create_material_expression(m,unreal.MaterialExpressionMultiply,-150,180)
    need(MAT.connect_material_expressions(mask,'',opacity,'A'),'Mask')
    need(MAT.connect_material_expressions(color,'A',opacity,'B'),'Opacity alpha')
    need(MAT.connect_material_property(color,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'Emission')
    need(MAT.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY),'Opacity')
    MAT.recompile_material(m);save(m)
    REPORT.setdefault('materials',{})[path]=str(unreal.MaterialNodeService.get_material_diagnostics(path))
    return path


def scratch(path,emitter,stage,name,inputs,outputs,code):
    r=SP.create_scratch_module(path,emitter,stage,name);need(r.success,'Scratch '+name);module=str(r.module_name)
    h=SP.add_custom_hlsl_node(path,emitter,module,code);need(h.success,'HLSL '+name);hid=str(h.node_id)
    read=SP.add_node(path,emitter,module,'MapGet');need(read.success,'MapGet');rid=str(read.node_id)
    inp=next(n for n in SP.list_nodes(path,emitter,module) if str(n.node_type).endswith('NodeInput') or str(n.node_type)=='Input')
    iid=str(inp.node_id)
    ipin=next(str(p.pin_name) for p in SP.get_node_pins(path,emitter,module,iid) if str(p.direction).lower()=='output')
    rpin=next(str(p.pin_name) for p in SP.get_node_pins(path,emitter,module,rid) if str(p.direction).lower()=='input')
    need(SP.connect_pins(path,emitter,module,iid,ipin,rid,rpin),'Map wire')
    for pin,kind,var in inputs:
        need(SP.add_pin(path,emitter,module,rid,'Output',kind,var).success,'Read '+var)
        need(SP.add_pin(path,emitter,module,hid,'Input',kind,pin).success,'Input '+pin)
        need(SP.connect_pins(path,emitter,module,rid,var,hid,pin),'Input wire '+pin)
    for pin,kind,var in outputs:
        need(SP.add_pin(path,emitter,module,hid,'Output',kind,pin).success,'Output '+pin)
        target=SP.add_module_output(path,emitter,module,var,kind);need(target.success,'Write '+var)
        need(SP.connect_pins(path,emitter,module,hid,pin,str(target.node_id),var),'Output wire '+pin)
    need(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(AS.load_asset(path)),'Pin order')
    need(SP.apply_changes(path),'Apply '+name)


def ri(path,emitter,stage,module,key,value):
    need(NS.set_rapid_iteration_param_by_stage(path,emitter,stage,f'Constants.{emitter}.{module}.{key}',str(value)),'RI '+key)


def system(name,emitter,count,lifetime):
    path=DEST+'/'+name
    if not AS.does_asset_exist(path):need(NS.create_system(name,DEST).success,'Create '+name)
    for e in list(NS.list_emitters(path)):need(NS.remove_emitter(path,str(e.emitter_name)),'Replace owned emitter')
    need(NS.add_emitter(path,TEMPLATE,emitter),'Add emitter')
    for m in list(EM.list_modules(path,emitter)):
        if str(m.module_name) not in ('EmitterState','InitializeParticle','ParticleState'):need(EM.remove_module(path,emitter,str(m.module_name)),'Strip template')
    need(EM.add_module(path,emitter,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate'),'Burst')
    ri(path,emitter,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Count',count)
    ri(path,emitter,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Time',0)
    ri(path,emitter,'EmitterUpdate','EmitterState','Loop Duration',lifetime+.05)
    need(EM.set_module_input(path,emitter,'EmitterState','Life Cycle Mode','NewEnumerator1'),'Self lifecycle')
    need(EM.set_module_input(path,emitter,'EmitterState','Loop Behavior','NewEnumerator1'),'Once lifecycle')
    for param,kind,value in [('User.Lifetime','float',lifetime),('User.Emission','float',CFG['BlinkEnergyEmission'])]:
        if not NS.parameter_exists(path,param):need(NS.add_user_parameter(path,param,kind,str(value)),'Parameter '+param)
        need(NS.set_parameter(path,param,str(value)),'Default '+param)
    obj=AS.load_asset(path);obj.set_editor_property('max_pool_size',8);obj.set_editor_property('pool_prime_size',0)
    return path


def finish(path):
    r=NS.compile_with_results(path)
    row={'path':path,'success':r.success,'errors':list(map(str,r.errors)),'warnings':list(map(str,r.warnings)),
         'emitters':list(NS.summarize(path).emitter_names)}
    REPORT['systems'].append(row)
    need(r.success and not r.errors,'Niagara compile '+json.dumps(row))
    save(path)


def add_armor_flecks(path):
    e='ArmorFlecks'
    need(NS.add_emitter(path,TEMPLATE,e),'Add armor flecks')
    for m in list(EM.list_modules(path,e)):
        if str(m.module_name) not in ('EmitterState','InitializeParticle','ParticleState'):need(EM.remove_module(path,e,str(m.module_name)),'Strip fleck template')
    need(EM.add_module(path,e,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate'),'Fleck burst')
    ri(path,e,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Count',CFG['BlinkShardCount'])
    ri(path,e,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Time',0)
    ri(path,e,'EmitterUpdate','EmitterState','Loop Duration',CFG['BlinkRibbonLifetimeSeconds']+.05)
    need(EM.set_module_input(path,e,'EmitterState','Life Cycle Mode','NewEnumerator1'),'Fleck lifecycle')
    need(EM.set_module_input(path,e,'EmitterState','Loop Behavior','NewEnumerator1'),'Fleck once')
    for param,kind,value in [('User.ConcealDuration','float',CFG['BlinkConcealSeconds']),('User.ShardDuration','float',CFG['BlinkShardDuration']),
                              ('User.ShardSpeed','float',CFG['BlinkShardSpeed']),('User.Forward','Vector','(X=1,Y=0,Z=0)')]:
        if not NS.parameter_exists(path,param):need(NS.add_user_parameter(path,param,kind,str(value)),'Fleck parameter')
        need(NS.set_parameter(path,param,str(value)),'Fleck default')
    mat=material('M_GroundMech_BlinkArmorFleck',False)
    need(EM.set_renderer_property(path,e,0,'Material',mat),'Fleck material')
    inputs=[('SampleIndex','int','Particles.UniqueID'),('Owner','Position','Engine.Owner.Position'),('Forward','Vector','User.Forward'),
            ('Duration','float','User.Lifetime'),('Power','float','User.Emission'),('Transit','float','User.ConcealDuration'),
            ('ShardTime','float','User.ShardDuration'),('Speed','float','User.ShardSpeed')]
    outputs=[('Pos','Position','Particles.Position'),('Vel','Vector','Particles.Velocity'),('Life','float','Particles.Lifetime'),
             ('Tint','Color','Particles.Color'),('Size','vec2','Particles.SpriteSize'),('Spin','float','Particles.SpriteRotation')]
    shape=f'''float k=fmod(float(SampleIndex),{int(CFG['BlinkShardCount'])}.0)+1;
float h=abs(frac(sin(k*12.9898)*43758.5453)); float g=abs(frac(sin(k*78.233)*19341.314));
float f=abs(frac(sin(k*39.425)*13763.321)); float group=floor(fmod(k,4.0));
float u=(h*2-1)*110; float z=(g*2-1)*180;
u=group==1?(h<.5?-185.0:185.0):u; z=group==1?30+g*150:z;
u=group==2?(h<.5?-65.0:65.0):u; z=group==2?-220+g*145:z;
z=group==3?160+g*65:z;
float birth=f*Transit; float age=max(0.0,T-birth); float q=saturate(age/max(ShardTime,.01));
float3 dir=normalize(Forward); float3 side=normalize(cross(dir,float3(0,0,1))+float3(.0001,0,0));
Pos=Owner+side*(u+(g*2-1)*age*180)+float3(0,0,z+age*(h*2-1)*100)-dir*age*Speed;
Vel=-dir*Speed; Life=Duration; float size=lerp(8.0,28.0,h)*(1-q*.6); Size=float2(size,size*lerp(.3,1.1,g));
float alpha=(T>=birth?1.0:0.0)*(1-q)*(1-q); Tint=float4(lerp(float3(.08,.9,.015),float3(.7,1,.4),h)*Power*1.2,alpha);
Spin=h*6.2831853+age*(g*2-1)*3.0;'''
    scratch(path,e,'ParticleSpawn','ArmorPeelSpawn',inputs,outputs,'float T=0;'+shape)
    scratch(path,e,'ParticleUpdate','ArmorPeelDrift',inputs+[('T','float','Particles.Age')],outputs,shape)


def main():
    need(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Stop PIE before authoring')
    OUT.mkdir(parents=True,exist_ok=True)
    strand=material('M_GroundMech_BlinkStrand',True)
    count=int(CFG['BlinkRibbonCount']);points=int(CFG['BlinkRibbonPoints'])
    path=system('NS_GroundMech_Blink','Strands',count*points,CFG['BlinkRibbonLifetimeSeconds'])
    # A short world-space vector relative to the LWC-aware owner position avoids
    # a second absolute float world position and works with the installed API.
    if not NS.parameter_exists(path,'User.BlinkOffset'):
        need(NS.add_user_parameter(path,'User.BlinkOffset','Vector','(X=-1200,Y=0,Z=0)'),'Path offset parameter')
    need(NS.set_parameter(path,'User.BlinkOffset',f"(X=-{CFG['BlinkTrailLengthCm']},Y=0,Z=0)"),'Preview trail length')
    if not NS.parameter_exists(path,'User.Width'):need(NS.add_user_parameter(path,'User.Width','float',str(CFG['BlinkRibbonWidthCm'])),'Width parameter')
    need(NS.set_parameter(path,'User.Width',str(CFG['BlinkRibbonWidthCm'])),'Width default')
    need(EM.remove_renderer(path,'Strands',0),'Remove sprite')
    need(EM.add_renderer(path,'Strands','Ribbon'),'Ribbon renderer')
    need(EM.set_renderer_property(path,'Strands',0,'Material',strand),'Strand material')
    inputs=[('SampleIndex','int','Particles.UniqueID'),('Owner','Position','Engine.Owner.Position'),('Offset','Vector','User.BlinkOffset'),
            ('Duration','float','User.Lifetime'),('Power','float','User.Emission'),('BaseWidth','float','User.Width')]
    outputs=[('Pos','Position','Particles.Position'),('Vel','Vector','Particles.Velocity'),('Life','float','Particles.Lifetime'),
             ('Tint','Color','Particles.Color'),('Width','float','Particles.RibbonWidth'),('Order','float','Particles.RibbonLinkOrder'),('Id','int','Particles.BlinkRibbonId')]
    shape=f'''float3 B=Owner; float3 A=Owner+Offset;
float k=fmod(float(SampleIndex),{count*points}.0); float strand=floor(k / {points}.0);
Id=int(strand); float j=fmod(k,{points}.0); float t=j / {points-1}.0; Order=t;
float r=strand+1.0; float h=abs(frac(sin(r*12.9898)*43758.5453)); float g=abs(frac(sin(r*78.233)*19341.314));
float f=abs(frac(sin(r*39.425)*13763.321)); float3 axis=normalize(B-A+float3(.001,0,0));
float3 side=normalize(cross(axis,float3(0,0,1))+float3(.0001,0,0)); float3 up=cross(side,axis);
float width=210.0; float z=220.0;
float arc=(fmod(strand,9.0)<.5?150.0:20.0)*sin(t*3.14159265);
Pos=lerp(A,B,t)+side*((h*2-1)*width+arc*(g*2-1))+up*((g*2-1)*z+arc*sin(t*3.14159265+f*6.28));
Life=Duration; Vel=float3(0,0,0); Width=BaseWidth*lerp(.75,1.45,f);
float q=saturate(T/Duration); float v=saturate((q-.08)/.92); float fade=1-v*v*(3-2*v);
Tint=float4(lerp(float3(.025,.85,.006),float3(.28,1.0,.055),h*.7)*Power,fade*(.85+.15*h));'''
    scratch(path,'Strands','ParticleSpawn','BlinkStrandsSpawn',inputs,outputs,'float T=0;'+shape)
    scratch(path,'Strands','ParticleUpdate','BlinkStrandsFade',inputs+[('T','float','Particles.Age')],outputs,shape)
    need(unreal.GuLiCombatEffectAuthoringLibrary.configure_ground_blink_ribbons(AS.load_asset(path)),'Bind independent strand IDs')
    add_armor_flecks(path)
    finish(path)
    REPORT.update({'ribbon_count':count,'points_per_ribbon':points,'total_ribbon_points':count*points,
                   'endpoint_clouds':0,'moving_mech_shape':True,'visual_trail_cm':CFG['BlinkTrailLengthCm'],'armor_flecks':CFG['BlinkShardCount'],'source_vendor_modified':False})
    (OUT/'fx_readback.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ribbons':count,'points':count*points,'systems':REPORT['systems']}))


main()

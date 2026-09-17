"""Project-owned variants of the three approved marketplace explosions.

The vendor systems and their fire/smoke lifetime curves remain untouched.
Run in the rendered editor using commander_editor_python.py.
"""
import unreal,json,traceback,ast
from pathlib import Path

ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StylePass_20260917'
DEST='/Game/GuLiStrike/FX/CombatExplosions'
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
LIB=unreal.EditorAssetLibrary;MAT=unreal.MaterialEditingLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
OWNER='GuLi.ReferenceExplosions.20260917'
REPORT={'success':False,'systems':[]}
SYSTEM=''

def need(value,message):
    if not value:raise RuntimeError(message)
    return value

def save(asset):
    need(asset.get_path_name().startswith(DEST+'/'),'Save scope')
    LIB.set_metadata_tag(asset,'GuLi.VFX.Owner',OWNER)
    need(LIB.save_loaded_asset(asset,False),'Save '+asset.get_path_name())

def owned_duplicate(source,path):
    obj=unreal.load_asset(path)
    if obj:
        need(LIB.get_metadata_tag(obj,'GuLi.VFX.Owner')==OWNER,'Unowned '+path)
        return obj
    LIB.make_directory(path.rsplit('/',1)[0])
    obj=need(LIB.duplicate_asset(source,path),'Duplicate '+path)
    LIB.set_metadata_tag(obj,'GuLi.VFX.Owner',OWNER)
    return obj

# Reuse the established, compiled scratch graph authoring helper without
# executing the old explosion builder or modifying any of its assets.
tree=ast.parse((ROOT/'Scripts/build_wingman_toon_explosion.py').read_text(encoding='utf8'))
helper=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
exec(compile(ast.Module(body=[helper],type_ignores=[]),'<shared Niagara scratch helper>','exec'))

def ring_material(sprite):
    name='M_AirShockwave' if sprite else 'M_GroundShockwave';path=DEST+'/'+name
    m=unreal.load_asset(path)
    if m:need(LIB.get_metadata_tag(m,'GuLi.VFX.Owner')==OWNER,'Unowned material')
    else:m=TOOLS.create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    MAT.delete_all_material_expressions(m)
    for key,value in [('blend_mode',unreal.BlendMode.BLEND_ADDITIVE),('shading_model',unreal.MaterialShadingModel.MSM_UNLIT),('two_sided',True)]:m.set_editor_property(key,value)
    MAT.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES if sprite else unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
    pc=MAT.create_material_expression(m,unreal.MaterialExpressionParticleColor)
    MAT.connect_material_property(pc,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if sprite:
        uv=MAT.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate)
        h=MAT.create_material_expression(m,unreal.MaterialExpressionCustom)
        h.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        h.set_editor_property('code','float r=length(UV*2-1); return smoothstep(.94,.96,r)*(1-smoothstep(.99,1.,r))*Alpha;')
        ins=[]
        for n in ['UV','Alpha']:
            i=unreal.CustomInput();i.set_editor_property('input_name',n);ins.append(i)
        h.set_editor_property('inputs',ins)
        need(MAT.connect_material_expressions(uv,'',h,'UV'),'Ring UV')
        need(MAT.connect_material_expressions(pc,'A',h,'Alpha'),'Ring alpha')
        MAT.connect_material_property(h,'',unreal.MaterialProperty.MP_OPACITY)
    else:MAT.connect_material_property(pc,'A',unreal.MaterialProperty.MP_OPACITY)
    MAT.layout_material_expressions(m);MAT.recompile_material(m);save(m)
    return path

def ri(emitter,stage,module,key,value):
    need(NS.set_rapid_iteration_param_by_stage(SYSTEM,emitter,stage,'Constants.'+emitter+'.'+module+'.'+key,str(value)),'RI '+emitter+'/'+key)

def add_shockwave(air):
    for e in NS.list_emitters(SYSTEM):
        if str(e.emitter_name) in ('NE_AirShockwave','NE_GroundShockwave'):need(NS.remove_emitter(SYSTEM,str(e.emitter_name)),'Replace unfinished wave')
    emitter=NS.add_emitter(SYSTEM,'/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain','NE_AirShockwave' if air else 'NE_GroundShockwave')
    need(emitter,'Shockwave emitter')
    for m in list(EM.list_modules(SYSTEM,emitter)):
        if str(m.module_name) not in ('EmitterState','InitializeParticle','ParticleState'):need(EM.remove_module(SYSTEM,emitter,str(m.module_name)),'Strip template')
    need(EM.add_module(SYSTEM,emitter,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate'),'Wave burst')
    ri(emitter,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Count',1)
    ri(emitter,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Time',.05)
    ri(emitter,'EmitterUpdate','EmitterState','Loop Duration',.6)
    need(EM.set_module_input(SYSTEM,emitter,'EmitterState','Loop Behavior','NewEnumerator1'),'Once')
    if not NS.parameter_exists(SYSTEM,'User.ShockwaveDiameter'):
        need(NS.add_user_parameter(SYSTEM,'User.ShockwaveDiameter','float','1050' if air else '950'),'Wave diameter')
    common=[('Owner','Position','Engine.Owner.Position'),('Area','float','User.Area_Scale'),('Diameter','float','User.ShockwaveDiameter')]
    outputs=[('Life','float','Particles.Lifetime'),('Pos','Position','Particles.Position'),('Vel','Vector','Particles.Velocity'),('Tint','Color','Particles.Color'),('Size','vec2','Particles.SpriteSize'),('Scale','Vector','Particles.Scale')]
    shape='float q=saturate(T/.5); float d=Diameter*max(Area,.001)*(.12+.88*(1-(1-q)*(1-q))); float f=saturate((q-.55)/.45); f=f*f*(3-2*f); Life=.5; Vel=float3(0,0,0); Tint=float4(2.2,1.55,.65,1-f); Size=float2(d,d); Scale=float3(d/200,d/200,1); '
    shape+=('Pos=Owner;' if air else 'Pos=Owner+float3(0,0,12*Area);')
    scratch(emitter,'ParticleSpawn','WaveInitialize',common,outputs,'float T=0; '+shape)
    scratch(emitter,'ParticleUpdate','WaveExpand',common+[('T','float','Particles.Age')],outputs,shape)
    material=ring_material(air)
    if air:
        need(EM.set_renderer_property(SYSTEM,emitter,0,'Material',material),'Air material')
        need(EM.set_renderer_property(SYSTEM,emitter,0,'FacingMode','FaceCamera'),'Air facing')
    else:
        mesh_path=DEST+'/SM_ShockwaveRing'
        mesh=owned_duplicate('/Game/GuLiStrike/FX/WingmanWeapons/StylizedExplosion/SM_WingmanToon_Ring128',mesh_path)
        mesh.set_material(0,unreal.load_asset(material));save(mesh)
        need(EM.remove_renderer(SYSTEM,emitter,0),'Remove sprite')
        need(EM.add_renderer(SYSTEM,emitter,'Mesh'),'Ring mesh renderer')
        need(EM.set_renderer_property(SYSTEM,emitter,0,'Meshes','((Mesh="'+mesh_path+'"))'),'Ring mesh')
        need(EM.set_renderer_property(SYSTEM,emitter,0,'bOverrideMaterials','True'),'Ring override')
        need(EM.set_renderer_property(SYSTEM,emitter,0,'OverrideMaterials','((ExplicitMat="'+material+'"))'),'Ring material')

def main():
    global SYSTEM
    need(not unreal.WidgetService.is_pie_running(),'PIE active')
    cache=json.loads((OUT/'explosion_parameters.json').read_text(encoding='utf8'))
    for number,name in [('03','NS_GroundDestruction_03'),('05','NS_WingmanDestruction_05'),('01','NS_WingmanBombardment_01')]:
        SYSTEM=DEST+'/'+name
        obj=owned_duplicate('/Game/_VFXResources/Niagara_System/NS_Explosion_'+number,SYSTEM)
        # Version marker makes repeated deployment preserve reviewed authoring.
        if LIB.get_metadata_tag(obj,'GuLi.VFX.Revision')=='2':
            REPORT['systems'].append({'path':SYSTEM,'already_authored':True});continue
        changes=[]
        for entry in cache[number]:
            e=entry['name'];params=entry['parameters']
            for p in params:
                if p['stage']=='EmitterUpdate' and p['name'].endswith('.EmitterState.MaxDistance'):
                    ri(e,'EmitterUpdate','EmitterState','MaxDistance',180000)
            # Only replace bursts whose count is linked to Area_Scale. This
            # preserves authoring-scale-one counts and separates area from cost.
            products=[p for p in params if p['stage']=='EmitterUpdate' and '.Multiply_Float' in p['name'] and p['name'].endswith('.A')]
            if products:
                need(len(products)==1,'Ambiguous burst product '+e)
                count=int(round(float(products[0]['value'])))
                burst=next(m for m in EM.list_modules(SYSTEM,e) if str(m.module_name)=='SpawnBurst_Instantaneous')
                vals={p['name'].split('.SpawnBurst_Instantaneous.',1)[1]:p['value'] for p in params if p['stage']=='EmitterUpdate' and '.SpawnBurst_Instantaneous.' in p['name']}
                need(EM.remove_module(SYSTEM,e,'SpawnBurst_Instantaneous'),'Replace scaled burst')
                before={str(m.module_name) for m in EM.list_modules(SYSTEM,e)}
                need(EM.add_module(SYSTEM,e,str(burst.script_asset_path),'EmitterUpdate'),'Fixed burst')
                new=next(str(m.module_name) for m in EM.list_modules(SYSTEM,e) if 'SpawnBurst_Instantaneous' in str(m.module_name) and str(m.module_name) not in before)
                for key in ('Spawn Time','Spawn Group','Spawn Probability'):
                    if key in vals:ri(e,'EmitterUpdate',new,key,vals[key])
                ri(e,'EmitterUpdate',new,'Spawn Count',count)
                changes.append({'emitter':e,'fixed_burst_count':count})
            if number=='01' and e in ('NE_CloudOut_01','NE_GlowOut','NE_DustOut_01'):
                # These are the source's outward-moving annulus layers. Only
                # initial radial displacement/velocity change; core curves stay.
                scratch(e,'ParticleSpawn','CompactExpansion', [('P','Position','Particles.Position'),('V','Vector','Particles.Velocity')], [('OutP','Position','Particles.Position'),('OutV','Vector','Particles.Velocity')], 'OutP=float3(P.xy*.70,P.z); OutV=float3(V.xy*.70,V.z);')
                changes.append({'emitter':e,'radial_scale':.70})
            if number=='01' and e=='NE_Ground_01':
                ri(e,'ParticleSpawn','InitializeParticle','Uniform Sprite Size',1750)
        if number!='01':add_shockwave(number=='05')
        obj.set_editor_property('max_pool_size',32)
        obj.set_editor_property('pool_prime_size',0)
        result=NS.compile_with_results(SYSTEM)
        need(result.success and not result.errors,'Compile '+SYSTEM+': '+str(result))
        LIB.set_metadata_tag(obj,'GuLi.VFX.Revision','2');save(obj)
        REPORT['systems'].append({'path':SYSTEM,'source':'NS_Explosion_'+number,'compile':str(result),'changes':changes,'emitters':[str(x.emitter_name) for x in NS.list_emitters(SYSTEM)],'parameters':[str(p) for p in NS.list_parameters(SYSTEM)]})
    REPORT['success']=True

try:main()
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'reference_explosions_build.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'success':REPORT['success'],'error':REPORT.get('error'),'systems':len(REPORT['systems'])}))

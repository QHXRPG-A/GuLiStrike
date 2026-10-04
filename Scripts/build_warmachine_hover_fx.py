"""Task-owned translucent hover jets and world-space residue, using the existing array-pool contract.

No template is modified. The second emitter uses unaligned sprites: its persistent
slots carry independent world snapshots, never ribbon links to a reused nozzle.
"""
import ast,json,traceback
from pathlib import Path
import unreal
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineHover_20260929';OUT.mkdir(parents=True,exist_ok=True)
DEST='/Game/GuLiStrike/FX/WarMachineHover';SYSTEM=DEST+'/NS_WarMachineHoverPool'
SOURCE='/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool'
LIB=unreal.EditorAssetLibrary;EDIT=unreal.MaterialEditingLibrary
NS=unreal.NiagaraService;EM=unreal.NiagaraEmitterService
SP=unreal.NiagaraScratchPadService;ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
r={'success':False,'source':SOURCE,'system':SYSTEM,'saved':[]}
def require(value,message):
    if not value:raise RuntimeError(message)
    return value
def prop(value,name):return value.get_editor_property(name)
def node(mat,cls,**props):
    n=EDIT.create_material_expression(mat,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def link(a,b,pin='',out=''):
    assert EDIT.connect_material_expressions(a,out,b,pin)
def make_material(name,trail):
    path=DEST+'/'+name
    mat=unreal.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    assert mat
    # This script owns the complete graph of these two new assets only.
    while EDIT.get_num_material_expressions(mat):EDIT.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',True)
    EDIT.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv=node(mat,unreal.MaterialExpressionTextureCoordinate)
    tint=node(mat,unreal.MaterialExpressionParticleColor)
    mask_code=('float2 p=(UV-.5)*2; return pow(saturate(1-dot(p,p)),2)*Alpha;'
        if trail else 'float t=saturate(1-UV.y); float width=lerp(1.0,.14,t); float x=abs(UV.x*2-1)/width; '
        'return pow(saturate(1-x*x),.65)*smoothstep(0,.06,t)*pow(saturate(1-t),.5)*Alpha;')
    def custom(code,output,names):
        n=node(mat,unreal.MaterialExpressionCustom,code=code,output_type=output)
        pins=[]
        for name in names:
            p=unreal.CustomInput();p.set_editor_property('input_name',name);pins.append(p)
        n.set_editor_property('inputs',pins);return n
    color=custom('float core=pow(saturate(1-abs(UV.x*2-1)*2.8),2); return lerp(Tint,float3(.55,.85,1.0),core*.7)*1.5;',unreal.CustomMaterialOutputType.CMOT_FLOAT3,['UV','Tint'])
    link(uv,color,'UV');link(tint,color,'Tint','RGB')
    assert EDIT.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mask=custom(mask_code,unreal.CustomMaterialOutputType.CMOT_FLOAT1,['UV','Alpha'])
    link(uv,mask,'UV');link(tint,mask,'Alpha','A')
    fade=node(mat,unreal.MaterialExpressionDepthFade,fade_distance_default=25.0)
    link(mask,fade,'Opacity')
    assert EDIT.connect_material_property(fade,'',unreal.MaterialProperty.MP_OPACITY)
    EDIT.layout_material_expressions(mat);EDIT.recompile_material(mat)
    r.setdefault('materials',{})[path]=str(unreal.MaterialNodeService.get_material_diagnostics(path))
    assert LIB.save_loaded_asset(mat,False);r['saved'].append(path)
    return path
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert hasattr(unreal.GuLiCombatEffectAuthoringLibrary,'configure_war_machine_hover_system'), 'Native GPU authoring bridge not loaded; compile the new Editor code before building the final GPU System.'
    core=make_material('M_WarMachineHoverJet',False)
    tail=make_material('M_WarMachineHoverTrail',True)
    r['source_summary']=str(NS.summarize(SOURCE))
    system=unreal.load_asset(SYSTEM) if LIB.does_asset_exist(SYSTEM) else LIB.duplicate_asset(SOURCE,SYSTEM)
    assert system
    for entry in list(NS.list_emitters(SYSTEM)):
        assert NS.remove_emitter(SYSTEM,str(prop(entry,'emitter_name')))
    assert unreal.GuLiCombatEffectAuthoringLibrary.configure_war_machine_hover_system(system) is not None
    for name,kind,value in [('HoverTime','Float','0')]:
        if not NS.parameter_exists(SYSTEM,'User.'+name):assert NS.add_user_parameter(SYSTEM,name,kind,value)
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    exec(compile(ast.Module(body=[function],type_ignores=[]),'shared_scratch_helper','exec'),globals())
    for emitter,mat,count,alignment in [('LaserBolts',core,1024,'CustomAlignment'),('LaserMuzzles',tail,10240,'Unaligned')]:
        is_tail=emitter=='LaserMuzzles'
        added=NS.add_emitter(SYSTEM,'/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst',emitter);assert added==emitter
        for entry in list(EM.list_modules(SYSTEM,emitter)):
            name=str(prop(entry,'module_name'))
            if not name.startswith(('EmitterState','ParticleState')):assert EM.remove_module(SYSTEM,emitter,name)
        assert EM.add_module(SYSTEM,emitter,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate')
        assert NS.set_rapid_iteration_param_by_stage(SYSTEM,emitter,'EmitterUpdate',f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count',str(count))
        outputs=[('Slot','int','Particles.LaserSlot'),('Life','float','Particles.Lifetime'),('Position','Position','Particles.Position'),
            ('Velocity','Vector','Particles.Velocity'),('Size','vec2','Particles.SpriteSize'),('Color','Color','Particles.Color'),('Rotation','float','Particles.SpriteRotation')]
        # GPU execution index is an intrinsic, not a user/map variable (which defaults to zero).
        code='Slot=ExecIndex(); Life=1000000000.0; Position=float3(0,0,0); Velocity=float3(0,0,0); Size=float2(0,0); Color=float4(0,0,0,0); Rotation=0;'
        if is_tail:
            outputs += [('Lane','int','Particles.HoverLane'),('Anchor','Position','Particles.HoverAnchor'),('Down','Vector','Particles.HoverDown'),
                ('Born','float','Particles.HoverBorn'),('Width','float','Particles.HoverWidth'),('Generation','float','Particles.HoverGeneration'),('Bucket','int','Particles.HoverBucket'),
                ('ViewFade','float','Particles.HoverViewFade')]
            code += 'Slot=ExecIndex() / 10; Lane=ExecIndex() % 10; Anchor=float3(0,0,0); Down=float3(0,0,-1); Born=-100000; Width=0; Generation=-1; Bucket=-1; ViewFade=0;'
        scratch(SYSTEM,emitter,'ParticleSpawn','InitializeHoverLane',[],outputs,code)
        reader=SP.create_scratch_module(SYSTEM,emitter,'ParticleUpdate','ReadHoverNozzle')
        assert prop(reader,'success')
        result=unreal.GuLiCombatEffectAuthoringLibrary.wire_laser_pool_reader(system,unreal.load_object(None,str(prop(reader,'script_path'))),is_tail)
        assert result is not None
        assert SP.apply_changes(SYSTEM)
        if is_tail:
            inputs=[('NozzlePosition','Position','Particles.Position'),('NozzleDirection','Vector','Particles.SpriteAlignment'),
                ('NozzleSize','vec2','Particles.SpriteSize'),('Meta','Color','Particles.Color'),('Lane','int','Particles.HoverLane'),
                ('Now','float','User.HoverTime'),('Camera','Position','User.HoverCamera'),('SavedAnchor','Position','Particles.HoverAnchor'),
                ('SavedDirection','Vector','Particles.HoverDown'),('Born','float','Particles.HoverBorn'),('SavedWidth','float','Particles.HoverWidth'),
                ('SeenGeneration','float','Particles.HoverGeneration'),('SeenBucket','int','Particles.HoverBucket'),('SavedViewFade','float','Particles.HoverViewFade')]
            outputs=[('PositionOut','Position','Particles.Position'),('SizeOut','vec2','Particles.SpriteSize'),('ColorOut','Color','Particles.Color'),
                ('AnchorOut','Position','Particles.HoverAnchor'),('DirectionOut','Vector','Particles.HoverDown'),('BornOut','float','Particles.HoverBorn'),
                ('WidthOut','float','Particles.HoverWidth'),('GenerationOut','float','Particles.HoverGeneration'),('BucketOut','int','Particles.HoverBucket'),('ViewFadeOut','float','Particles.HoverViewFade')]
            scratch(SYSTEM,emitter,'ParticleUpdate','AdvanceHoverTrailGPU',inputs,outputs,(ROOT/'Scripts/Niagara/GuLiHoverTrail.hlsl').read_text())
        assert EM.set_renderer_property(SYSTEM,emitter,0,'Material',mat)
        assert EM.set_renderer_property(SYSTEM,emitter,0,'Alignment',alignment)
        r.setdefault('emitters',{})[emitter]={'count':count,'modules':[str(x) for x in EM.list_modules(SYSTEM,emitter)],'renderers':[str(x) for x in EM.list_renderers(SYSTEM,emitter)]}
    configured=unreal.GuLiCombatEffectAuthoringLibrary.configure_war_machine_hover_system(system)
    assert configured is not None
    for emitter in r['emitters']:
        properties=EM.get_emitter_properties(SYSTEM,emitter)
        assert str(properties.sim_target)=='GPUComputeSim',str(properties)
        r['emitters'][emitter]['properties']=str(properties)
    system.set_editor_property('max_pool_size',8);system.set_editor_property('pool_prime_size',0)
    result=NS.compile_with_results(SYSTEM)
    r['compile']={'success':bool(result.get_editor_property('success')),'errors':[str(x) for x in result.get_editor_property('errors')],
        'warnings':[str(x) for x in result.get_editor_property('warnings')]}
    r['native_compile_diagnostics']=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
    assert r['compile']['success'] and not r['compile']['errors'],r['compile']
    assert 'VM ERROR:' not in r['native_compile_diagnostics'] and 'GPU ERROR:' not in r['native_compile_diagnostics'],r['native_compile_diagnostics']
    assert r['native_compile_diagnostics'].count('GPU finished=1 complete=1')==2,r['native_compile_diagnostics']
    LIB.set_metadata_tag(system,'GuLi.Hover.Contract','GPUComputeSim; 1024 nozzle uploads; 1024 cores + 10240 GPU world-history lanes; lifetime .30; no CPU particle simulation/readback; no collision/lights')
    assert LIB.save_loaded_asset(system,False);r['saved'].append(SYSTEM)
    r['summary']=str(NS.summarize(SYSTEM));r['settings']=str(NS.get_all_editable_settings(SYSTEM))
    r['success']=True
except:r['error']=traceback.format_exc()
(OUT/'ue-fx-gpu-assets.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':r['success'],'error':r.get('error'),'compile':r.get('compile'),'saved':r['saved']}))

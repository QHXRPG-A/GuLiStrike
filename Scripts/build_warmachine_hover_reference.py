"""Build the WM01 candidate; the explicit publisher may target the approved formal assets."""
import ast
import hashlib
import json
import shutil
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/WarMachineHover_20260930'
OUT.mkdir(parents=True,exist_ok=True)
PUBLISH=bool(globals().pop('WM01_HOVER_PUBLISH',False))
BASE='/Game/GuLiStrike/FX/WarMachineHover'
DEST=BASE if PUBLISH else BASE+'/Reference_v2'
SYSTEM=BASE+'/NS_WarMachineHoverPool' if PUBLISH else DEST+'/NS_WarMachineHoverReference'
SOURCE='/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool'
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
LIB,EDIT=unreal.EditorAssetLibrary,unreal.MaterialEditingLibrary
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
r={'success':False,'source':SOURCE,'system':SYSTEM,'saved':[],'production_reference_changed':False}
r['production_asset_updated']=PUBLISH
JET_CODE=(ROOT/'Scripts/Niagara/GuLiHoverReferenceJet.hlsl').read_text(encoding='utf-8')

def require(value,message):
    if not value:raise RuntimeError(message)
    return value
def prop(value,name):return value.get_editor_property(name)
tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
exec(compile(ast.Module(body=[function],type_ignores=[]),'shared_scratch_helper','exec'),globals())

def make_material(name,trail=False):
    path=DEST+'/'+name
    mat=unreal.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    require(mat,path)
    EDIT.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',True)
    EDIT.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    def node(cls,**props):
        n=EDIT.create_material_expression(mat,cls)
        for k,v in props.items():n.set_editor_property(k,v)
        return n
    def link(a,b,pin='',out=''):require(EDIT.connect_material_expressions(a,out,b,pin),'link '+pin)
    uv=node(unreal.MaterialExpressionTextureCoordinate)
    color=node(unreal.MaterialExpressionParticleColor)
    time=node(unreal.MaterialExpressionTime)
    def custom(code,output):
        n=node(unreal.MaterialExpressionCustom,code=code,output_type=output)
        pins=[]
        for key in ['UV','Alpha','Clock']:
            p=unreal.CustomInput();p.set_editor_property('input_name',key);pins.append(p)
        n.set_editor_property('inputs',pins)
        link(uv,n,'UV');link(color,n,'Alpha','A');link(time,n,'Clock')
        return n
    shape=('float t=saturate(1-UV.y); float x=abs(UV.x*2-1); '
           'float flow=sin(t*23-Clock*10)*.022*t; '
           'float w=max(.001,pow(1-t,.80)); float q=(x+flow)/w; ')
    if trail:
        mask='float x=abs(UV.x*2-1); float y=abs(UV.y*2-1); return (1-smoothstep(.66,1.0,x))*pow(saturate(1-y*y),.45)*Alpha;'
        light='float core=pow(saturate(1-abs(UV.x*2-1)*3.5),2); return lerp(float3(.008,.25,1.0),float3(.80,1.20,1.65),core)*1.7;'
    else:
        mask=shape+'return pow(saturate(1-q*q),.7)*smoothstep(0,.018,t)*pow(saturate(1-t),.30)*Alpha;'
        light=shape+'float core=pow(saturate(1-q*1.65),.65)*(1-smoothstep(.60,.94,t)); return lerp(float3(.015,.45,1.8),float3(.85,.97,1.0)*5.0,core);'
    masknode=custom(mask,unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    lightnode=custom(light,unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    depth=node(unreal.MaterialExpressionDepthFade,fade_distance_default=8.0)
    link(masknode,depth,'Opacity')
    require(EDIT.connect_material_property(lightnode,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'emissive')
    require(EDIT.connect_material_property(depth,'',unreal.MaterialProperty.MP_OPACITY),'opacity')
    EDIT.layout_material_expressions(mat);EDIT.recompile_material(mat)
    d=unreal.MaterialNodeService.get_material_diagnostics(path)
    require(d.success and d.is_compiled_ok,str(d))
    require(LIB.save_loaded_asset(mat,False),'save '+path)
    r['saved'].append(path)
    r.setdefault('materials',{})[path]={'diagnostics':str(d),'graph':json.loads(unreal.MaterialNodeService.export_material_graph(path))}
    return path

try:
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'PIE active')
    original=ROOT/'ArtSource/WarMachineHover_20260930/reference.png'
    if not original.exists():shutil.copy2('C:/Users/a/AppData/Local/Temp/codex-clipboard-2706ebe8-37da-404c-b024-13552be275fc.png',original)
    r['reference_sha256']=hashlib.sha256(original.read_bytes()).hexdigest()
    core=make_material('M_WarMachineHoverJet' if PUBLISH else 'M_WarMachineReferenceJet')
    trail=make_material('M_WarMachineHoverTrail' if PUBLISH else 'M_WarMachineReferenceTrail',True)
    system=unreal.load_asset(SYSTEM) if LIB.does_asset_exist(SYSTEM) else LIB.duplicate_asset(SOURCE,SYSTEM)
    require(system,'duplicate source')
    for name,value in [('HoverMinTrailLength','500'),('HoverMaxTrailLength','1500'),('HoverTrailGrowTime','1.0')]:
        if not NS.parameter_exists(SYSTEM,'User.'+name):require(NS.add_user_parameter(SYSTEM,name,'Float',value),'parameter '+name)
        require(NS.set_parameter(SYSTEM,'User.'+name,value),'set parameter '+name)
    # Preserve the existing DI readers, GPU settings and batch capacity.
    for emitter in ['LaserBolts','LaserMuzzles']:
        for entry in list(EM.list_modules(SYSTEM,emitter)):
            name=str(entry.module_name)
            if name.startswith(('ReferenceJetShape','InitializeReferenceHistory','AdvanceReferenceHistory','AdvanceHoverTrailGPU')):
                require(EM.remove_module(SYSTEM,emitter,name),'remove '+name)
    scratch(SYSTEM,'LaserBolts','ParticleUpdate','ReferenceJetShape',
        [('Center','Position','Particles.Position'),('Down','Vector','Particles.SpriteAlignment'),
         ('Size','vec2','Particles.SpriteSize'),('Tint','Color','Particles.Color')],
        [('PositionOut','Position','Particles.Position'),('SizeOut','vec2','Particles.SpriteSize'),('ColorOut','Color','Particles.Color')],
        JET_CODE)
    fields=[('Previous','Position','Particles.RefPrevious'),('PreviousTime','float','Particles.RefPreviousTime'),
        ('Travel','float','Particles.RefTravel'),('Phase','float','Particles.RefPhase'),
        ('SampleDistance','float','Particles.RefSampleDistance'),('Heading','Vector','Particles.RefHeading'),
        ('LastMoving','float','Particles.RefLastMoving'),('Span','float','Particles.RefSpan'),('Length','float','Particles.RefLength'),
        ('MoveAge','float','Particles.RefMoveAge'),('WasMoving','float','Particles.RefWasMoving'),
        ('ViewFade','float','Particles.RefViewFade')]
    history_init=('Previous=float3(0,0,0); PreviousTime=-100000; Travel=0; Phase=0; SampleDistance=-100000; '
        'Heading=float3(1,0,0); LastMoving=-100000; Span=0; Length=0; MoveAge=0; WasMoving=0; ViewFade=0;')
    scratch(SYSTEM,'LaserMuzzles','ParticleSpawn','InitializeReferenceHistory',[],fields,history_init)
    inputs=[('NozzlePosition','Position','Particles.Position'),('NozzleSize','vec2','Particles.SpriteSize'),
        ('Meta','Color','Particles.Color'),('Lane','int','Particles.HoverLane'),('Now','float','User.HoverTime'),
        ('Camera','Position','User.HoverCamera'),('SavedAnchor','Position','Particles.HoverAnchor'),
        ('Born','float','Particles.HoverBorn'),('SeenGeneration','float','Particles.HoverGeneration'),('SavedWidth','float','Particles.HoverWidth'),
        ('PreviousPosition','Position','Particles.RefPrevious'),('PreviousTime','float','Particles.RefPreviousTime'),
        ('SavedTravel','float','Particles.RefTravel'),('SavedPhase','float','Particles.RefPhase'),
        ('SavedSampleDistance','float','Particles.RefSampleDistance'),('SavedHeading','Vector','Particles.RefHeading'),
        ('SavedLastMoving','float','Particles.RefLastMoving'),('SavedSpan','float','Particles.RefSpan'),
        ('SavedLength','float','Particles.RefLength'),('MinLength','float','User.HoverMinTrailLength'),('MaxLength','float','User.HoverMaxTrailLength'),
        ('SavedMoveAge','float','Particles.RefMoveAge'),('SavedWasMoving','float','Particles.RefWasMoving'),('GrowTime','float','User.HoverTrailGrowTime'),
        ('SavedViewFade','float','Particles.RefViewFade')]
    outputs=[('PositionOut','Position','Particles.Position'),('AlignmentOut','Vector','Particles.SpriteAlignment'),
        ('SizeOut','vec2','Particles.SpriteSize'),('ColorOut','Color','Particles.Color'),
        ('AnchorOut','Position','Particles.HoverAnchor'),('BornOut','float','Particles.HoverBorn'),
        ('GenerationOut','float','Particles.HoverGeneration'),('WidthOut','float','Particles.HoverWidth'),('PreviousOut','Position','Particles.RefPrevious'),
        ('PreviousTimeOut','float','Particles.RefPreviousTime'),('TravelOut','float','Particles.RefTravel'),
        ('PhaseOut','float','Particles.RefPhase'),('SampleDistanceOut','float','Particles.RefSampleDistance'),
        ('HeadingOut','Vector','Particles.RefHeading'),('LastMovingOut','float','Particles.RefLastMoving'),
        ('SpanOut','float','Particles.RefSpan'),('LengthOut','float','Particles.RefLength'),
        ('MoveAgeOut','float','Particles.RefMoveAge'),('WasMovingOut','float','Particles.RefWasMoving'),
        ('ViewFadeOut','float','Particles.RefViewFade')]
    scratch(SYSTEM,'LaserMuzzles','ParticleUpdate','AdvanceReferenceHistory',inputs,outputs,(ROOT/'Scripts/Niagara/GuLiHoverReferenceTrail.hlsl').read_text(encoding='utf-8'))
    for emitter,mat in [('LaserBolts',core),('LaserMuzzles',trail)]:
        require(EM.set_renderer_property(SYSTEM,emitter,0,'Material',mat),'material')
        require(EM.set_renderer_property(SYSTEM,emitter,0,'Alignment','CustomAlignment'),'alignment')
    result=NS.compile_with_results(SYSTEM)
    r['compile']={'success':bool(result.success),'errors':[str(x) for x in result.errors],'warnings':[str(x) for x in result.warnings]}
    require(r['compile']['success'] and not r['compile']['errors'],str(r['compile']))
    LIB.set_metadata_tag(system,'GuLi.Hover.Reference','v2 attached core; speed720=500cm speed1440=1500cm; grow 1s on each start; clear cyan GPU distance history; '+('formal application authorized 20260930' if PUBLISH else 'candidate'))
    require(LIB.save_loaded_asset(system,False),'save system');r['saved'].append(SYSTEM)
    r['summary']=str(NS.summarize(SYSTEM))
    r['emitters']={name:{'properties':str(EM.get_emitter_properties(SYSTEM,name)),
        'renderer':str(EM.get_renderer_details(SYSTEM,name,0)),'modules':[str(m) for m in EM.list_modules(SYSTEM,name)]} for name in ['LaserBolts','LaserMuzzles']}
    if not PUBLISH:
        # Editor array setters restart active systems. An isolated preview copy
        # supplies the model's current root and speed. Never integrate the model's
        # movement again from Niagara age: editor/manual simulation clocks can drift.
        preview=DEST+'/NS_WarMachineHoverReference_Preview'
        if not LIB.does_asset_exist(preview):LIB.duplicate_asset(SYSTEM,preview)
        for name,kind,value in [('HoverPreviewRoot','Vector','(X=8000,Y=-26000,Z=120)'),('HoverPreviewSpeed','Float','0'),('HoverMinTrailLength','Float','500'),('HoverMaxTrailLength','Float','1500'),('HoverTrailGrowTime','Float','1.0')]:
            if not NS.parameter_exists(preview,'User.'+name):require(NS.add_user_parameter(preview,name,kind,value),'preview parameter '+name)
            require(NS.set_parameter(preview,'User.'+name,value),'set preview parameter '+name)
        preview_prefix='''
    float3 offset=Slot==0 ? float3(293.39724,326.25042,0) : (Slot==1 ? float3(293.39724,-326.25042,0) : (Slot==2 ? float3(-350.82282,380.42546,0) : float3(-350.82282,-380.42546,0)));
    float3 previewNozzle=PreviewRoot+offset;
    float oldLength=60+30*saturate(PreviewSpeed/1440);
    '''
        for emitter in ['LaserBolts','LaserMuzzles']:
            for entry in list(EM.list_modules(preview,emitter)):
                if str(entry.module_name).startswith(('ReferenceJetShape','InitializeReferenceHistory','AdvanceReferenceHistory','PreviewReferenceJet','PreviewReferenceHistory')):
                    require(EM.remove_module(preview,emitter,str(entry.module_name)),'remove preview module')
        scratch(preview,'LaserMuzzles','ParticleSpawn','InitializeReferenceHistory',[],fields,history_init)
        preview_inputs=[('Slot','int','Particles.LaserSlot'),('PreviewRoot','Vector','User.HoverPreviewRoot'),('PreviewSpeed','float','User.HoverPreviewSpeed')]
        scratch(preview,'LaserBolts','ParticleUpdate','PreviewReferenceJet',
            [('Center','Position','Particles.Position'),('Down','Vector','Particles.SpriteAlignment'),('Size','vec2','Particles.SpriteSize'),('Tint','Color','Particles.Color')]+preview_inputs,
            [('PositionOut','Position','Particles.Position'),('SizeOut','vec2','Particles.SpriteSize'),('ColorOut','Color','Particles.Color'),('DownOut','Vector','Particles.SpriteAlignment')],
            preview_prefix+'Down=float3(0,0,-1); DownOut=Down; Center=previewNozzle+Down*oldLength*.5; Size=Slot<4 ? float2(430.5925,oldLength) : float2(0,0); Tint=float4(1,1,1,Slot<4?.90:0);'+JET_CODE)
        scratch(preview,'LaserMuzzles','ParticleUpdate','PreviewReferenceHistory',inputs+preview_inputs,outputs,
            preview_prefix+'NozzlePosition=previewNozzle-float3(0,0,oldLength*.65); NozzleSize=Slot<4 ? float2(430.5925,PreviewSpeed) : float2(0,0); Meta=float4(1,Slot<4?1:0,0,0); Camera=PreviewRoot+float3(2050,-2750,1180);'+(ROOT/'Scripts/Niagara/GuLiHoverReferenceTrail.hlsl').read_text())
        result=NS.compile_with_results(preview);require(result.success and not result.errors,str(result))
        r['preview_compile']={'success':bool(result.success),'errors':[str(x) for x in result.errors],'warnings':[str(x) for x in result.warnings]}
        require(LIB.save_loaded_asset(unreal.load_asset(preview),False),'save preview')
        r['saved'].append(preview);r['preview_system']=preview
    r['saved_parameters']={path:{name:str(NS.get_parameter(path,'User.'+name)) for name in ['HoverMinTrailLength','HoverMaxTrailLength','HoverTrailGrowTime']} for path in ([SYSTEM] if PUBLISH else [SYSTEM,preview])}
    r['attachment_contract']='Core reads current nozzle each update. Preview consumes the same current root and speed as the model; only trail emitter stores history.'
    r['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'Scripts/build_warmachine_hover_reference.py',ROOT/'Scripts/Niagara/GuLiHoverReferenceJet.hlsl',ROOT/'Scripts/Niagara/GuLiHoverReferenceTrail.hlsl']}
    r['success']=True
except Exception:r['error']=traceback.format_exc()
(OUT/('formal-build.json' if PUBLISH else 'candidate-build.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k:r.get(k) for k in ['success','error','saved','compile']}))

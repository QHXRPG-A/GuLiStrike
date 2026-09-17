"""Build only the owned Wingman toon explosion assets. Run through ue_exec.py.

No gameplay references are changed here. Geometry is native MeshDescription;
Niagara uses CPU Fountain initialization/state plus a compact analytic burst.
All positions and sizes are world space: Owner.Scale is applied exactly once.
"""
import json
import math
import traceback
from pathlib import Path
import unreal

DEST='/Game/GuLiStrike/FX/WingmanWeapons/StylizedExplosion'
SYSTEM=DEST+'/NS_WingmanGroundExplosion_Toon'
OUT=Path('D:/UE5.7/test1/ArtSource/FX/WingmanGroundExplosion_Toon')
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
MAT=unreal.MaterialEditingLibrary
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
REPORT={'saved':[],'materials':{},'emitters':[]}

def need(v,m):
    if not v: raise RuntimeError(m)
    return v

def save(obj):
    path=obj.get_path_name() if not isinstance(obj,str) else obj
    need(path.startswith(DEST+'/'),'Out of scope '+path)
    need(ASSETS.save_asset(path,only_if_is_dirty=True),'Save '+path)
    REPORT['saved'].append(path)

def expression(mat,cls,**props):
    n=MAT.create_material_expression(mat,cls)
    for k,v in props.items(): n.set_editor_property(k,v)
    return n

def custom(mat,code,inputs,kind):
    node=expression(mat,unreal.MaterialExpressionCustom,code=code,output_type=kind)
    defs=[]
    for name in inputs:
        p=unreal.CustomInput();p.set_editor_property('input_name',name);defs.append(p)
    node.set_editor_property('inputs',defs)
    for name,(source,pin) in inputs.items(): need(MAT.connect_material_expressions(source,pin,node,name),'Wire '+name)
    return node

def material(name,kind):
    p=DEST+'/'+name
    m=ASSETS.load_asset(p) if ASSETS.does_asset_exist(p) else TOOLS.create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    need(m,'Create material')
    MAT.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED if kind in ('puff','ring') else unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided',kind=='ring')
    MAT.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES if kind in ('puff','ring') else unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv=expression(m,unreal.MaterialExpressionTextureCoordinate)
    pc=expression(m,unreal.MaterialExpressionParticleColor)
    age=expression(m,unreal.MaterialExpressionParticleRelativeTime)
    if kind=='puff':
        normal=expression(m,unreal.MaterialExpressionVertexNormalWS)
        view=expression(m,unreal.MaterialExpressionCameraVectorWS)
        color=custom(m,
            'float d=dot(normalize(N),normalize(float3(-.4,-.6,.7))); '
            'float f=abs(dot(normalize(N),normalize(V))); '
            'float3 fire=d>.32?float3(3.0,2.6,1.8):(d>-.3?float3(2.2,.8,.16):float3(.95,.12,.04)); '
            'if(f<.16) fire=float3(.95,.12,.04); '
            'float3 smoke=d>.32?float3(2.1,1.9,1.85):(d>-.3?float3(1.5,1.32,1.34):float3(1.0,.74,.82)); '
            'if(f<.18) smoke=float3(.65,.4,.48); '
            'float fade=smoothstep(.23,.48,Age); return lerp(fire,smoke,fade);',
            {'N':(normal,''),'V':(view,''),'Age':(age,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        mask=custom(m,'float n=.5+.24*sin(UV.x*43+sin(UV.y*27)*2)+.23*cos(UV.y*39); '
                         'return n-smoothstep(.76,1,Age)*1.1;',
                    {'UV':(uv,''),'Age':(age,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        m.set_editor_property('opacity_mask_clip_value',0.02)
    elif kind=='ring':
        color=custom(m,'return float3(1.4,.44,.1);',{},unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        mask=custom(m,'return step(Age,.98);',{'Age':(age,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    else:
        color=custom(m,'return float3(7,3.4,.65)*Color.rgb;',{'Color':(pc,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        mask=custom(m,('float2 p=UV*2-1; float v=saturate(1-dot(p,p)); return v*v*Alpha;' if kind=='flash' else
                       'float2 p=abs(UV*2-1); return saturate(1-p.x)*saturate(1-p.y)*Alpha;'),
                    {'UV':(uv,''),'Alpha':(pc,'A')},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    MAT.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY_MASK if kind in ('puff','ring') else unreal.MaterialProperty.MP_OPACITY)
    MAT.layout_material_expressions(m);MAT.recompile_material(m)
    d=unreal.MaterialNodeService.get_material_diagnostics(p)
    REPORT['materials'][p]=str(d)
    deferred='-nullrhi' in unreal.SystemLibrary.get_command_line().lower()
    need(d.success and (d.is_compiled_ok or deferred) and not d.compile_errors,'Material '+str(d));save(m)
    if deferred:REPORT['rendered_material_validation']='PENDING: validate all saved materials in rendered editor'
    return p

def mesh(name,material_path,ring=False):
    path=DEST+'/'+name
    if ASSETS.does_asset_exist(path): return path
    if ring:
        vertices=[];faces=[]
        for i in range(64):
            a=i*math.tau/64
            for r in (94,100): vertices.append((r*math.cos(a),r*math.sin(a),0))
        for i in range(64):
            a=i*2;b=((i+1)%64)*2;faces.extend([(a,b,a+1),(a+1,b,b+1)])
    else:
        t=(1+math.sqrt(5))/2
        vertices=[(-1,t,0),(1,t,0),(-1,-t,0),(1,-t,0),(0,-1,t),(0,1,t),(0,-1,-t),(0,1,-t),(t,0,-1),(t,0,1),(-t,0,-1),(-t,0,1)]
        def unit(p):
            r=math.sqrt(sum(a*a for a in p));return tuple(a/r for a in p)
        vertices=[unit(p) for p in vertices]
        faces=[(0,11,5),(0,5,1),(0,1,7),(0,7,10),(0,10,11),(1,5,9),(5,11,4),(11,10,2),(10,7,6),(7,1,8),(3,9,4),(3,4,2),(3,2,6),(3,6,8),(3,8,9),(4,9,5),(2,4,11),(6,2,10),(8,6,7),(9,8,1)]
        for _ in range(2):
            cache={};new=[]
            def midpoint(a,b):
                key=tuple(sorted((a,b)))
                if key not in cache:
                    cache[key]=len(vertices);vertices.append(unit(tuple((vertices[a][j]+vertices[b][j])*.5 for j in range(3))))
                return cache[key]
            for a,b,c in faces:
                ab,bc,ca=midpoint(a,b),midpoint(b,c),midpoint(c,a)
                new.extend([(a,ab,ca),(b,bc,ab),(c,ca,bc),(ab,bc,ca)])
            faces=new
        vertices=[tuple(c*100*(1+.09*math.sin(x*8+y*4)*math.cos(z*7)) for c in (x,y,z)) for x,y,z in vertices]
        need(len(faces)==320,'Puff triangle budget')
    obj=need(TOOLS.create_asset(name,DEST,unreal.StaticMesh,unreal.NiagaraBakerStaticMeshFactoryNew()),'Fresh mesh')
    desc=unreal.StaticMesh.create_static_mesh_description();group=desc.create_polygon_group()
    desc.set_polygon_group_material_slot_name(group,'Toon')
    verts=[]
    for p in vertices:
        v=desc.create_vertex();desc.set_vertex_position(v,unreal.Vector(*p));verts.append(v)
    for face in faces:
        ins=[]
        for i in face:
            vi=desc.create_vertex_instance(verts[i]);p=vertices[i]
            desc.set_vertex_instance_uv(vi,unreal.Vector2D(.5+math.atan2(p[1],p[0])/math.tau,.5-p[2]/220),0)
            ins.append(vi)
        desc.create_triangle(group,ins)
    slot=unreal.StaticMaterial();slot.set_editor_property('material_interface',unreal.load_asset(material_path));slot.set_editor_property('material_slot_name','Toon')
    obj.set_editor_property('static_materials',[slot]);obj.build_from_static_mesh_descriptions([desc],False,False)
    save(obj)
    REPORT.setdefault('meshes',{})[path]={'triangles':desc.get_triangle_count(),'bounds':str(obj.get_bounds())}
    (OUT/(name+'.geometry.json')).write_text(json.dumps({'vertices':vertices,'triangles':faces}),encoding='utf8')
    return path

def scratch(emitter,stage,name,inputs,outputs,code):
    r=SP.create_scratch_module(SYSTEM,emitter,stage,name);need(r.success,'Create scratch '+name);module=str(r.module_name)
    h=SP.add_custom_hlsl_node(SYSTEM,emitter,module,code);need(h.success,'HLSL');hid=str(h.node_id)
    r=SP.add_node(SYSTEM,emitter,module,'MapGet');need(r.success,'Read map');rid=str(r.node_id)
    inp=next(n for n in SP.list_nodes(SYSTEM,emitter,module) if str(n.node_type).endswith('Input') or str(n.node_type).endswith('NodeInput'))
    ipin=next(str(p.pin_name) for p in SP.get_node_pins(SYSTEM,emitter,module,str(inp.node_id)) if str(p.direction).lower()=='output')
    rpin=next(str(p.pin_name) for p in SP.get_node_pins(SYSTEM,emitter,module,rid) if str(p.direction).lower()=='input')
    need(SP.connect_pins(SYSTEM,emitter,module,str(inp.node_id),ipin,rid,rpin),'Map input')
    links=[]
    for pin,kind,var in inputs:
        need(SP.add_pin(SYSTEM,emitter,module,rid,'Output',kind,var).success,'Read '+var)
        need(SP.add_pin(SYSTEM,emitter,module,hid,'Input',kind,pin).success,'Input '+pin)
        links.append((rid,var,hid,pin))
    for pin,kind,var in outputs:
        need(SP.add_pin(SYSTEM,emitter,module,hid,'Output',kind,pin).success,'Output '+pin)
        r=SP.add_module_output(SYSTEM,emitter,module,var,kind);need(r.success,'Map output '+var)
        links.append((hid,pin,str(r.node_id),var))
    # All pins now exist in Signature; refresh reconstructs correct dynamic-pin order.
    # This uses VibeUE's public API and requires no project C++ authoring extension.
    need(SP.set_custom_hlsl_code(SYSTEM,emitter,module,hid,code),'Refresh signature')
    for a,b,c,d in links:need(SP.connect_pins(SYSTEM,emitter,module,a,b,c,d),'Wire '+b+' -> '+d)
    need(SP.apply_changes(SYSTEM),'Apply scratch')

def main():
    need(not unreal.WidgetService.is_pie_running(),'Stop PIE')
    OUT.mkdir(parents=True,exist_ok=True)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Niagara/DefaultAssets','/Niagara/Modules'],force_rescan=False)
    mats={k:material('M_WingmanToon_'+k.title(),k) for k in ('puff','ring','flash','spark')}
    meshes={'puff':mesh('SM_WingmanToon_Puff320',mats['puff']), 'ring':mesh('SM_WingmanToon_Ring128',mats['ring'],True)}
    if not ASSETS.does_asset_exist(SYSTEM):need(NS.create_system(SYSTEM.rsplit('/',1)[1],DEST).success,'New system')
    for e in NS.list_emitters(SYSTEM):need(NS.remove_emitter(SYSTEM,str(e.emitter_name)),'Remove owned emitter')
    common=[('Owner','Position','Engine.Owner.Position'),('OwnerScale','Vector','Engine.Owner.Scale')]
    outs=[('Life','float','Particles.Lifetime'),('Pos','Position','Particles.Position'),('Vel','Vector','Particles.Velocity'),('Tint','Color','Particles.Color'),('Size','vec2','Particles.SpriteSize'),('Scale','Vector','Particles.Scale'),('Alignment','Vector','Particles.SpriteAlignment'),('Rotation','float','Particles.SpriteRotation')]
    # Radius tuning is in author centimeters; runtime scale remains 5.0.
    for name,kind,count,life,delay in [('Ignition','flash',1,.1,0),('FireSmoke','puff',8,1.8,0),('ExpansionRing','ring',1,.5,.05),('Sparks','spark',12,.65,0)]:
        emitter=need(NS.add_emitter(SYSTEM,'/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain',name),'Emitter')
        for module in list(EM.list_modules(SYSTEM,emitter)):
            if str(module.module_name) not in ('EmitterState','InitializeParticle','ParticleState'):need(EM.remove_module(SYSTEM,emitter,str(module.module_name)),'Strip '+str(module.module_name))
        need(EM.add_module(SYSTEM,emitter,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate'),'Burst module')
        for mod,key,val in [('SpawnBurst_Instantaneous','Spawn Count',count),('SpawnBurst_Instantaneous','Spawn Time',delay),('EmitterState','Loop Duration',2.0)]:
            need(NS.set_rapid_iteration_param_by_stage(SYSTEM,emitter,'EmitterUpdate',f'Constants.{emitter}.{mod}.{key}',str(val)),'RI '+key)
        # The Fountain template loops; use the internal enumerator for Once.
        need(EM.set_module_input(SYSTEM,emitter,'EmitterState','Loop Behavior','NewEnumerator1'),'Once lifecycle')
        base=f'Life={life}; Vel=float3(0,0,0); Rotation=0; Tint=float4(1,1,1,1); Alignment=float3(0,0,1); '
        if kind=='puff':
            shape=('float theta=Index*2.399963; float ring=Index<1?0:1; '
                   'float g=saturate(T/.15); float growth=g*g*(3-2*g); float q=saturate((T-1.1)/.7); float shrink=1-q*q*(3-2*q); '
                   'float rad=(130+Index*12)*ring*growth; '
                   'float3 offset=float3(cos(theta)*rad,sin(theta)*rad,135+(Index-floor(Index/3.0)*3.0)*42+T*105); '
                   'Pos=Owner+offset*OwnerScale*.70; float s=(1.7+frac(Index*.37)*.45)*(.24+.76*growth)*shrink; '
                   'Scale=OwnerScale*s*.70; Size=float2(1,1);')
        elif kind=='ring':
            shape=('float q=saturate(T/.5); Pos=Owner+float3(0,0,90+q*200)*OwnerScale; '
                   'Scale=OwnerScale*float3(2+q*4.4,2+q*4.4,1); Size=float2(1,1);')
        elif kind=='flash':
            shape=('Pos=Owner+float3(0,0,80)*OwnerScale; Scale=float3(1,1,1); '
                   'Size=OwnerScale.xy*(650*(1-saturate(T/.1))); Tint.a=1-saturate(T/.1);')
        else:
            shape=('float a=Index*2.399963; float3 v=float3(cos(a),sin(a),.7+frac(Index*.31))*620; '
                   'Pos=Owner+(float3(0,0,65)+v*T/(1+T*3)+float3(0,0,-250)*T*T)*OwnerScale; '
                   'Scale=float3(1,1,1); Size=OwnerScale.xy*float2(8,50)*(1-saturate(T/.65)); '
                   'Alignment=normalize(v); Tint.a=1-saturate(T/.65);')
        init=base+'float Index=(float)Id; float T=0; '+shape+' OutIndex=Index;'
        scratch(emitter,'ParticleSpawn','Toon'+name+'Init',common+[('Id','int','Particles.UniqueID')],outs+[('OutIndex','float','Particles.ToonIndex')],init)
        scratch(emitter,'ParticleUpdate','Toon'+name+'Motion',common+[('Index','float','Particles.ToonIndex'),('T','float','Particles.Age')],outs,base+shape)
        if kind in meshes:
            need(EM.remove_renderer(SYSTEM,emitter,0),'Remove sprite');need(EM.add_renderer(SYSTEM,emitter,'Mesh'),'Mesh renderer')
            need(EM.set_renderer_property(SYSTEM,emitter,0,'Meshes',f'((Mesh="{meshes[kind]}"))'),'Mesh reference')
            need(EM.set_renderer_property(SYSTEM,emitter,0,'bOverrideMaterials','True'),'Override material')
            need(EM.set_renderer_property(SYSTEM,emitter,0,'OverrideMaterials',f'((ExplicitMat="{mats[kind]}"))'),'Mesh material')
        else:
            need(EM.set_renderer_property(SYSTEM,emitter,0,'Material',mats[kind]),'Sprite material')
            if kind=='spark':need(EM.set_renderer_property(SYSTEM,emitter,0,'Alignment','CustomAlignment'),'Spark align')
        result=NS.compile_with_results(SYSTEM)
        REPORT['emitters'].append({'name':emitter,'count':count,'lifetime':life,'compile':str(result)})
        deferred='-nullrhi' in unreal.SystemLibrary.get_command_line().lower()
        # NullRHI commandlets cannot produce the render-ready status. Persist the
        # owned draft graph for the mandatory rendered compile gate, never install it.
        need((result.success and not result.errors) or deferred,'Niagara compile '+str(result))
        if deferred:REPORT['rendered_niagara_validation']='PENDING: compile saved graph in rendered editor'
    system=ASSETS.load_asset(SYSTEM)
    for key,value in [('max_pool_size',32),('pool_prime_size',0),('bFixedBounds',True),('bDeterminism',True),('RandomSeed',1337),('FixedBounds',unreal.Box(min=unreal.Vector(-1800,-1800,-500),max=unreal.Vector(1800,1800,2000)))]:system.set_editor_property(key,value)
    save(system)
    REPORT['readback']=[{'name':str(e.emitter_name),'properties':str(EM.get_emitter_properties(SYSTEM,str(e.emitter_name))),'renderer':str(EM.get_renderer_details(SYSTEM,str(e.emitter_name),0))} for e in NS.list_emitters(SYSTEM)]
    REPORT['success']=True

try:main()
except Exception:REPORT.update(success=False,error=traceback.format_exc())
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'build.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')
print(json.dumps({'success':REPORT.get('success'),'error':REPORT.get('error'),'report':str(OUT/'build.json')}))

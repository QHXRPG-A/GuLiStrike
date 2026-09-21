"""Author only CommanderWeapons assets through the installed UE5.7 / VibeUE 4.0 APIs.

Run through Scripts/ue_exec.py. Set COMMANDER_FX_STAGE to materials/gunfire-materials/gunfire/gunfire-catalog/machinegun-impact/flight/explosions/catalog
in the editor Python globals for a scoped retry. Source marketplace assets are never modified.
"""
import json
import math
import traceback
from pathlib import Path
import unreal

import sys
from pathlib import Path
sys.path.insert(0, str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "Scripts/Vfx"))
from vfx_registry import vfx_id, resource as vfx_resource, scale as vfx_scale, require_id, visual_variant

DEST = '/Game/GuLiStrike/FX/CommanderWeapons'
MACHINEGUN_IMPACT_SOURCE = vfx_resource('MachineGunImpact')
OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'outputs/commander-combat-effects'
OUT.mkdir(parents=True, exist_ok=True)
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService
SP = unreal.NiagaraScratchPadService
MAT = unreal.MaterialEditingLibrary
FOUNTAIN = '/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain'
BURST = '/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst'
report = {'saved': [], 'compiles': [], 'stage': globals().get('COMMANDER_FX_STAGE', 'all')}

def require(value, operation):
    if not value:
        raise RuntimeError(operation)
    return value

def prop(obj, name):
    return obj.get_editor_property(name)

def save(asset):
    path = asset.get_path_name() if not isinstance(asset, str) else asset
    require(path.startswith(DEST + '/'), 'Refusing an out-of-scope save: '+path)
    require(ASSETS.save_asset(path, only_if_is_dirty=True), 'Save failed: '+path)
    report['saved'].append(path)

def compile_system(path):
    # Cap resident free components, but absorb synchronized WM01 firing waves.
    system=ASSETS.load_asset(path)
    system.set_editor_property('max_pool_size',128 if path.endswith('NS_WM01_MissileFlight') else (1 if path.endswith('NS_CommanderGunfireBatch') else 64))
    system.set_editor_property('pool_prime_size',0)
    result = NS.compile_with_results(path)
    data = {'path': path, 'success': bool(prop(result, 'success')), 'errors': list(map(str, prop(result, 'errors'))),
            'warnings': list(map(str, prop(result, 'warnings')))}
    report['compiles'].append(data)
    require(data['success'] and not data['errors'], 'Niagara compile: '+json.dumps(data))
    return data

def set_ri(path, emitter, stage, module, name, value):
    require(NS.set_rapid_iteration_param_by_stage(path, emitter, stage, f'Constants.{emitter}.{module}.{name}', str(value)),
            f'Rapid iteration {emitter}/{stage}/{module}.{name}')

def set_renderer(path, emitter, key, value, index=0):
    require(EM.set_renderer_property(path, emitter, index, key, str(value)), f'Renderer {emitter}.{key}')

def new_system(name):
    path = DEST + '/' + name
    if not ASSETS.does_asset_exist(path):
        result = NS.create_system(name, DEST)
        require(prop(result, 'success'), 'Create '+path)
    # Rebuild only this task-owned system; shared marketplace/engine emitters remain untouched.
    for emitter in list(NS.list_emitters(path)):
        require(NS.remove_emitter(path, str(prop(emitter, 'emitter_name'))), 'Remove task-owned emitter')
    return path

def material(name, shape, translucent=False):
    path = DEST + '/' + name
    obj = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    require(obj, 'Create '+path)
    MAT.delete_all_material_expressions(obj)
    obj.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_ADDITIVE)
    obj.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    obj.set_editor_property('two_sided', True)
    MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_NIAGARA_RIBBONS)
    uv = MAT.create_material_expression(obj, unreal.MaterialExpressionTextureCoordinate, -700, 200)
    color = MAT.create_material_expression(obj, unreal.MaterialExpressionParticleColor, -700, -200)
    age = MAT.create_material_expression(obj, unreal.MaterialExpressionParticleRelativeTime, -700, 400)
    mask = MAT.create_material_expression(obj, unreal.MaterialExpressionCustom, -400, 150)
    mask.set_editor_property('description', 'Analytic combat VFX mask; no texture dependencies')
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    uv_input = unreal.CustomInput(); uv_input.set_editor_property('input_name','UV')
    age_input = unreal.CustomInput(); age_input.set_editor_property('input_name','Age')
    mask.set_editor_property('inputs', [uv_input, age_input])
    radial = 'float2 p=UV*2-1; float r=length(p); float m=pow(saturate(1-r),2); '
    if shape == 'beam':
        # Full-length first-frame connection: soften the width and only the last
        # 1/64 at either end, not a stretched radial blob with an invisible tail.
        radial = ('float2 p=UV*2-1; float edge=pow(saturate(1-abs(p.x)),2); '
                  'float core=pow(saturate(1-abs(p.x)*3.0),4); '
                  'float m=saturate(edge*0.65+core)*saturate((1-abs(p.y))*64.0); ')
    if shape == 'muzzle':
        # An elongated, texture-free flame. It stays at full strength through most
        # of each short refresh particle, then only fades at the very end.
        radial = ('float2 p=UV*2-1; float axial=pow(saturate(1-abs(p.y)),0.45); '
                  'float taper=saturate(1-abs(p.x)*(2.0+1.5*abs(p.y))); '
                  'float core=pow(taper,3)*axial; float halo=pow(taper,1.2)*pow(axial,1.6); '
                  'float m=saturate(core+halo*0.55); ')
    fade = 'saturate((1-Age)*8.0)' if shape == 'muzzle' else 'saturate(1-Age)'
    mask.set_editor_property('code', radial + 'return m*' + fade + ';')
    require(MAT.connect_material_expressions(uv, '', mask, 'UV'), 'UV mask link')
    require(MAT.connect_material_expressions(age, '', mask, 'Age'), 'Age mask link')
    opacity = mask
    if shape not in ('beam','muzzle'):
        opacity = MAT.create_material_expression(obj, unreal.MaterialExpressionMultiply, -100, 180)
        MAT.connect_material_expressions(mask, '', opacity, 'A'); MAT.connect_material_expressions(color, 'A', opacity, 'B')
    intensity = MAT.create_material_expression(obj, unreal.MaterialExpressionMultiply, -100, -150)
    intensity.set_editor_property('const_b', 1.0 if translucent else (48.0 if shape == 'beam' else (90.0 if shape == 'muzzle' else 22.0)))
    MAT.connect_material_expressions(color, 'RGB', intensity, 'A')
    MAT.connect_material_property(intensity, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    MAT.layout_material_expressions(obj); MAT.recompile_material(obj)
    diagnostic = unreal.MaterialNodeService.get_material_diagnostics(path)
    report.setdefault('materials', {})[path] = str(diagnostic)
    save(obj)
    return path

def scratch(path, emitter, stage, name, inputs, outputs, code):
    result = SP.create_scratch_module(path, emitter, stage, name)
    require(prop(result, 'success'), 'Scratch '+name)
    module = str(prop(result, 'module_name'))
    hlsl = SP.add_custom_hlsl_node(path, emitter, module, code)
    require(prop(hlsl, 'success'), 'HLSL '+name)
    node = str(prop(hlsl, 'node_id'))
    read = SP.add_node(path, emitter, module, 'MapGet')
    read_id = str(prop(read, 'node_id'))
    # Connect the module input parameter map to our reads.
    nodes = list(SP.list_nodes(path, emitter, module))
    input_node = next(n for n in nodes if str(prop(n, 'node_type')) in ('Input', 'NiagaraNodeInput') or str(prop(n, 'node_type')).endswith('NodeInput'))
    input_id = str(prop(input_node, 'node_id'))
    pins = SP.get_node_pins(path, emitter, module, input_id)
    input_pin = next(str(prop(p, 'pin_name')) for p in pins if str(prop(p, 'direction')).lower() == 'output')
    read_pins = SP.get_node_pins(path, emitter, module, read_id)
    read_pin = next(str(prop(p, 'pin_name')) for p in read_pins if str(prop(p, 'direction')).lower() == 'input')
    require(SP.connect_pins(path, emitter, module, input_id, input_pin, read_id, read_pin), 'Scratch map read')
    for pin_name, type_name, variable in inputs:
        require(prop(SP.add_pin(path, emitter, module, read_id, 'Output', type_name, variable), 'success'), 'Read '+variable)
        require(prop(SP.add_pin(path, emitter, module, node, 'Input', type_name, pin_name), 'success'), 'Input '+pin_name)
        require(SP.connect_pins(path, emitter, module, read_id, variable, node, pin_name), 'Input wire '+pin_name)
    for pin_name, type_name, variable in outputs:
        require(prop(SP.add_pin(path, emitter, module, node, 'Output', type_name, pin_name), 'success'), 'Output '+pin_name)
        target = SP.add_module_output(path, emitter, module, variable, type_name)
        require(prop(target, 'success'), 'Map output '+variable)
        require(SP.connect_pins(path, emitter, module, node, pin_name, str(prop(target, 'node_id')), variable), 'Output wire '+variable)
    require(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(ASSETS.load_asset(path)), 'Finalize dynamic pin order')
    require(SP.apply_changes(path), 'Apply scratch '+module)
    return module

def strip_emitter(path, emitter, allowed):
    for module in list(EM.list_modules(path, emitter)):
        name = str(prop(module, 'module_name'))
        if not any(name.startswith(a) for a in allowed):
            require(EM.remove_module(path, emitter, name), 'Remove '+name)

def gunfire():
    channel_path = DEST + '/NDC_CommanderGunfire'
    channel = ASSETS.load_asset(channel_path) if ASSETS.does_asset_exist(channel_path) else TOOLS.create_asset('NDC_CommanderGunfire', DEST, unreal.NiagaraDataChannelAsset, unreal.NiagaraDataChannelAssetFactoryNew())
    result = unreal.GuLiCombatEffectAuthoringLibrary.configure_gunfire_channel(channel)
    require(result is not None, 'Configure NDC: '+str(result))
    path = new_system('NS_CommanderGunfireBatch')
    for emitter_name, muzzle in [('Tracer', False), ('Muzzle', True)]:
        emitter = require(NS.add_emitter(path, FOUNTAIN, emitter_name), 'Add '+emitter_name)
        strip_emitter(path, emitter, ['EmitterState', 'InitializeParticle', 'ParticleState'])
        bind = SP.create_scratch_module(path, emitter, 'EmitterSpawn', 'BindGunfireNDC')
        emit = SP.create_scratch_module(path, emitter, 'EmitterUpdate', 'SpawnGunfireNDC')
        init = SP.create_scratch_module(path, emitter, 'ParticleSpawn', 'ReadGunfireNDC')
        require(prop(bind, 'success') and prop(emit, 'success') and prop(init, 'success'), 'Create NDC modules')
        result = unreal.GuLiCombatEffectAuthoringLibrary.wire_gunfire_reader(
            ASSETS.load_asset(path), channel, unreal.load_object(None, str(prop(bind, 'script_path'))),
            unreal.load_object(None, str(prop(emit, 'script_path'))), unreal.load_object(None, str(prop(init, 'script_path'))))
        require(result is not None, 'Wire NDC: '+str(result))
        SP.apply_changes(path)
        wanted_mode = 1 if muzzle else 0
        code = (f'Alive=(Mode=={wanted_mode}); Position=InputPosition; Alignment=normalize(Direction); '
                'Size=float2(Width,max(Length,1.0)); TintOut=float4(Tint.rgb*VisualIntensity,LightBrightness); LifeOut=Life; '
                'Rotation=0.0; Velocity=float3(0,0,0); LightEnabled=LightBrightness>0.0; LightRadiusOut=LightRadius;')
        scratch(path, emitter, 'ParticleSpawn', 'PersistentMuzzleShape' if muzzle else 'ExactTracerSegment',
                [('InputPosition','Position','Particles.Position'),('Direction','Vector','Particles.Direction'),
                 ('Length','float','Particles.Length'),('Mode','int','Particles.Mode'),('Width','float','Particles.Width'),
                 ('Tint','Color','Particles.Tint'),('Life','float','Particles.Lifetime'),
                 ('VisualIntensity','float','Particles.VisualIntensity'),('LightBrightness','float','Particles.LightBrightness'),
                 ('LightRadius','float','Particles.LightRadius')],
                [('Position','Position','Particles.Position'),('Size','vec2','Particles.SpriteSize'),
                 ('Alignment','Vector','Particles.SpriteAlignment'),('TintOut','Color','Particles.Color'),
                 ('LifeOut','float','Particles.Lifetime'),('Rotation','float','Particles.SpriteRotation'),
                 ('Velocity','Vector','Particles.Velocity'),('Alive','bool','Particles.Alive'),
                 ('LightEnabled','bool','Particles.LightEnabled'),('LightRadiusOut','float','Particles.LightRadius')], code)
        set_renderer(path, emitter, 'Material', DEST + ('/M_Commander_Muzzle' if muzzle else '/M_Commander_Tracer'))
        set_renderer(path, emitter, 'Alignment', 'CustomAlignment')
        require(EM.add_renderer(path,emitter,'Light'),'Add capped '+emitter_name+' light renderer')
        for key,value in [('bUseInverseSquaredFalloff','false'),('bAlphaScalesBrightness','true'),
                          ('bAffectsTranslucency','false'),('bAllowMegaLights','false'),
                          ('RadiusScale','1.0'),('DefaultExponent','2.0'),('SpecularScale','0.2'),('DiffuseScale','1.0')]:
            set_renderer(path,emitter,key,value,1)
    compile_system(path); save(channel); save(path)

def missile_mesh():
    """Build an original low-poly finned rocket; replaces every template vertex and material."""
    material_path=DEST+'/M_WM01_MissileBody'
    body=ASSETS.load_asset(material_path) if ASSETS.does_asset_exist(material_path) else TOOLS.create_asset('M_WM01_MissileBody',DEST,unreal.Material,unreal.MaterialFactoryNew())
    MAT.delete_all_material_expressions(body)
    body.set_editor_property('two_sided',True)
    MAT.set_material_usage(body,unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
    color=MAT.create_material_expression(body,unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',unreal.LinearColor(.16,.21,.27,1))
    MAT.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for value,output in [(.65,unreal.MaterialProperty.MP_METALLIC),(.32,unreal.MaterialProperty.MP_ROUGHNESS)]:
        node=MAT.create_material_expression(body,unreal.MaterialExpressionConstant); node.set_editor_property('r',value)
        MAT.connect_material_property(node,'',output)
    MAT.recompile_material(body); save(body)
    path=DEST+'/SM_WM01_Missile'
    if ASSETS.does_asset_exist(path): return path
    # No initialized render resources may be replaced from a bridge TaskGraph callback.
    mesh=require(unreal.GuLiCombatEffectAuthoringLibrary.create_missile_mesh_container(),'Fresh mesh container')
    description=unreal.StaticMesh.create_static_mesh_description()
    group=description.create_polygon_group(); description.set_polygon_group_material_slot_name(group,'MissileBody')
    rings=[]
    for x,radius in [(-160,20),(-140,32),(95,32),(165,.5)]:
        ring=[]
        for index in range(12):
            angle=index*math.tau/12
            vertex=description.create_vertex()
            description.set_vertex_position(vertex,unreal.Vector(x,radius*math.cos(angle),radius*math.sin(angle)))
            ring.append(vertex)
        rings.append(ring)
    def triangle(vertices): description.create_triangle(group,[description.create_vertex_instance(v) for v in vertices])
    for start,end in zip(rings,rings[1:]):
        for i in range(12):
            j=(i+1)%12; triangle([start[i],end[i],end[j]]); triangle([start[i],end[j],start[j]])
    for angle in [0,math.pi/2,math.pi,3*math.pi/2]:
        vertices=[]
        for x,r in [(-135,28),(-150,82),(-55,32)]:
            v=description.create_vertex(); description.set_vertex_position(v,unreal.Vector(x,r*math.cos(angle),r*math.sin(angle))); vertices.append(v)
        triangle(vertices)
    slot=unreal.StaticMaterial(); slot.set_editor_property('material_interface',body); slot.set_editor_property('material_slot_name','MissileBody')
    mesh.set_editor_property('static_materials',[slot])
    mesh.build_from_static_mesh_descriptions([description],False,False)
    save(mesh)
    report['missile_mesh']={'path':path,'triangles':description.get_triangle_count(),'bounds':str(mesh.get_bounds())}
    return path

def flight():
    body_mesh=missile_mesh()
    path = new_system('NS_WM01_MissileFlight')
    NS.add_user_parameter(path, 'Velocity', 'Vector', '0,0,0')
    NS.add_user_parameter(path, 'VisualScale', 'Float', '1')
    for name, rate, life, size, smoke in [('Core',30,.05,110,False),('Exhaust',80,.16,85,False),('Trail',45,.5,95,True)]:
        emitter = require(NS.add_emitter(path, FOUNTAIN, name), 'Flight emitter')
        strip_emitter(path, emitter, ['EmitterState','SpawnRate','InitializeParticle','ParticleState'])
        set_ri(path, emitter, 'EmitterUpdate','SpawnRate','SpawnRate', rate)
        if name=='Core':
            require(EM.add_module(path,emitter,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate'),
                    'Immediate missile body at launch')
            set_ri(path,emitter,'EmitterUpdate','SpawnBurst_Instantaneous','Spawn Count',1)
        # Explicit spawn outputs replace the template's random sizes, sphere locations and fountain velocity.
        scratch(path, emitter, 'ParticleSpawn', 'MissileParticleShape',
                [('Owner','Position','Engine.Owner.Position'),('Vel','Vector','User.Velocity'),('EffectScale','float','User.VisualScale')],
                [('Life','float','Particles.Lifetime'),('Size','vec2','Particles.SpriteSize'),('Tint','Color','Particles.Color'),('Velocity','Vector','Particles.Velocity'),('Position','Position','Particles.Position')]+([('MeshScale','Vector','Particles.Scale')] if name=='Core' else []),
                f'Life={life}; Size=float2({size},{size})*EffectScale; Tint='+('float4(0.10,0.12,0.15,0.26);' if smoke else 'float4(1.0,0.58,0.12,1.0);')+
                ' Velocity=float3(0,0,0); Position=Owner;'+(' MeshScale=float3(EffectScale,EffectScale,EffectScale);' if name=='Core' else ' Position-=Vel/max(length(Vel),1.0)*155.0*EffectScale;'))
        if name == 'Core':
            scratch(path, emitter, 'ParticleUpdate', 'FollowMissileBody', [('Owner','Position','Engine.Owner.Position'),('Vel','Vector','User.Velocity')], [('Position','Position','Particles.Position'),('Velocity','Vector','Particles.Velocity')], 'Position=Owner; Velocity=Vel;')
            require(EM.add_renderer(path,emitter,'Mesh'),'Missile mesh renderer')
            set_renderer(path,emitter,'Meshes',f'((Mesh="{body_mesh}.{body_mesh.split("/")[-1]}"))',1)
            set_renderer(path,emitter,'FacingMode','Velocity',1)
            # Keep the mesh visible; its own authored metallic material supplies shading.
            require(EM.enable_renderer(path,emitter,0,False),'Disable core billboard over the missile body')
        set_renderer(path, emitter, 'Material', DEST + ('/M_Commander_Smoke' if smoke else '/M_Commander_Exhaust'))
        if name == 'Trail':
            require(EM.add_renderer(path, emitter, 'Ribbon'), 'Trail ribbon')
            set_renderer(path, emitter, 'Material', DEST+'/M_Commander_Smoke', 1)
            scratch(path, emitter, 'ParticleSpawn', 'TrailWidth', [('EffectScale','float','User.VisualScale')], [('Width','float','Particles.RibbonWidth')], 'Width=35*EffectScale;')
    compile_system(path); save(path)

def explosions():
    source_dir='/Game/Assets/VFX/Explosions/'
    def copy(name, output):
        path=DEST+'/'+output
        if not ASSETS.does_asset_exist(path): require(ASSETS.duplicate_asset(source_dir+name,path),'Copy '+name)
        return require(ASSETS.load_asset(path),'Load '+path)
    # The marketplace pack was flattened and its old AllExplosions parent/texture
    # references no longer resolve. Repair only these project-owned dependencies.
    textures={n:copy(n,'T_WM01_'+n[2:]) for n in ['T_Expl_7','T_Expl_54','T_Expl_55','T_Impact','T_Smoke_1','T_Smoke_Wisp']}
    for texture in textures.values():
        texture.set_editor_property('max_texture_size',2048)
        texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
        texture.set_editor_property('never_stream',True)
        save(texture)
    materials={}
    for source,output,texture in [('M_Explosion_1','M_WM01_Explosion','T_Expl_7'),
                                   ('M_Impact_3','M_WM01_Impact','T_Impact'),
                                   ('M_Smoke_1','M_WM01_BlastSmoke','T_Smoke_1'),
                                   ('M_Smoke_Wisp_2','M_WM01_SmokeWisp','T_Smoke_Wisp')]:
        obj=copy(source,output)
        samples=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionTextureSample) if n.get_outer()==obj]
        require(len(samples)==1,'Expected one authored texture in '+source)
        samples[0].set_editor_property('texture',textures[texture])
        if source=='M_Smoke_1' and not isinstance(samples[0],unreal.MaterialExpressionParticleSubUV):
            # The flattened marketplace copy samples the entire 8x8 smoke sheet.
            # Preserve its lit smoke/depth fade, replacing only the broken sample.
            original=samples[0]
            subuv=MAT.create_material_expression(obj,unreal.MaterialExpressionParticleSubUV)
            subuv.set_editor_property('texture',textures[texture])
            subuv.set_editor_property('blend',True)
            base=MAT.get_material_property_input_node(obj,unreal.MaterialProperty.MP_BASE_COLOR)
            fade=MAT.get_material_property_input_node(obj,unreal.MaterialProperty.MP_OPACITY)
            alpha=MAT.get_inputs_for_material_expression(obj,fade)[0]
            require(isinstance(base,unreal.MaterialExpressionMultiply) and isinstance(alpha,unreal.MaterialExpressionMultiply),
                    'Expected copied smoke color and alpha multipliers')
            require(MAT.connect_material_expressions(subuv,'RGB',base,'B'),'Smoke color sub-frame')
            require(MAT.connect_material_expressions(subuv,'A',alpha,'B'),'Smoke alpha sub-frame')
            MAT.delete_material_expression(obj,original)
            MAT.layout_material_expressions(obj)
        if source=='M_Explosion_1':
            scale=next((n for n in unreal.ObjectIterator(unreal.MaterialExpressionScalarParameter)
                        if n.get_outer()==obj and str(n.get_editor_property('parameter_name'))=='CombatExposure'),None)
            if not scale:
                original=MAT.get_material_property_input_node(obj,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
                output=MAT.get_material_property_input_node_output_name(obj,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
                require(original,'Explosion emissive output')
                scale=MAT.create_material_expression(obj,unreal.MaterialExpressionScalarParameter)
                scale.set_editor_property('parameter_name','CombatExposure')
                multiply=MAT.create_material_expression(obj,unreal.MaterialExpressionMultiply)
                MAT.connect_material_expressions(original,output,multiply,'A')
                MAT.connect_material_expressions(scale,'',multiply,'B')
                MAT.connect_material_property(multiply,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            scale.set_editor_property('default_value',.12)
        MAT.set_material_usage(obj,unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
        MAT.recompile_material(obj); save(obj); materials[source]=obj
    for index,texture in [(14,'T_Expl_54'),(15,'T_Expl_55')]:
        obj=copy('MI_Explosion_'+str(index),'MI_WM01_Explosion_'+str(index))
        obj.set_editor_property('parent',materials['M_Explosion_1'])
        MAT.set_material_instance_texture_parameter_value(obj,'Texture',textures[texture])
        MAT.update_material_instance(obj); save(obj); materials['MI_Explosion_'+str(index)]=obj
    materials['MI_Glow_3']=ASSETS.load_asset(DEST+'/M_Commander_Exhaust')
    for number in (7,8,9):
        source = '/Game/Assets/VFX/Explosions/NS_Explosion_Medium_' + str(number)
        dest = DEST + '/NS_WM01_Explosion_' + str(number)
        if not ASSETS.does_asset_exist(dest): require(ASSETS.duplicate_asset(source, dest), 'Copy explosion')
        # Remove unused emitters completely, including stale dependency references.
        for emitter in list(NS.list_emitters(dest)):
            name = str(prop(emitter,'emitter_name'))
            if name in ['refr_sprite','refr_mesh','spark_l','spark001','spark002','parts']:
                require(NS.remove_emitter(dest,name), 'Remove expensive '+name)
                continue
            if name=='smoke_shockwave':
                # Explicit flipbook metadata is required even on already repaired assets.
                set_renderer(dest,name,'SubImageSize','(X=8,Y=8)')
                set_renderer(dest,name,'bSubImageBlend','True')
                if not any(str(prop(m,'module_name')).startswith('CombatSmokeFlipbook') for m in EM.list_modules(dest,name)):
                    scratch(dest,name,'ParticleUpdate','CombatSmokeFlipbook',
                            [('Age','float','Particles.NormalizedAge')],
                            [('Frame','float','Particles.SubImageIndex')],
                            'Frame=saturate(Age)*63.0;')
            for index,_ in enumerate(EM.list_renderers(dest,name)):
                detail=EM.get_renderer_details(dest,name,index)
                if str(prop(detail,'renderer_type'))=='Light':
                    require(EM.enable_renderer(dest,name,index,False),'Disable dynamic light')
                    continue
                old=str(prop(detail,'material_path')).split('.')[-1]
                if old.startswith(('M_WM01_','MI_WM01_','M_Commander_')): continue
                require(old in materials,'Unreviewed explosion material '+old)
                set_renderer(dest,name,'Material',materials[old].get_path_name(),index)
        require(NS.set_parameter(dest,'User.LifetimeMult','0.7'), 'Explosion lifetime')
        compile_system(dest); save(dest)

def data_asset(name, cls):
    path = DEST+'/'+name
    if ASSETS.does_asset_exist(path): return ASSETS.load_asset(path)
    factory = unreal.DataAssetFactory(); factory.set_editor_property('data_asset_class',cls)
    return require(TOOLS.create_asset(name, DEST, cls, factory), 'Create '+path)

def configure_machinegun_impact(catalog):
    # The specified source naturally completes after about 1 second. Reuse it
    # unchanged; the presentation subsystem provides pooled ownership and a 3s cap.
    system = require(ASSETS.load_asset(MACHINEGUN_IMPACT_SOURCE), 'Load machine-gun impact')
    require(isinstance(system, unreal.NiagaraSystem), 'Machine-gun impact must be Niagara')
    impact = unreal.GuLiEffectVisualVariant()
    impact.set_editor_property('vfx_id', vfx_id('MachineGunImpact'))
    impact.set_editor_property('scale_parameter_name', 'None')
    impact.set_editor_property('random_yaw', False)
    impact.set_editor_property('maximum_lifetime', 3.0)
    impact.set_editor_property('additional_layers', [])
    catalog.set_editor_property('machine_gun_impact', impact)


def machinegun_impact():
    catalog = require(ASSETS.load_asset(DEST+'/DA_CommanderCombatEffects'), 'Load existing combat catalog')
    configure_machinegun_impact(catalog)
    save(catalog)
    impact = prop(catalog, 'machine_gun_impact')
    report['machinegun_impact'] = {
        'system': vfx_resource(prop(impact, 'vfx_id')),
        'scale': vfx_scale(prop(impact, 'vfx_id')),
        'maximum_lifetime': prop(impact, 'maximum_lifetime'),
    }


def configure_gunfire_catalog(catalog):
    catalog.set_editor_property('gunfire_channel',ASSETS.load_asset(DEST+'/NDC_CommanderGunfire'))
    catalog.set_editor_property('gunfire_vfx_id',vfx_id('CommanderGunfire'))
    catalog.set_editor_property('ground_machine_gun_vfx_id',vfx_id('GroundMachineGunTracer'))
    catalog.set_editor_property('wingman_laser_vfx_id',vfx_id('MachineGunTracer'))
    catalog.set_editor_property('tracer_lifetime',.075)
    catalog.set_editor_property('tracer_width',6.0)
    catalog.set_editor_property('muzzle_activity_hold_seconds',2.0)
    catalog.set_editor_property('muzzle_refresh_rate',30.0)
    catalog.set_editor_property('muzzle_particle_lifetime',.06)
    catalog.set_editor_property('muzzle_width',84.0)
    catalog.set_editor_property('muzzle_length',240.0)
    catalog.set_editor_property('muzzle_strobe_rate',9.0)
    catalog.set_editor_property('muzzle_strobe_duty_cycle',.45)
    catalog.set_editor_property('maximum_muzzle_lights_per_frame',12)
    catalog.set_editor_property('maximum_tracer_lights_per_frame',6)
    catalog.set_editor_property('muzzle_light_radius',520.0)
    catalog.set_editor_property('muzzle_light_brightness',35.0)
    catalog.set_editor_property('tracer_light_radius',360.0)
    catalog.set_editor_property('tracer_light_brightness',25.0)
    catalog.set_editor_property('gunfire_tint',unreal.LinearColor(1.0,.72,.32,1.0))
    configure_machinegun_impact(catalog)

def gunfire_catalog():
    catalog=data_asset('DA_CommanderCombatEffects',unreal.GuLiCombatEffectCatalog)
    configure_gunfire_catalog(catalog)
    save(catalog)

def catalog():
    field = data_asset('DA_WM01_MissileExplosion',unreal.GuLiSpellFieldDefinition)
    field.set_editor_property('config_id','WM01_MissileExplosion')
    field.set_editor_property('radius',160.0)
    field.set_editor_property('visual_reference_radius',800.0)
    field.set_editor_property('timing',unreal.GuLiSpellFieldTiming.INSTANT)
    field.set_editor_property('dissipation_seconds',3.0)
    variants=[visual_variant('CommanderMissileExplosion', 'User.VisualScale')]
    field.set_editor_property('activation_variants',variants); save(field)
    projectile=data_asset('DA_WM01_Missile',unreal.GuLiProjectileEffectDefinition)
    projectile.set_editor_property('impact_field',field)
    projectile.set_editor_property('flight_vfx_id',vfx_id('MissileFlight'))
    projectile.set_editor_property('trail_fade_seconds',.55); save(projectile)
    catalog=data_asset('DA_CommanderCombatEffects',unreal.GuLiCombatEffectCatalog)
    configure_gunfire_catalog(catalog)
    calibration = json.loads((OUT.parents[1]/'Data/Json/DT_GuLiStrikeCommander_WeaponMounts.json').read_text(encoding='utf-8'))
    require(calibration and all(item['bCalibrated'] for item in calibration),
            'Complete final Excel WeaponMounts calibration first')
    aims={}
    grouped={}
    soldiers=json.loads((OUT.parents[1]/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8'))
    model_scales={int(row['Id']):float(row['PresentationScale']) for row in soldiers}
    for item in calibration:
        unit=int(item['UnitTypeId'])
        point=unreal.Vector(item['Offset']['X'],item['Offset']['Y'],item['Offset']['Z'])*model_scales[unit]
        if item['PointRole'].lower()=='aimtarget':
            require(unit not in aims and int(item['PointIndex'])==0 and not item.get('SlotId'),
                    'Invalid or duplicate Excel AimTarget row')
            aims[unit]=point
        elif item['PointRole'].lower()=='muzzle':
            key=(unit,item.get('SlotId','').strip())
            require(key[1], 'Excel muzzle row requires SlotId')
            entries=grouped.setdefault(key,{})
            index=int(item['PointIndex'])
            require(index not in entries, 'Duplicate Excel muzzle index')
            entries[index]=point
        else:
            raise RuntimeError('Unknown Excel WeaponMounts PointRole: '+item['PointRole'])
    mounts=[]
    for (unit,slot),entries in sorted(grouped.items()):
        require(unit in aims and sorted(entries)==list(range(len(entries))),
                'Excel WeaponMounts requires an AimTarget and contiguous muzzle indices')
        m=unreal.GuLiWeaponEffectMount()
        m.set_editor_property('unit_type_id',unit); m.set_editor_property('slot_id',slot)
        m.set_editor_property('skill_id','WM01_HomingMissile' if slot=='MissileLauncher' else 'Strafe')
        m.set_editor_property('muzzles',[entries[index] for index in range(len(entries))])
        m.set_editor_property('aim_offset',aims[unit])
        m.set_editor_property('calibrated',True)
        if slot=='MissileLauncher': m.set_editor_property('projectile',projectile)
        mounts.append(m)
    catalog.set_editor_property('mounts',mounts); save(catalog)

try:
    for name, func in [('materials',lambda: [material('M_Commander_'+n,s,t) for n,s,t in [('Tracer','beam',False),('Muzzle','muzzle',False),('Exhaust','soft',False),('Smoke','soft',True)]]),
                       ('gunfire-materials',lambda: [material('M_Commander_Tracer','beam',False),material('M_Commander_Muzzle','muzzle',False)]),
                       ('gunfire',gunfire),('gunfire-catalog',gunfire_catalog),('machinegun-impact',machinegun_impact),('flight',flight),('explosions',explosions),('catalog',catalog)]:
        if report['stage'] in ('all', name): func()
    report['success']=True
except Exception as error:
    report['success']=False; report['error']=str(error); report['traceback']=traceback.format_exc()
finally:
    report['dirty_content']=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    (OUT/('authoring_'+report['stage']+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))

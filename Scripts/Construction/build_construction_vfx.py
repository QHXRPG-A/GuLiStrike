"""Author only the three construction systems and their owned tail materials in the live editor."""
import gc
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'outputs/construction-vfx'
DEST = '/Game/GuLiStrike/Buildings/Construction'
SOURCE = '/Game/Assets/VFX/NiagaraUpgradeGlow'
NS, EM, SP = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
report = {'saved': [], 'compiles': {}, 'materials': {}}


def need(value, why):
    if not value:
        raise RuntimeError(why)
    return value


def duplicate(source, target):
    need(target.startswith(DEST + '/'), 'Owned destination')
    if not unreal.EditorAssetLibrary.does_asset_exist(target):
        need(unreal.EditorAssetLibrary.duplicate_asset(source, target), 'Duplicate ' + target)
    return need(unreal.load_asset(target), 'Load ' + target)


def save(path):
    need(path.startswith(DEST + '/'), 'Scoped save')
    need(unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False), 'Save ' + path)
    report['saved'].append(path)


def compile_system(path):
    r = NS.compile_with_results(path)
    report['compiles'][path] = {'success': r.success, 'errors': list(r.errors), 'warnings': list(r.warnings)}
    need(r.success and not r.errors, 'Niagara compile: ' + str(r))
    save(path)


def scratch(path, emitter, stage, name, inputs, outputs, code):
    r = SP.create_scratch_module(path, emitter, stage, name)
    need(r.success, 'Create scratch ' + name)
    module = str(r.module_name)
    h = SP.add_custom_hlsl_node(path, emitter, module, code)
    need(h.success, 'HLSL ' + name)
    node = str(h.node_id)
    read = SP.add_node(path, emitter, module, 'MapGet')
    need(read.success, 'MapGet')
    rid = str(read.node_id)
    inp = next(n for n in SP.list_nodes(path, emitter, module)
               if str(n.node_type) in ('Input', 'NiagaraNodeInput') or str(n.node_type).endswith('NodeInput'))
    iid = str(inp.node_id)
    ip = next(str(p.pin_name) for p in SP.get_node_pins(path, emitter, module, iid) if str(p.direction).lower() == 'output')
    rp = next(str(p.pin_name) for p in SP.get_node_pins(path, emitter, module, rid) if str(p.direction).lower() == 'input')
    need(SP.connect_pins(path, emitter, module, iid, ip, rid, rp), 'Read parameter map')
    for pin, kind, variable in inputs:
        need(SP.add_pin(path, emitter, module, rid, 'Output', kind, variable).success, 'Read ' + variable)
        need(SP.add_pin(path, emitter, module, node, 'Input', kind, pin).success, 'Input ' + pin)
        need(SP.connect_pins(path, emitter, module, rid, variable, node, pin), 'Connect ' + pin)
    for pin, kind, variable in outputs:
        need(SP.add_pin(path, emitter, module, node, 'Output', kind, pin).success, 'Output ' + pin)
        target = SP.add_module_output(path, emitter, module, variable, kind)
        need(target.success, 'Write ' + variable)
        need(SP.connect_pins(path, emitter, module, node, pin, str(target.node_id), variable), 'Connect ' + variable)
    # A stale native bridge must stop here: misordered Custom HLSL pins can assert inside Niagara.
    need(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(unreal.load_asset(path)),
         'Construction scratch support requires the updated native module')
    need(SP.apply_changes(path), 'Apply graph ' + name)


def make_top_loop():
    path = DEST + '/NS_ConstructionTopLoop'
    duplicate(SOURCE + '/Particles/P_UpgradeGlow011_Converted', path)
    # Retain the tail's actual material/texture graphs, while replacing one-shot timing and box geometry.
    column = DEST + '/M_ConstructionColumnTail'
    mote = DEST + '/M_ConstructionMote'
    for src, dst in [('M_line_Down_21', column), ('M_mask', mote)]:
        material = duplicate(SOURCE + '/Materials/' + src, dst)
        material.set_editor_property('two_sided', True)
        unreal.MaterialEditingLibrary.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
        unreal.MaterialEditingLibrary.recompile_material(material)
        d = unreal.MaterialNodeService.get_material_diagnostics(dst)
        report['materials'][dst] = str(d)
        need(d.success and d.is_compiled_ok and not d.compile_errors, 'Material ' + dst)
        save(dst)
    for e in list(NS.list_emitters(path)):
        need(NS.remove_emitter(path, str(e.emitter_name)), 'Reset owned top emitters')
    for name, kind, default in [('RoofPoints', 'ArrayVector', ''), ('RoofTangents','ArrayVector',''),
                                ('RoofPointCount', 'Int', '64'), ('SegmentLengths','ArrayFloat',''), ('ConstructionHeight','Float','0'),
                                ('ColumnHeight', 'Float', '360'), ('SparksActive', 'Float', '1')]:
        if not any(str(p.parameter_name) == 'User.' + name for p in NS.list_parameters(path)):
            need(NS.add_user_parameter(path, name, kind, default), 'User ' + name)
    common = [('Index', 'int', 'Particles.ConstructionSlot'), ('Time', 'float', 'System.Age'),
              ('Origin', 'Position', 'Engine.Owner.Position'), ('Points', 'ArrayVector', 'User.RoofPoints'),
              ('Tangents','ArrayVector','User.RoofTangents'), ('Lengths','ArrayFloat','User.SegmentLengths'),
              ('ConstructionHeight','float','User.ConstructionHeight'),
              ('Count', 'int', 'User.RoofPointCount'), ('Height', 'float', 'User.ColumnHeight'),
              ('Active', 'float', 'User.SparksActive')]
    outputs = [('Position', 'Position', 'Particles.Position'), ('Color', 'Color', 'Particles.Color'),
               ('Size', 'vec2', 'Particles.SpriteSize'), ('Facing', 'Vector', 'Particles.SpriteFacing'),
               ('Alignment', 'Vector', 'Particles.SpriteAlignment'), ('Rotation', 'float', 'Particles.SpriteRotation')]
    # Sample uniform arc length, including slopes. All renderers share the same authored polygon as vehicle beams.
    sampling = '''
float seed=frac(sin((Index+1)*78.233)*43758.5453);
float t=frac(sin((Index+1)*12.9898)*43758.5453);
int slot=clamp(int(SAMPLE*Count),0,max(Count-1,0));
float3 base; float3 edge; float Step;
Points.Get(slot,base); Tangents.Get(slot,edge); Lengths.Get(slot,Step);
base.z+=ConstructionHeight;
float breath=0.68+0.32*(0.5-0.5*cos(Time*6.28318530718));
Position=Origin+base; Color=float4(0,0,0,0); Size=float2(0,0);
Facing=normalize(cross(edge,float3(0,0,1))); Alignment=float3(0,0,1); Rotation=0;
'''
    jobs = [
        ('TailColumns', 64, column, '(Index+0.5)/max(Count,1)', '''
Position+=float3(0,0,Height*0.5);
Size=float2(Step*1.015,Height);
Color=float4(float3(2.0,6.0,4.5)*breath,0.28*breath);
'''),
        ('FloatingMotes', 64, mote, 'seed', '''
float age=frac(Time/(2.0+t*1.1)+seed);
Position+=float3(sin(seed*90+Time*0.7)*9,cos(seed*40+Time*0.6)*9,age*Height);
float fadeIn=saturate(age/0.12); float fadeOut=saturate((1-age)/0.35);
float fade=fadeIn*fadeIn*(3-2*fadeIn)*fadeOut*fadeOut*(3-2*fadeOut);
Size=float2(6+t*5,6+t*5); Color=float4(float3(4,12,9)*breath,fade*0.8*breath); Rotation=seed*6.28318;
'''),
        ('EdgeSparks', 80, '/Niagara/DefaultAssets/DefaultSpriteMaterial', 'frac(seed+floor(Time*7+seed)*0.6180339)', '''
float age=frac(Time*7+seed);
float3 velocity=float3(sin(seed*53)*50,cos(seed*87)*50,25+t*65);
Position+=velocity*age*0.14;
Alignment=normalize(velocity); Size=float2(2.5+t*2,13+t*19);
Color=float4(float3(12,30,50),Active*pow(1-age,2)*(0.7+0.3*sin(Time*57+seed*99)));
''')]
    for name, count, material, sample, code in jobs:
        emitter = need(NS.add_emitter(path, '/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst', name), 'Emitter ' + name)
        for m in list(EM.list_modules(path, emitter)):
            if str(m.module_name) not in ('EmitterState', 'ParticleState', 'SpawnBurst_Instantaneous', 'InitializeParticle'):
                need(EM.remove_module(path, emitter, str(m.module_name)), 'Remove template module')
        need(NS.set_rapid_iteration_param_by_stage(path, emitter, 'EmitterUpdate',
             f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count', str(count)), 'Pool size')
        scratch(path, emitter, 'ParticleSpawn', 'AllocateConstructionSlots', [('Index','int','Engine.ExecIndex')],
                [('Slot','int','Particles.ConstructionSlot'), ('Life','float','Particles.Lifetime'),
                 ('Position','Position','Particles.Position'), ('Velocity','Vector','Particles.Velocity'),
                 ('Size','vec2','Particles.SpriteSize'), ('Color','Color','Particles.Color')],
                'Slot=Index; Life=1000000000.0; Position=float3(0,0,0); Velocity=float3(0,0,0); Size=float2(0,0); Color=float4(0,0,0,0);')
        scratch(path, emitter, 'ParticleUpdate', 'FollowConstructionRoof', common, outputs, sampling.replace('SAMPLE', sample) + code)
        need(unreal.GuLiConstructionShapeLibrary.bind_construction_spawn_count(unreal.load_asset(path), emitter), 'Adaptive spawn count')
        need(EM.set_renderer_property(path, emitter, 0, 'Material', material), 'Renderer material')
        if name == 'TailColumns':
            need(EM.set_renderer_property(path, emitter, 0, 'FacingMode', 'CustomFacingVector'), 'Column facing')
            need(EM.set_renderer_property(path, emitter, 0, 'Alignment', 'CustomAlignment'), 'Column up')
        elif name == 'EdgeSparks':
            need(EM.set_renderer_property(path, emitter, 0, 'Alignment', 'CustomAlignment'), 'Spark direction')
    system = unreal.load_asset(path)
    system.set_editor_property('warmup_time', 0.0)
    system.set_editor_property('bFixedBounds', True)
    system.set_editor_property('FixedBounds', unreal.Box(min=unreal.Vector(-1600,-1600,-600), max=unreal.Vector(1600,1600,1600)))
    system.set_editor_property('max_pool_size', 0)
    compile_system(path)
    report['top_saved'] = {'emitters': [str(e.emitter_name) for e in NS.list_emitters(path)],
                           'parameters': [str(p.parameter_name) for p in NS.list_parameters(path)]}


def adapt_completion(path):
    """Keep the source emitter lifetimes/color curves; replace its box silhouettes with footprint meshes."""
    system = unreal.load_asset(path)
    shape = need(unreal.load_asset(DEST + '/Shapes/DA_ConstructionShape_6'), 'Baked preview footprint')
    for name, kind, default in [('RoofPoints','ArrayVector',''), ('RoofPointCount','Int','64'),
                                ('BuildingHeight','Float','440'), ('ColumnHeight','Float','360')]:
        if not any(str(p.parameter_name) == 'User.' + name for p in NS.list_parameters(path)):
            need(NS.add_user_parameter(path,name,kind,default),'Completion input '+name)
    for info in list(NS.list_emitters(path)):
        emitter = str(info.emitter_name)
        modules = list(EM.list_modules(path, emitter))
        # An explicit repeat authoring run replaces only our own scratch module calls.
        for module in modules:
            if str(module.module_name).startswith(('ConstructionShape','RememberConstruction')):
                need(EM.remove_module(path,emitter,str(module.module_name)), 'Remove previous shape adapter')
        details = list(EM.get_renderer_details(path,emitter,r.renderer_index) for r in EM.list_renderers(path,emitter))
        mesh = any(str(r.renderer_type) == 'Mesh' for r in details)
        floor = emitter == 'Decal'
        if mesh or floor:
            material_path = next((str(r.material_path) for r in details if r.has_material), '')
            material = need(unreal.load_asset(SOURCE + '/Materials/M_base_2' if floor else material_path),'Original glow material')
            if floor:
                for renderer in reversed(list(EM.list_renderers(path,emitter))):
                    need(EM.remove_renderer(path,emitter,renderer.renderer_index),'Replace floor billboard')
                need(EM.add_renderer(path,emitter,'Mesh'),'Footprint floor renderer')
            scratch(path,emitter,'ParticleSpawn','RememberConstructionScale',
                [('Scale','vec3','Particles.Scale')], [('OriginalScale','vec3','Particles.ConstructionSourceScale')],
                'OriginalScale=Scale;')
            code = '''
float front=saturate(Age*2.5); front=front*front*(3-2*front);
Position=float3(0,0,2);
Scale=float3(1,1,max(0.015,OriginalScale.z/6.0)*Height/100.0*(0.08+front*1.2));
'''
            if emitter == 'UP01':
                code += 'Position.z+=Age*Height; Scale.z=max(0.025,OriginalScale.z/6.0)*Height/100.0;'
                for module in list(EM.list_modules(path,emitter)):
                    if str(module.module_name).startswith('SetVariables_'):
                        need(EM.remove_module(path,emitter,str(module.module_name)),'Remove source random box orientation')
            if floor:
                code = 'Position=float3(0,0,3); Scale=float3(1,1,1);'
            scratch(path,emitter,'ParticleUpdate','ConstructionShapeGeometry',
                [('OriginalScale','vec3','Particles.ConstructionSourceScale'), ('Age','float','Particles.NormalizedAge'),
                 ('Height','float','User.BuildingHeight')],
                [('Position','Position','Particles.Position'), ('Scale','vec3','Particles.Scale')], code)
            need(unreal.GuLiConstructionShapeLibrary.bind_construction_mesh_renderer(
                system,emitter,'User.FootprintMesh' if floor else 'User.ContourWallMesh',
                shape.get_editor_property('base_mesh' if floor else 'wall_mesh'),material), 'Runtime footprint mesh binding')
        else:
            # Keep each source's color, lifetime and velocity; distribute its origin around the actual perimeter.
            scratch(path,emitter,'ParticleSpawn','ConstructionShapeParticleOrigins',
                [('Index','int','Engine.ExecIndex'), ('Points','ArrayVector','User.RoofPoints'),
                 ('Count','int','User.RoofPointCount')], [('Position','Position','Particles.Position')],
                '''float seed=frac(sin((Index+1)*78.233)*43758.5453);
int slot=clamp(int(seed*Count),0,max(Count-1,0)); float3 sourcePoint; Points.Get(slot,sourcePoint);
Position=sourcePoint+float3(0,0,4);''')
    system.set_editor_property('warmup_time',0.0)
    system.set_editor_property('max_pool_size',0)


def main():
    need(not unreal.WidgetService.is_pie_running(), 'Author outside PIE')
    complete = DEST + '/NS_ConstructionComplete'
    duplicate(SOURCE + '/Particles/P_UpgradeGlow011_Converted', complete)
    adapt_completion(complete)
    compile_system(complete)
    laser = DEST + '/NS_ConstructionLaser_Purple'
    duplicate('/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green', laser)
    for e, color in [('Beam','18,1.0,50,1'), ('Beam001','5,0.25,14,1'),
                     ('Spark','5,9,14,1'), ('Spark001','2,4,6,1')]:
        for stage in ['ParticleSpawn', 'ParticleUpdate']:
            need(NS.set_rapid_iteration_param_by_stage(laser,e,stage,f'Constants.{e}.Color.Color',color),'Purple color')
    for e, width in [('Beam','8'),('Beam001','2.5')]:
        need(NS.set_rapid_iteration_param_by_stage(laser,e,'ParticleSpawn',f'Constants.{e}.BeamWidth.Beam Width',width),'Readable beam width')
    compile_system(laser)
    make_top_loop()


try:
    main()
    report['success'] = True
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'asset-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'saved':report['saved']}))
gc.collect()

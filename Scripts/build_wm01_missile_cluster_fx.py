"""Build dedicated WM01 GPU pools after the new editor module is loaded.

Does not touch the old shared flight system or start gameplay. Native and GPU
compiler diagnostics, save results and emitter counts are written separately.
"""
import ast
import json
import traceback
import sys
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/WM01MissileCards/Visual_v3'
sys.path.insert(0, str(ROOT/'Scripts'))
from wm01_missile_visual_config import read_profile
PROFILE, VISUAL_PARAMETERS = read_profile(unreal)
DEST = '/Game/GuLiStrike/FX/WM01Missiles'
SOURCE = '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool'
LIB = unreal.EditorAssetLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
EDIT = unreal.MaterialEditingLibrary
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService
SP = unreal.NiagaraScratchPadService
report = {'success': False, 'visual_revision': 3, 'systems': [], 'materials': [],
          'visual_parameters': {'source':PROFILE['Name'],'body_length_cm':195,'user_parameters':VISUAL_PARAMETERS,
              'flame_peak_rgb':[9,7.02,3.6],'smoke_lifetime':2.4,
              'smoke_cold_rgb':[.055,.060,.065],'smoke_alpha':.70,'smoke_fade_power':1.25,'warm_seconds':.08}}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def prop(obj, name):
    return obj.get_editor_property(name)


def node(owner, cls, **props):
    result = EDIT.create_material_expression(owner, cls)
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def material(name, trail):
    path = DEST + '/' + name
    mat = unreal.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    while EDIT.get_num_material_expressions(mat):
        EDIT.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    EDIT.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate)
    color = node(mat, unreal.MaterialExpressionParticleColor)
    code = ('float n=.5+.25*sin(dot(World,float3(.027,.019,.013)))+.25*sin(dot(World,float3(-.013,.031,.017))); '
            'float x=abs(UV.x*2-1+(n-.5)*.18); float endFade=min(.04,2/max(Size.y,1)); return pow(saturate(1-x*x),2)*lerp(.55,1,n)*smoothstep(0,endFade,UV.y)*smoothstep(0,endFade,1-UV.y)*Alpha;'
            if trail else 'float t=saturate(UV.y); float x=abs(UV.x*2-1)/lerp(.08,1,t); return pow(saturate(1-x*x),1.2)*sin(t*3.14159)*Alpha;')
    mask = node(mat, unreal.MaterialExpressionCustom, code=code, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for key in (('UV', 'Alpha', 'World', 'Size') if trail else ('UV', 'Alpha')):
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', key)
        pins.append(entry)
    mask.set_editor_property('inputs', pins)
    assert EDIT.connect_material_expressions(uv, '', mask, 'UV')
    assert EDIT.connect_material_expressions(color, 'A', mask, 'Alpha')
    if trail:
        world = node(mat, unreal.MaterialExpressionWorldPosition)
        assert EDIT.connect_material_expressions(world, '', mask, 'World')
        # A percentage of an 800+ cm low-detail segment leaves visible gaps.
        # Limit longitudinal edge feathering to 2 cm; transverse soft edges stay unchanged.
        size = node(mat, unreal.MaterialExpressionParticleSize)
        assert EDIT.connect_material_expressions(size, '', mask, 'Size')
    assert EDIT.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY)
    if trail:
        assert EDIT.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        # Peak luminance comes from a small pale-yellow core; the edge stays warm.
        glow = node(mat, unreal.MaterialExpressionCustom,
            code='float t=saturate(UV.y); float x=abs(UV.x*2-1)/lerp(.08,1,t); float core=pow(saturate(1-x),3)*smoothstep(.1,.55,t); return lerp(Tint,float3(Tint.r,Tint.r*.78,Tint.r*.4),core);',
            output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        inputs=[]
        for name in ['UV','Tint']:
            pin=unreal.CustomInput();pin.set_editor_property('input_name',name);inputs.append(pin)
        glow.set_editor_property('inputs',inputs)
        assert EDIT.connect_material_expressions(uv,'',glow,'UV')
        assert EDIT.connect_material_expressions(color,'RGB',glow,'Tint')
        assert EDIT.connect_material_property(glow,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat)
    diagnostics = unreal.MaterialNodeService.get_material_diagnostics(path)
    assert diagnostics.is_compiled_ok, str(diagnostics)
    assert LIB.save_loaded_asset(mat, False)
    report['materials'].append(path)
    return path


def build_system(suffix, lanes, flame, trail, preview=False):
    path = DEST + '/NS_WM01MissileCluster_' + suffix
    system = unreal.load_asset(path) if LIB.does_asset_exist(path) else LIB.duplicate_asset(SOURCE, path)
    assert system
    for entry in list(NS.list_emitters(path)):
        assert NS.remove_emitter(path, str(prop(entry, 'emitter_name')))
    parameters = [('MissileTime','Float','0'), ('MissileHeads','Float','1'),
                                ('MissileEmitHistory','Float','1'), ('MissileTrailWeight','Float','1'),
                                ('MissileSlotCount','Int','64'), ('MissileHistoryCount','Int',str(64*lanes))]
    if preview:
        parameters.append(('MissilePreviewSpeedRatio','Float',str(PROFILE['SpeedCentimetersPerSecond']/1200.)))
    parameters += [(name.removeprefix('User.'),'Float',str(value)) for name,value in VISUAL_PARAMETERS.items()]
    for name, kind, default in parameters:
        if not NS.parameter_exists(path, 'User.'+name):
            assert NS.add_user_parameter(path, name, kind, default)
        else:
            assert NS.set_parameter(path, 'User.'+name, default)
    entries = [('Body', 64), ('Flame', 64)] + ([('History', 64 * lanes)] if lanes else [])
    for emitter, count in entries:
        assert NS.add_emitter(path, '/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst', emitter) == emitter
        for entry in list(EM.list_modules(path, emitter)):
            name = str(prop(entry, 'module_name'))
            if not name.startswith(('EmitterState', 'ParticleState')):
                assert EM.remove_module(path, emitter, name)
        assert EM.add_module(path, emitter, '/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous', 'EmitterUpdate')
        assert NS.set_rapid_iteration_param_by_stage(path, emitter, 'EmitterUpdate', f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count', str(count))
        outputs = [('Slot', 'int', 'Particles.LaserSlot'), ('Life', 'float', 'Particles.Lifetime'), ('Position', 'Position', 'Particles.Position'),
                   ('Velocity', 'Vector', 'Particles.Velocity'), ('Size', 'vec2', 'Particles.SpriteSize'), ('Color', 'Color', 'Particles.Color'), ('Rotation', 'float', 'Particles.SpriteRotation')]
        init = 'Slot=ExecIndex(); Life=1e9; Position=float3(0,0,0); Velocity=float3(0,0,0); Size=float2(0,0); Color=float4(0,0,0,0); Rotation=0;'
        if emitter == 'History':
            outputs += [('Lane','int','Particles.MissileLane'), ('Start','Position','Particles.HistoryStart'), ('End','Position','Particles.HistoryEnd'),
                        ('Sample','Position','Particles.HistorySample'), ('Born','float','Particles.HistoryBorn'), ('Generation','float','Particles.HistoryGeneration'),
                        ('Bucket','int','Particles.HistoryBucket'), ('Time','float','Particles.HistoryTime'), ('State','float','Particles.HistoryState')]
            init += f'Slot=ExecIndex()/{lanes}; Lane=ExecIndex()%{lanes}; Start=Position; End=Position; Sample=Position; Born=-10000; Generation=-1; Bucket=-1; Time=-10000; State=0;'
        scratch(path, emitter, 'ParticleSpawn', 'InitializeMissileSlot', [], outputs, init)
        if preview:
            scratch(path, emitter, 'ParticleUpdate', 'IllustrativePreviewPath',
                [('Slot','int','Particles.LaserSlot'),('Age','float','System.Age'),('SpeedRatio','float','User.MissilePreviewSpeedRatio')],
                [('PositionOut','Position','Particles.Position'),('DirectionOut','Vector','Particles.SpriteAlignment'),
                 ('SizeOut','vec2','Particles.SpriteSize'),('MetaOut','Color','Particles.Color'),('TimeOut','float','Particles.PreviewTime')],
                (ROOT/'Scripts/Niagara/GuLiMissileArtPreview.hlsl').read_text(encoding='utf-8-sig'))
        else:
            reader = SP.create_scratch_module(path, emitter, 'ParticleUpdate', 'ReadMissileSlot')
            assert prop(reader, 'success')
            assert unreal.GuLiCombatEffectAuthoringLibrary.wire_laser_pool_reader(system, unreal.load_object(None, str(prop(reader, 'script_path'))), False) is not None
            assert SP.apply_changes(path)
        common = [('Head','Position','Particles.Position'), ('Direction','Vector','Particles.SpriteAlignment'),
                  ('Size','vec2','Particles.SpriteSize'), ('Meta','Color','Particles.Color')]
        if emitter == 'Body':
            assert EM.remove_renderer(path, emitter, 0)
            assert EM.add_renderer(path, emitter, 'Mesh')
            assert EM.set_renderer_property(path, emitter, 0, 'FacingMode', 'Velocity')
            scratch(path, emitter, 'ParticleUpdate', 'MissileBody', common+[('Heads','float','User.MissileHeads')],
                    [('PositionOut','Position','Particles.Position'), ('VelocityOut','Vector','Particles.Velocity'), ('ScaleOut','Vector','Particles.Scale'), ('ColorOut','Color','Particles.Color')],
                    'PositionOut=Head; VelocityOut=Direction; float alive=(Meta.g>.5 && Meta.g<1.5)?Heads:0; ScaleOut=float3(1,1,1)*(Size.y/325)*alive; ColorOut=float4(1,1,1,alive);')
        elif emitter == 'Flame':
            assert EM.set_renderer_property(path, emitter, 0, 'Material', flame)
            assert EM.set_renderer_property(path, emitter, 0, 'Alignment', 'CustomAlignment')
            scratch(path, emitter, 'ParticleUpdate', 'MissileFlame', common+[('Heads','float','User.MissileHeads'),
                    ('FlameWidth','float','User.MissileFlameWidth'),('FlameLength','float','User.MissileFlameLength')],
                    [('PositionOut','Position','Particles.Position'), ('SizeOut','vec2','Particles.SpriteSize'), ('ColorOut','Color','Particles.Color')],
                    'float alive=(Meta.g>.5 && Meta.g<1.5)?Heads:0; PositionOut=Head-Direction*(Size.y*.4923+FlameLength*.5); SizeOut=float2(FlameWidth,FlameLength)*alive; ColorOut=float4(9,3.3,.66,.85*alive);')
        else:
            assert EM.set_renderer_property(path, emitter, 0, 'Material', trail)
            assert EM.set_renderer_property(path, emitter, 0, 'Alignment', 'CustomAlignment')
            inputs = [('Head','Position','Particles.Position'), ('Width','float','Particles.HistoryWidth'), ('MaximumWidth','float','User.MissileSmokeMaximumWidth'), ('Meta','Color','Particles.Color'),
                      ('Lane','int','Particles.MissileLane'), ('Now','float','Particles.PreviewTime' if preview else 'User.MissileTime'), ('SavedStart','Position','Particles.HistoryStart'),
                      ('SavedEnd','Position','Particles.HistoryEnd'), ('SavedSample','Position','Particles.HistorySample'), ('Born','float','Particles.HistoryBorn'),
                      ('SeenGeneration','float','Particles.HistoryGeneration'), ('SeenBucket','int','Particles.HistoryBucket'),
                      ('LastTime','float','Particles.HistoryTime'), ('SeenState','float','Particles.HistoryState'),
                      ('EmitHistory','float','User.MissileEmitHistory'), ('TrailWeight','float','User.MissileTrailWeight')]
            scratch(path, emitter, 'ParticleUpdate', 'MissileTrailOrigin', common+[('SmokeWidth','float','User.MissileSmokeInitialWidth'),
                    ('FlameLength','float','User.MissileFlameLength')],
                    [('PositionOut','Position','Particles.Position'),('WidthOut','float','Particles.HistoryWidth')], 'PositionOut=Head-Direction*(Size.y*.4923+FlameLength*(7.0/9.0)); WidthOut=SmokeWidth;')
            outputs = [('PositionOut','Position','Particles.Position'), ('AlignmentOut','Vector','Particles.SpriteAlignment'), ('SizeOut','vec2','Particles.SpriteSize'),
                       ('ColorOut','Color','Particles.Color'), ('StartOut','Position','Particles.HistoryStart'), ('EndOut','Position','Particles.HistoryEnd'),
                       ('SampleOut','Position','Particles.HistorySample'), ('BornOut','float','Particles.HistoryBorn'), ('GenerationOut','float','Particles.HistoryGeneration'),
                       ('BucketOut','int','Particles.HistoryBucket'), ('TimeOut','float','Particles.HistoryTime'), ('StateOut','float','Particles.HistoryState')]
            code = (ROOT/'Scripts/Niagara/GuLiMissileHistory.hlsl').read_text(encoding='utf-8-sig').replace('LANES',str(lanes))
            scratch(path, emitter, 'ParticleUpdate', 'MissileRealHistory', inputs, outputs, code)
    assert unreal.GuLiCombatEffectAuthoringLibrary.configure_missile_cluster_system(system) is not None
    system.set_editor_property('max_pool_size', 16)
    system.set_editor_property('pool_prime_size', 0)
    result = NS.compile_with_results(path)
    diagnostics = unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
    bound = bool(NS.parameter_exists(path,'User.MissileContractVersion'))
    entry = {'path':path, 'lanes':lanes, 'particle_capacity':64*(2+lanes), 'capacity_binding_ready':bound, 'service_success':bool(prop(result,'success')),
             'errors':[str(x) for x in prop(result,'errors')], 'native_diagnostics':diagnostics}
    report['systems'].append(entry)
    assert entry['service_success'] and not entry['errors'], entry
    assert 'VM ERROR:' not in diagnostics and 'GPU ERROR:' not in diagnostics, diagnostics
    assert diagnostics.count('GPU finished=1 complete=1') == len(entries), diagnostics
    LIB.set_metadata_tag(system, 'GuLi.WM01Missiles.Contract', f'{"v2" if bound else "draft;capacity binding pending native reload"};GPU;{lanes} world-history lanes;2.4s tail;.15s handoff;no collision/lights;generation-isolated')
    LIB.set_metadata_tag(system, 'GuLi.WM01Missiles.VisualRevision', '3;table-authored smoke/flame centimeters;2.4s charcoal history;body/brightness unchanged')
    if preview:
        system.set_editor_property('fixed_bounds',unreal.Box(unreal.Vector(-600,-1600,-600),unreal.Vector(3000,1600,2000)))
        LIB.set_metadata_tag(system,'GuLi.PreviewOnly','Illustrative self-running paths; no gameplay reference; smoke/flame art review only')
    assert LIB.save_loaded_asset(system, False)


try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert hasattr(unreal.GuLiCombatEffectAuthoringLibrary, 'configure_missile_cluster_system'), 'Compile and load the new editor module first.'
    tree = ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    function = next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    exec(compile(ast.Module(body=[function],type_ignores=[]),'shared_scratch_helper','exec'),globals())
    for name in ('M_WM01_MissileBody', 'SM_WM01_Missile'):
        if not LIB.does_asset_exist(DEST+'/'+name):
            assert LIB.duplicate_asset('/Game/GuLiStrike/FX/CommanderWeapons/'+name, DEST+'/'+name)
    mesh = unreal.load_asset(DEST+'/SM_WM01_Missile')
    mesh.set_material(0, unreal.load_asset(DEST+'/M_WM01_MissileBody'))
    assert LIB.save_loaded_asset(mesh, False)
    flame = material('M_WM01MissileFlame',False)
    trail = material('M_WM01MissileHistory',True)
    preview_only = bool(globals().get('GULI_MISSILE_PREVIEW',False))
    for suffix, lanes in (('Full',24),('Lite',8),('Minimal',0)):
        build_system(('Preview' if preview_only else '')+suffix,lanes,flame,trail,preview_only)
    definition = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
    report['production_cluster_enabled'] = bool(definition.get_editor_property('use_missile_cluster_rendering'))
    report['candidate_entry'] = 'LVL_CommanderMassPrototype PIE: gs.MissileCluster.Candidate 1'
    # The live combat definition is switched only after the playable candidate is approved.
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True,exist_ok=True)
(OUT/('missile-cluster-preview-assets.json' if globals().get('GULI_MISSILE_PREVIEW',False) else 'missile-cluster-assets.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'systems':[r['path'] for r in report['systems']]}))

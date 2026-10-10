"""Run inside the UE editor. Publish frozen, independent review candidates, never production references."""
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'outputs/performance/20261009-implementation'
AS = unreal.EditorAssetLibrary
NS = unreal.NiagaraService
ES = unreal.NiagaraEmitterService
SP = unreal.NiagaraScratchPadService
MATERIAL = '/Game/GuLiStrike/FX/Mining/M_ToolLaser_Opaque'
CONTRACTS = [
    ('Mining', '/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green',
     '/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized', {'Beam': (5.0, 7.5), 'Beam001': (1.0, 1.5)}),
    ('Construction', '/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Purple',
     '/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized', {'Beam': (8.0, 12.0), 'Beam001': (2.5, 3.75)}),
]
report = {'success': False, 'production_references_changed': False, 'systems': [], 'saved': []}


def need(result, message):
    if not result:
        raise RuntimeError(message)
    return result


def duplicate(source, target):
    need(source.startswith('/Game/'), source)
    if not AS.does_asset_exist(target):
        need(AS.duplicate_asset(source, target), 'Duplicate ' + target)
    return target


def compile_save(path):
    r = NS.compile_with_results(path)
    need(r.success and r.error_count == 0, str(r))
    # This bridge reads the native VM compiler messages, rather than readiness alone.
    system = AS.load_asset(path)
    diagnostics = unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
    need('VM ERROR:' not in diagnostics and 'GPU ERROR:' not in diagnostics, diagnostics)
    native = {'raw': diagnostics}
    need(NS.save_system(path), 'Save ' + path)
    report['saved'].append(path)
    settings = NS.get_all_editable_settings(path)
    report['systems'].append({'path': path, 'compile': str(r), 'native': native,
        'parameters': [{'path': x.setting_path, 'value': x.current_value, 'emitter': x.emitter_name}
                       for x in settings.rapid_iteration_parameters
                       if 'Beam Width' in x.setting_path or 'Spawn Count' in x.setting_path],
        'emitters': str(NS.summarize(path))})


def make_material():
    material = AS.load_asset(MATERIAL)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'M_ToolLaser_Opaque', '/Game/GuLiStrike/FX/Mining', unreal.Material, unreal.MaterialFactoryNew())
    need(material, 'Opaque material')
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)
    material.set_editor_property('used_with_niagara_ribbons', True)
    lib = unreal.MaterialEditingLibrary
    # Rebuild only this owned candidate graph, making reruns deterministic.
    lib.delete_all_material_expressions(material)
    color = lib.create_material_expression(material, unreal.MaterialExpressionParticleColor, -240, 0)
    need(lib.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'Particle RGB emissive')
    lib.recompile_material(material)
    report['material_graph'] = json.loads(unreal.MaterialNodeService.export_material_graph(MATERIAL))
    report['material_diagnostics'] = str(unreal.MaterialNodeService.get_material_diagnostics(MATERIAL))
    need(AS.save_asset(MATERIAL, only_if_is_dirty=True), 'Save opaque material')
    report['saved'].append(MATERIAL)


def envelope(path, emitter):
    name = 'ToolLaserOpaqueWidthEnvelope'
    if any(str(x.module_name).startswith(name) for x in ES.list_modules(path, emitter)):
        return
    r = SP.create_scratch_module(path, emitter, 'ParticleUpdate', name)
    need(r.success, 'Create width envelope')
    module = str(r.module_name)
    hlsl = SP.add_custom_hlsl_node(path, emitter, module, 'OutWidth = max(0.0, Width * saturate(Color.a));')
    need(hlsl.success, 'Envelope HLSL')
    hid = str(hlsl.node_id)
    read = SP.add_node(path, emitter, module, 'MapGet')
    need(read.success, 'Envelope MapGet')
    rid = str(read.node_id)
    inp = next(n for n in SP.list_nodes(path, emitter, module)
               if str(n.node_type).endswith('NodeInput') or str(n.node_type) == 'Input')
    iid = str(inp.node_id)
    ipin = next(str(p.pin_name) for p in SP.get_node_pins(path, emitter, module, iid)
                if str(p.direction).lower() == 'output')
    rpin = next(str(p.pin_name) for p in SP.get_node_pins(path, emitter, module, rid)
                if str(p.direction).lower() == 'input')
    need(SP.connect_pins(path, emitter, module, iid, ipin, rid, rpin), 'Envelope map wire')
    for pin, kind, variable in [('Width', 'float', 'Particles.RibbonWidth'), ('Color', 'LinearColor', 'Particles.Color')]:
        need(SP.add_pin(path, emitter, module, rid, 'Output', kind, variable).success, 'Read ' + variable)
        need(SP.add_pin(path, emitter, module, hid, 'Input', kind, pin).success, 'Input ' + pin)
        need(SP.connect_pins(path, emitter, module, rid, variable, hid, pin), 'Wire ' + pin)
    need(SP.add_pin(path, emitter, module, hid, 'Output', 'float', 'OutWidth').success, 'Width output')
    target = SP.add_module_output(path, emitter, module, 'Particles.RibbonWidth', 'float')
    need(target.success, 'Width map output')
    need(SP.connect_pins(path, emitter, module, hid, 'OutWidth', str(target.node_id), 'Particles.RibbonWidth'), 'Width map wire')
    need(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(AS.load_asset(path)), 'Finalize envelope pins')
    need(SP.apply_changes(path), 'Compile envelope')


def set_widths(path, widths):
    for emitter, (_, published) in widths.items():
        need(NS.set_parameter(path, f'Constants.{emitter}.BeamWidth.Beam Width', str(published)), 'Absolute width ' + emitter)


def build_lasers():
    for label, source, opaque, widths in CONTRACTS:
        source_settings = NS.get_all_editable_settings(source)
        for emitter, (baseline, _) in widths.items():
            values = [float(p.current_value) for p in source_settings.rapid_iteration_parameters
                      if p.emitter_name == emitter and p.setting_path == f'Constants.{emitter}.BeamWidth.Beam Width']
            need(values and all(abs(v - baseline) < .0001 for v in values), 'Baseline width changed: ' + label)
        wide = opaque + '_WideAdditive'
        duplicate(source, wide); set_widths(wide, widths); compile_save(wide)
        duplicate(source, opaque); set_widths(opaque, widths)
        for emitter in widths:
            for renderer in ES.list_renderers(opaque, emitter):
                if str(renderer.renderer_type) == 'Ribbon':
                    need(ES.set_renderer_property(opaque, emitter, renderer.renderer_index, 'Material', MATERIAL), 'Opaque ribbon')
            envelope(opaque, emitter)
        compile_save(opaque)
        for count in [16, 8]:
            candidate = opaque + f'_Nodes{count}'
            duplicate(opaque, candidate)
            for emitter in widths:
                need(NS.set_rapid_iteration_param_by_stage(candidate, emitter, 'EmitterUpdate',
                    f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count', str(count)), 'Owning-stage node count')
                need(NS.set_parameter(candidate,
                    f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count', str(count)), 'Copied system node count')
            compile_save(candidate)
            settings = NS.get_all_editable_settings(candidate)
            for emitter in widths:
                values = [int(float(p.current_value)) for p in settings.rapid_iteration_parameters
                          if p.setting_path == f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count']
                need(values and all(value == count for value in values), 'Compiled node count ' + str(values))
        collision = opaque + '_NoPresentationCollision'
        duplicate(opaque, collision)
        # The source Collision module belongs to endpoint Spark, not Beam.
        # Keep it in Opaque; this copy explicitly evaluates an endpoint change.
        for emitter in ['Spark', 'Spark001']:
            if any(str(m.module_name) == 'Collision' for m in ES.list_modules(collision, emitter)):
                need(ES.enable_module(collision, emitter, 'Collision', False), 'Presentation collision candidate')
        compile_save(collision)
        solver = opaque + '_NoSolver'
        duplicate(opaque, solver)
        for emitter in widths:
            need(ES.enable_module(solver, emitter, 'SolveForcesAndVelocity', False), 'Beam solver candidate')
        compile_save(solver)
        gpu = opaque + '_GPU'
        duplicate(opaque, gpu)
        error = unreal.GuLiCombatEffectAuthoringLibrary.configure_tool_laser_gpu_candidate(AS.load_asset(gpu))
        need(error is not None, 'Beam-only GPU candidate: ' + str(error))
        compile_save(gpu)
        report[label] = {'source': source, 'wide': wide, 'opaque': opaque, 'widths': widths,
                         'enabled_emitters_preserved': True, 'spark_renderers_preserved': True}


def build_flashes():
    import sys
    sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
    from certify_machinegun_bounds import certify
    source = '/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Flash_1'
    for role in ['Muzzle', 'Impact']:
        candidate = '/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun' + role + '_Optimized'
        duplicate(source, candidate)
        # Both roles currently use all vendor layers. Event producers must remain
        # until the corresponding follower is removed by a measured visual candidate.
        compile_save(candidate)
        certify(candidate)
        if role == 'Muzzle':
            collision = candidate + '_NoCollision'
            duplicate(candidate, collision)
            for emitter in ['Sparks', 'Debris']:
                need(ES.enable_module(collision, emitter, 'Collision', False), 'Muzzle collision candidate')
            compile_save(collision)
            certify(collision)
        report[role] = {'source': source, 'candidate': candidate,
                        'module_pruning': 'All visible and event-linked layers retained; independent no-collision review copy is evaluated separately.'}


try:
    need(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'End our capture PIE first')
    make_material()
    build_lasers()
    build_flashes()
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'candidate-build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'error': report.get('error'), 'saved': report['saved']}))

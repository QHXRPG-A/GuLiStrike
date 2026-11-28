"""Build a typed Niagara Scratch Pad module with one Custom HLSL node."""
import unreal

SP = unreal.NiagaraScratchPadService
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def scratch(system, emitter, stage, name, inputs, outputs, code):
    result = SP.create_scratch_module(system, emitter, stage, name)
    require(result.success, 'Create scratch ' + name)
    module = str(result.module_name)
    hlsl = SP.add_custom_hlsl_node(system, emitter, module, code)
    require(hlsl.success, 'HLSL ' + name)
    node = str(hlsl.node_id)
    read = SP.add_node(system, emitter, module, 'MapGet')
    require(read.success, 'MapGet ' + name)
    read_id = str(read.node_id)
    input_node = next(n for n in SP.list_nodes(system, emitter, module)
                      if str(n.node_type) in ('Input', 'NiagaraNodeInput') or str(n.node_type).endswith('NodeInput'))
    input_id = str(input_node.node_id)
    input_pin = next(str(p.pin_name) for p in SP.get_node_pins(system, emitter, module, input_id)
                     if str(p.direction).lower() == 'output')
    read_pin = next(str(p.pin_name) for p in SP.get_node_pins(system, emitter, module, read_id)
                    if str(p.direction).lower() == 'input')
    require(SP.connect_pins(system, emitter, module, input_id, input_pin, read_id, read_pin), 'Map read')
    for pin, kind, variable in inputs:
        require(SP.add_pin(system, emitter, module, read_id, 'Output', kind, variable).success, 'Read ' + variable)
        require(SP.add_pin(system, emitter, module, node, 'Input', kind, pin).success, 'Input ' + pin)
        require(SP.connect_pins(system, emitter, module, read_id, variable, node, pin), 'Wire ' + pin)
    for pin, kind, variable in outputs:
        require(SP.add_pin(system, emitter, module, node, 'Output', kind, pin).success, 'Output ' + pin)
        target = SP.add_module_output(system, emitter, module, variable, kind)
        require(target.success, 'Map output ' + variable)
        require(SP.connect_pins(system, emitter, module, node, pin, str(target.node_id), variable), 'Wire ' + variable)
    require(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(ASSETS.load_asset(system)), 'Finalize pins')
    require(SP.apply_changes(system), 'Apply scratch')

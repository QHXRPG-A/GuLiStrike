"""Remove only the two beam endpoint clamps in the existing miner Blueprint.

Run through commander_editor_python after PIE ends. Does not start gameplay,
change the Niagara System, rebuild the vehicle, or modify its StateTree.
"""
import json
from pathlib import Path
import unreal

BP = '/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/engineering-navigation/mining-fix-20260922/beam-asset-update.json'


def incoming(service, graph, node, pin):
    return [edge for edge in service.get_connections(BP, graph)
            if edge.target_node_id == node and edge.target_pin_name == pin]


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'End PIE before changing its presentation Blueprint.'
    service = unreal.BlueprintService
    assert unreal.EditorAssetLibrary.does_asset_exist(BP), BP
    plans = []
    # Inspect both graphs completely before changing either one.
    for graph in ('UpdateCollectorL', 'UpdateCollectorR'):
        nodes = service.get_nodes_in_graph(BP, graph)
        assert nodes, f'Graph unavailable: {graph}'
        pin_map = {node.node_id: {pin.pin_name: pin for pin in service.get_node_pins(BP, graph, node.node_id)}
                   for node in nodes}
        aims = [node for node in nodes if {'Start', 'Target', 'ReturnValue'}.issubset(pin_map[node.node_id])]
        endpoints = [node for node in nodes
                     if {'InVariableName', 'InValue'}.issubset(pin_map[node.node_id])
                     and pin_map[node.node_id]['InVariableName'].default_value.strip('"') == 'User.Beam End']
        assert len(aims) == len(endpoints) == 1, f'Ambiguous target or endpoint in {graph}'
        target_edges = incoming(service, graph, aims[0].node_id, 'Target')
        endpoint_edges = incoming(service, graph, endpoints[0].node_id, 'InValue')
        assert len(target_edges) == len(endpoint_edges) == 1, f'Missing endpoint wiring in {graph}'
        edge = target_edges[0]
        plans.append({'graph': graph, 'source': edge.source_node_id, 'pin': edge.source_pin_name,
                      'endpoint': endpoints[0].node_id, 'previous_source': endpoint_edges[0].source_node_id})

    for plan in plans:
        current = incoming(service, plan['graph'], plan['endpoint'], 'InValue')[0]
        if current.source_node_id == plan['source'] and current.source_pin_name == plan['pin']:
            continue
        assert service.disconnect_pin(BP, plan['graph'], plan['endpoint'], 'InValue')
        assert service.connect_nodes(BP, plan['graph'], plan['source'], plan['pin'], plan['endpoint'], 'InValue')

    compiled = service.compile_blueprint(BP)
    assert compiled.success and compiled.num_errors == 0, list(compiled.errors)
    asset = unreal.load_asset(BP)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False), 'Blueprint save failed'
    for plan in plans:
        edges = incoming(service, plan['graph'], plan['endpoint'], 'InValue')
        assert len(edges) == 1 and edges[0].source_node_id == plan['source'] and edges[0].source_pin_name == plan['pin']
    return {'success': True, 'blueprint': BP, 'saved': True, 'endpoint_is_full_target': True,
            'plans': plans, 'compile_errors': list(compiled.errors), 'compile_warnings': list(compiled.warnings),
            'gameplay_started': False}


report = run()
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))

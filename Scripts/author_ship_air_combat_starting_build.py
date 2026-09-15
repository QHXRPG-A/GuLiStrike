"""Author the air-combat map's starting hangar through the committed Ship build API.

Run with source UE5.7 -ExecutePythonScript=.../author_ship_air_combat_starting_build.py.
Only the dedicated GameMode Blueprint and the target map are saved.
"""
import json
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_ShipWingmanAirCombatPrototype'
BLUEPRINT = '/Game/GuLiStrike/Ship/BP_GM_ShipWingmanAirCombatPrototype'
GRAPH = 'EventGraph'


def author():
    service = unreal.BlueprintService
    if not unreal.EditorAssetLibrary.does_asset_exist(BLUEPRINT):
        directory, name = BLUEPRINT.rsplit('/', 1)
        assert service.create_blueprint(name, '/Script/GuLiStrike.GuLiShipTestGameMode', directory)
    assert service.get_parent_class(BLUEPRINT) == 'GuLiShipTestGameMode'

    # This Blueprint is wholly owned by this map preset authoring script.
    for node in service.get_nodes_in_graph(BLUEPRINT, GRAPH):
        assert service.delete_node(BLUEPRINT, GRAPH, node.node_id)

    nodes = {}

    def add(key, operation, *args):
        nodes[key] = operation(BLUEPRINT, GRAPH, *args)
        assert nodes[key], key
        return nodes[key]

    def function(key, owner, name, x, y):
        return add(key, service.create_node_by_key, 'FUNC ' + owner + '::' + name, x, y)

    def pins(key):
        return {pin.pin_name for pin in service.get_node_pins(BLUEPRINT, GRAPH, nodes[key])}

    def link(source, source_pin, target, target_pin):
        assert source_pin in pins(source), (source, source_pin, pins(source))
        assert target_pin in pins(target), (target, target_pin, pins(target))
        assert service.connect_nodes(BLUEPRINT, GRAPH, nodes[source], source_pin, nodes[target], target_pin)

    def value(key, pin, text):
        assert pin in pins(key), (key, pin, pins(key))
        assert service.set_node_pin_value(BLUEPRINT, GRAPH, nodes[key], pin, text)

    add('apply', service.add_custom_event_node, 'ApplyStartingBuild', 0, 0)
    assert service.add_custom_event_input(BLUEPRINT, GRAPH, nodes['apply'], 'NewPlayer', 'AController')
    assert service.compile_blueprint(BLUEPRINT).success

    add('restart', service.add_event_node, 'K2_OnRestartPlayer', 0, -700)
    function('game_state', 'GameplayStatics', 'GetGameState', 0, -500)
    function('begun_play', 'GameStateBase', 'HasBegunPlay', 300, -500)
    add('world_started', service.add_branch_node, 600, -700)
    add('apply_restart', service.add_function_call_node, 'self', 'ApplyStartingBuild', 920, -700)
    add('match_state', service.add_event_node, 'K2_OnSetMatchState', 0, -1300)
    function('in_progress', 'KismetMathLibrary', 'EqualEqual_NameName', 300, -1100)
    add('match_started', service.add_branch_node, 600, -1300)
    function('controllers', 'GameplayStatics', 'GetAllActorsOfClass', 920, -1300)
    value('controllers', 'ActorClass', '/Script/Engine.PlayerController')
    add('each_player', service.add_macro_instance_node, 'ForEachLoop', 1250, -1300)
    add('apply_initial', service.add_function_call_node, 'self', 'ApplyStartingBuild', 1530, -1300)
    function('pawn', 'Controller', 'K2_GetPawn', 0, 220)
    add('ship', service.add_cast_node, 'GuLiStrikeShip', 300, 0)
    add('player_state', service.add_member_get_node, 'Controller', 'PlayerState', 300, 250)
    add('battle_state', service.add_cast_node, 'GuLiBattlePlayerState', 600, 0)
    function('build', 'GuLiBattlePlayerState', 'GetShipBuild', 600, 280)
    function('state', 'GuLiShipBuildComponent', 'GetBuildState', 920, 280)
    assert service.split_pin(BLUEPRINT, GRAPH, nodes['state'], 'ReturnValue')
    function('contains', 'KismetArrayLibrary', 'Array_Contains', 1250, 230)
    add('already_chosen', service.add_branch_node, 1530, 0)
    function('request', 'KismetGuidLibrary', 'NewGuid', 1530, 450)
    function('commit', 'GuLiShipBuildComponent', 'CommitConfirmedChoice', 1850, 0)
    assert service.split_pin(BLUEPRINT, GRAPH, nodes['commit'], 'ReturnValue')
    add('committed', service.add_branch_node, 2250, 0)
    function('failure', 'KismetSystemLibrary', 'PrintString', 2550, 0)

    # Initial RestartPlayer precedes BeginPlay. Match-state notification runs after
    # every initial Pawn has begun play; later spawns already have a started World.
    link('restart', 'then', 'world_started', 'execute')
    link('world_started', 'then', 'apply_restart', 'execute')
    link('game_state', 'ReturnValue', 'begun_play', 'self')
    link('begun_play', 'ReturnValue', 'world_started', 'Condition')
    link('restart', 'NewPlayer', 'apply_restart', 'NewPlayer')
    link('match_state', 'then', 'match_started', 'execute')
    link('match_state', 'NewState', 'in_progress', 'A')
    value('in_progress', 'B', 'InProgress')
    link('in_progress', 'ReturnValue', 'match_started', 'Condition')
    link('match_started', 'then', 'controllers', 'execute')
    link('controllers', 'then', 'each_player', 'Exec')
    link('controllers', 'OutActors', 'each_player', 'Array')
    link('each_player', 'LoopBody', 'apply_initial', 'execute')
    link('each_player', 'Array Element', 'apply_initial', 'NewPlayer')

    # Only Ship players enter this preset. Non-Ship roles need no starting Ship build.
    link('apply', 'then', 'ship', 'execute')
    link('ship', 'then', 'battle_state', 'execute')
    link('battle_state', 'then', 'already_chosen', 'execute')
    link('already_chosen', 'else', 'commit', 'execute')
    link('commit', 'then', 'committed', 'execute')
    link('committed', 'else', 'failure', 'execute')
    link('apply', 'NewPlayer', 'pawn', 'self')
    link('pawn', 'ReturnValue', 'ship', 'Object')
    link('apply', 'NewPlayer', 'player_state', 'self')
    link('player_state', 'PlayerState', 'battle_state', 'Object')
    link('battle_state', 'AsGu Li Battle Player State', 'build', 'self')
    link('build', 'ReturnValue', 'state', 'self')
    link('state', 'ReturnValue_ChosenNodeIds', 'contains', 'TargetArray')
    link('contains', 'ReturnValue', 'already_chosen', 'Condition')
    link('request', 'ReturnValue', 'commit', 'RequestId')
    link('build', 'ReturnValue', 'commit', 'self')
    link('state', 'ReturnValue_MatchEpoch', 'commit', 'MatchEpoch')
    link('state', 'ReturnValue_BuildRevision', 'commit', 'ExpectedBuildRevision')
    link('commit', 'ReturnValue_bCommitted', 'committed', 'Condition')
    link('commit', 'ReturnValue_Reason', 'failure', 'InString')
    value('contains', 'ItemToFind', '08')
    value('commit', 'NodeId', '08')
    value('failure', 'bPrintToScreen', 'false')
    value('failure', 'bPrintToLog', 'true')

    result = service.compile_blueprint(BLUEPRINT)
    assert result.success and result.num_errors == 0, list(result.errors)
    blueprint = unreal.load_asset(BLUEPRINT)
    assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)

    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    assert world and str(world.get_path_name()) == MAP + '.LVL_ShipWingmanAirCombatPrototype'
    settings = world.get_world_settings()
    game_mode = unreal.load_class(None, BLUEPRINT + '.BP_GM_ShipWingmanAirCombatPrototype_C')
    if settings.get_editor_property('default_game_mode') != game_mode:
        settings.modify()
        settings.set_editor_property('default_game_mode', game_mode)
        unreal.EditorLoadingAndSavingUtils.fully_load_assets([world])
        assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()

    report = {
        'success': True, 'map': MAP, 'game_mode': game_mode.get_path_name(),
        'starting_choices': ['08'], 'events': ['K2_OnSetMatchState', 'K2_OnRestartPlayer'],
        'blueprint_errors': result.num_errors, 'blueprint_warnings': result.num_warnings,
        'nodes': [{ 'id': key, 'pins': sorted(pins(key)) } for key in nodes],
    }
    output = Path(unreal.Paths.project_dir()) / 'TestResults/ShipAirCombatLevel/starting-build-authoring.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    author()

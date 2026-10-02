"""Inspect current PIE navigation without changing unit orders or map settings.

Runs ordinary UE path queries and temporarily uses two hidden, non-colliding
NavigationTestingActors to expose the native search-exhaustion flag. Both
diagnostic actors are destroyed within the same editor Python request.
"""

import json
from pathlib import Path

from commander_editor_python import call_editor


OUTPUT = Path("outputs/commander-uphill-routing-20261001")
SELECTED_IDS = json.loads(
    (OUTPUT / "connectivity-selection-before.json").read_text(encoding="utf-8")
)["selected_ids"]

EDITOR_CODE = r'''
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world is not None
sub = unreal.find_object(None, world.get_path_name() + ':GuLiBattleAuthoritySubsystem_0')
state = json.loads(sub.get_move_response_diagnostics())
selected_ids = SELECTED_IDS_LITERAL
units = [u for u in state['soldiers'] if u['id'] in selected_ids]
assert len(units) == len(selected_ids)
nav = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)
           if a.get_name() == 'RecastNavMesh-CommanderSoldier')
source_world = unreal.find_object(None, '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
source_nav = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(source_world, unreal.RecastNavMesh)
                  if a.get_name() == 'RecastNavMesh-CommanderSoldier')
spawn_library = unreal.get_default_object(unreal.GameplayStatics)
notify_never = unreal.PropertyAccessChangeNotifyMode.NEVER
notify_always = unreal.PropertyAccessChangeNotifyMode.ALWAYS
diag_actors = []
report = {'success': False, 'world': world.get_path_name(), 'selected_ids': selected_ids,
          'frame': state['frame'], 'nav_generation_before': state['nav_generation'],
          'agent_radius_cm': 150.0, 'agent_height_cm': 28.8,
          'runtime_queries': [], 'source_queries': [], 'controls': []}

def ordinary_query(query_world, query_nav, start, goal):
    path = unreal.NavigationSystemV1.find_path_to_location_synchronously(
        query_world, unreal.Vector(*start), unreal.Vector(*goal), query_nav, None)
    points = [list(p.to_tuple()) for p in path.get_editor_property('path_points')]
    projected_start = unreal.NavigationSystemV1.project_point_to_navigation(
        query_world, unreal.Vector(*start), query_nav, None, unreal.Vector(10, 10, 50))
    projected_goal = unreal.NavigationSystemV1.project_point_to_navigation(
        query_world, unreal.Vector(*goal), query_nav, None, unreal.Vector(10, 10, 50))
    return {'valid': path.is_valid(), 'partial': path.is_partial(), 'points': points,
            'projected_start': list(projected_start.to_tuple()) if projected_start else None,
            'projected_goal': list(projected_goal.to_tuple()) if projected_goal else None}

try:
    for coordinate in [units[0]['position'], units[0]['movement_target']]:
        transform = unreal.Transform(location=unreal.Vector(*coordinate))
        actor = spawn_library.call_method('BeginDeferredActorSpawnFromClass', (
            world, unreal.NavigationTestingActor.static_class(), transform,
            unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None,
            unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        assert actor is not None
        diag_actors.append(actor)
        actor.set_actor_hidden_in_game(True)
        actor.set_actor_enable_collision(False)
        actor.set_editor_property('gather_detailed_info', False, notify_never)
        agent = actor.get_editor_property('nav_agent_props')
        agent.agent_radius = 150.0
        agent.agent_height = 28.8
        actor.set_editor_property('nav_agent_props', agent, notify_never)
        actor.set_editor_property('querying_extent', unreal.Vector(10, 10, 50), notify_never)
        spawn_library.call_method('FinishSpawningActor', (
            actor, transform, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    start_actor, goal_actor = diag_actors
    start_actor.set_editor_property('other_actor', goal_actor, notify_never)
    start_actor.set_editor_property('search_start', True, notify_never)

    def native_query(start, goal):
        start_actor.set_actor_location(unreal.Vector(*start), False, True)
        goal_actor.set_actor_location(unreal.Vector(*goal), False, True)
        start_actor.set_editor_property('use_hierarchical_pathfinding', False, notify_always)
        return {'path_exists': start_actor.path_exist,
                'partial': start_actor.path_is_partial,
                'search_out_of_nodes': start_actor.path_search_out_of_nodes,
                'path_cost': start_actor.path_cost,
                'query_microseconds': start_actor.pathfinding_time}

    for unit in units:
        start, goal = unit['position'], unit['movement_target']
        report['runtime_queries'].append({
            'id': unit['id'], 'start': start, 'goal': goal,
            'native': native_query(start, goal),
            'ordinary': ordinary_query(world, nav, start, goal)})
        report['source_queries'].append({
            'id': unit['id'], 'start': start, 'goal': goal,
            'ordinary': ordinary_query(source_world, source_nav, start, goal)})

    u135 = next(u for u in units if u['id'] == 135)
    u138 = next(u for u in units if u['id'] == 138)
    for name, start, goal in [
        ('plateau_to_foot_135', u135['movement_target'], u135['position']),
        ('foot_to_nearby_foot', u135['position'], u138['position']),
        ('plateau_to_nearby_plateau', u135['movement_target'], u138['movement_target'])]:
        report['controls'].append({'name': name, 'start': start, 'goal': goal,
                                   'native': native_query(start, goal),
                                   'ordinary': ordinary_query(world, nav, start, goal)})
    unreal.SystemLibrary.execute_console_command(world, 'getall NavigationTestingActor MyNavData')
    unreal.SystemLibrary.execute_console_command(world, 'getall RecastNavMesh DefaultMaxSearchNodes')
    unreal.SystemLibrary.execute_console_command(world, 'getall RecastNavMesh DefaultMaxHierarchicalSearchNodes')
    report['success'] = True
finally:
    actor_paths = [a.get_path_name() for a in diag_actors]
    for actor in diag_actors:
        actor.destroy_actor()
    report['temporary_actor_paths'] = actor_paths
    report['temporary_actors_removed'] = not any(
        a.get_path_name() in actor_paths
        for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.NavigationTestingActor))
    state_after = json.loads(sub.get_move_response_diagnostics())
    report['nav_generation_after'] = state_after['nav_generation']
    report['frame_after'] = state_after['frame']
    unreal.SystemLibrary.execute_console_command(world,
        'gs.Commander.QA.InputSnapshot D:/UE5.7/test1/outputs/commander-uphill-routing-20261001/connectivity-selection-after.json')

unreal.MCPythonHelper.submit_result(json.dumps(report))
'''.replace("SELECTED_IDS_LITERAL", repr(SELECTED_IDS))


def main():
    response = call_editor(EDITOR_CODE)
    (OUTPUT / "connectivity-native-audit.json").write_text(
        json.dumps(response, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    assert response.get("success") and response.get("result", {}).get("success"), response
    result = response["result"]
    summary = {
        "success": True,
        "nav_generation": [result["nav_generation_before"], result["nav_generation_after"]],
        "frame": [result["frame"], result["frame_after"]],
        "temporary_actors_removed": result["temporary_actors_removed"],
        "runtime": [dict(id=q["id"], **q["native"]) for q in result["runtime_queries"]],
        "source": [dict(id=q["id"], valid=q["ordinary"]["valid"], partial=q["ordinary"]["partial"])
                   for q in result["source_queries"]],
        "controls": [dict(name=q["name"], **q["native"]) for q in result["controls"]],
    }
    print(json.dumps(summary, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()

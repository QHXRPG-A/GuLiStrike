"""Read the new authority engineering state; does not start play or change assets."""
import json
import re
import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
assert world and 'LVL_CommanderMassPrototype' in world.get_path_name(), 'Open the designated gameplay world first.'
assert unreal.GameplayStatics.get_game_mode(world), 'Sample the authority world, not a client.'
assert hasattr(unreal.GuLiEngineeringPathSubsystem, 'get_budget_debug'), 'The new native build is required.'
def subsystem(cls):
    return next((obj for obj in unreal.ObjectIterator(cls) if obj.get_outer() == world), None)


service = subsystem(unreal.GuLiEngineeringPathSubsystem)
mining = subsystem(unreal.GuLiMiningVehicleManager)
resources = subsystem(unreal.GuLiResourceWorldSubsystem)
assert service and mining and resources.is_runtime_ready(), 'Wait for the new authority world and navigation.'


def counters(value):
    return {key: float(number) for key, number in re.findall(r'(\w+)=([\d.]+)', value)}


def amounts(value):
    return {key: value.get_editor_property(key) for key in ('blue', 'red')}


def vehicle(actor):
    travel = actor.get_component_by_class(unreal.GuLiEngineeringTravelComponent)
    data = {'name': actor.get_name(), 'class': actor.get_class().get_name(), 'team': str(actor.get_team()),
            'location': list(actor.get_actor_location().to_tuple()),
            'speed': actor.get_velocity().length(), 'travel': travel.get_travel_debug()}
    data['travel_counters'] = counters(data['travel'])
    if isinstance(actor, unreal.GuLiMiningVehiclePawn):
        data.update(team=str(actor.get_team()), state=str(actor.get_task_state()),
                    cluster=actor.get_target_cluster_id(), node=actor.get_mining_node_id(),
                    cargo=amounts(actor.get_cargo()))
    return data


buildings = []
world_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
for actor in world_actors:
    component = actor.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
    if not component:
        continue
    state = component.get_state()
    buildings.append({'name': actor.get_name(), 'phase': str(state.phase),
                      'work_done': state.work_done, 'progress': component.get_construction_progress(),
                      'slots_ready': component.are_construction_slots_ready(),
                      'slots': len(component.get_construction_slot_poses()),
                      'occupied': component.get_reserved_construction_slots()})

result = {
    'success': True, 'world': world.get_path_name(),
    'actor_count': len(world_actors),
    'world_seconds': unreal.GameplayStatics.get_time_seconds(world),
    'navigation_building': unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world),
    'path': counters(service.get_budget_debug()), 'slots': counters(mining.get_slot_debug()),
    'ore_remaining_raw': sum(resources.get_cluster_remaining_raw(i + 1)
                             for i in range(len(resources.get_map_definition().get_editor_property('clusters')))),
    'queue_wait_ms': list(service.get_recent_queue_wait_milliseconds()),
    'vehicles': [vehicle(actor) for cls in (unreal.GuLiMiningVehiclePawn, unreal.GuLiConstructionVehiclePawn)
                 for actor in unreal.GameplayStatics.get_all_actors_of_class(world, cls)],
    'buildings': buildings,
    'factory_queues': {actor.get_name(): actor.get_queued_raw_amount()
                       for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiResourceFactoryActor)},
    'inventory': {str(team): amounts(resources.get_team_inventory(team)) for team in (unreal.GuLiTeam.RED, unreal.GuLiTeam.BLUE)},
}
unreal.MCPythonHelper.submit_result(json.dumps(result))

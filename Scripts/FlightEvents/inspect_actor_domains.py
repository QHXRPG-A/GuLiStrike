import json
import unreal

rows = []
for world in unreal.EditorLevelLibrary.get_pie_worlds(True):
    state = unreal.GameplayStatics.get_game_state(world)
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiFlightVisualActor)
    kinds = {}
    examples = {}
    expired = 0
    presentation = next((s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem)
                         if s.get_outer().get_path_name() == world.get_path_name()), None)
    for prediction in presentation.get_effect_states() if presentation else []:
        kind = str(prediction.source.kind)
        kinds[kind] = kinds.get(kind, 0) + 1
        expired += int(prediction.end_time < state.get_server_world_time_seconds())
        if kind not in examples:
            examples[kind] = {'start': prediction.start_time, 'end': prediction.end_time,
                              'position': str(prediction.location)}
    physical = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiStrikeProjectile)
    rows.append({'world': world.get_name(), 'server_time': state.get_server_world_time_seconds(),
                 'pool_kinds': kinds, 'past_end': expired, 'examples': examples,
                 'server_physical_count': len(physical),
                 'physical_example': {'remaining': physical[0].get_life_span(),
                                      'initial_lifespan': physical[0].get_editor_property('initial_life_span')}
                 if physical else None})
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'rows': rows}))

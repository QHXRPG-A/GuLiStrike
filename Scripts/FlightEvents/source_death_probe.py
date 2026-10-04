import json
import time
import unreal

server = unreal.EditorLevelLibrary.get_pie_worlds(True)[0]
producer = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.GuLiStrikeShip)[0]
fixture = flight_subsystem(server, unreal.GuLiFlightAcceptanceSubsystem)
probe = {'started': fixture.start_load(500, 20), 'time': time.monotonic(), 'stage': 0}


def source_probe_tick(delta):
    if time.monotonic() - probe['time'] < 2:
        return
    client = unreal.EditorLevelLibrary.get_pie_worlds(True)[1]
    visuals = flight_subsystem(client, unreal.GuLiCombatEffectPresentationSubsystem)
    if probe['stage'] == 0:
        probe['before'] = flight_sample()
        probe['ids'] = {unreal.GuidLibrary.conv_guid_to_string(s.effect_id) for s in visuals.get_effect_states()}
        fixture.stop_load()
        probe['destroyed'] = producer.destroy_actor()
        probe['stage'] = 1
        probe['time'] = time.monotonic()
        return
    states = visuals.get_effect_states()
    remaining = {unreal.GuidLibrary.conv_guid_to_string(s.effect_id) for s in states}
    report = {k: v for k, v in probe.items() if k != 'ids'}
    report['original_visual_entries_remaining'] = len(remaining & probe['ids'])
    report['identity_scope'] = 'all effect visuals, including non-flight fields; flight-only counts are in pool_active'
    report['after'] = flight_sample()
    flight_reports.joinpath('source_death_result.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(source_probe_handle)


source_probe_handle = unreal.register_slate_post_tick_callback(source_probe_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success': probe['started'], 'scheduled': True}))

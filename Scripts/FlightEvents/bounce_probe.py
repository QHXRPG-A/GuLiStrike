import json
import time
import unreal

probe_start = time.monotonic()
bounce_samples = []


def bounce_probe_tick(delta):
    worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
    if not worlds:
        unreal.unregister_slate_post_tick_callback(bounce_probe_handle)
        return
    server = worlds[0]
    physical = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.GuLiStrikeProjectile)
    server_left = sum(p.get_velocity().x < -10 for p in physical)
    clients = []
    for world in worlds[1:]:
        local = []
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiFlightVisualActor):
            mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
            if mesh and mesh.get_editor_property('static_mesh'):
                local.append(actor)
        clients.append({'mesh_flights': len(local),
                        'turned_back': sum(abs(a.get_actor_rotation().yaw) > 170 for a in local),
                        'physical_projectiles': len(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiStrikeProjectile))})
    bounce_samples.append({'elapsed': time.monotonic() - probe_start,
                           'server_physical': len(physical), 'server_reversed': server_left,
                           'clients': clients})
    if time.monotonic() - probe_start < 10:
        return
    unreal.unregister_slate_post_tick_callback(bounce_probe_handle)
    report = {'samples': bounce_samples, 'server_reversed_peak': max(s['server_reversed'] for s in bounce_samples),
              'client_reversed_peaks': [max(s['clients'][i]['turned_back'] for s in bounce_samples) for i in range(len(clients))]}
    flight_reports.joinpath('bounce_probe.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


bounce_probe_handle = unreal.register_slate_post_tick_callback(bounce_probe_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'scheduled': True}))

"""Bounded read-only navigation queries at the captured stalled units."""
import json
from pathlib import Path
import unreal

root = Path('D:/UE5.7/test1/Artifacts/CommanderMove/20260923')
world = unreal.find_object(None, '/Game/Maps/UEDPIE_0_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
assert world is not None
starts = json.loads((root / 'selected-starts.json').read_text(encoding='utf-8'))
target = unreal.Vector(-13760, 60814, -1778)
result = {'nav': [], 'obstacles': []}
def vec(value):
    return [value.x, value.y, value.z] if value is not None else None
for nav in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
    row = {'name': nav.get_name(), 'paths': []}
    for prop in ('agent_radius', 'agent_height', 'runtime_generation', 'cell_size', 'cell_height'):
        try:
            row[prop] = str(nav.get_editor_property(prop))
        except Exception as error:
            row[prop] = str(error)
    projected_target = unreal.NavigationSystemV1.project_point_to_navigation(world, target, nav, None, unreal.Vector(150, 150, 5000))
    row['target_projection'] = vec(projected_target)
    for entry in starts:
        start = unreal.Vector(*entry['position'])
        projected_start = unreal.NavigationSystemV1.project_point_to_navigation(world, start, nav, None, unreal.Vector(10, 10, 5000))
        path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, projected_target or target, nav)
        row['paths'].append({'id': entry['id'], 'start_projection': vec(projected_start),
                             'valid': path.is_valid() if path else False,
                             'partial': path.is_partial() if path else None,
                             'points': [vec(v) for v in path.path_points] if path else []})
    result['nav'].append(row)
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
    location = actor.get_actor_location()
    if abs(location.x + 17500) > 8000 or abs(location.y - 60000) > 8000:
        continue
    if not any(t in actor.get_class().get_name() for t in ('Building', 'Factory', 'StaticMesh')):
        continue
    origin, extent = actor.get_actor_bounds(True)
    result['obstacles'].append({'name': actor.get_name(), 'class': actor.get_class().get_name(),
                                'location': vec(location), 'bounds_origin': vec(origin), 'bounds_extent': vec(extent)})
(root / 'live-paths.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('GULI_MOVE_PATH_CAPTURE ' + str(root / 'live-paths.json'))

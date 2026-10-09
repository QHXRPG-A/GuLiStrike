"""Transient samples in each actual client; formal assets and gameplay ownership stay intact."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')
IDS = [1001, 1002, 1006, 2001, 2002, 2003, 2005, 2006, 2008, 3001, 3002, 3003, 3004, 3005, 3006]


def run():
    report = {'samples': [], 'errors': [], 'transient': True, 'static_pose_captures': True}
    for sub in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem):
        world = sub.get_world()
        if not world or 'UEDPIE' not in world.get_path_name():
            continue
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        own = pc.player_state.get_team()
        enemy = unreal.GuLiTeam.BLUE if own == unreal.GuLiTeam.RED else unreal.GuLiTeam.RED
        registry = next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world() == world)
        for previous in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
            if previous.actor_has_tag('GuLi.ModelRuntimePaintSample'):
                previous.destroy_actor()
        for index, mid in enumerate(IDS):
            for relation, team in [('own', own), ('enemy', enemy)]:
                location = unreal.Vector(-45000 + index * 1800, 48000 if relation == 'own' else 45000, 13000)
                actor = unreal.GuLiModelAuthoringLibrary.spawn_runtime_paint_sample(world, mid, team, location)
                if not actor:
                    report['errors'].append(str((world.get_name(), mid, relation, 'spawn failed')))
                    continue
                actor.tags = ['GuLi.ModelRuntimePaintSample', 'ModelId=' + str(mid), 'Relation=' + relation]
                center, extent = actor.get_actor_bounds(False, True)
                scale = 400 / max(extent.x, extent.y, extent.z, 1)
                actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
                for component in actor.get_components_by_class(unreal.StaticMeshComponent):
                    # This preview preserves the source's resting pose; actual Mass
                    # animation is separately inspected on its normal HISM batches.
                    component.set_editor_property('evaluate_world_position_offset', False)
                sub.apply_local_team_colors()
                parameters = []
                for component in actor.get_components_by_class(unreal.MeshComponent):
                    part = next((b.part_key for b in registry.get_model_parts(mid)
                                 if b.component_path == component.get_name()), 'Root')
                    p = registry.get_vector_parameter(component, mid, 'Root', '*', 'TeamPrimary')
                    s = registry.get_vector_parameter(component, mid, 'Root', '*', 'TeamSecondary')
                    parameters.append({'component': component.get_name(), 'primary': list(p.to_tuple()) if p else None,
                                       'secondary': list(s.to_tuple()) if s else None,
                                       'part': part, 'lamp': registry.get_scalar_parameter(component, mid, 'Root', '*', 'TeamLightStrength')})
                report['samples'].append({'world': world.get_path_name(), 'view_team': str(own), 'model_id': mid,
                                          'relation': relation, 'actual_team': str(team), 'actor': actor.get_path_name(),
                                          'scale': scale, 'parameters': parameters})
    for sample in report['samples']:
        for p in sample['parameters']:
            if p['primary'] is None or p['secondary'] is None or p['lamp'] is None:
                report['errors'].append(str((sample['model_id'], sample['relation'], p['component'], 'missing actual CPD binding')))
    report['success'] = len(report['samples']) == 60 and not report['errors']
    (OUT / 'runtime-paint-samples.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return {'success': report['success'], 'samples': len(report['samples']), 'errors': report['errors']}


unreal.MCPythonHelper.submit_result(json.dumps(run()))

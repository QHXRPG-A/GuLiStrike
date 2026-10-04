"""Explicit, bounded live PIE acceptance operations; never starts PIE itself."""
import json
import math
import time
import traceback
from pathlib import Path

import unreal

QA_REPORTS = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/Reports')
qa_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
qa_pc = unreal.GameplayStatics.get_player_controller(qa_world, 0)
qa_library = unreal.GuLiPioneerQALibrary
qa_run = {'handle': None, 'done': True}


def qa_snapshot(label=None):
    result = json.loads(qa_library.snapshot(qa_pc))
    if not result.get('success'):
        raise RuntimeError(result)
    result['paused'] = unreal.GameplayStatics.is_game_paused(qa_world)
    result['presentation'] = []
    result['skeletal_components'] = 0
    for actor in unreal.GameplayStatics.get_all_actors_of_class(qa_world, unreal.GuLiCommanderPresentationActor):
        result['skeletal_components'] += len(actor.get_components_by_class(unreal.SkeletalMeshComponent))
        for component in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            mesh = component.get_editor_property('static_mesh')
            slots = component.get_editor_property('num_custom_data_floats')
            custom = list(component.get_editor_property('per_instance_sm_custom_data'))
            count = component.get_instance_count()
            instances = []
            for index in range(count):
                transform = component.get_instance_transform(index, world_space=True)
                position = transform.translation
                instances.append({'position': [position.x, position.y, position.z],
                                  'scale': [transform.scale3d.x, transform.scale3d.y, transform.scale3d.z],
                                  'vat': custom[index * slots + 51:index * slots + 59]})
            result['presentation'].append({'name': component.get_name(), 'mesh': mesh.get_path_name() if mesh else None,
                                           'instances': count, 'custom_slots': slots, 'instance_data': instances})
    skills = qa_pc.player_state.get_commander_skills()
    reply = skills.get_last_reply()
    result['last_reply'] = {'id': str(reply.request_id), 'code': str(reply.code), 'error': reply.error, 'units': []}
    for unit in reply.units:
        execution = unit.execution
        result['last_reply']['units'].append({'id': unit.soldier_id.get_editor_property('value'),
                                            'code': str(unit.code), 'ready_at': unit.ready_at_server_seconds,
                                            'success': execution.succeeded, 'error': execution.error})
    if label:
        (QA_REPORTS / ('qa_' + label + '.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


def qa_brief(result):
    return {key: result.get(key) for key in ('success', 'world_time', 'sim_tick', 'member_count', 'population', 'reserved',
                                           'selected', 'last_reply', 'skeletal_components')} | {
        'units': [{key: row.get(key) for key in ('id', 'type', 'alive', 'health', 'position', 'velocity', 'moving',
                                                'range', 'damage', 'rate', 'projectile_count', 'spread', 'rounds', 'target', 'ready_at')}
                  for row in result['units']],
        'shots': len(result.get('shots', [])), 'projectiles': len(result.get('projectiles', [])),
        'vat': [row for row in result['presentation'] if row['instances'] and 'Pioneer' in (row['mesh'] or '')]}


def qa_advance(seconds, label, q_release=False):
    """Advance normal game time and pause at the end, collecting real runtime samples."""
    global qa_run
    if not qa_run.get('done', True):
        raise RuntimeError('An acceptance interval is already active')
    if not qa_pc.can_issue_commander_orders():
        raise RuntimeError('Wait for Commander bootstrap readiness before starting an acceptance interval')
    start = unreal.GameplayStatics.get_time_seconds(qa_world)
    qa_run = {'done': False, 'label': label, 'start': start, 'seconds': seconds,
              'clock': time.monotonic(), 'samples': [], 'last_sample': start - 1, 'q_release': q_release}

    def tick(delta):
        try:
            now = unreal.GameplayStatics.get_time_seconds(qa_world)
            if qa_run['q_release'] and now > start + .05:
                qa_library.action(qa_pc, 'QUp')
                qa_run['q_release'] = False
            if now >= qa_run['last_sample'] + .1:
                sample = qa_snapshot()
                sample.pop('shots', None)
                sample.pop('projectiles', None)
                qa_run['samples'].append(sample)
                qa_run['last_sample'] = now
            if now >= start + seconds:
                unreal.GameplayStatics.set_game_paused(qa_world, True)
                final = qa_snapshot(label)
                qa_run['result'] = qa_brief(final)
                qa_run['done'] = True
                (QA_REPORTS / ('qa_' + label + '_samples.json')).write_text(
                    json.dumps(qa_run['samples'], ensure_ascii=False, indent=2), encoding='utf-8')
                unreal.unregister_slate_post_tick_callback(qa_run['handle'])
            elif time.monotonic() - qa_run['clock'] > 55:
                raise TimeoutError('PIE did not advance requested game time within 55 real seconds')
        except Exception:
            unreal.GameplayStatics.set_game_paused(qa_world, True)
            qa_run['done'] = True
            qa_run['error'] = traceback.format_exc()
            unreal.unregister_slate_post_tick_callback(qa_run['handle'])
            (QA_REPORTS / ('qa_' + label + '_error.json')).write_text(json.dumps(qa_run['error']), encoding='utf-8')

    qa_run['handle'] = unreal.register_slate_post_tick_callback(tick)
    unreal.GameplayStatics.set_game_paused(qa_world, False)
    return {'queued': label, 'seconds': seconds}


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'loaded': True, 'paused': unreal.GameplayStatics.is_game_paused(qa_world)}))

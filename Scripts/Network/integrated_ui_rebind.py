"""Rebind the real local view while an original native receipt is held."""
import copy
import math
import time

from integrated_acceptance import entry, identity, input_and_wait, objects
from integrated_faults import arm, fault, release


def ui(session, endpoint):
    return session.states[endpoint['name']]['scene_ui']


def reply_count(value, field, event):
    return sum(v['final_count'] for v in value.get(field, []) if identity(v['event']) == event)


def held_events(session, endpoint):
    return {identity(v['event']) for v in fault(session, endpoint).get('messages', [])}


def stable_completion(session, predicate):
    started = time.monotonic()
    def stable():
        if not predicate():
            raise RuntimeError('A completed original request changed or completed more than once')
        return time.monotonic() - started >= 3
    session.wait('single-final-completion', stable, timeout=15)


def pending_choice(session, role):
    from integrated_roles import switch_rendered
    switch_rendered(session, role)
    from integrated_commander import fund
    fund(session, {'blue': 128, 'red': 128})
    session.begin(role.lower() + '-original-request-ui-rebind')
    endpoint = entry(session, 'Red', role)
    pending_field, final_field = ('pending_skill_requests', 'skill_final_receipts') if role == 'Commander' else ('pending_choice_requests', 'choice_final_receipts')
    if role == 'Commander':
        from integrated_commander import center
        source = next(o for o in objects(session, True) if o['team'] == 1 and 'relocation_cast' in o.get('capabilities', {}))
        army = [o for o in objects(session, True) if o['team'] == 1 and o['kind'] == 1 and o['life'] == 1]
        if not army:
            from acceptance_session import PreparationSkipped
            raise PreparationSkipped('No actual living Mass target for the pending skill request')
        midpoint=center(army)
        point=min(army,key=lambda o:math.dist(o['position'],midpoint))['position']
        kind, arguments, business = 'global_skill', {'skill': 'Teleport', 'point': point}, 17
    else:
        source = next(o for o in objects(session, True) if o['team'] == 1 and o['definition'] == 'player/ship')
        choices = [v for v in ui(session, endpoint)['ship_choices'] if v['available']]
        if not choices:
            raise RuntimeError('Air pending UI check requires an actual available assembly choice')
        kind, arguments, business = 'ship_equipment', {'node': choices[0]['node']}, 16
    held = arm(session, endpoint, 'receipt_hold', count=1, business_kind=business, source=source['id'])
    try:
        operation = session.operation(kind, endpoint['player'], **arguments)
        def pending():
            action = session.action(operation)
            if not action or action['status'] != 'submitted': return False
            request = identity(action['request'])
            return request in {identity(v) for v in ui(session, endpoint).get(pending_field, [])} and fault(session, endpoint).get('intercepted', 0) > 0
        with session.preparation('Original pending '+role+' UI request'):
            session.wait('original-request-pending', pending)
        action = copy.deepcopy(session.action(operation)); request = identity(action['request'])
        original = copy.deepcopy(ui(session, endpoint)); events = held_events(session, endpoint)
        if not events or reply_count(original, final_field, request):
            raise RuntimeError('The held request already has a final UI receipt')
        _, rebind_path = input_and_wait(session, 'ui_rebind', endpoint['player'])
        session.wait('same-request-after-rebind', lambda: ui(session, endpoint)['bindings_generation'] > original['bindings_generation']
            and request in {identity(v) for v in ui(session, endpoint).get(pending_field, [])}
            and reply_count(ui(session, endpoint), final_field, request) == 0)
        release(session, endpoint, held); held = None
        def complete():
            value = ui(session, endpoint); current = session.action(operation)
            return bool(current and current['status'] == 'completed'
                and reply_count(value, final_field, request) == 1
                and request not in {identity(v) for v in value.get(pending_field, [])}
                and (current['result'].get('code') == 0 if role == 'Commander' else current['result'].get('committed')))
        session.wait('original-final-receipt', complete)
        stable_completion(session, complete)
        session.write_json(session.stage_output/'request-chain.json', {'operation': operation, 'original_request': request,
            'held_native_events': sorted(events), 'original_action': action, 'before_rebind': original,
            'final_action': session.action(operation), 'final_ui': ui(session, endpoint),
            'scope': 'Same live local controller and original request; actual subscriptions and HUD bindings refreshed while awaiting the native receipt.'})
        paths = [session.stage_output/name for name in ('original-request-pending.json', rebind_path,
            'same-request-after-rebind.json', 'original-final-receipt.json', 'single-final-completion.json', 'request-chain.json')]
    finally:
        if held: release(session, endpoint, held)
    session.finish()
    if role == 'Commander':
        session.begin('ui-rebind-teleport-cleanup')
        input_and_wait(session, 'teleport_cancel', endpoint['player'])
        session.wait('released-cast', lambda: ui(session, endpoint)['teleport']['phase'] in (0, 5)
                     and not any(o['phased'] for o in objects(session, True) if o['team'] == 1))
        session.finish()
    return paths


def ground(session):
    from integrated_roles import switch_rendered
    from integrated_commander import prepare_construction_site, fund
    switch_rendered(session, 'Ground')
    prepare_construction_site(session)
    fund(session, {'blue': 80, 'red': 40})
    session.begin('ground-original-placement-ui-rebind')
    endpoint = entry(session, 'Red', 'Ground')
    source = next(o for o in objects(session, True) if o['team'] == 1 and o['definition'] == 'player/ground')
    point = list(source['position']); point[0] += 1200
    before = copy.deepcopy(ui(session, endpoint))
    # Spawn also creates the internal placement work. Hold the final Building
    # birth receipt, which is the original UI request's completion boundary.
    held = arm(session, endpoint, 'receipt_hold', count=1, business_kind=5, target_kind=7)
    try:
        operation = session.operation('building_place', endpoint['player'], definition=6, point=point)
        with session.preparation('Original pending Ground placement receipt'):
            session.wait('original-placement-pending', lambda: bool(session.action(operation)
                and session.action(operation)['status'] == 'submitted'
                and held_events(session, endpoint).intersection(identity(v) for v in ui(session, endpoint).get('pending_placement_events', []))))
        events = held_events(session, endpoint).intersection(identity(v) for v in ui(session, endpoint)['pending_placement_events'])
        _, rebind_path = input_and_wait(session, 'ui_rebind', endpoint['player'])
        session.wait('same-placement-after-rebind', lambda: ui(session, endpoint)['bindings_generation'] > before['bindings_generation']
            and events.issubset(identity(v) for v in ui(session, endpoint)['pending_placement_events'])
            and ui(session, endpoint)['placement_receipt_count'] == before['placement_receipt_count'])
        release(session, endpoint, held); held = None
        def complete():
            value = ui(session, endpoint); action = session.action(operation)
            return bool(action and action['status'] == 'completed' and action['result'].get('accepted')
                and value['placement_receipt_count'] == before['placement_receipt_count'] + 1
                and value['building_feedback_receipts'] == before['building_feedback_receipts'] + 1
                and value['placement_receipt']['accepted'] and value['placement_receipt']['request'] == action['result']['request']
                and not events.intersection(identity(v) for v in value.get('pending_placement_events', [])))
        session.wait('original-placement-final', complete)
        stable_completion(session, complete)
        session.write_json(session.stage_output/'request-chain.json', {'operation': operation, 'original_native_events': sorted(events),
            'before': before, 'final_ui': ui(session, endpoint), 'final_action': session.action(operation)})
        session.prove('ground_ui_rebind', complete(), 'Original placement event survived real HUD/subscription rebinding, completed once and released its waiting state',
            [session.stage_output/name for name in ('original-placement-pending.json', rebind_path, 'same-placement-after-rebind.json',
             'original-placement-final.json', 'single-final-completion.json', 'request-chain.json')])
    finally:
        if held: release(session, endpoint, held)
    session.finish()


def run(session):
    from integrated_roles import switch_rendered
    from integrated_commander import fund
    # Prepare both teams before presentation restarts. These development
    # credits cannot serve as mining, processing, or production evidence.
    fund(session, {'blue': 256, 'red': 256})
    fund(session, {'blue': 128, 'red': 128}, team='Blue')
    paths = pending_choice(session, 'Commander')
    paths += pending_choice(session, 'Air')
    session.prove('commander_air_ui_rebind', True, 'Commander skill and Air equipment each retained the original request, received one actual final callback and cleared waiting after local UI rebinding', paths)
    session.begin('blue-air-equipment-for-vacancy-lifecycle')
    blue=entry(session,'Blue','Air')
    choices=[v for v in ui(session,blue)['ship_choices'] if v['available']]
    if not choices: raise RuntimeError('Blue Air has no available native assembly choice')
    action,_=input_and_wait(session,'ship_equipment',blue['player'],node=choices[0]['node'])
    if not action['result'].get('committed'): raise RuntimeError('Blue Air assembly did not commit')
    session.wait('fifty-real-members',lambda:all(sum(m['life_state']==1 and m['entity']
        for m in session.states[e['name']].get('wingman_states',[]))==50 for e in session.clients))
    session.finish()
    ground(session)
    switch_rendered(session, 'Commander')


def collect_role(session, role):
    """Collect an existing role branch without depending on broken room joins."""
    if role == 'Ground':
        ground(session)
        paths=[session.stage_output/'request-chain.json']
    else:
        paths=pending_choice(session,role)
    session.write_json(session.output/'ui-role-collection.json',{
        'role':role,'baseline':session.plan['baseline']['identity'],
        'evidence':[str(path.resolve()) for path in paths],
        'scope':'Existing original-request, final-receipt, once-only completion and waiting-clear branch; no combined Commander/Air or full F gate granted.'})

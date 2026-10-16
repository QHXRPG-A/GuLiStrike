"""Fixed identities/roles; change only which owned client renders the scene."""
from pathlib import Path
from integrated_acceptance import entry, identity


def transition_sample(session):
    import time
    fields=('player','join','sent','imported','acked','elapsed','capture_objects','capture_offset',
            'capture_splits','send_part','send_parts_total','blocked','captured_state_bytes',
            'captured_staged_bytes','received_state_bytes','directory_expected','directory_received')
    return {'monotonic':time.monotonic(),
            'rooms':[{**{k:r.get(k) for k in ('membership','membership_paused','membership_deadline_failures',
                                         'directory_records','retired_transient_records')},
                      'handoff_progress':{'count':len(r.get('handoffs',[])),
                          'targets':{t:sum(v.get('target')==t for v in r.get('handoffs',[]))
                                     for t in sorted({v.get('target','') for v in r.get('handoffs',[])})},
                          'ready':sum(bool(v.get('target_prepared')) for v in r.get('handoffs',[])),
                          'frozen':sum(bool(int(v.get('frozen_state',0))) for v in r.get('handoffs',[])),
                          'installed':sum(bool(int(v.get('installed_state',0))) for v in r.get('handoffs',[]))}}
                     for r in session.states.get('server',{}).get('rooms',[])],
            'clients':{name:{'pid':state.get('pid'),
                       'membership':{k:state.get('membership_details',{}).get(k) for k in fields},
                       'issues':state.get('handoff_issues',{}),'measurements':state.get('handoff_measurements',{}),
                       'network':{'receive_queue_overflows':state.get('network',{}).get('receive_queue_overflows'),
                           'peers':[{k:v for k,v in p.items() if k=='peer' or k.startswith('lane_3_')}
                                    for p in state.get('network',{}).get('peers',[])]}}
                       for name,state in session.states.items() if name!='server'}}


def require_membership_deadline(session):
    for room in session.states.get('server',{}).get('rooms',[]):
        if room.get('membership_deadline_failures'):
            session.write_json(session.stage_output/'member-transition-deadline.json',session.states)
            from runtime_evidence import RuntimeEvidenceUnavailable
            raise RuntimeEvidenceUnavailable('Coordinator applied the existing 15-second membership deadline: '
                +str(room['membership_deadline_failures'][-1]))
        membership=room.get('membership') or {}
        if room.get('membership_paused') and membership.get('seconds_remaining',0)<0:
            session.write_json(session.stage_output/'member-transition-deadline.json',session.states)
            from runtime_evidence import RuntimeEvidenceUnavailable
            raise RuntimeEvidenceUnavailable('Member transition exceeded its existing 15-second deadline')


def leave(session, item, abrupt=False):
    _,process=next((e,p) for e,p in session.records if e['name']==item['name'])
    if abrupt:
        session.stop_owned(item['name'])
    else:
        session.expected_stopped.add(item['name'])
        timeline=[transition_sample(session)]
        operation=session.operation('leave_match',item['player'])
        def exiting():
            timeline.append(transition_sample(session))
            if process.poll() is not None: return True
            require_membership_deadline(session)
            return False
        try:
            session.wait('exit-'+item['name'],exiting,timeout=30)
        except RuntimeError as error:
            if process.poll() is None: session.expected_stopped.discard(item['name'])
            from runtime_evidence import RuntimeEvidenceUnavailable
            raise RuntimeEvidenceUnavailable('Normal departure did not complete; dependent role/room checks cannot continue: '+str(error)) from error
        finally:
            session.write_json(session.stage_output/('exit-transition-'+operation+'.json'),
                               {'player':item['player'],'operation':operation,'timeline':timeline})
        import json
        receipt_path=Path(item['directory'])/('leave-'+operation+'.json')
        receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
        if process.returncode!=0 or receipt.get('schema')!=1 or receipt.get('run_id')!=session.plan['run_id'] \
                or receipt.get('pid')!=process.pid or receipt.get('status')!='completed' \
                or identity(receipt.get('id',''))!=operation or identity(receipt.get('player',''))!=item['player'] \
                or receipt.get('stage_id')!=session.stage_id or receipt.get('kind')!='leave_match':
            raise RuntimeError('Normal exit lacks its published formal quit receipt')
        session.write_json(session.stage_output/('exit-receipt-'+item['name']+'.json'),receipt)
    session.control()
    def departed():
        require_membership_deadline(session)
        return session.settled() and all(any(
            identity(m['player'])==item['player'] and not m['online'] for m in v.get('role_members',[]))
            for n,v in session.states.items() if n!='server')
    try:
        session.wait('member-left-'+item['name'],departed)
    except RuntimeError as error:
        from runtime_evidence import RuntimeEvidenceUnavailable
        raise RuntimeEvidenceUnavailable('Member exit did not converge; the shared room cannot support dependent stages: '+str(error)) from error
    return process.pid


def returned(session, item):
    observed_online=False
    launch_seat=item.get('launch_seat',item['seat'])
    def active():
        nonlocal observed_online
        require_membership_deadline(session)
        rooms=session.states.get('server',{}).get('rooms',[])
        member=next((m for r in rooms for m in r.get('members',[]) if identity(m['player'])==item['player']),None)
        if member and member['online']: observed_online=True
        if observed_online and member and not member['online']:
            from runtime_evidence import RuntimeEvidenceUnavailable
            raise RuntimeEvidenceUnavailable('Coordinator rejected the replacement admission; dependent room checks cannot continue')
        return session.settled() and all(any(
            identity(m['player'])==item['player'] and m['online'] and m['role']==launch_seat
            for m in v.get('role_members',[])) for n,v in session.states.items() if n!='server')
    session.wait('fixed-role-return-'+item['name'],active)
    session.sync_seats()


def switch_rendered(session, destination):
    session.sync_seats()
    previous=next(e for e in session.clients if e['rendered'])
    if previous['seat']==destination: return
    target=entry(session,previous['team'],destination)
    session.begin('rendered-'+previous['seat'].lower()+'-to-'+destination.lower())
    identities={e['name']:(e['player'],e['seat']) for e in session.clients}
    rendered_mask,logic_mask=previous['mask'],target['mask']
    old_render=leave(session,previous)
    previous['mask']=logic_mask
    session.restart_owned(previous['name'],rendered=False);returned(session,previous)
    old_logic=leave(session,target)
    target['mask']=rendered_mask
    session.restart_owned(target['name'],rendered=True);returned(session,target)
    if sum(e['rendered'] for e in session.clients)!=1 or identities!={e['name']:(e['player'],e['seat']) for e in session.clients}:
        raise RuntimeError('Presentation restart changed a fixed identity/role')
    session.write_json(session.stage_output/'presentation-restart.json',{'old_render_pid':old_render,'old_logic_pid':old_logic,
        'identities':identities,'processes':[{'name':e['name'],'pid':p.pid,'mask':e['mask'],'rendered':e['rendered']} for e,p in session.records]})
    session.finish()


def fixed_reconnect(session, abrupt=False):
    session.sync_seats();session.begin('fixed-role-'+('abrupt-' if abrupt else '')+'reconnect')
    pilot=entry(session,'Red','Air')
    before={m['stable_id']:m['life'] for m in session.states[pilot['name']].get('wingman_states',[]) if m['team']=='Red' and m['life_state']==1}
    old=leave(session,pilot,abrupt)
    from admission_probe import reject
    import uuid
    negative=[reject(session,pilot['player'],pilot['team'],'Ground','Role is locked for this match')]
    negative.extend(getattr(session,'preliminary_admission_paths',[]) or
                    [reject(session,uuid.uuid4().hex,pilot['team'],'Ground','Role seat occupied')])
    session.wait('vacant-ship',lambda:all(any(o['definition']=='player/ship' and o['team']==1 and o['dormant'] for o in s['scene_objects'])
        and all(m['life_state']==3 and not m['entity'] for m in s.get('wingman_states',[]) if m['team']=='Red') for n,s in session.states.items() if n!='server'))
    # A real new client fills the vacant original seat. The old identity must
    # be refused, rather than being silently demoted or moved to another role.
    original=pilot['player'];replacement=uuid.uuid4().hex
    pilot['player']=replacement
    pilot['command']=[('-GuLiDevPlayerId='+replacement) if a.startswith('-GuLiDevPlayerId=') else a for a in pilot['command']]
    session.restart_owned(pilot['name']);returned(session,pilot)
    negative.append(reject(session,original,pilot['team'],pilot['seat'],'Role seat occupied'))
    leave(session,pilot)
    pilot['player']=original
    pilot['command']=[('-GuLiDevPlayerId='+original) if a.startswith('-GuLiDevPlayerId=') else a for a in pilot['command']]
    new=session.restart_owned(pilot['name']);returned(session,pilot)
    session.prove('fixed_role_rejections',True,'Real admission rejects role mutation, occupied seats and a returning identity whose seat was filled',negative)
    if before:
        session.wait('wingmen-new-lives',lambda:all(all(any(m['stable_id']==k and m['life']>life and m['life_state']==1 and m['entity']
            and identity(m['controller'])==pilot['player'] and m['controller']==m['simulator'] for m in s.get('wingman_states',[]))
            for k,life in before.items()) for n,s in session.states.items() if n!='server'))
    result_path=session.stage_output/'fixed-role-result.json'
    session.write_json(result_path,{'player':original,'team':pilot['team'],'seat':pilot['seat'],
        'old_pid':old,'returned_pid':new,'replacement_player':replacement,'abrupt':abrupt})
    paths=[session.stage_output/'baseline.json',session.stage_output/'vacant-ship.json',session.stage_output/('fixed-role-return-'+pilot['name']+'.json'),result_path]
    check='member_loss' if abrupt else 'fixed_reconnect'
    session.prove(check,old!=new,'Same player reconnects into the original fixed Air seat after vacancy',paths)
    if before and abrupt: session.prove('wingman_vacancy_rebirth',True,'Vacant Ship stops; all old wingmen retire and regenerated members use newer lives',paths+[session.stage_output/'wingmen-new-lives.json'])
    session.finish()

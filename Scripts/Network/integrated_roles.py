"""Fixed identities/roles; change only which owned client renders the scene."""
from pathlib import Path
from integrated_acceptance import entry, identity


def leave(session, item, abrupt=False):
    _,process=next((e,p) for e,p in session.records if e['name']==item['name'])
    if abrupt:
        session.stop_owned(item['name'])
    else:
        session.expected_stopped.add(item['name'])
        operation=session.operation('leave_match',item['player'])
        session.wait('exit-'+item['name'],lambda:process.poll() is not None,timeout=30)
        last=session.read_state(Path(item['directory'])/'state.json',session.plan['run_id'],process.pid)
        receipt=next((a for a in last.get('review_actions',[]) if identity(a['id'])==operation),{})
        if process.returncode!=0 or receipt.get('status')!='completed':
            raise RuntimeError('Normal exit lacks a committed leave receipt')
        session.write_json(session.stage_output/('exit-receipt-'+item['name']+'.json'),last)
    session.control()
    session.wait('member-left-'+item['name'],lambda:session.settled() and all(any(
        identity(m['player'])==item['player'] and not m['online'] for m in v.get('role_members',[]))
        for n,v in session.states.items() if n!='server'))
    return process.pid


def returned(session, item):
    session.wait('fixed-role-return-'+item['name'],lambda:session.settled() and all(any(
        identity(m['player'])==item['player'] and m['online'] and m['role']==item['launch_seat']
        for m in v.get('role_members',[])) for n,v in session.states.items() if n!='server'))
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
    negative=[reject(session,pilot['player'],pilot['team'],'Ground','Role is locked for this match'),
              reject(session,uuid.uuid4().hex,pilot['team'],'Ground','Role seat occupied')]
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

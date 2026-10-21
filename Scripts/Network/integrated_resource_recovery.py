"""Focused current-snapshot recovery faults; no production/mining setup loop."""
from __future__ import annotations

import copy

from integrated_acceptance import entry, identity, input_and_wait
from integrated_roles import leave, returned


def owner_process_loss(session):
    session.begin('resource-owner-process-loss')
    session.refresh()
    reference=next(session.states[e['name']] for e in session.clients)
    groups=reference.get('resource_snapshots',[])
    if len(groups)!=1:
        raise RuntimeError('Focused resource fault requires the authored single resource group')
    original=copy.deepcopy(groups[0])
    group=identity(original['group'])
    owner=next(e for e in session.clients if e['player']==identity(original['owner']))
    backup=next(e for e in session.clients if e['player']==identity(original['backup']))

    def group_state(endpoint):
        return next((g for g in session.states[endpoint['name']].get('resource_snapshots',[])
                     if identity(g['group'])==group),None)

    with session.preparation('Existing resource backup readiness'):
        session.wait('two-complete-backups',lambda:bool((g:=group_state(backup))
                     and g['complete_copies']==2 and not int(g['recovering'])))
    session.write_json(session.stage_output/'replica-before-kill.json',{
        'owner':owner['player'],'backup':backup['player'],'group':group_state(backup),
        'scope':'Actual process loss. No graceful export or final quit receipt is available.'})
    departed=False
    try:
        # Only the launcher's owned process is terminated. Detection, fencing,
        # backup choice, installation, grant commit and deadlines are production.
        departed=True
        old_pid=leave(session,owner,abrupt=True)
        active=[e for e in session.clients if e['name']!=owner['name']]

        def installed():
            cuts=[group_state(e) for e in active]
            return all(c and int(c['installed_recovery'])>int(original['installed_recovery'])
                       and int(c['snapshot_epoch'])==int(original['request_epoch'])
                       and int(c['request_epoch'])==int(original['request_epoch'])+1
                       and not int(c['recovering']) and len(c['digest'])==64 for c in cuts) \
                and len({(c['snapshot'],c['sample'],c['digest']) for c in cuts})==1 and session.settled()

        session.wait('survivors-installed-one-backup',installed)
        cuts={e['name']:copy.deepcopy(group_state(e)) for e in active}
        new_owners={identity(c['owner']) for c in cuts.values()}
        if len(new_owners)!=1 or owner['player'] in new_owners:
            raise RuntimeError('Resource process loss did not commit one new owner')
        new_owner=next(iter(new_owners))
        local={}
        for e in active:
            for o in session.states[e['name']].get('scene_objects',[]):
                if o['ownership_policy']==4 and o['life']==1 and o['local_simulation']:
                    local.setdefault((identity(o['id']),o['generation']),[]).append(e['player'])
        expected={(identity(o['id']),o['generation']) for o in session.states[active[0]['name']].get('scene_objects',[])
                  if o['ownership_policy']==4 and o['life']==1}
        if not expected or set(local)!=expected or any(v!=[new_owner] for v in local.values()):
            raise RuntimeError('Recovered resource objects lack exactly one actual local simulator')
        # A new controlled economy request proves the new epoch accepts work.
        # This is explicitly a development credit, never evidence of mining.
        commander=entry(session,'Red' if owner['team']=='Blue' else 'Blue','Commander')
        action,evidence=input_and_wait(session,'resource_supply',commander['player'],
                                       team=commander['team'],color='blue',amount=1)
        event=identity(action['request'])
        if not action['result'].get('committed'):
            raise RuntimeError('Fresh resource request failed after process-loss recovery')
        session.wait('fresh-epoch-receipt-on-survivors',lambda:event in session.decisions
                     and session.decisions[event]['status']==0 and session.observed_by_active(event))
        session.write_json(session.stage_output/'resource-process-loss-contract.json',{
            'old_pid':old_pid,'before':original,'installed':cuts,'new_owner':new_owner,
            'local_simulated_count':len(local),'fresh_event':event,'decision':session.decisions[event],
            'scope':'Actual process loss, complete common cut, unique simulation and fresh request. '
                    'Exact rollback of individual fields is covered separately; this stage does not claim it.'})
        session.restart_owned(owner['name'])
        returned(session,owner)
        departed=False
        session.prove('resource_owner_process_loss',True,
            'Resource owner process was terminated; all survivors installed one complete backup, '
            'activated one simulator, committed a fresh request and accepted the returning client',
            [session.stage_output/p for p in ('replica-before-kill.json','survivors-installed-one-backup.json',
             'resource-process-loss-contract.json',evidence,'fresh-epoch-receipt-on-survivors.json',
             'fixed-role-return-'+owner['name']+'.json')])
    finally:
        import sys
        original_error=sys.exc_info()[1]
        if departed and next(p for e,p in session.records if e['name']=='server').poll() is None:
            process=next(p for e,p in session.records if e['name']==owner['name'])
            if process.poll() is not None:
                try:
                    session.restart_owned(owner['name'])
                    returned(session,owner)
                except RuntimeError as cleanup_error:
                    session.write_json(session.stage_output/'resource-process-loss-cleanup-failure.json',{
                        'original_failure':str(original_error) if original_error else None,
                        'cleanup_failure':str(cleanup_error)})
                    if original_error is None:
                        raise
    session.finish()


def backup_process_loss(session):
    session.begin('resource-backup-process-loss')
    session.refresh()
    groups=session.states[session.clients[0]['name']].get('resource_snapshots',[])
    if len(groups)!=1:
        raise RuntimeError('Focused backup fault requires the authored single resource group')
    original=copy.deepcopy(groups[0])
    group=identity(original['group'])
    backup=next(e for e in session.clients if e['player']==identity(original['backup']))
    owner=identity(original['owner'])
    if backup['player']==owner:
        raise RuntimeError('Backup fault requires a storage-only peer distinct from the simulator')

    def group_state(endpoint):
        return next((g for g in session.states[endpoint['name']].get('resource_snapshots',[])
                     if identity(g['group'])==group),None)

    with session.preparation('Existing storage-only backup readiness'):
        session.wait('two-complete-backups',lambda:bool((g:=group_state(backup))
                     and g['complete_copies']==2 and not int(g['recovering'])))
    session.write_json(session.stage_output/'replica-before-kill.json',group_state(backup))
    departed=False
    try:
        departed=True
        old_pid=leave(session,backup,abrupt=True)
        active=[e for e in session.clients if e['name']!=backup['name']]

        def replacement_ready():
            cuts=[group_state(e) for e in active]
            if not all(c and identity(c['owner'])==owner
                       and c['request_epoch']==original['request_epoch']
                       and c['owner_version']==original['owner_version']
                       and c['installed_recovery']==original['installed_recovery']
                       and not int(c['recovering']) for c in cuts):
                raise RuntimeError('Losing storage-only backup changed the resource simulator or request epoch')
            backups={identity(c['backup']) for c in cuts}
            if len(backups)!=1 or owner in backups or backup['player'] in backups:
                return False
            replacement=next((e for e in active if e['player'] in backups),None)
            return bool(replacement and group_state(replacement)['complete_copies']==2 and session.settled())

        session.wait('replacement-has-two-complete-cuts',replacement_ready)
        local={}
        for e in active:
            for o in session.states[e['name']].get('scene_objects',[]):
                if o['ownership_policy']==4 and o['life']==1 and o['local_simulation']:
                    local.setdefault((identity(o['id']),o['generation']),[]).append(e['player'])
        expected={(identity(o['id']),o['generation']) for o in session.states[active[0]['name']].get('scene_objects',[])
                  if o['ownership_policy']==4 and o['life']==1}
        if not expected or set(local)!=expected or any(v!=[owner] for v in local.values()):
            raise RuntimeError('Backup replacement changed actual resource simulation authority')
        session.write_json(session.stage_output/'resource-backup-loss-contract.json',{
            'old_pid':old_pid,'before':original,'after':{e['name']:group_state(e) for e in active},
            'local_simulated_count':len(local),
            'scope':'Storage-only process terminated; original simulator and epoch retained, new custodian has two complete cuts.'})
        session.restart_owned(backup['name'])
        returned(session,backup)
        departed=False
        session.prove('resource_backup_process_loss',True,
            'Backup process loss retained one resource simulator and the same request epoch; '
            'replacement stored two complete cuts and the departed client rejoined',
            [session.stage_output/p for p in ('replica-before-kill.json','replacement-has-two-complete-cuts.json',
             'resource-backup-loss-contract.json','fixed-role-return-'+backup['name']+'.json')])
    finally:
        import sys
        original_error=sys.exc_info()[1]
        if departed and next(p for e,p in session.records if e['name']=='server').poll() is None:
            process=next(p for e,p in session.records if e['name']==backup['name'])
            if process.poll() is not None:
                try:
                    session.restart_owned(backup['name'])
                    returned(session,backup)
                except RuntimeError as cleanup_error:
                    session.write_json(session.stage_output/'resource-backup-loss-cleanup-failure.json',{
                        'original_failure':str(original_error) if original_error else None,
                        'cleanup_failure':str(cleanup_error)})
                    if original_error is None:
                        raise
    session.finish()

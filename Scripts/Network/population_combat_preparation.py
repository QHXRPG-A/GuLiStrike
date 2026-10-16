"""Cheat spatial setup with readback; real Ground shots release population."""
import math
import re
import time
import uuid

from integrated_acceptance import entry, identity, input_and_wait, objects
from acceptance_session import preparation


def terminal_lives(session):
    return {(identity(obj['id']),obj.get('generation',1))
            for endpoint in session.clients if endpoint['name'] in session.states
            for obj in session.states[endpoint['name']].get('scene_objects',[]) if obj['life']>=2}


def resolve_owners(session, requests, allow_terminal=False):
    """Resolve the requested lives after selection or birth grants settle."""
    if any(obj is None for obj,_,_,_ in requests):
        raise RuntimeError('Spatial preparation requires the original stable object life, not a transient missing owner view')
    wanted={(identity(obj['id']),obj.get('generation',1)) for obj,_,_,_ in requests}
    if len(wanted)!=len(requests):
        raise RuntimeError('Spatial preparation repeats the same object life')
    resolved={}; skipped=set()
    def ready():
        resolved.clear()
        skipped.update(wanted & terminal_lives(session))
        if skipped and not allow_terminal:
            from acceptance_session import PreparationSkipped
            raise PreparationSkipped('Requested preparation lives are already terminal: '+str(sorted(skipped)))
        for obj in objects(session,True):
            key=(identity(obj['id']),obj.get('generation',1))
            if key not in wanted or key in skipped or obj['life']!=1: continue
            if key in resolved: return False
            resolved[key]=obj
        return set(resolved)==wanted-skipped
    session.wait('position-owners-ready-'+uuid.uuid4().hex,ready)
    if skipped:
        session.write_json(session.stage_output/('terminal-position-lives-'+uuid.uuid4().hex+'.json'),{
            'lives':[{'object':key,'generation':generation} for key,generation in sorted(skipped)],
            'scope':'Cleanup only. A terminal life needs no unlock or relocation; live same-generation objects still require an actual simulator and native receipts.'})
    return [(resolved[key],point,locked,token) for obj,point,locked,token in requests
            if (key:=(identity(obj['id']),obj.get('generation',1))) not in skipped]


@preparation
def position(session, obj, point, locked=False, token=None, find_nearby=False):
    obj,point,locked,token=resolve_owners(session,[(obj,point,locked,token)])[0]
    owner=next(e for e in session.clients if e['player']==identity(obj['simulator']))
    values={'object':obj['id'],'point':point,'locked':locked}
    if find_nearby: values['find_nearby']=True
    if token: values['token']=token
    result,path=input_and_wait(session,'fixture_position',owner['player'],**values)
    applied=result['result']['applied']
    # The native receipt contains QueryPosition immediately after placement.
    # An unlocked moving carrier may already have left that point by the next
    # published snapshot; only locked targets must remain there.
    session.wait('position-readback-'+result['id'],lambda:any(o['id']==obj['id'] and (not locked or math.dist(o['position'],applied)<100)
        and (o['kind']!=1 or bool(o.get('preparation_locked'))==locked) for o in objects(session,True)))
    return result,path


@preparation
def positions(session, requests, find_nearby=False, allow_terminal=False):
    """Batch setup at the actual owners, with one readback per owner."""
    requests=resolve_owners(session,requests,allow_terminal=allow_terminal)
    groups={}
    for obj,point,locked,token in requests:
        groups.setdefault(identity(obj['simulator']),[]).append((obj,point,locked,token))
    applied=[]
    for player,items in groups.items():
        for first in range(0,len(items),64):
            batch=items[first:first+64]
            values=[{'object':obj['id'],'generation':obj.get('generation',1),'point':point,'locked':locked,**({'token':token} if token else {})}
                    for obj,point,locked,token in batch]
            result,path=input_and_wait(session,'fixture_positions',player,positions=values,allow_terminal=allow_terminal,
                                       **({'find_nearby':True} if find_nearby else {}))
            records={r['object'].lower():r for r in result['result']['positions']}
            def readback():
                current={o['id'].lower():o for o in objects(session,True)}
                terminal=terminal_lives(session) if allow_terminal else set()
                return all((identity(obj['id']),obj.get('generation',1)) in terminal or ((key:=obj['id'].lower()) in current and key in records
                    and (not locked or math.dist(current[key]['position'],records[key]['applied'])<100)
                    and (obj['kind']!=1 or bool(current[key].get('preparation_locked'))==locked)
                    )
                    for obj,_,locked,_ in batch)
            session.wait('batch-position-readback-'+result['id'],readback)
            applied.extend({'object':obj,'input':path,'result':{'id':result['id'],'result':records[obj['id'].lower()]}}
                           for obj,_,_,_ in batch)
    return applied


@preparation
def prepare_ground_duel(session):
    session.begin('ground-combat-spatial-preparation')
    receipts=[]
    for team,y in ((1,3000),(2,-3000)):
        obj=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==team and o['life']==1)
        point=[8000 if team==1 else 10500,0,obj['position'][2]] if getattr(session.args,'continue_remaining',False) else [8000,y,obj['position'][2]]
        result,path=position(session,obj,point,find_nearby=True)
        receipts.append({'input':path,'result':result})
    session.write_json(session.stage_output/'preparation-readback.json',{'scope':'Positions only. Movement, weapon geometry, damage and terminal life still use production input and results.','receipts':receipts})
    session.finish()


@preparation
def prepare_mining(session):
    """Put both real startup cohorts beside legal ore, before mining baselines."""
    import json
    from pathlib import Path
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    numbers=manifest['numbers'];session.begin('mining-spatial-preparation')
    session.control('gameplay',enable_mining=True)
    session.wait('mining-preparation-ready',lambda:all(session.states[e['name']].get('mining_ready') for e in session.clients),timeout=15)
    requests=[];chosen=[]
    for team,faction in (('Red',1),('Blue',2)):
        commander=entry(session,team,'Commander')
        miners=[o for o in objects(session,True) if o['definition']=='unit/3' and o['team']==faction and o['life']==1]
        if not miners: raise RuntimeError('No real startup miners for '+team)
        factories=[o for o in objects(session,True) if o['team']==faction and o.get('inventory',{}).get('processing')]
        live={int(c['id']):c for c in session.states[commander['name']]['mining_clusters']}
        choices=[]
        for i in range(int(float(numbers['resource_map/Clusters/count']))):
            prefix=f'resource_map/Clusters[{i}]/';key=int(float(numbers[prefix+'ClusterId']));state=live.get(key)
            if not state or state['remaining']<=0 or not state[team.lower()+'_allowed']: continue
            point=[float(numbers[prefix+'Center/'+a]) for a in 'XYZ']
            distance=min(math.dist(point,f['position']) for f in factories) if factories else 0
            choices.append((distance,key,point))
        if not choices: raise RuntimeError('No legal ore preparation deposit for '+team)
        _,cluster,point=min(choices,key=lambda c:c[0]);chosen.append({'team':team,'cluster':cluster,'point':point})
        for index,miner in enumerate(miners):
            angle=math.tau*index/max(1,len(miners));start=list(point)
            start[0]+=2200*math.cos(angle);start[1]+=2200*math.sin(angle)
            requests.append((miner,start,False,None))
    receipts=positions(session,requests)
    session.write_json(session.stage_output/'mining-position-readback.json',{'deposits':chosen,'positions':receipts,
        'scope':'Preparation positions only; the subsequent unchanged mining driver records a fresh baseline and requires actual orders, extraction, cargo, FIFO, output and conservation.'})
    session.finish()


@preparation
def prepare_mining_approach(session, key):
    """Skip travel to an actual reserved work position; never create ore or a lease."""
    key=identity(key);chosen={}
    def reserved():
        chosen.clear()
        for endpoint in session.clients:
            for value in session.states[endpoint['name']].get('miner_states',[]):
                if identity(value['stable_id'])!=key or not value.get('local_simulation'): continue
                if value['cargo_blue']+value['cargo_red']>0 or value['task_state']==2:
                    chosen['ready']=dict(value);return True
                goal=re.search(r'Goal=X=([-+0-9.]+) Y=([-+0-9.]+) Z=([-+0-9.]+)',value.get('travel',''))
                lease=value.get('lease',{})
                if value['task_state']==1 and goal and int(identity(lease.get('token','0')),16):
                    chosen.update(before=dict(value),point=[float(goal.group(i)) for i in (1,2,3)])
                    return True
        return False
    session.wait('actual-mining-work-position-'+key,reserved)
    if 'ready' in chosen: return
    obj=next(o for o in objects(session,True) if identity(o['id'])==key and o['life']==1
             and o.get('generation',1)==chosen['before']['generation'])
    result,path=position(session,obj,chosen['point'])
    # Action-only control clears query flags; the accepted mining task and its
    # real reservation remain active. Do not issue a new task or inject cargo.
    session.control('gameplay',enable_mining=True)
    session.write_json(session.stage_output/('mining-approach-preparation-'+key+'.json'),{
        'before':chosen['before'],'reserved_point':chosen['point'],'input':path,'result':result,
        'scope':'User-authorized travel preparation to this miner\'s actual reserved work position. Original task/lease, extraction, cargo, delivery, processing and conservation remain production state.'})


def run(session, targets, free, slots):
    pilot=entry(session,'Blue','Ground')
    def actor():
        return next(g for g in session.states[pilot['name']]['ground_states'] if g['team']==2)
    if not actor()['alive']:
        from acceptance_session import PreparationSkipped
        raise PreparationSkipped('The Blue Ground population shooter is already dead; prepare production before the source-death fault')
    source_object=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==2 and o['life']==1)
    source_home=list(actor()['position']);prepared=[];timeline=[];initial_events=set(session.decisions)
    population_waiting=False
    try:
        requests=[]
        # Reuse the central lane validated by actual population deaths in R113;
        # spawn platforms and outlying sentries do not define playable bounds.
        lane_x=10000
        lane_y=0
        for index,key in enumerate(sorted(targets)):
            obj=next(o for o in objects(session,True) if o['id']==key and o['life']==1)
            point=[lane_x+(index%5)*40,lane_y-80+(index//5)*40,obj['position'][2]]
            requests.append((obj,point,True,None))
        prepared=positions(session,requests,find_nearby=True)
        source_result,source_path=position(session,source_object,[lane_x-2000,lane_y,source_home[2]],find_nearby=True)
        session.write_json(session.stage_output/'spatial-preparation.json',{'targets':prepared,'source':source_result,
            'source_input':source_path,'scope':'User-authorized cheat positions and temporary target action locks only; no health, damage or population mutation.'})
        def released():
            nonlocal population_waiting
            source=actor()
            if free()>=slots:
                return any(e not in initial_events and d['kind']==0 and d['status']==0
                    and d['state_domain'] in targets and int(identity(d['death_event']),16)
                    and session.observed_by_active(e) for e,d in session.decisions.items())
            if not source['alive']: raise RuntimeError('The population-preparation Ground shooter died')
            enemies=[o for o in objects(session,True) if o['id'] in targets and o['life']==1 and o['health']>0]
            if not enemies:
                # Health/life and population commits can arrive in different
                # snapshots. Stop held fire and await the original receipt and
                # slot predicate within the same short preparation budget.
                if not population_waiting:
                    population_waiting=True;session.control('gameplay')
                    timeline.append({'at':time.monotonic(),'waiting_for_committed_population':True,'available_slots':free()})
                return False
            population_waiting=False
            enemy=min(enemies,key=lambda o:math.dist(source['position'],o['position']))
            # The real Mass damage adapter uses a sphere centered at this
            # location. A character-height offset misses its collision body.
            aim=list(enemy['position'])
            control={'player':pilot['player'],'move_x':0,'move_y':0,'aim_x':aim[0],'aim_y':aim[1],'aim_z':aim[2],
                'sprint':False,'fire':True,'rocket':True}
            session.control('ground_combat',ground_inputs=[control])
            timeline.append({'at':time.monotonic(),'source':source['position'],'target':enemy['id'],
                'target_position':enemy['position'],'target_health':enemy['health'],'shots':source['shots_fired'],'input':control})
            return False
        session.wait('population-released-by-ground-combat',released)
    finally:
        session.control('gameplay')
        session.write_json(session.stage_output/'population-ground-inputs.json',{'targets':sorted(targets),'samples':timeline})
        previous_deadline=session.preparation_deadline
        session.preparation_deadline=time.monotonic()+15
        cleanup_errors=[]
        try:
            for _ in range(3):
                session.refresh();restore=[]
                for value in prepared:
                    original=value['object'];living=next((o for o in objects(session,True) if o['id']==original['id']
                        and o.get('generation')==original.get('generation') and o['life']==1 and o.get('preparation_locked')),None)
                    if living: restore.append((living,living['position'],False,value['result']['result']['token']))
                if not restore: break
                try: positions(session,restore,find_nearby=True,allow_terminal=True)
                except RuntimeError as error: cleanup_errors.append(str(error))
            session.refresh()
            locked=[o['id'] for o in objects(session,True) if o['id'] in targets and o['life']==1 and o.get('preparation_locked')]
            session.write_json(session.stage_output/'preparation-lock-cleanup.json',{'remaining_locked':locked,'retry_errors':cleanup_errors,
                'scope':'Unlock surviving same-life targets at their current validated positions first; real deaths remain terminal.'})
            if locked: raise RuntimeError('Preparation locks still active: '+str(locked))
            homes=[]
            live={(o['id'],o.get('generation')):o for o in objects(session,True) if o['life']==1}
            for value in prepared:
                original=value['object'];key=(original['id'],original.get('generation'))
                if key in live: homes.append((live[key],original['position'],False,None))
            session.preparation_deadline=time.monotonic()+15
            returned=positions(session,homes,find_nearby=True,allow_terminal=True) if homes else []
            session.write_json(session.stage_output/'survivor-home-restoration.json',{'positions':returned,
                'scope':'After unlock, restore near the original positions using explicit bounded preparation navigation search; native receipts record actual applied points.'})
            if actor()['alive']: position(session,source_object,source_home,find_nearby=True)
        finally:
            session.preparation_deadline=previous_deadline


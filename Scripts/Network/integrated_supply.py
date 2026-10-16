"""Territory and power checks using real movement, upkeep and explicit preparation credits."""
import base64
import json
import math
import struct
from pathlib import Path

from integrated_acceptance import entry, objects, input_and_wait, prove
from integrated_commander import center, select, order, fund, current


def control_point(value):
    cap=value.get('capabilities',{}).get('control_point')
    if not cap: return None
    raw=base64.b64decode(cap['data'],validate=True)
    if cap['version']!=4 or len(raw)<13: raise RuntimeError('Incompatible control-point evidence')
    index,rank,owner,progress=struct.unpack_from('<IIBf',raw)
    return {'index':index,'rank':rank,'owner':owner,'progress':progress}


def capture(session):
    session.begin('territory-capture-through-movement')
    commander=entry(session,'Red','Commander')
    army=[o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1]
    battlefield=center([o for o in objects(session,True) if o['kind']==1 and o['life']==1])
    # A small capture party fits ordinary formation slots and does not send
    # hundreds of temporary summons into a narrow authored control point.
    army=sorted(army,key=lambda o:math.dist(o['position'],battlefield))[:25]
    start=center(army)
    candidates=[o for o in objects(session) if (p:=control_point(o)) and p['owner']==0]
    if not candidates: raise RuntimeError('No neutral authored territory remains for the capture input')
    target=min(candidates,key=lambda o:math.dist(battlefield[:2],o['position'][:2]))
    key=target['id']
    old_children={o['id']:o['simulator'] for o in objects(session) if o['parent']==key and o['life']<2}
    select(session,commander,army)
    with session.preparation('capture party positions at the authored control point'):
        from population_combat_preparation import positions
        requests=[]
        # Arrive just outside the center in a compact ring. Capture progress,
        # faction settlement and inherited facility supply remain untouched.
        for index,unit in enumerate(army):
            angle=math.tau*index/max(1,len(army));point=list(target['position'])
            point[0]+=900*math.cos(angle);point[1]+=900*math.sin(angle)
            requests.append((unit,point,False,None))
        prepared=positions(session,requests)
        session.write_json(session.stage_output/'capture-position-preparation.json',{'positions':prepared,
            'scope':'User-authorized positions; actual capture, ownership and power still run through the production rules.'})
    _,receipt=order(session,commander,target['position'])
    def captured():
        for e in session.clients:
            view={o['id']:o for o in session.states[e['name']]['scene_objects']}
            if key not in view or control_point(view[key])['owner']!=1: return False
            for child,simulator in old_children.items():
                if child not in view or view[child]['team']!=1 or view[child]['simulator']!=simulator: return False
        return any(o['parent']==key and o['team']==1 and o['entity'] and o['life']==1 for o in objects(session,True))
    session.wait('territory-captured',captured)
    session.write_json(session.stage_output/'capture-input.json',{'target':target,'existing_children':old_children,'input':receipt})
    prove(session,'territory_supply',captured(),
          'Formal movement captured an authored territory; its real facilities inherited the faction without moving existing simulators',
          receipt,'capture-input.json','territory-captured.json')
    with session.preparation('return surviving capture party after actual capture'):
        living={(o['id'],o.get('generation')):o for o in objects(session,True) if o['life']==1}
        requests=[(living[key],value['object']['position'],False,None) for value in prepared
                  if (key:=(value['object']['id'],value['object'].get('generation'))) in living]
        restored=positions(session,requests,find_nearby=True) if requests else []
        session.write_json(session.stage_output/'capture-party-restoration.json',{'positions':restored,
            'scope':'Return surviving same-generation preparation participants after the real capture proof; ownership, power and capture progress are untouched.'})
    session.finish()



def unpaid(session):
    session.begin('unpaid-gift-production-stops')
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    def unpaid():
        directory={o['id']:o for o in objects(session,True)}
        return [o for o in directory.values() if o['life']==1 and o['completed'] and o['team']==1
                and o['parent'] in directory and control_point(directory[o['parent']])
                and float(manifest['numbers'].get(o['definition']+'/ProductionCount',0))>0
                and not o['supplied'] and not o.get('producing',False)]
    with session.preparation('unpaid facility prerequisite'):
        session.wait('unpaid-facility',lambda:bool(unpaid()),timeout=45)
    facility=unpaid()[0];facility_key=facility['id']
    def age():
        return next(o['logical_age'] for e in session.clients for o in session.states[e['name']]['objects']
                    if o['id']==facility_key and o['local_simulation'])
    initial_age=age();initial_cycle=facility['production_seconds']
    session.wait('unpaid-cycle-stable',lambda: age()>=initial_age+180 and
                 (now:=current(session,facility_key)) is not None and not now['supplied'] and not now['producing'])
    now=current(session,facility_key)
    if abs(now['production_seconds']-initial_cycle)>.001:
        raise RuntimeError('An unpaid gifted producer advanced its production cycle')
    unpaid_evidence=[session.stage_output/'unpaid-facility.json',session.stage_output/'unpaid-cycle-stable.json']
    session.finish()

    session.supply_fixture={'facility':facility,'evidence':unpaid_evidence}


def resume(session):
    if not getattr(session,'supply_fixture',None):
        raise RuntimeError('Supply resume lacks an observed unpaid production baseline')
    facility=session.supply_fixture['facility'];facility_key=facility['id']
    unpaid_evidence=session.supply_fixture['evidence']
    # Authorized setup credits pay ordinary upkeep; power itself is not changed.
    amount=max(sum((p:=control_point(o)) is not None and p['owner']==team for o in objects(session)) for team in (1,2))+2
    fund(session,{'blue':amount,'red':amount})
    session.begin('paid-gift-production-resumes')
    def powered():
        for e in session.clients:
            view={o['id']:o for o in session.states[e['name']]['scene_objects']}
            if facility_key not in view or not view[facility_key]['supplied'] or not view[facility['parent']]['supplied']: return False
        active=current(session,facility_key)
        return active and active['producing']
    session.wait('paid-facility',powered)
    cycle=current(session,facility_key)['production_seconds']
    before_children={o['id'] for o in objects(session) if o['parent']==facility_key}
    session.wait('resumed-production-progress',lambda: powered() and
        (abs(current(session,facility_key)['production_seconds']-cycle)>.1 or
         any(o['parent']==facility_key and o['id'] not in before_children and o['life']==1 and o['entity'] for o in objects(session,True))))
    session.prove('supply_loss_resume',True,
        'Unpaid facility stopped for three of its own logical seconds; explicit preparation credits paid real upkeep and restored parent/child supply and real production progress',
        unpaid_evidence+[session.stage_output/'paid-facility.json',session.stage_output/'resumed-production-progress.json'])
    session.finish()


def run(session):
    capture(session)
    unpaid(session)
    resume(session)

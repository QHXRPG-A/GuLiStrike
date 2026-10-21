"""Authority-aware workload contracts shared by native-capture orchestration and reports."""
from __future__ import annotations

# Lower bounds inferred from 1,081 logical batches in the 2026-10-18 trace.
# Render instance counts (1,139) are deliberately not used as live population.
HIGH_LOAD_MINIMUM = {
    'unit/1': 165, 'unit/2': 135, 'unit/5': 365,
    'unit/3': 16, 'unit/4': 16, 'territory/7': 49,
    'building/4': 34, 'building/5': 34, 'building/6': 36,
}
SHAPE_FIELDS = ('alive', 'local_alive_owned', 'peer_alive_owned', 'local_simulation', 'dormant')


def simulation_shape(census):
    return {name: {field: row[field] for field in SHAPE_FIELDS}
            for name, row in census['by_definition'].items()
            if name.startswith(('unit/', 'building/', 'territory/'))}


def comparable_census(a, b):
    if not a or not b or not a.get('ready') or not b.get('ready'):
        return False
    try:
        return simulation_shape(a) == simulation_shape(b)
    except (KeyError, TypeError):
        return False


def validate_minimum(census, minimum=HIGH_LOAD_MINIMUM):
    if not census or not census.get('ready'):
        raise ValueError('No ready simulation census')
    actual = census.get('by_definition', {})
    missing = {name: {'required': count, 'actual': actual.get(name, {}).get('local_simulation', 0)}
               for name, count in minimum.items()
               if actual.get(name, {}).get('local_simulation', 0) < count}
    if missing:
        raise ValueError(f'Local live simulation workload is too small: {missing}')


def combat_inputs(preparation):
    """Compare applied participants/positions; request IDs and old positions vary."""
    return {'enemy_ordinal': preparation['enemy_ordinal'], 'enemy_position': preparation['enemy_position'],
            'members': [{key: row[key] for key in ('ordinal', 'definition', 'generation', 'applied')}
                        for row in preparation['positions']]}


def sampled_window_load(profile, exact_units=False):
    """Normal casualties remain evidence, but cannot qualify a reduced load."""
    observations=[('native_'+str(i),row.get('simulation_census'))
                  for i,row in enumerate(profile.get('network_samples',[]))]
    work=profile.get('hotspot_workload',{})
    observations += [(name,work[name]) for name in ('simulation_census_start','simulation_census_end') if name in work]
    failures=[]
    for name,value in observations:
        try:
            validate_minimum(value)
            if exact_units:
                expected={key:count for key,count in HIGH_LOAD_MINIMUM.items() if key.startswith('unit/')}
                actual={key:value['by_definition'].get(key,{}).get('local_simulation',0) for key in expected}
                if actual!=expected:
                    raise ValueError(f'Frozen local unit composition changed: expected={expected}, actual={actual}')
        except ValueError as error:failures.append({'sample':name,'reason':str(error)})
    return {'maintained':len(observations)>=2 and not failures,'samples':len(observations),'failures':failures,
            'exact_local_units_required':exact_units,
            'basis':'Native start/end and 1 Hz authority/life/type census; no render-instance denominator.'}


def history_scale_comparison(a,b):
    """Keep stochastic retired history within a declared 5% control band.

    Low counts must match exactly. Both actual counts stay in the report;
    matching live actors cannot substitute for this independent history gate.
    """
    rows={}
    for key in ('directory_records','retired_target_records'):
        av=a.get(key);bv=b.get(key)
        present=isinstance(av,(int,float)) and isinstance(bv,(int,float))
        limit=av*.05 if present and av>=20 else 0
        rows[key]={'A':av,'B':bv,'allowed_difference':limit,
                   'matched':present and abs(bv-av)<=limit}
    return {'matched':all(row['matched'] for row in rows.values()),'control_band_percent':5,'counts':rows}


def camera_comparison(a,b):
    """A matching viewport does not imply the same camera tier, pose or UI load."""
    fields=('viewport','camera_location','camera_rotation','camera_tier',
            'camera_target_height_m','camera_transitioning')
    complete=all(k in x and x[k] is not None for x in (a,b) for k in fields)
    checks={'complete':complete}
    if complete:
        checks.update(viewport_equal=a['viewport']==b['viewport'],
            tier_equal=a['camera_tier']==b['camera_tier'],
            settled=not a['camera_transitioning'] and not b['camera_transitioning'],
            target_height_equal=abs(a['camera_target_height_m']-b['camera_target_height_m'])<.01,
            position_equal=all(abs(x-y)<=1 for x,y in zip(a['camera_location'],b['camera_location'])),
            rotation_equal=all(abs((x-y+180)%360-180)<=.01 for x,y in zip(a['camera_rotation'],b['camera_rotation'])))
    return {'comparable':all(checks.values()),'checks':checks}


def slate_presentation_comparison(root):
    """Review retained start/end context and semantic UI workload, independently of timings."""
    import json
    load=lambda p:json.loads(p.read_text(encoding='utf-8'))
    checks={};details={}
    for phase in ('before','after'):
        controllers=[load(root/v/f'context-{phase}.json')['worlds'][0]['controllers'][0] for v in ('A','B')]
        details[phase]=camera_comparison(*controllers)
        # Older captures lack tier metadata; still expose their observed pose mismatch.
        checks[phase+'_camera']=details[phase]['comparable']
        shapes=[load(root/v/f'scene-{phase}.json')['widgets'] for v in ('A','B')]
        details[phase+'_ui']=[[{k:w.get(k) for k in ('hud_shapes','hud_expired','rings','health_bars','surface_resources')} for w in rows] for rows in shapes]
        checks[phase+'_widget_count']=len(shapes[0])==len(shapes[1])==1
        checks[phase+'_hud_presence']=checks[phase+'_widget_count'] and all(
            bool(a['hud_shapes'])==bool(b['hud_shapes']) and a['hud_expired']==b['hud_expired']
            and bool(a['surface_resources'])==bool(b['surface_resources'])
            for a,b in zip(shapes[0],shapes[1]))
    roles=[]
    for v in ('A','B'):
        data=load(root/v/'slate-window-scope.json')
        roles.append(sorted((w['kind'],w['excluded_editor_only'],w['paint_calls']>0) for w in data['windows']))
    checks['window_roles_equal']=roles[0]==roles[1]
    return {'comparable':all(checks.values()),'checks':checks,'details':details,
            'scope':'Camera tier, settled pose, viewport, HUD presence and window roles. Dynamic visual correctness and within-window input monitoring remain separate.'}

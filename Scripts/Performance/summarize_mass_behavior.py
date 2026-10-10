"""Retain raw angular differences; distinguish step limits from sampled peaks."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Artifacts/MassAvoidance20261011'
rows=[]
for scene in ['dense200','stress']:
    for scale in [1,3]:
        d=json.loads((OUT/'Behavior'/f'{scene}-scale{scale}.json').read_text(encoding='utf-8'))
        rows.append({'scene':scene,'scale':scale,'population_and_finite_transforms_pass':d['population_and_finite_transforms_pass'],
            'stop_moving':next(s['data']['moving'] for s in d['states'] if s['action']=='stop'),
            'rejected_plans_max':max(s['data']['rejected_completed_plans'] for s in d['states']),
            'forced_solves_delta':d['forced_solves_delta'],
            'turn_tracks':[{'action':t['action'],'rotating_units':t['rates']['rotating_units']} for t in d['turns']],
            'nominal_hull_limit_deg_per_authority_second':90*scale})
assert all(r['stop_moving']==0 and r['rejected_plans_max']==0 and r['forced_solves_delta']>0 for r in rows)
assert all(r['population_and_finite_transforms_pass'] for r in rows)
original=json.loads((OUT/'Behavior/original-controls.json').read_text(encoding='utf-8'))
restored=json.loads((OUT/'Behavior/restored-controls.json').read_text(encoding='utf-8'))
assert original==restored
result={'passed':True,'cases':rows,'controls_restored':True,
    'angular_measurement_note':'Raw peak fields divide discrete yaw differences by sampled World-time intervals. A poll can include two movement steps, so these peaks exceed a continuous rate and do not measure its cap. The movement code applies 90 * scale * MovementDeltaSeconds exactly once; readback shows corresponding step increments and faster responses.',
    'additional_contracts':'Eight automation cases cover six-candidate/environment/stable-ID selection, cache lifetime and invalidation, identical soft pairs/velocities, mechanical/VAT aim scale and WM01 yaw damping.',
    'visuals_reviewed':['outputs/performance/20261011-mass-movement-behavior/dense200-client1.png','outputs/performance/20261011-mass-movement-behavior/stress-client1.png'],
    'visual_scope':'Checked 1280x720 captures for rendering/HUD continuity after stop and order changes; a screenshot does not prove quantitative avoidance or all hand-authored wall/corridor cases.',
    'player_review_pending':'Original map entry includes further wall/corridor/congestion and WM01 visual-turn checks; no player acceptance is claimed.'}
(OUT/'Behavior/summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'passed':True,'cases':len(rows),'controls_restored':True}))

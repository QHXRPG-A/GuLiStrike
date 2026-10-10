"""End our opt-in acceptance fixtures with the approved optimization controls enabled."""
import json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance.client_three_review_support import OUT,capture_controls,restore_controls

before=capture_controls(review)
values=json.loads((ROOT/'outputs/performance/20261009-implementation/benchmark-original-cvars.json').read_text())
values.update({k:1 for k in ['gs.Effects.OffscreenLifecycle','gs.Effects.ThreeTierLOD',
 'gs.Units.Offscreen5Hz','gs.Flights.OffscreenPresentation','gs.WingmanFlight.Presentation','gs.WingmanFlight.Warnings']})
values['gs.Impacts.BatchMode']=2
restore_controls(review,values)
after=capture_controls(review)
assert all(after[k]==v for k,v in values.items()),(values,after)
(OUT/'debug-controls-restored.json').write_text(json.dumps({'success':True,'before':before,'after':after,
 'scope':'Candidate input version stays 0 and formal candidate references remain empty. Controls do not certify resources.'},indent=2),encoding='utf-8')
print(json.dumps({'success':True,'original_team_cap_restored':after['guli.stronghold.TeamUnitCap']}))

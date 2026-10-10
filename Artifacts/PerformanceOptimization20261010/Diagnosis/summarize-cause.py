"""Persist scope, native-load and cleanup evidence without changing old captures."""
import csv
import hashlib
import json
from pathlib import Path
import statistics
import sys

ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent


def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))


def timer(directory,name):
    s=read(directory/'insights-windowed/summary.json')
    rows=[t for t in s['timers'] if t['timer']==name]
    return {'calls_per_frame':sum(t['calls'] for t in rows)/s['frames'],
            'inclusive_ms_per_frame':sum(t['inclusive_ms_per_frame'] for t in rows),
            'exclusive_ms_per_frame':sum(t['exclusive_ms_per_frame'] for t in rows)}


def main():
    a=read(OUT/'analysis.json')
    scopes=['VehiclePresentation','Slate::DrawWindows','GuLiSceneUI_Paint',
            'UCharacterMovementComponent_TickComponent','DeferredRenderUpdates_GameThread',
            'ProcessUntilTasksComplete','WinPumpMessages','GuLiCommanderMassStateTreeProcessor_0']
    detailed=[]
    for block in ['AAA2','ABA2']:
        ds=sorted((OUT/'Paired').glob('*'+block+'*'))
        row={'block':block,'paths':[str(d.relative_to(ROOT)) for d in ds], 'timers':{}}
        for name in scopes:
            vals=[timer(d,name) for d in ds]
            row['timers'][name]={'three_windows':vals,
                  'middle_minus_mean_flanks_ms':vals[1]['inclusive_ms_per_frame']-(vals[0]['inclusive_ms_per_frame']+vals[2]['inclusive_ms_per_frame'])/2}
        detailed.append(row)
    path=OUT/'Paired/switchback-dense200-onscreen-ABA2-r2-combined/context-before.json'
    contexts=read(path)['worlds']
    vehicles=[{'world':w['world'],'time_seconds':w['time_seconds'],
               'mining_vehicles':w['classes'].get('GuLiMiningVehiclePawn',0),
               'construction_vehicles':w['classes'].get('GuLiConstructionVehiclePawn',0)} for w in contexts]
    peers=[c for r in a['runs'] for w in r['network'] if w['net_mode']==1 for c in w['connections']]
    old=read(OUT.parent/'Build/final-static-verification.json')
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in old['source_hashes']}
    mismatches=[p for p,h in hashes.items() if h!=old['source_hashes'][p]]
    result={'scope':'Cause diagnosis only; original 198 valid formal windows and default-adoption decisions unchanged.',
      'complete_diagnostic_windows':len(a['runs']), 'frame_scope_deltas':detailed,'ambient_actor_inventory':vehicles,
      'network':{'server_connection_mean_KBps_range':[min(c['send_bytes_per_second'] for c in peers)/1000,max(c['send_bytes_per_second'] for c in peers)/1000],
        'budgets':sorted(set(c['budget_Bps'] for c in peers)),
        'flight_budget_block_fraction_max':max(c['flight_peer']['budget_blocked_fraction'] for c in peers),
        'flight_batch_cap_fraction_max':max(c['flight_peer']['batch_cap_fraction'] for c in peers),
        'flight_head_end_ms_max':max(c['flight_peer']['head_age_end_ms'] for c in peers),
        'flight_queue_growth_events_per_second_range':[min(c['flight_peer']['queue_growth_per_second'] for c in peers),max(c['flight_peer']['queue_growth_per_second'] for c in peers)],
        'queue_conservation_error_max':max(abs(c['flight_peer']['conservation_error']) for c in peers),
        'loss_rates_max':max(max(c['receive_lost_per_second'],c['send_lost_per_second']) for c in peers)},
      'capture_errors':[e for r in a['runs'] for e in r['capture_errors']],
      'build4_native_source_hashes_still_match':not mismatches,'native_source_mismatches':mismatches,
      'production_configs_unchanged':read(OUT/'config-hashes-before.json')==read(OUT/'config-hashes-after.json'),
      'controls_restored_exactly':read(OUT/'original-controls.json')['values']==read(OUT/'restored-controls.json'),
      'end_worlds':read(OUT/'end-worlds.json'),
      'notes':['VehiclePresentation is an inclusive child of CharacterMovement; do not add both.',
               'Slate DrawWindows includes SceneUI Paint; do not add both.',
               'Scope deltas above use equal flanks for readability; analysis.json uses measured time interpolation.',
               'The transform peak is reproduced in AAA with all optimization flags off. This proves a confound, not a complete causal explanation for all previous P95 differences.']}
    (OUT/'cause-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='frame_scope_deltas'},ensure_ascii=False,indent=2))


if __name__=='__main__':main()

"""Summarize only the explicitly named final captures, excluding preliminary runs."""
import csv,json,math,statistics
from pathlib import Path

root=Path(__file__).resolve().parents[1]/'ArtSource/WarMachineHover_20260929/performance-held'
keys={'frame_ms':'FrameTime','game_ms':'GameThreadTime','draw_ms':'RenderThreadTime','gpu_ms':'GPUTime','draw_calls':'RHI/DrawCalls',
      'nozzles':'GuLiHover/Nozzles','moving_nozzles':'GuLiHover/MovingNozzles','systems':'GuLiHover/NiagaraSystems','gpu_slot_capacity':'GuLiHover/NiagaraParticleCapacity',
      'niagara_gpu_ms':'GPU/NiagaraGPUSimulation','translucency_gpu_ms':'GPU/Translucency'}
report={'scope':'Editor standalone PIE, RTX 4090 / i9-14900KF; 500 mixed units. Median and p95, milliseconds unless count. No packaged performance claim.',
        'discarded_initial_frames':20,'samples':[]}
for distance in [100,200,360]:
    for enabled in [0,1]:
        path=root/f'idle-{distance}m-fx{enabled}.csv'
        with path.open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
        data=[]
        for row in rows:
            try:
                # GPU timing scopes are absent when no emitter executed that pass.
                item={k:float(row.get(v,0) if k=='niagara_gpu_ms' else row[v]) for k,v in keys.items()}
                if item['frame_ms']>0 and all(math.isfinite(x) for x in item.values()):data.append(item)
            except (KeyError,ValueError,TypeError):pass
        assert len(data)==240,(path,len(data))
        data=data[20:]
        def summarize(key):
            values=sorted(x[key] for x in data)
            return {'median':statistics.median(values),'p95':values[math.ceil(.95*len(values))-1],'min':values[0],'max':values[-1]}
        report['samples'].append({'csv':path.name,'distance_to_fixture_center_m':distance,'fx':bool(enabled),'frames':len(data),
            'duration_seconds':sum(x['frame_ms'] for x in data)/1000,'metrics':{k:summarize(k) for k in keys}})
report['capture']=json.loads((root/'capture.json').read_text(encoding='utf8'))
report['valid_held_population']=report['capture']['success'] and report['capture']['max_warmachine_root_drift_cm']<1
report['valid_for_effect_cost_comparison']=report['valid_held_population']
report['comparison_limit']='Automatic building placement can displace stopped units; root drift invalidates a controlled overhead conclusion. Raw timings remain observations.'
report['deltas']=[]
for off,on in zip(report['samples'][::2],report['samples'][1::2]):
    has_work=on['metrics']['nozzles']['median']>0
    report['deltas'].append({'distance_m':off['distance_to_fixture_center_m'],'effect_load_present':has_work,
        'interpretation':'Uncontrolled root displacement: do not infer effect overhead' if not report['valid_held_population'] else ('Observational A/B, not a worst-case guarantee' if has_work else 'Distance budget only; zero FX does not measure active effect cost'),
        'median_ms_on_minus_off':{k:round(on['metrics'][k]['median']-off['metrics'][k]['median'],4) for k in ['game_ms','gpu_ms','niagara_gpu_ms','translucency_gpu_ms']} if report['valid_held_population'] and has_work else None})
report['draw_timing_status']='RenderThreadTime counter is near zero in this editor session and is invalid as Draw timing; raw counter retained for audit only.'
(root/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'samples':[{'distance_m':s['distance_to_fixture_center_m'],'fx':s['fx'],'medians':{k:round(v['median'],3) for k,v in s['metrics'].items()}} for s in report['samples']],'deltas':report['deltas']},indent=2))

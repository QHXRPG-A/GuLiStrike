"""Summarize artist CSV captures; excludes trailing CSV metadata/header rows."""
import csv
import json
import statistics as st
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]/'ArtSource/FX/WingmanGroundExplosion_Toon/performance'
METRICS=['GPUTime','Exclusive/GameThread/Effects','Exclusive/AllWorkers/Niagara',
         'Exclusive/RenderThread/Niagara','GPU/Translucency','GPU/Distortion','GPU/NiagaraGPUSimulation','GPU/BasePass']

def read(path):
    rows=list(csv.reader(path.open(encoding='utf-8-sig')))
    # CSV can append newly registered stats; its final header is authoritative.
    header=rows[-2] if len(rows)>2 and rows[-2][0]=='EVENTS' else rows[0]
    data=[]
    for row in rows[1:]:
        try:float(row[header.index('FrameTime')])
        except (ValueError,IndexError):continue
        data.append(row+['0']*max(0,len(header)-len(row)))
    # Fixed window includes spawn, full original lifetime, and recycling.
    data=data[8:290]
    if not data:raise RuntimeError('No numeric frame rows: '+str(path))
    result={'frames':len(data)}
    for metric in METRICS:
        values=[float(row[header.index(metric)]) for row in data] if metric in header else [0.0]*len(data)
        result[metric]={'mean':st.mean(values),'median':st.median(values),'p95':sorted(values)[int(len(values)*.95)]}
    if result['GPUTime']['mean']<=0:raise RuntimeError('GPU timestamps unavailable: '+str(path))
    return result

def main():
    report={'source':'UE5.7 CSV profiler','scope':'Controlled 1280x960 SceneCapture with identical frame-stepped solo simulations; actual combat entry validated separately','cases':{},'raw':{}}
    for variant in ['old','toon']:
        if not (ROOT/f'{variant}-30-r3.csv').exists():continue
        raw={f'{n}-r{r}':read(ROOT/f'{variant}-{n:02d}-r{r}.csv') for n in (0,1,10,30) for r in (1,2,3)}
        report['raw'][variant]=raw
        for n in (1,10,30):
            case=report['cases'].setdefault(str(n),{})
            case[variant]={}
            for metric in METRICS:
                absolute=[raw[f'{n}-r{r}'][metric]['mean'] for r in (1,2,3)]
                delta=[raw[f'{n}-r{r}'][metric]['mean']-raw[f'0-r{r}'][metric]['mean'] for r in (1,2,3)]
                case[variant][metric]={'absolute_ms':st.mean(absolute),'delta_ms':st.mean(delta),'delta_runs_ms':delta,'repeat_range_ms':max(delta)-min(delta),'repeat_stdev_ms':st.stdev(delta)}
    for case in report['cases'].values():
        if not all(v in case for v in ('old','toon')):continue
        o,t=case['old']['GPUTime'],case['toon']['GPUTime']
        gain=o['delta_ms']-t['delta_ms']
        variation=max(o['repeat_range_ms'],t['repeat_range_ms'])
        case['gpu_comparison']={'saved_ms':gain,'saved_percent':gain/o['delta_ms']*100 if o['delta_ms']>0 else None,'repeat_range_ms':variation,'exceeds_repeat_range':gain>variation}
    (ROOT/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps({'cases':report['cases']},indent=2))

if __name__=='__main__':main()

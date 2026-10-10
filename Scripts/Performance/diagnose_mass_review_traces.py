"""Offline trace diagnosis. Run after the fixed PIE matrix has stopped."""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import export_review_traces as trace
from Performance.analyze_stress_frames import events, aggregate
OUT=ROOT/'Artifacts/MassAvoidance20261011'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('runs',nargs='+')
    args=p.parse_args()
    summaries=[]
    for name in args.runs:
        directory=OUT/'Paired'/name
        assert directory.resolve().parent==(OUT/'Paired').resolve()
        summary=trace.export(directory)
        if not summary['cpu_eligible']:
            summaries.append({'run':name,'rejected':summary})
            continue
        begin,end=summary['trace_interval_seconds']
        ticks=[r for r in trace.rows(directory/'insights-windowed/engine-events.csv')
               if float(r['StartTime'])>=begin and float(r['EndTime'])<=end]
        ticks.sort(key=lambda r:float(r['Duration']))
        dest=directory/'representative-frames';dest.mkdir(exist_ok=True)
        choices={'median':ticks[len(ticks)//2],'p95':ticks[min(len(ticks)-1,int(len(ticks)*.95))],'worst':ticks[-1]}
        commands=[]
        for key,tick in choices.items():
            a,b=float(tick['StartTime']),float(tick['EndTime'])
            path=dest/(key+'-events.csv')
            if not path.exists():
                commands.append(f'TimingInsights.ExportTimingEvents "{path.as_posix()}" -startTime={a:.9f} -endTime={b:.9f} -columns=ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime,Duration,Depth')
        if commands:trace.analyze(directory,commands,dest)
        frames=[]
        for key,tick in choices.items():
            a,b=float(tick['StartTime']),float(tick['EndTime'])
            threads=events(dest/(key+'-events.csv'),a,b)
            gt=threads.get('GameThread',[])
            frames.append({'name':key,'engine_tick_ms':float(tick['Duration'])*1000,
                'top_gt_timers':aggregate(gt)[:30],
                'gt_accounted_ms':sum(e['exclusive_ms'] for e in gt),
                'threads':[{'name':k,'exclusive_work_ms':sum(e['exclusive_ms'] for e in values),
                            'top_timers':aggregate(values)[:8]} for k,values in threads.items()]})
        result={'run':name,'window_frames':summary['frames'],'window_top_gt_timers':summary['timers'][:30],
                'representative_frames':frames,
                'scope':'Representative trace frames and mean GT exclusive work; CPU worker/GPU intervals overlap and are never added to GT latency.'}
        (dest/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        summaries.append(result)
        print(json.dumps({'run':name,'frames':summary['frames'],'representative_ticks_ms':{f['name']:f['engine_tick_ms'] for f in frames}},ensure_ascii=False),flush=True)
    (OUT/'trace-diagnosis.json').write_text(json.dumps(summaries,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':main()

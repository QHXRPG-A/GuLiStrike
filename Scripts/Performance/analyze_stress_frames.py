"""Export real representative GT frames and their complete CPU/GPU event trees.

Times are trace seconds; exclusive times subtract direct children within each
thread only. Async work is reported separately and is never added to GT latency.
"""
from __future__ import annotations

import collections
import csv
import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance.export_review_traces import analyze, rows

OUT = ROOT / 'outputs/performance/20261009-stress-frame-analysis'
DIRECTORY = OUT / 'paired/runtime-p1-frame-diagnosis'
DEST = OUT / 'selected-frames'


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def events(path, begin, end):
    by_thread = collections.defaultdict(list)
    for order, row in enumerate(rows(path)):
        start, stop = float(row['StartTime']), float(row['EndTime'])
        if not math.isfinite(stop) or stop <= begin or start >= end:
            continue
        by_thread[row['ThreadName']].append({'timer':row['TimerName'],
            'start':start, 'end':stop, 'clipped_start':max(start,begin),
            'clipped_end':min(stop,end), 'duration_ms':float(row['Duration'])*1000,
            'depth':int(row['Depth']), 'order':order})
    for thread, values in by_thread.items():
        # Native exporter preserves timeline preorder. Its Start/End fields use
        # %.9g (10us precision at this session's 3200s clock), while Duration has
        # nine decimal places. Sorting rounded starts reorders tiny siblings and
        # corrupts direct-child attribution; retain native order and Duration.
        stack = []
        for event in values:
            while stack and stack[-1]['depth'] >= event['depth']:
                stack.pop()
            full = event['start'] >= begin-1e-5 and event['end'] <= end+1e-5
            event['inclusive_ms'] = event['duration_ms'] if full else min(event['duration_ms'],(event['clipped_end']-event['clipped_start'])*1000)
            event['exclusive_ms'] = event['inclusive_ms']
            event['ancestors'] = [x['timer'] for x in stack]
            if stack:
                stack[-1]['exclusive_ms'] -= event['inclusive_ms']
            stack.append(event)
        for event in values:
            event['exclusive_ms'] = max(0,event['exclusive_ms'])
    return by_thread


def aggregate(values):
    data = collections.defaultdict(lambda:{'calls':0,'inclusive_ms':0.,'exclusive_ms':0.})
    for event in values:
        r = data[event['timer']]
        r['calls'] += 1
        r['inclusive_ms'] += event['inclusive_ms']
        r['exclusive_ms'] += event['exclusive_ms']
    return sorted([dict(timer=k,**v) for k,v in data.items()], key=lambda x:x['exclusive_ms'],reverse=True)


def main():
    DEST.mkdir(parents=True,exist_ok=True)
    summary = json.loads((DIRECTORY/'insights-windowed/summary.json').read_text())
    assert summary['cpu_eligible']
    begin,end = summary['trace_interval_seconds']
    ticks = [r for r in rows(DIRECTORY/'insights-windowed/engine-events.csv')
             if float(r['StartTime'])>=begin and float(r['EndTime'])<=end]
    ticks.sort(key=lambda r:float(r['Duration']))
    choices = {'median':ticks[len(ticks)//2],
               'p95':ticks[min(len(ticks)-1,int(len(ticks)*.95))],
               'worst':ticks[-1]}
    commands = []
    for name,tick in choices.items():
        a,b = float(tick['StartTime']),float(tick['EndTime'])
        target = DEST/(name+'-events.csv')
        if not target.exists():
            commands.append(f'TimingInsights.ExportTimingEvents "{target.as_posix()}" -startTime={a:.9f} -endTime={b:.9f} -columns=ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime,Duration,Depth')
    if not (DEST/'threads.csv').exists():
        commands.append(f'TimingInsights.ExportThreads "{(DEST/"threads.csv").as_posix()}"')
    if commands:
        analyze(DIRECTORY,commands,DEST)
    frames=[]
    for name,tick in choices.items():
        a,b = float(tick['StartTime']),float(tick['EndTime'])
        by_thread = events(DEST/(name+'-events.csv'),a,b)
        gt = by_thread['GameThread']
        worlds=[]
        for index,event in enumerate(e for e in gt if e['timer']=='UWorld_Tick'):
            members=[]
            for child in gt:
                if child['order']<=event['order']:continue
                if child['depth']<=event['depth']:break
                members.append(child)
            worlds.append({'tick_order':index+1,'inclusive_ms':event['inclusive_ms'],
                           'exclusive_ms':event['exclusive_ms'],'ancestor_chain':event['ancestors'],
                           'top_children':aggregate(members)[:15],
                           'identity_scope':'Unlabelled engine UWorld scope; order is not asserted as server/client identity.'})
        impact=collections.defaultdict(lambda:{'calls':0,'inclusive_ms':0.,'exclusive_ms':0.})
        for event in gt:
            if 'NS_MachineGunImpact' not in event['timer']:continue
            key=' > '.join(event['ancestors'][-4:])
            s=impact[key];s['calls']+=1;s['inclusive_ms']+=event['inclusive_ms'];s['exclusive_ms']+=event['exclusive_ms']
        branches=[{'path':k,**v} for k,v in sorted(impact.items(),key=lambda kv:kv[1]['exclusive_ms'],reverse=True)]
        engine_ms=float(tick['Duration'])*1000
        accounted=sum(e['exclusive_ms'] for e in gt)
        assert abs(accounted-engine_ms)<.05,(name,accounted,engine_ms)
        frame={'name':name,'trace_start':a,'trace_end':b,'engine_tick_ms':engine_ms,
            'gt_exclusive_accounted_ms':sum(e['exclusive_ms'] for e in gt),
            'top_gt_timers':aggregate(gt)[:40],'world_ticks':worlds,
            'impact_parent_chains':branches[:16],
            'threads':[{'name':k,'events':len(v),'exclusive_ms':sum(e['exclusive_ms'] for e in v),
                        'top_timers':aggregate(v)[:8]} for k,v in by_thread.items()],
            'normalization':'One actual FEngineLoop::Tick. Events clipped to its interval; thread sums and GPU queue spans overlap and cannot be summed into GT.'}
        write(DEST/(name+'-analysis.json'),frame)
        # Compact timeline excludes tiny leaf work; retains recorded ancestors.
        write(DEST/(name+'-gt-timeline.json'),[{'timer':e['timer'],
            'start_ms':(e['clipped_start']-a)*1000,'end_ms':(e['clipped_end']-a)*1000,
            'depth':e['depth'],'exclusive_ms':e['exclusive_ms']} for e in gt
            if e['inclusive_ms']>=.15 and e['depth']<=10])
        frames.append(frame)
    result={'trace':str(DIRECTORY/'session.utrace'),'eligible_frames':len(ticks),
            'selection_basis':'Duration of complete GT FEngineLoop::Tick inside QPC window; CSV engine frame percentiles are separately reported.',
            'frames':frames}
    write(OUT/'frame-analysis.json',result)
    print(json.dumps([{'name':f['name'],'engine_tick_ms':f['engine_tick_ms'],
        'gt_accounted_ms':f['gt_exclusive_accounted_ms'],'gt':f['top_gt_timers'][:10],
        'impact_paths':f['impact_parent_chains'][:7],
        'worlds':[{'order':w['tick_order'],'ms':w['inclusive_ms'],'top':w['top_children'][:4]} for w in f['world_ticks']]}
        for f in frames],ensure_ascii=False,indent=2),flush=True)


if __name__=='__main__':main()

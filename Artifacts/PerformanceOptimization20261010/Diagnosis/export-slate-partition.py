"""Offline disjoint Slate paint grouping; does not guess client/window identity."""
import bisect
import concurrent.futures
import csv
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import export_review_traces as trace
OUT=Path(__file__).resolve().parent


def export(d):
    s=json.loads((d/'insights-windowed/summary.json').read_text())
    begin,end=s['trace_interval_seconds']
    dst=d/'slate-partition';dst.mkdir(exist_ok=True)
    file=dst/'events-v2.csv'
    patterns='FEngineLoop::Tick,Slate::Prepass,Slate::DrawWindows,Slate::DrawWindow,*Paint: Game UI*,*SLevelEditor*,*GuLiSceneUIWidget*'
    if not file.exists():
        trace.analyze(d,[f'TimingInsights.ExportTimingEvents "{file.as_posix()}" -threads=GameThread -timers="{patterns}" -columns=ThreadId,TimerName,StartTime,EndTime,Duration,Depth -startTime={begin:.9f} -endTime={end:.9f}'],dst)
    events=[]
    with file.open(encoding='utf-8-sig',newline='') as f:
        for r in csv.DictReader(f):
            events.append({'name':r['TimerName'],'start':float(r['StartTime']),
                           'end':float(r['EndTime']),'ms':float(r['Duration'])*1000,'depth':int(r['Depth'])})
    events.sort(key=lambda e:(e['start'],e['depth']))
    starts=[r['start'] for r in events]
    def children(root):
        return [e for e in events[bisect.bisect_left(starts,root['start']):bisect.bisect_right(starts,root['end'])]
                if e['depth']>root['depth'] and e['end']<=root['end']+1e-6]
    frames=[r for r in events if r['name']=='FEngineLoop::Tick' and r['start']>=begin and r['end']<=end]
    records=[]
    for frame in frames:
        sub=children(frame)
        roots=[e for e in sub if e['name']=='Slate::DrawWindow']
        # The editor viewport has an empty "Paint: Game UI" too. Only roots
        # containing the real project SceneUI widget identify game clients.
        paints=[e for e in sub if e['name']=='Paint: Game UI'
                and any(k['name'].startswith('GuLiSceneUIWidget ') for k in children(e))]
        if len(paints)!=2:
            raise RuntimeError((d.name,'expected two game UI roots',len(paints)))
        draw=sum(e['ms'] for e in sub if e['name']=='Slate::DrawWindows')
        game=sum(e['ms'] for e in paints)
        editor=0
        for r in roots:
            kids=children(r)
            if any(e['name'].startswith('SLevelEditor ') for e in kids):
                editor+=r['ms']-sum(e['ms'] for e in paints if e in kids)
        prepass=sum(e['ms'] for e in sub if e['name']=='Slate::Prepass')
        common=draw-game-editor-prepass
        assert common>=-.001,(d.name,common)
        records.append({'start':frame['start'],'engine_ms':frame['ms'],'draw_ms':draw,'game_ui_ms':game,
                        'editor_window_excluding_game_ui_ms':editor,'remaining_draw_ms':common,
                        'prepass_ms':prepass})
    result={'run':d.name,'frames':len(frames),
            'scope':'DrawWindows = Prepass + two game UI roots identified by SceneUI widgets + window(s) containing SLevelEditor after subtracting embedded game UI + remaining drawing. No per-client identity inferred.',
            'mean':{k:sum(r[k] for r in records)/len(records) for k in records[0] if k!='start'}}
    (dst/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    with (dst/'frames.csv').open('w',newline='',encoding='utf-8') as f:
        writer=csv.DictWriter(f,fieldnames=list(records[0]));writer.writeheader();writer.writerows(records)
    return result


if __name__=='__main__':
    ds=[d for d in (OUT/'Paired').iterdir() if any(x in d.name for x in ['AAA2','ABA2'])]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: results=list(pool.map(export,sorted(ds)))
    (OUT/'slate-partition.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    for label in ['AAA2','ABA2']:
        rows=[r for r in results if label in r['run']]
        print(label,{k:round(rows[1]['mean'][k]-(rows[0]['mean'][k]+rows[2]['mean'][k])/2,5) for k in rows[0]['mean']})

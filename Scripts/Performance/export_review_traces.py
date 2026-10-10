"""Export captured timer statistics only after sampling finishes.

Each row retains its scope, sample and normalization. GPU timing comes from the
paired CSV, not from mixed CPU/GPU timer statistics.
"""
from __future__ import annotations
import argparse, csv, json, subprocess, collections, math, sys, re
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-implementation'
INSIGHTS=Path('C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe')
sys.path.insert(0,str(ROOT/'Scripts'))
from export_commander_pose_traces import trace_clock


def analyze(directory,commands,destination):
    command_file=destination/'export-commands.txt'
    command_file.write_text('\n'.join(commands)+'\n',encoding='utf-8')
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    subprocess.run([str(INSIGHTS),'-OpenTraceFile='+str(directory/'session.utrace'),'-NoUI','-AutoQuit','-Unattended',
        '-ExecOnAnalysisCompleteCmd=@='+str(command_file),'-abslog='+str(destination/'export.log')],check=True,startupinfo=startup)


def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as stream:return list(csv.DictReader(stream))


def export(directory):
    destination=directory/'insights-windowed';destination.mkdir(exist_ok=True)
    record=json.loads((directory/'result.json').read_text())
    csv_frames=record['csv']['FrameTime']['n']
    events=destination/'engine-events.csv'
    if not events.exists():
        analyze(directory,[f'TimingInsights.ExportTimingEvents "{events.as_posix()}" -threads=GameThread -timers=FEngineLoop::Tick -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth'],destination)
    ticks=[x for x in rows(events) if math.isfinite(float(x['EndTime']))]
    clock_path=directory/'clock.json'
    if clock_path.exists():
        clock=trace_clock(directory/'session.utrace')
        host=json.loads(clock_path.read_text())['window_monotonic_seconds']
        # This runner uses Python's current Windows time.monotonic(), whose
        # origin is raw QPC seconds. The older pose exporter also supports a
        # legacy host that adds 2**24; that offset does not belong to this host.
        begin,end=[x-clock['qpc_origin_seconds'] for x in host]
        selected=[x for x in ticks if float(x['StartTime'])>=begin and float(x['EndTime'])<=end]
        method='Host QPC window; start/stop bridge frames excluded.'
    else:
        # Old captures have no host clock/bookmarks. Recover the final CSV-sized
        # contiguous frame window; retain the small end-command uncertainty.
        selected=ticks[-csv_frames:]
        begin=float(selected[0]['StartTime']) if selected else 0
        end=float(selected[-1]['EndTime']) if selected else 0
        method='Retrospective final CSV-N complete engine frames. Start/stop uncertainty <= 0.25 s; diagnostic CPU comparison, not exact CSV boundary.'
    frames=len(selected)
    health=frames>0 and abs(frames-csv_frames)<=max(5,csv_frames*.02) and abs((end-begin)-record['capture_seconds'])<.25 and max(float(x['Duration']) for x in selected)<1
    summary={'cpu_eligible':health,'window_method':method,'trace_interval_seconds':[begin,end],
        'frames':frames,'csv_frames':csv_frames,'case':record['case'],'pair':record['pair'],'variant':record['variant'],
        'source_directory':directory.name,'scope':'GT timer statistics and all-thread CPU work are separate. GPU-only rows removed. Engine frame normalization inside the declared window.'}
    if clock_path.exists():
        summary['clock_mapping']={'host_clock':'Windows Python time.monotonic / raw QPC seconds',
            'qpc_origin_seconds':clock['qpc_origin_seconds'],'start_cycle':clock['start_cycle'],
            'cycle_frequency':clock['cycle_frequency']}
    if not health:
        summary['rejection']='Missing/corrupt GT frame events or window mismatch; CSV remains separately usable.'
        (destination/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
        print(json.dumps(summary),flush=True);return summary
    paths={key:destination/(key+'-timers.csv') for key in ['gt','all','gpu']}
    if not all(x.exists() for x in paths.values()):
        analyze(directory,[f'TimingInsights.ExportTimerStatistics "{path.as_posix()}" {arg} -startTime={begin:.9f} -endTime={end:.9f} -sortBy=TotalInclusiveTime'
            for key,path,arg in [('gt',paths['gt'],'-threads=GameThread'),('all',paths['all'],''),('gpu',paths['gpu'],'-threads=GPU1,GPU2')]],destination)
    def cpu_rows(key):
        # UE 5.7's statistics exporter always includes GPU queues, even under
        # a CPU thread filter (TimingExporter.cpp). Remove identical GPU-only
        # rows rather than adding them to CPU costs or relying on timer names.
        gpu=collections.Counter(tuple(sorted(r.items())) for r in rows(paths['gpu']))
        result=[]
        for r in rows(paths[key]):
            identity=tuple(sorted(r.items()))
            if gpu[identity]:gpu[identity]-=1;continue
            result.append({'timer':r['Name'],'calls':int(r['Count']),
                'inclusive_ms_per_frame':float(r['Incl'])*1000/frames,
                'exclusive_ms_per_frame':float(r['Excl'])*1000/frames,'max_call_ms':float(r['I.Max'])*1000})
        return sorted(result,key=lambda x:x['exclusive_ms_per_frame'],reverse=True)
    timers=cpu_rows('gt');all_cpu=cpu_rows('all')
    summary.update({
             'timers':timers,'vfx':[x for x in timers if 'NS_' in x['timer'] or 'Niagara' in x['timer']],
             'all_cpu_vfx':[x for x in all_cpu if 'NS_' in x['timer'] or 'Niagara' in x['timer']],
             'prediction':[x for x in timers if 'FlightPrediction' in x['timer']]})
    (destination/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print(json.dumps({'exported':directory.name,'timers':len(timers),'vfx':summary['vfx'][:12],'prediction':summary['prediction']}),flush=True)
    return summary


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cases',nargs='+');ap.add_argument('--workers',type=int,default=1);args=ap.parse_args()
    directories=[]
    excluded_path=OUT/'runtime-pre-domain-fix-exclusion.json'
    excluded=set(json.loads(excluded_path.read_text())['excluded']) if excluded_path.exists() else set()
    for path in sorted((OUT/'paired').glob('*/result.json')):
        record=json.loads(path.read_text())
        if str(path.relative_to(OUT)) in excluded:continue
        if record.get('adoption_eligible') and (not args.cases or record['case'] in args.cases):directories.append(path.parent)
    with ThreadPoolExecutor(max_workers=args.workers) as pool:summaries=list(pool.map(export,directories))
    (OUT/'trace-review-summaries-windowed.json').write_text(json.dumps(summaries,indent=2),encoding='utf-8')


if __name__=='__main__':main()

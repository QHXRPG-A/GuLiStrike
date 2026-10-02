"""Run one serial editor authoring action and retain its complete result."""
import argparse,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts'))
from commander_editor_python import call_editor

parser=argparse.ArgumentParser()
parser.add_argument('script');parser.add_argument('action')
parser.add_argument('--capture',action='store_true')
parser.add_argument('--timeout',type=float,default=180)
args=parser.parse_args()
path=ROOT/'Scripts/CommanderIsland'/args.script
capture_started=time.time_ns()
response=call_editor(f'ISLAND_ACTION={args.action!r}\nISLAND_CAPTURE={args.capture!r}\n'+path.read_text(encoding='utf-8'),args.timeout)
result=response.get('result')
if args.capture and isinstance(result,dict) and result.get('success') and result.get('screenshot'):
    capture_path=Path(result['screenshot']);deadline=time.monotonic()+30
    while not (capture_path.is_file() and capture_path.stat().st_mtime_ns>=capture_started):
        if time.monotonic()>deadline:raise TimeoutError(f'Editor capture did not finish: {capture_path}')
        time.sleep(.1)
    response['capture_file_ready']=True
evidence=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1/EditorEvidence'
evidence.mkdir(parents=True,exist_ok=True)
filename=path.stem+'_'+args.action.replace(':','_')+'.json'
(evidence/filename).write_text(json.dumps(response,ensure_ascii=False,indent=2),encoding='utf-8')
result=response.get('result');display=dict(result) if isinstance(result,dict) else result
if isinstance(display,dict):
    for key in ('samples','nodes','actors','cells','boxes'):
        if isinstance(display.get(key),list):display[key+'_count']=len(display.pop(key))
print(json.dumps({'success':response.get('success'),'result':display,'evidence':str(evidence/filename)},ensure_ascii=False,indent=2))
raise SystemExit(0 if response.get('success') and isinstance(result,dict) and result.get('success') else 1)

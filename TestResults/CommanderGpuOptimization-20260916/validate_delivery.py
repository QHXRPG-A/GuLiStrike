"""Summarize existing capture logs, hashes and regression reports for delivery."""
import difflib
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parent
PROJECT=ROOT.parents[1]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


expected={'r.AntiAliasingMethod':1,'r.VolumetricFog':0,
    'r.Shadow.Virtual.ResolutionLodBiasDirectional':0,'r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving':0,
    'r.Shadow.Virtual.ResolutionLodBiasLocal':1,'r.Shadow.Virtual.ResolutionLodBiasLocalMoving':2,
    'r.Lumen.ScreenProbeGather.DownsampleFactor':32,'r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution':16,
    'r.LumenScene.DirectLighting.UpdateFactor':64,'r.LumenScene.Radiosity.UpdateFactor':128,'r.Lumen.AsyncCompute':1}
records=[]
for run in sorted(ROOT.glob('[ABC]-*-n1200-c*')):
    for role in ['server','client1','client2']:
        path=run/(role+'.log')
        if not path.exists():continue
        log=path.read_text(encoding='utf-8',errors='replace')
        errors=[line for line in log.splitlines() if any(marker in line for marker in
            ['Fatal error:', 'Assertion failed:', 'Ensure condition failed:', 'Missing unit body material variant', 'Attempted to send bunch exceeding max allowed size.'])]
        row={'run':run.name,'role':role,'runtime_errors':errors}
        if run.name[0] in 'BC':
            values=dict(expected,**{'r.VRS.EnableSoftware':int(run.name[0]=='B'),'r.VRS.ContrastAdaptiveShading':int(run.name[0]=='B')})
            actual={}
            for name in values:
                matches=re.findall(re.escape(name)+r'\s*=\s*"([^"\n]+)"\s+LastSetBy:\s*(\w+)',log,re.I)
                actual[name]={'value':float(matches[-1][0]),'priority':matches[-1][1]} if matches else None
            row['cvars']=actual
            row['cvars_correct']=all(actual[name] and actual[name]['value']==value for name,value in values.items())
        records.append(row)
(ROOT/'runtime-validation.json').write_text(json.dumps({'logs':records,'all_optimized_cvars_correct':all(r.get('cvars_correct',True) for r in records), 'no_runtime_error_markers':all(not r['runtime_errors'] for r in records)},indent=2))

def failures(report):
    return {t['fullTestPath']:sorted(e['event']['message'] for e in t['entries'] if e['event']['type']=='Error')
            for t in report['tests'] if t['state']=='Fail'}

current=read(ROOT/'Automation/index.json')
control=read(ROOT/'Automation-original-source/index.json')
cf,of=failures(current),failures(control)
regression={'implementation':{k:current.get(k) for k in ['succeeded','succeededWithWarnings','failed','notRun']},
    'original_source':{k:control.get(k) for k in ['succeeded','succeededWithWarnings','failed','notRun']},
    'same_failures_on_original_source':cf==of, 'failures':cf,
    'scope':'Same existing 37-test suite and execution order; original session source/config restored temporarily, implementation then restored byte-for-byte. New helper class remained registered but had no callers in the original source. No new automation test cases.'}
(ROOT/'regression-comparison.json').write_text(json.dumps(regression,ensure_ascii=False,indent=2),encoding='utf-8')

before=read(ROOT/'before-manifest.json')
new_files=['Source/GuLiStrike/Gameplay/Presentation/GuLiUnitRenderPolicy.h','Source/GuLiStrike/Gameplay/Presentation/GuLiUnitRenderPolicy.cpp',
    'Scripts/rendering/create_unit_low_reflection_materials.py','Scripts/rendering/preview_unit_low_reflection.py','Scripts/rendering/audit_unit_low_reflection.py']
patch=[];manifest=[]
for item in before+ [{'path':p} for p in new_files]:
    name=item['path'];path=PROJECT/name
    # Preserve each line ending: existing files contain both CRLF and LF.
    old=(ROOT/'before'/name).read_bytes().decode('utf-8').splitlines(True) if 'sha256' in item else []
    new=path.read_bytes().decode('utf-8').splitlines(True)
    patch.extend(difflib.unified_diff(old,new,fromfile='a/'+name if old else '/dev/null',tofile='b/'+name))
    manifest.append({'path':name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
(ROOT/'implementation.patch').write_bytes(''.join(patch).encode('utf-8'))
(ROOT/'implementation-manifest.json').write_text(json.dumps(manifest,indent=2))
ids=[]
for item in read(ROOT/'buildids.json'):
    path=Path(item['path']);ids.append({'path':str(path),'build_id':read(path)['BuildId']})
assert len({r['build_id'] for r in ids})==1
(ROOT/'buildids.json').write_text(json.dumps(ids,indent=2))
print(json.dumps({'logs':len(records),'cvars_correct':all(r.get('cvars_correct',True) for r in records),'no_runtime_errors':all(not r['runtime_errors'] for r in records),'same_original_failures':cf==of,'build_ids_match':True}))

"""Supplemental named GPU profiles outside all valid sampling windows."""
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import run_four_stage_review as review
from Performance.run_muzzle_batch_review import launch, commands, COUNTERS

OUT = ROOT / 'outputs/performance/20261010-muzzle-batch/gpu-supplement'
OUT.mkdir(exist_ok=True)
KEYS = ['r.RDG.Events', 'r.ShowMaterialDrawEvents', 'r.ProfileGPU.ShowUI',
        'r.ProfileGPU.ThresholdPercent', 'r.ProfileGPU.UnicodeOutput', 'r.ProfileGPU.Sort']
original = review.run("unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'values':"
                      "{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in "
                      + repr(KEYS) + "}}))")['values']
report = []
try:
    for scene in ('dense200', 'stress'):
        for mode in (0, 2):
            dest = OUT / f'{scene}-mode{mode}'
            dest.mkdir(exist_ok=True)
            print(json.dumps({'gpu_setup': scene, 'mode': mode}), flush=True)
            setup = launch(scene, mode)
            time.sleep(10)
            review.result(review.LOCK_VIEW)
            commands(['r.RDG.Events 3', 'r.ShowMaterialDrawEvents 1',
                      'r.ProfileGPU.ShowUI 0', 'r.ProfileGPU.ThresholdPercent 0',
                      'r.ProfileGPU.UnicodeOutput 0', 'r.ProfileGPU.Sort 0'])
            item = {'scene': scene, 'mode': mode, 'setup': setup,
                    'before': review.run(COUNTERS), 'profiles': [],
                    'scope': 'Outside measurement windows. Full named GPU profiles include '
                             'both clients/editor; cannot isolate Client1 by summing queues.'}
            for index in range(3):
                log = ROOT / 'Saved/Logs/GuLiStrike.log'
                offset = log.stat().st_size
                commands(['ProfileGPU'])
                time.sleep(2.5)
                with log.open('rb') as stream:
                    stream.seek(offset)
                    text = stream.read().decode('utf-8', errors='replace')
                (dest / f'profile-{index}.txt').write_text(text, encoding='utf-8')
                ribbons = []
                for line in text.splitlines():
                    if '|' not in line or 'ribbon' not in line.lower():
                        continue
                    cols = [c.strip() for c in line.split('|')]
                    if len(cols) >= 15:
                        try:
                            ribbons.append({'event': cols[-2], 'exclusive_dispatches': int(cols[2]),
                                            'inclusive_dispatches': int(cols[8]),
                                            'exclusive_ms': float(cols[6].split()[0]),
                                            'inclusive_ms': float(cols[12].split()[0])})
                        except ValueError:
                            pass
                item['profiles'].append({'index': index, 'instrumentation_transition': index == 0, 'ribbon_rows': ribbons,
                    'ribbon_exclusive_dispatches': sum(x['exclusive_dispatches'] for x in ribbons),
                    'ribbon_exclusive_ms': sum(x['exclusive_ms'] for x in ribbons),
                    'attribution': 'All named Ribbon GPU events, not ID52 attribution. '
                                   'Muzzle renderers preserve CPU init; see ribbon-renderer-readback.json.'})
            item['after'] = review.run(COUNTERS)
            (dest / 'result.json').write_text(json.dumps(item, ensure_ascii=False, indent=2), encoding='utf-8')
            report.append(item)
            review.stop()
finally:
    review.stop()
    commands([f'{k} {v:g}' for k, v in original.items()], True)
(OUT / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps([{'scene': r['scene'], 'mode': r['mode'],
                   'dispatches': [p['ribbon_exclusive_dispatches'] for p in r['profiles']]}
                  for r in report], ensure_ascii=False), flush=True)

"""One merged gameplay room, one handoff/member room, then six performance rooms."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from acceptance_contract import SCENARIOS, digest, validate_gate

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--through',choices=('gameplay','fault','performance'),default='performance')
    parser.add_argument('--etw-helper',type=Path)
    parser.add_argument('--etw-calibration',type=Path)
    parser.add_argument('--diagnostic-trace',action='store_true')
    parser.add_argument('--coordinator-port',type=int,default=18787)
    parser.add_argument('--udp-base',type=int,default=19801)
    parser.add_argument('--manifest',type=Path,help='Use one explicitly prepared, validated manifest for both G and F')
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to(ROOT): raise RuntimeError('Evidence must stay in the project')
    output.mkdir(parents=True,exist_ok=True)
    baseline=None
    for gate in ('gameplay','fault'):
        scenarios={}
        for case,(_,kind,role,_) in SCENARIOS.items():
            if kind!=gate: continue
            folder=output/case
            # Never silently overwrite an earlier failed attempt or gate.
            if folder.exists(): raise RuntimeError('Use a new suite directory: '+str(folder))
            command=[sys.executable,str(ROOT/'Scripts/Network/run_hybrid_acceptance.py'),
                '--case',case,'--formation','3v3','--population','600','--repeat','1',
                '--render-role',role,'--output',str(folder),'--gates',str(output),
                '--coordinator-port',str(args.coordinator_port),'--udp-base',str(args.udp_base)]
            if args.diagnostic_trace:
                command.append('--diagnostic-trace')
            if args.manifest:
                command += ['--manifest',str(args.manifest.resolve())]
            completed=subprocess.run(command,cwd=ROOT)
            if completed.returncode: return completed.returncode
            paths=list(folder.glob('*/scenario.json'))
            if len(paths)!=1: raise RuntimeError('No unambiguous integrated scenario result')
            result=json.loads(paths[0].read_text(encoding='utf-8'))
            baseline=baseline or result['baseline']
            if baseline!=result['baseline']: raise RuntimeError('Build/input baseline changed within this suite')
            scenarios[case]={'result':str(paths[0]),'sha256':digest(paths[0])}
        report={'schema':'guli-gate/v1','status':'passed','baseline':baseline,'scenarios':scenarios}
        path=output/(gate+'-gate.json')
        path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        validate_gate(output,gate,baseline)
        if args.through==gate: return 0
    if not args.etw_helper or not args.etw_calibration:
        raise RuntimeError('Performance requires an active owned ETW helper and fresh two-PID calibration evidence')
    return subprocess.run([sys.executable,str(ROOT/'Scripts/Network/run_hybrid_acceptance.py'),
        '--case','steady','--formation','all','--population','600','--repeat','3',
        '--gates',str(output),'--output',str(output/'performance'),
        '--coordinator-port',str(args.coordinator_port),'--udp-base',str(args.udp_base),
        '--etw-helper',str(args.etw_helper),'--etw-calibration',str(args.etw_calibration)],cwd=ROOT).returncode


if __name__=='__main__': raise SystemExit(main())

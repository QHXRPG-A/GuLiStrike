"""Run the existing synchronization cases, with real authority for the field case.

The caller owns the editor. This script owns only its fresh local PIE room and
records every completion; missing, timed out and failed cases never pass.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from pathlib import Path

from capture_live_pie import ROOT, PERF, editor, status, stop_owned_pie


CASES = [
    'CombatEffects.FlightBatchBootstrapAndLifecycle',
    'CombatEffects.MathAndIdentity', 'CombatEffects.NetworkOrderAndCleanup',
    'Commander.Mass.Navigation.WorkerFrozenInput',
    'Commander.Orders.AppendCapacityAndSafeCancellation',
    'Commander.Orders.InitialAndPersistentLifecycle',
    'Commander.Orders.MoveReuseBoundaryAndLifecycle',
    'Commander.Orders.UntrustedCommandValidation',
    'Commander.UI.SceneUI.IndexedGeometryEquivalence',
    'Commander.UI.SceneUI.PositionSamplingLifetimes',
    'Network.AdapterRegistry', 'Network.ComponentBindingLifetimes',
    'Network.ObjectStateEnvelope', 'Network.PeerPayload',
    'Network.ReflectedState', 'Network.SmallPeerPayload',
    'Teleport.ConfigurationAndBoundaries',
    'Teleport.PawnIdentityMaterialCameraAndRecovery',
    'Teleport.PeriodicEligibilityAndRestore',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--mask', type=int, default=31, choices=range(512))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    save = lambda name, data: (out / name).write_text(
        json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    if status().get('active'):
        raise RuntimeError('A pre-existing PIE room must not be replaced by the regression runner')
    original = editor("r={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in "
                      "['gs.Perf.HotspotMask','gs.Perf.GameplayWorkers','gs.SceneUI.IndexedGeometry','t.IdleWhenNotForeground','Slate.bAllowThrottling']}")
    records = []
    log = ROOT / 'Saved/Logs/GuLiStrike.log'

    def execute(name, cases):
        offset = log.stat().st_size
        command = 'Automation RunTests ' + '+'.join('GuLiStrike.' + case for case in cases)
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
               f"unreal.SystemLibrary.execute_console_command(w,{command!r})\nr={{'success':True}}")
        deadline = time.monotonic() + 180
        completed_queue = False
        while time.monotonic() < deadline:
            with log.open('rb') as stream:
                stream.seek(offset)
                raw = stream.read().decode('utf-8', errors='replace')
            if 'Automation Test Queue Empty' in raw:
                completed_queue = True
                break
            time.sleep(.5)
        (out / (name + '.log')).write_text(raw, encoding='utf-8')
        lines = [line for line in raw.splitlines() if 'Test Completed.' in line]
        paths = [match.group(1) for line in lines
                 if (match := re.search(r'Path=\{([^}]+)\}', line))]
        expected = ['GuLiStrike.' + case for case in cases]
        record = {'group': name, 'queue_completed': completed_queue, 'expected': expected,
                  'completed': lines, 'paths': paths,
                  'passed': completed_queue and sorted(paths) == sorted(expected)
                  and all('Result={成功}' in line or 'Result={Success}' in line for line in lines)}
        records.append(record)
        save('results.json', records)
        print(json.dumps({'group': name, 'completed': len(lines), 'passed': record['passed']}), flush=True)

    try:
        values = [f'gs.Perf.HotspotMask {args.mask}', 'gs.Perf.GameplayWorkers 2', 'gs.SceneUI.IndexedGeometry 0',
                  't.IdleWhenNotForeground 0', 'Slate.bAllowThrottling 0']
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n" +
               '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{v!r})' for v in values) +
               "\nr={'success':True}")
        execute('core-lifecycles', CASES)
        # UE ends an existing PIE when automation starts. The test's first
        # latent command now starts its own room after that boundary.
        execute('real-hybrid-field', ['CombatEffects.SpellFieldTableConfig'])
        save('field-room.json', status())
    finally:
        save('stopped.json', stop_owned_pie())
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n" +
               '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{(k+" "+str(v))!r})'
                         for k, v in original.items()) + "\nr={'success':True}")
        identity = {}
        for name in ('GuLiStrike', 'GuLiStrikeEditor', 'GuLiNetCore'):
            path = ROOT / 'Binaries/Win64' / ('UnrealEditor-' + name + '.dll')
            with path.open('rb') as stream:
                identity[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
        save('identity.json', {'mask': args.mask, 'modules': identity})
    if len(records) != 2 or not all(row['passed'] for row in records):
        raise RuntimeError('Synchronization automation has incomplete or failed evidence')


if __name__ == '__main__':
    main()

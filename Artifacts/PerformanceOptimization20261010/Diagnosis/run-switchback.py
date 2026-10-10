"""Diagnostic adapter for the existing capture entry: same-PIE ABA/BAB and AAA.

Fifteen-second windows are causal probes, not replacements for the formal matrix.
OS queries are read-only, 1 Hz, buffered in this process, and never enumerate UE objects.
"""
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'Scripts'))
from Performance import run_snapshot_parallel_review as capture
from Performance import run_four_stage_review as review
from Performance import run_muzzle_batch_review as fixture

OUT = Path(__file__).resolve().parent
capture.OUT = OUT
capture.VARIANTS['combined'] = [0, 1, 0, 1]


class OSClock:
    def __init__(self, pid, tid):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.k.OpenProcess.restype = w.HANDLE
        self.k.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.k.OpenThread.restype = w.HANDLE
        self.k.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)]*4
        self.k.GetThreadTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)]*4
        self.k.QueryThreadCycleTime.argtypes = [w.HANDLE, c.POINTER(c.c_ulonglong)]
        self.k.GetSystemTimes.argtypes = [c.POINTER(w.FILETIME)]*3
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.p = self.k.OpenProcess(0x1000, False, pid)
        self.t = self.k.OpenThread(0x0800, False, tid)
        assert self.p and self.t, c.get_last_error()
        self.rows, self.quit = [], threading.Event()
        self.worker = threading.Thread(target=self.loop, daemon=True)

    @staticmethod
    def seconds(v):
        return ((v.dwHighDateTime << 32) | v.dwLowDateTime)/1e7

    def used(self, fn, handle):
        vals = [w.FILETIME() for _ in range(4)]
        assert fn(handle, *[c.byref(v) for v in vals]), c.get_last_error()
        return self.seconds(vals[2]) + self.seconds(vals[3])

    def sample(self):
        vals = [w.FILETIME() for _ in range(3)]
        assert self.k.GetSystemTimes(*[c.byref(v) for v in vals])
        cyc = c.c_ulonglong()
        assert self.k.QueryThreadCycleTime(self.t, c.byref(cyc))
        return {'qpc_seconds': time.monotonic(),
                'process_cpu_seconds': self.used(self.k.GetProcessTimes, self.p),
                'gt_cpu_seconds': self.used(self.k.GetThreadTimes, self.t),
                'gt_cycles': cyc.value,
                'system_idle_seconds': self.seconds(vals[0]),
                'system_cpu_capacity_seconds': self.seconds(vals[1])+self.seconds(vals[2])}

    def loop(self):
        try:
            while not self.quit.is_set():
                self.rows.append(self.sample())
                self.quit.wait(1)
        except Exception as error:
            self.rows.append({'error': str(error), 'qpc_seconds': time.monotonic()})

    def close(self):
        self.quit.set(); self.worker.join()
        self.rows.append(self.sample())
        self.k.CloseHandle(self.p); self.k.CloseHandle(self.t)


def main():
    assert not (OUT/'switchback-results.json').exists(), 'Never overwrite a completed run'
    meta = review.run(f"""
import ctypes,os
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
ew=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert 'LVL_CommanderMassPrototype' in ew.get_path_name()
unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'pid':os.getpid(),
 'game_thread_id':ctypes.windll.kernel32.GetCurrentThreadId(),
 'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {capture.KEYS!r}}}}}))
""")
    capture.write(OUT/'original-controls.json', meta)
    cfg_paths = ['Config/DefaultEngine.ini', 'Config/DefaultGame.ini']
    cfg = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in cfg_paths}
    capture.write(OUT/'config-hashes-before.json', cfg)
    # Same engine, native code, map, server/clients and quality as build4.
    review.run("""
ew=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
n=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(ew,True)
r=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
unreal.MCPythonHelper.submit_result(json.dumps({'success':r.success and n.success}))
""", timeout=180)
    sampler = OSClock(meta['pid'], meta['game_thread_id'])
    sampler.worker.start()
    blocks = [('AAA1',['baseline']*3), ('ABA1',['baseline','combined','baseline']),
              ('BAB1',['combined','baseline','combined']), ('ABA2',['baseline','combined','baseline']),
              ('BAB2',['combined','baseline','combined']), ('AAA2',['baseline']*3)]
    records = []
    try:
        for label, variants in blocks:
            setup = None
            for index, variant in enumerate(variants, 1):
                record = capture.capture('dense200', label, index, variant, seconds=15,
                                         phase='switchback', reuse_setup=setup,
                                         warmup_seconds=10 if index == 1 else 2,
                                         stop_after=False)
                setup = record['setup']
                records.append({'block':label,'position':index,'record':record})
            review.stop()
    finally:
        review.stop()
        fixture.commands([f'{k} {v:g}' for k,v in meta['values'].items()], True)
        restored = review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {capture.KEYS!r}}}}}))")['values']
        sampler.close()
        capture.write(OUT/'os-times.json', {'pid':meta['pid'],'gt_tid':meta['game_thread_id'],
                      'basis':'Windows GetThreadTimes/GetProcessTimes/GetSystemTimes cumulative seconds; no UE CSV percentage normalization.',
                      'samples':sampler.rows})
        capture.write(OUT/'restored-controls.json', restored)
        after = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in cfg_paths}
        capture.write(OUT/'config-hashes-after.json', after)
        assert meta['values'] == restored and cfg == after
    capture.write(OUT/'switchback-results.json', {'blocks':blocks,'records':records,
                  'scope':'Diagnostic 15-second same-PIE switchback with two-second settling; initial warmup ten seconds. Natural battle continues. No adoption decision.'})
    print('SWITCHBACK_COMPLETE', flush=True)


if __name__ == '__main__':
    main()

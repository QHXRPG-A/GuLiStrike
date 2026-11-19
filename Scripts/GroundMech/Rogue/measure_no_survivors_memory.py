"""Measure Windows editor private commit around a first and repeated load of the barrage FX."""
import ctypes
from ctypes import wintypes
from concurrent.futures import ThreadPoolExecutor
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "Artifacts/GroundNoSurvivors/memory-readback.json"
BRIDGE = ROOT / "Scripts/ue_exec.py"
PREAMBLE = """
import json, os, unreal
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
paths = ['/Game/GuLiStrike/FX/GroundMech/NoSurvivors/NS_NoSurvivors' + s for s in ('Flight', 'Impact')]
"""


def editor(body):
    process = subprocess.run([sys.executable, str(BRIDGE), "-"], input=PREAMBLE + body, text=True,
                             capture_output=True, check=True, timeout=180)
    reply = json.loads(process.stdout)
    assert reply["success"], reply
    return json.loads(reply["result"])


class Counters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("page_faults", wintypes.DWORD)] + [
        (n, ctypes.c_size_t) for n in ("peak_ws", "working_set", "peak_paged", "paged", "peak_nonpaged",
                                       "nonpaged", "pagefile", "peak_pagefile", "private_bytes")]


def measure():
    info = editor("""
loaded = [unreal.find_object(None, p + '.' + p.rsplit('/', 1)[-1]) is not None for p in paths]
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.MCPythonHelper.submit_result(json.dumps(dict(pid=os.getpid(), map=world.get_path_name(), already_loaded=loaded)))
""")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    handle = kernel.OpenProcess(0x1000 | 0x0010, False, info["pid"])
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    get_memory = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    get_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)

    def sample():
        if not get_memory(handle, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return counters.private_bytes

    def idle_window():
        values = []
        for _ in range(30):
            time.sleep(.05)
            values.append(sample())
        return values

    load_and_compile = """
rows = []
for p in paths:
    resource = unreal.load_asset(p)
    result = unreal.NiagaraService.compile_with_results(p)
    assert result.success and result.error_count == 0, result.export_text()
    vm = unreal.GuLiGroundMechFireEditorLibrary.get_compiled_vm_buffer_bytes(resource)
    assert vm > 0, (p, vm)
    rows.append(dict(system=p, compiled_vm_buffer_bytes=vm, pool_prime=resource.get_editor_property('pool_prime_size')))
unreal.MCPythonHelper.submit_result(json.dumps(dict(systems=rows)))
"""
    try:
        with ThreadPoolExecutor(max_workers=1) as worker:
            for phase in ("first_load", "repeat_loaded"):
                baseline = idle_window()
                job = worker.submit(editor, load_and_compile)
                observed = []
                while not job.done():
                    observed.append(sample())
                    time.sleep(.05)
                info["systems"] = job.result()["systems"]
                settled = idle_window()
                before = int(statistics.median(baseline))
                after = int(statistics.median(settled))
                info[phase] = dict(baseline_private_bytes=before, settled_private_bytes=after,
                                   delta_private_bytes=after-before, sampled_peak_delta_bytes=max(observed + settled)-before,
                                   baseline_range_bytes=max(baseline)-min(baseline), settled_range_bytes=max(settled)-min(settled))
    finally:
        kernel.CloseHandle(wintypes.HANDLE(handle))
    info["sampling"] = ("Windows editor process private commit at 50 ms; baseline and settled medians over 1.5 s. "
                        "Includes editor activity and pre-existing shared dependencies. No play world, component pool "
                        "priming, GPU memory measurement or on-disk cache reset. VM bytes count allocated bytecode and context buffers.")
    OUT.write_text(json.dumps(info, indent=2), encoding="utf8")
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    measure()

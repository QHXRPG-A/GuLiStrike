"""Capture an already verified new-logic authority PIE session. Never starts PIE.

Invoke after functional validation, at each requested load. The default duration
is the requested 30-minute soak. Trace/CSV hold frame and scope timings; JSONL
holds bounded-queue counters and actual mining/construction progress.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from commander_editor_python import call_editor

ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = Path(__file__).with_name('snapshot_engineering_runtime.py').read_text(encoding='utf-8')


class ProcessMemory(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('page_faults', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in ('peak_working_set', 'working_set', 'peak_paged_pool',
            'paged_pool', 'peak_nonpaged_pool', 'nonpaged_pool', 'pagefile', 'peak_pagefile', 'private_bytes')]


def memory_reader(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    query = ctypes.WinDLL('psapi', use_last_error=True).GetProcessMemoryInfo
    query.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessMemory), wintypes.DWORD]
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    path = ctypes.create_unicode_buffer(32768)
    size = wintypes.DWORD(len(path))
    if not kernel.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)) or Path(path.value).name.lower() != 'unrealeditor.exe':
        kernel.CloseHandle(handle)
        raise RuntimeError('Use the new UnrealEditor process ID.')

    def read():
        value = ProcessMemory()
        value.cb = ctypes.sizeof(value)
        if not query(handle, ctypes.byref(value), value.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return {key: getattr(value, key) for key, _ in value._fields_ if key != 'cb'}
    return read, lambda: kernel.CloseHandle(handle)


def run(code):
    response = call_editor(code, timeout=20)
    result = response.get('result')
    if not response.get('success') or not isinstance(result, dict) or not result.get('success'):
        raise RuntimeError(json.dumps(response, ensure_ascii=False))
    return result


def commands(values):
    run('import unreal, json\n'
        'w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n'
        'assert w\n'
        + '\n'.join(f'unreal.SystemLibrary.execute_console_command(w, {value!r})' for value in values)
        + '\nunreal.MCPythonHelper.submit_result(json.dumps({"success": True}))')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=1800)
    parser.add_argument('--interval', type=float, default=10)
    parser.add_argument('--label', required=True)
    parser.add_argument('--editor-pid', type=int, required=True)
    args = parser.parse_args()
    if args.seconds <= 0 or args.interval < 1:
        parser.error('Duration must be positive and sampling interval at least one second.')
    first = run(SNAPSHOT)  # Reject old native binaries / unready worlds before capture.
    output = ROOT / 'outputs/engineering-navigation' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    output.mkdir(parents=True, exist_ok=False)
    trace = (output / 'engineering.utrace').as_posix()
    csv_relative = (output / 'engineering.csv').relative_to(ROOT).as_posix()
    (output / 'configuration.json').write_text(json.dumps(vars(args), indent=2), encoding='utf-8')
    commands([f'Trace.File {trace} default,cpu,frame,bookmark', 'Trace.Bookmark EngineeringNewLogicStart',
              f'CsvProfile STARTFILE=../../../{csv_relative}'])
    read_memory, close_memory = memory_reader(args.editor_pid)
    started = time.monotonic()
    try:
        with (output / 'samples.jsonl').open('w', encoding='utf-8') as stream:
            sample = first
            while True:
                sample['elapsed_seconds'] = time.monotonic() - started
                sample['editor_memory'] = read_memory()
                stream.write(json.dumps(sample, ensure_ascii=False) + '\n')
                stream.flush()
                remaining = args.seconds - (time.monotonic() - started)
                if remaining <= 0:
                    break
                time.sleep(min(args.interval, remaining))
                sample = run(SNAPSHOT)
                assert sample['world'] == first['world'], 'Gameplay world changed during capture.'
    finally:
        close_memory()
        commands(['Trace.Bookmark EngineeringNewLogicEnd', 'CsvProfile STOP', 'Trace.Stop'])
    print(json.dumps({'output': str(output), 'duration_seconds': time.monotonic()-started}))


if __name__ == '__main__':
    main()

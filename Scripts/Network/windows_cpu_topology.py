"""Read Windows CPU-set topology and partition whole physical cores for acceptance.

Logical processor numbers are discovered, never assumed to encode P/E cores or SMT.
This module does not modify affinity until bind_process() is explicitly called.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import os
import struct


def discover() -> list[dict]:
    if os.name != "nt":
        raise RuntimeError("Acceptance CPU partitioning requires Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    query = kernel.GetSystemCpuSetInformation
    query.argtypes = [ctypes.c_void_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG),
                      wintypes.HANDLE, wintypes.ULONG]
    query.restype = wintypes.BOOL
    required = wintypes.ULONG()
    query(None, 0, ctypes.byref(required), None, 0)
    if not required.value:
        raise ctypes.WinError(ctypes.get_last_error())
    buffer = ctypes.create_string_buffer(required.value)
    if not query(buffer, required.value, ctypes.byref(required), None, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    cores: dict[tuple[int, int], dict] = {}
    offset = 0
    while offset < required.value:
        size, entry_type = struct.unpack_from("<II", buffer.raw, offset)
        if size < 8 or offset + size > required.value:
            raise RuntimeError("Invalid CPU-set information length")
        if entry_type == 0:
            if size < 32:
                raise RuntimeError("Incomplete CPU-set information")
            cpu_set, group, logical, core, _, _, efficiency, flags = struct.unpack_from(
                "<IHBBBBBB", buffer.raw, offset + 8)
            record = cores.setdefault((group, core), {
                "group": group, "core": core, "efficiency_class": efficiency,
                "logical_processors": [], "cpu_sets": [], "flags": [],
            })
            if record["efficiency_class"] != efficiency:
                raise RuntimeError("SMT siblings have inconsistent efficiency classes")
            record["logical_processors"].append(logical)
            record["cpu_sets"].append(cpu_set)
            record["flags"].append(flags)
        offset += size
    result = sorted(cores.values(), key=lambda c: (-c["efficiency_class"], c["group"], c["core"]))
    if not result or any(c["group"] != 0 for c in result):
        raise RuntimeError("This launcher requires a single Windows processor group")
    for core in result:
        core["logical_processors"].sort()
        core["affinity_mask"] = sum(1 << logical for logical in core["logical_processors"])
    return result


def partition(cores: list[dict]) -> dict:
    # 2 server + 6 renderer + 5*2 logic + at least 6 OS cores on the target host.
    if len(cores) < 24:
        raise RuntimeError(f"The agreed partition needs 24 physical cores; discovered {len(cores)}")
    assignments = {"server": cores[:2], "rendered": cores[2:8]}
    for index in range(5):
        assignments[f"logic{index + 1}"] = cores[8 + index * 2:10 + index * 2]
    assignments["system_reserve"] = cores[18:]
    masks = {}
    used = 0
    for role, members in assignments.items():
        mask = sum(c["affinity_mask"] for c in members)
        if mask & used:
            raise RuntimeError("CPU groups overlap")
        used |= mask
        masks[role] = mask
    return {"cores": assignments, "masks": masks, "physical_cores": len(cores),
            "logical_processors": sum(len(c["logical_processors"]) for c in cores),
            "basis": "GetSystemCpuSetInformation; complete SMT siblings; higher efficiency class first"}


def bind_process(pid: int, mask: int) -> int:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
    kernel.SetProcessAffinityMask.restype = wintypes.BOOL
    kernel.GetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    kernel.GetProcessAffinityMask.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x0200 | 0x0400, False, pid)  # SET + QUERY_INFORMATION
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if not kernel.SetProcessAffinityMask(handle, mask):
            raise ctypes.WinError(ctypes.get_last_error())
        actual, system = ctypes.c_size_t(), ctypes.c_size_t()
        if not kernel.GetProcessAffinityMask(handle, ctypes.byref(actual), ctypes.byref(system)):
            raise ctypes.WinError(ctypes.get_last_error())
        if actual.value != mask:
            raise RuntimeError(f"PID {pid} affinity differs: requested {mask:#x}, observed {actual.value:#x}")
        return actual.value
    finally:
        kernel.CloseHandle(handle)


def sample_process(pid: int) -> dict:
    """Read CPU and memory for an explicitly selected PID only."""
    class MemoryCounters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
                "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.GetProcessTimes.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        memory = MemoryCounters()
        memory.cb = ctypes.sizeof(memory)
        if not kernel.GetProcessTimes(handle, *[ctypes.byref(t) for t in times]):
            raise ctypes.WinError(ctypes.get_last_error())
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        cpu = sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 10_000_000
        return {"pid": pid, "cpu_seconds": cpu, "working_set_bytes": memory.WorkingSetSize,
                "private_bytes": memory.PrivateUsage}
    finally:
        kernel.CloseHandle(handle)

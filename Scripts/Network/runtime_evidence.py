"""Read atomically published evidence without blocking Windows replacement."""
from __future__ import annotations

import json
import os
from pathlib import Path


class RuntimeEvidenceUnavailable(RuntimeError):
    """The shared evidence stream is invalid; dependent stages cannot run."""


def pending_object_handoffs(values, identities):
    """Observe one cohort's grant convergence without stopping other objects."""
    keys = {key.replace('-', '').lower() for key in identities}
    views = [{o['id'].replace('-', '').lower(): o for o in value.get('objects', [])
              if o['id'].replace('-', '').lower() in keys} for value in values]
    if not views or any(set(view) != keys for view in views):
        raise RuntimeError('An observed object is missing from the authority directory')
    return {key for key in keys if any(view[key].get('handoff_pending') for view in views)
            or len({(view[key]['simulation_owner'], view[key]['owner_version']) for view in views}) != 1}


def additional_flight_counts(values):
    """Count actual owner-held flights; query copies are not additional load."""
    counts, identities = {}, set()
    for name, value in values.items():
        load = value.get('flight_load', {})
        flights = load.get('objects', [])
        if len(flights) != load.get('live_additional'):
            raise RuntimeError(f'{name}: additional-flight count has no matching real entities')
        for obj in flights:
            key = obj['id'].replace('-', '').lower()
            if not obj.get('local_simulation') or key in identities:
                raise RuntimeError('An additional flight has no unique local simulator')
            identities.add(key)
        counts[name] = len(flights)
    return counts


def ownership_views_agree(values):
    """Agree on persistent grants, without a barrier on newly emitted effects.

    Retained-source effects and per-operation work have separate event receipts.
    Their birth/retirement continuously overlaps independent endpoint samples.
    Destroyed records are tombstones, not participants in the live directory.
    Retirement is checked by its own event/state assertions, not by requiring
    every observer to keep an identical history of retired work objects.
    """
    views = [{(o['id'], o['generation']): (o['simulation_owner'], o['owner_version'],
                o['controller'], o['dormant'])
              for o in v.get('objects', [])
              if o.get('life') != 3 and o.get('ownership_policy') not in (3, 5)}
             for v in values]
    return bool(views and views[0]) and all(v == views[0] for v in views[1:])


def read_json_shared(path: Path):
    if os.name != "nt":
        return json.loads(path.read_text(encoding="utf-8-sig"))
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL("kernel32",use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE
    kernel.GetFileSizeEx.argtypes=[wintypes.HANDLE,ctypes.POINTER(ctypes.c_int64)]
    kernel.ReadFile.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),ctypes.c_void_p]
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=kernel.CreateFileW(str(path.resolve()),0x80000000,0x7,None,3,0x80,None)
    if handle==ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        size=ctypes.c_int64()
        if not kernel.GetFileSizeEx(handle,ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not 0<size.value<=16*1024*1024:
            raise RuntimeEvidenceUnavailable(f"Runtime JSON is empty or exceeds the evidence limit ({size.value} bytes): {path}")
        buffer=ctypes.create_string_buffer(size.value)
        received=wintypes.DWORD()
        if not kernel.ReadFile(handle,buffer,size.value,ctypes.byref(received),None):
            raise ctypes.WinError(ctypes.get_last_error())
        if received.value!=size.value:
            raise RuntimeEvidenceUnavailable(f"Incomplete runtime evidence read: {path}")
        return json.loads(buffer.raw.decode("utf-8-sig"))
    finally:
        kernel.CloseHandle(handle)

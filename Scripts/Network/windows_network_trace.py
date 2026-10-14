"""Owned ETW system-network session, retaining only the two measured PIDs.

Raw QPC timestamps and the payload PID are used; EVENT_HEADER.ProcessId is
not the originating PID for every network event. No existing session is stopped.
The fixture must prove UDP/TCP loopback attribution before a measured round.
Sources: Microsoft Learn StartTraceW, UdpIp_TypeGroup1 and TcpIp.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import ipaddress
import json
import gzip
import os
from pathlib import Path
import socket
import threading
import time
import uuid
import re

from runtime_evidence import read_json_shared


class GUID(C.Structure):
    _fields_ = [('a', W.DWORD), ('b', W.WORD), ('c', W.WORD), ('d', C.c_ubyte * 8)]

    def raw(self):
        return C.string_at(C.byref(self), 16)


class WNODE(C.Structure):
    _fields_ = [('BufferSize', W.ULONG), ('ProviderId', W.ULONG),
                ('HistoricalContext', C.c_uint64), ('TimeStamp', C.c_int64),
                ('Guid', GUID), ('ClientContext', W.ULONG), ('Flags', W.ULONG)]


class PROPERTIES(C.Structure):
    _fields_ = [('Wnode', WNODE)] + [(n, W.ULONG) for n in
        ('BufferSize', 'MinimumBuffers', 'MaximumBuffers', 'MaximumFileSize',
         'LogFileMode', 'FlushTimer', 'EnableFlags', 'AgeLimit', 'NumberOfBuffers',
         'FreeBuffers', 'EventsLost', 'BuffersWritten', 'LogBuffersLost',
         'RealTimeBuffersLost')] + [('LoggerThreadId', W.HANDLE),
                                   ('LogFileNameOffset', W.ULONG), ('LoggerNameOffset', W.ULONG)]


class DESCRIPTOR(C.Structure):
    _fields_ = [('Id', W.WORD), ('Version', C.c_ubyte), ('Channel', C.c_ubyte),
                ('Level', C.c_ubyte), ('Opcode', C.c_ubyte), ('Task', W.WORD), ('Keyword', C.c_uint64)]


class HEADER(C.Structure):
    _fields_ = [('Size', W.WORD), ('HeaderType', W.WORD), ('Flags', W.WORD), ('EventProperty', W.WORD),
                ('ThreadId', W.ULONG), ('ProcessId', W.ULONG), ('TimeStamp', C.c_int64),
                ('ProviderId', GUID), ('Descriptor', DESCRIPTOR), ('ProcessorTime', C.c_uint64), ('ActivityId', GUID)]


class RECORD(C.Structure):
    _fields_ = [('Header', HEADER), ('BufferContext', W.ULONG), ('ExtendedDataCount', W.WORD),
                ('UserDataLength', W.WORD), ('ExtendedData', C.c_void_p), ('UserData', C.c_void_p), ('UserContext', C.c_void_p)]


class TRACE_HEADER(C.Structure):
    _fields_ = [('Size', W.WORD), ('FieldTypeFlags', W.WORD), ('Version', W.ULONG),
                ('ThreadId', W.ULONG), ('ProcessId', W.ULONG), ('TimeStamp', C.c_int64), ('Guid', GUID), ('Flags', C.c_uint64)]


class EVENT_TRACE(C.Structure):
    _fields_ = [('Header', TRACE_HEADER), ('InstanceId', W.ULONG), ('ParentInstanceId', W.ULONG),
                ('ParentGuid', GUID), ('MofData', C.c_void_p), ('MofLength', W.ULONG), ('ClientContext', W.ULONG)]


class SYSTEMTIME(C.Structure):
    _fields_ = [(name, W.WORD) for name in ('Year', 'Month', 'DayOfWeek', 'Day', 'Hour', 'Minute', 'Second', 'Milliseconds')]


class TIME_ZONE(C.Structure):
    _fields_ = [('Bias', W.LONG), ('StandardName', W.WCHAR * 32), ('StandardDate', SYSTEMTIME), ('StandardBias', W.LONG),
                ('DaylightName', W.WCHAR * 32), ('DaylightDate', SYSTEMTIME), ('DaylightBias', W.LONG)]


class LOG_HEADER(C.Structure):
    _fields_ = [(n, W.ULONG) for n in ('BufferSize', 'Version', 'ProviderVersion', 'NumberOfProcessors')]
    _fields_ += [('EndTime', C.c_int64)]
    _fields_ += [(n, W.ULONG) for n in ('TimerResolution', 'MaximumFileSize', 'LogFileMode', 'BuffersWritten')]
    _fields_ += [('LogInstanceGuid', GUID), ('LoggerName', W.LPWSTR), ('LogFileName', W.LPWSTR), ('TimeZone', TIME_ZONE)]
    _fields_ += [(n, C.c_int64) for n in ('BootTime', 'PerfFreq', 'StartTime')]
    _fields_ += [('ReservedFlags', W.ULONG), ('BuffersLost', W.ULONG)]


class LOGFILE(C.Structure):
    _fields_ = [('LogFileName', W.LPWSTR), ('LoggerName', W.LPWSTR), ('CurrentTime', C.c_int64),
                ('BuffersRead', W.ULONG), ('ProcessTraceMode', W.ULONG), ('CurrentEvent', EVENT_TRACE), ('LogfileHeader', LOG_HEADER),
                ('BufferCallback', C.c_void_p), ('BufferSize', W.ULONG), ('Filled', W.ULONG), ('EventsLost', W.ULONG),
                ('EventRecordCallback', C.c_void_p), ('IsKernelTrace', W.ULONG), ('Context', C.c_void_p)]


def qpc():
    counter = C.c_int64()
    if not C.windll.kernel32.QueryPerformanceCounter(C.byref(counter)):
        raise C.WinError()
    return counter.value


def frequency():
    counter = C.c_int64()
    if not C.windll.kernel32.QueryPerformanceFrequency(C.byref(counter)):
        raise C.WinError()
    return counter.value


class NetworkTrace:
    def __init__(self, pids, max_events=2_000_000):
        if os.name != 'nt' or C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('Network collection requires 64-bit Windows Python')
        if not pids or len(pids) > 2 or len(set(pids)) != len(pids) or any(p <= 0 for p in pids):
            raise ValueError('Select the backend and rendered PID only (one PID is allowed for calibration)')
        for struct, size in ((WNODE, 48), (PROPERTIES, 120), (HEADER, 80), (RECORD, 112),
                             (EVENT_TRACE, 88), (LOG_HEADER, 280), (LOGFILE, 448)):
            if C.sizeof(struct) != size:
                raise RuntimeError(f'ETW ABI mismatch: {struct.__name__} size {C.sizeof(struct)} expected {size}')
        self.pids = set(pids)
        self.name = 'GuLiStrike.Acceptance.Network'
        self.events = []
        self.errors = []
        self.overflow = False
        self.max_events = max_events
        self.stopped = False
        self.created = False
        self.freq = frequency()
        self.api = C.WinDLL('advapi32', use_last_error=True)
        self.api.StartTraceW.argtypes = [C.POINTER(C.c_uint64), W.LPCWSTR, C.POINTER(PROPERTIES)]
        self.api.StartTraceW.restype = W.ULONG
        self.api.ControlTraceW.argtypes = [C.c_uint64, W.LPCWSTR, C.POINTER(PROPERTIES), W.ULONG]
        self.api.ControlTraceW.restype = W.ULONG
        self.api.OpenTraceW.argtypes = [C.POINTER(LOGFILE)]
        self.api.OpenTraceW.restype = C.c_uint64
        self.api.ProcessTrace.argtypes = [C.POINTER(C.c_uint64), W.ULONG, C.c_void_p, C.c_void_p]
        self.api.ProcessTrace.restype = W.ULONG
        self.api.CloseTrace.argtypes = [C.c_uint64]
        self.api.CloseTrace.restype = W.ULONG
        self.buffer = C.create_string_buffer(C.sizeof(PROPERTIES) + 1024 * C.sizeof(W.WCHAR))
        self.props = PROPERTIES.from_buffer(self.buffer)
        self.props.Wnode.BufferSize = C.sizeof(self.buffer)
        self.props.Wnode.ClientContext = 1  # QPC clock
        self.props.Wnode.Flags = 0x00020000  # WNODE_FLAG_TRACED_GUID
        self.props.BufferSize = 64
        self.props.MinimumBuffers = self.props.MaximumBuffers = 128
        self.props.LogFileMode = 0x02000100  # SYSTEM_LOGGER | REAL_TIME
        self.props.FlushTimer = 1
        self.props.EnableFlags = 0x10010000  # NETWORK_TCPIP | NO_SYSCONFIG
        self.props.LoggerNameOffset = C.sizeof(PROPERTIES)
        self.session = C.c_uint64()
        # Descriptive singleton name. An existing unowned session is an error;
        # never stop/query it as though it belonged to this collector.
        status = self.api.StartTraceW(C.byref(self.session), self.name, C.byref(self.props))
        if status:
            raise OSError(status, f'StartTraceW failed ({status}): {C.FormatError(status).strip()}')
        self.created = True
        self.providers = {uuid.UUID('bf3a50c5-a9c9-4988-a005-2df0b7c80f80').bytes_le: 'UDP',
                          uuid.UUID('9a280ac0-c8e0-11d1-84e2-00c04fb998a2').bytes_le: 'TCP'}
        self.callback = C.WINFUNCTYPE(None, C.POINTER(RECORD))(self._event)
        self.log = LOGFILE()
        self.log.LoggerName = self.name
        self.log.ProcessTraceMode = 0x10001100  # RAW_TIMESTAMP | REAL_TIME | EVENT_RECORD
        self.log.EventRecordCallback = C.cast(self.callback, C.c_void_p)
        self.consumer = C.c_uint64(self.api.OpenTraceW(C.byref(self.log)))
        if self.consumer.value == 0xffffffffffffffff:
            error = C.get_last_error()
            self.api.ControlTraceW(self.session, None, C.byref(self.props), 1)
            self.created = False
            raise C.WinError(error)
        self.thread = threading.Thread(target=self._consume, name='GuLiETW', daemon=True)
        self.thread.start()
        self.start_qpc = qpc()

    def _consume(self):
        status = self.api.ProcessTrace(C.byref(self.consumer), 1, None, None)
        if status not in (0, 1223):
            self.errors.append(f'ProcessTrace failed: {status}')

    def _event(self, value):
        try:
            record = value.contents
            protocol = self.providers.get(record.Header.ProviderId.raw())
            opcode = record.Header.Descriptor.Opcode
            if not protocol or opcode not in (10, 11, 26, 27):
                return
            if record.Header.Descriptor.Version != 2 or record.UserDataLength < 8:
                if 'Unsupported network event schema' not in self.errors:
                    self.errors.append('Unsupported network event schema')
                return
            data = C.string_at(record.UserData, record.UserDataLength)
            pid, size = int.from_bytes(data[:4], 'little'), int.from_bytes(data[4:8], 'little')
            if pid not in self.pids:
                return
            ipv6 = opcode >= 26
            length = 16 if ipv6 else 4
            if len(data) < 8 + 2 * length + 4:
                self.errors.append('Truncated selected-PID network payload')
                return
            dst = str(ipaddress.ip_address(data[8:8+length]))
            src = str(ipaddress.ip_address(data[8+length:8+2*length]))
            offset = 8 + 2 * length
            dport, sport = int.from_bytes(data[offset:offset+2], 'big'), int.from_bytes(data[offset+2:offset+4], 'big')
            if len(self.events) >= self.max_events:
                self.overflow = True
                return
            self.events.append({'qpc': record.Header.TimeStamp, 'pid': pid, 'protocol': protocol,
                                'direction': 'send' if opcode in (10, 26) else 'receive', 'size': size,
                                'source': src, 'destination': dst, 'source_port': sport, 'destination_port': dport,
                                'loopback': ipaddress.ip_address(src).is_loopback and ipaddress.ip_address(dst).is_loopback})
        except Exception as error:
            self.errors.append(f'Network callback: {error}')

    def stop(self):
        if self.stopped:
            return
        self.stopped = True
        self.stop_qpc = qpc()
        status = self.api.ControlTraceW(self.session, None, C.byref(self.props), 1)
        if status:
            self.errors.append(f'Stop owned trace failed: {status}')
        self.api.CloseTrace(self.consumer)
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            self.errors.append('Consumer did not finish')
        self.created = False

    def report(self, begin, end):
        if not self.stopped:
            raise RuntimeError('Stop/flush trace before reading its window')
        selected = [e for e in self.events if begin <= e['qpc'] < end]
        valid = not (self.errors or self.overflow or self.props.EventsLost or self.props.LogBuffersLost
                     or self.props.RealTimeBuffersLost or self.log.EventsLost or self.log.LogfileHeader.BuffersLost)
        valid &= self.start_qpc <= begin < end <= self.stop_qpc
        counts = {}
        for e in selected:
            key = f"{e['pid']}/{e['protocol']}/{e['direction']}"
            count = counts.setdefault(key, {'events': 0, 'bytes': 0, 'loopback_events': 0})
            count['events'] += 1
            count['bytes'] += e['size']
            count['loopback_events'] += int(e['loopback'])
        return {'valid': bool(valid), 'pids': sorted(self.pids), 'qpc_frequency': self.freq, 'begin_qpc': begin, 'end_qpc': end,
                'trace_start_qpc': self.start_qpc, 'trace_stop_qpc': self.stop_qpc, 'counts': counts,
                'overflow': self.overflow, 'events_lost': self.props.EventsLost, 'log_buffers_lost': self.props.LogBuffersLost,
                'realtime_buffers_lost': self.props.RealTimeBuffersLost, 'consumer_events_lost': self.log.EventsLost,
                'consumer_buffers_lost': self.log.LogfileHeader.BuffersLost, 'errors': self.errors, 'events': selected,
                'retained_events': len(self.events),
                'retained_timestamp_range': [min(e['qpc'] for e in self.events), max(e['qpc'] for e in self.events)] if self.events else [],
                'endpoint_definition': 'Native provider endpoints. TCP receive reports local port in sport; UDP receive retains sender sport. Calibration checks each convention.',
                'byte_definition': 'ETW network payload size field; calibrate against socket bytes before interpreting totals'}


def calibrate(report, totals):
    errors = []
    calibration = []
    for (protocol, src, dst), amount in totals.items():
        directions = {}
        for direction in ('send', 'receive'):
            # Observed Windows TCP receive convention is local/remote, while
            # UDP receive uses the datagram sender/receiver pair. Keep native
            # fields intact and explicitly calibrate their attribution.
            source, destination = (dst, src) if protocol == 'TCP' and direction == 'receive' else (src, dst)
            events = [e for e in report['events'] if e['protocol'] == protocol and e['direction'] == direction
                      and e['source_port'] == source and e['destination_port'] == destination and e['loopback']]
            directions[direction] = {'events': len(events), 'bytes': sum(e['size'] for e in events)}
            if not events or directions[direction]['bytes'] != amount:
                errors.append(f'{protocol}/{direction}: socket={amount}, ETW={directions[direction]}')
        calibration.append({'protocol': protocol, 'source_port': src, 'destination_port': dst,
                            'socket_bytes': amount, 'etw': directions})
    return calibration, errors


def publish(path, value):
    pending = path.with_suffix(path.suffix + '.pending')
    pending.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    pending.replace(path)


def control_session(folder):
    """One elevated helper; commands only create/stop its own filtered trace.

    Games and fixtures are launched by a separate, unelevated controller. Every
    capture requires exactly two distinct target PIDs. No command runs code,
    starts other processes or stops another trace session.
    """
    folder = folder.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    trace = None
    active_id = None
    previous = None
    expiry = time.monotonic() + 12 * 3600
    publish(folder / 'helper-ready.json', {'pid': os.getpid(), 'qpc_frequency': frequency(), 'ready': True})
    try:
        while time.monotonic() < expiry:
            if trace and time.monotonic() > capture_expiry:
                trace.stop()
                publish(folder / f'{active_id}-stop-error.json', {'error': 'Capture exceeded 30 minute limit'})
                trace, active_id = None, None
            try:
                request = read_json_shared(folder / 'request.json')
            except FileNotFoundError:
                time.sleep(.025)
                continue
            request_id = request.get('id', '')
            operation = request.get('operation')
            command = (request_id, operation)
            if command == previous:
                time.sleep(.025)
                continue
            previous = command
            if not isinstance(request_id, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', request_id):
                raise RuntimeError('Invalid collector request identity')
            try:
                if operation == 'start':
                    if trace:
                        raise RuntimeError('Stop the active capture before starting another')
                    pids = request.get('pids')
                    if (not isinstance(pids, list) or len(pids) != 2 or len(set(pids)) != 2
                            or any(type(pid) is not int or pid <= 0 or pid == os.getpid() for pid in pids)):
                        raise RuntimeError('Exactly two independent target PIDs are required')
                    trace = NetworkTrace(pids)
                    active_id = request_id
                    capture_expiry = time.monotonic() + 1800
                    publish(folder / f'{request_id}-ready.json', {'ready': True, 'pids': pids, 'trace_start_qpc': trace.start_qpc})
                elif operation == 'stop':
                    if not trace or request_id != active_id:
                        raise RuntimeError('Stop identity does not name the helper-owned active capture')
                    begin, end = request.get('begin_qpc'), request.get('end_qpc')
                    if type(begin) is not int or type(end) is not int:
                        raise RuntimeError('Explicit QPC window required')
                    # Give the live consumer time to drain events timestamped
                    # before the cut. Its flush/loss counters remain a pass gate.
                    time.sleep(1.2)
                    trace.stop()
                    report = trace.report(begin, end)
                    events = report.pop('events')
                    event_file = folder / f'{request_id}-events.jsonl.gz'
                    with gzip.open(event_file, 'wt', encoding='utf-8') as output:
                        for event in events:
                            output.write(json.dumps(event, separators=(',', ':')) + '\n')
                    report['event_file'] = event_file.name
                    publish(folder / f'{request_id}-report.json', report)
                    trace, active_id = None, None
                elif operation == 'shutdown':
                    break
                else:
                    raise RuntimeError('Unknown collector operation')
            except Exception as error:
                publish(folder / f'{request_id}-{operation}-error.json', {'error': str(error), 'pid': os.getpid()})
    finally:
        if trace:
            trace.stop()
        publish(folder / 'helper-exited.json', {'pid': os.getpid(), 'exited': True})


def preflight(output):
    result = {'passed': False, 'collector_ready': False, 'loopback_calibrated': False, 'pid': os.getpid()}
    trace = None
    try:
        trace = NetworkTrace([os.getpid()])
        result['collector_ready'] = True
        # Round-trip both protocols on authored local ports. Each socket send /
        # receive has a recorded byte count to detect missing loopback events.
        time.sleep(.1)
        begin = qpc()
        expected = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            receiver.bind(('127.0.0.1', 0))
            receiver.settimeout(2)
            sender.bind(('127.0.0.1', 0))
            for size in (127, 499, 999):
                payload = bytes(size)
                sent = sender.sendto(payload, receiver.getsockname())
                received, peer = receiver.recvfrom(2048)
                if len(received) != sent or peer != sender.getsockname():
                    raise RuntimeError('UDP fixture changed socket counts or identity')
                expected.append({'protocol': 'UDP', 'source_port': sender.getsockname()[1],
                                 'destination_port': receiver.getsockname()[1], 'socket_bytes': sent})
        with socket.socket() as listener, socket.socket() as sender:
            listener.bind(('127.0.0.1', 0));listener.listen(1)
            sender.settimeout(2);sender.connect(listener.getsockname())
            receiver, peer = listener.accept()
            with receiver:
                receiver.settimeout(2)
                for size in (137, 509, 997):
                    sender.sendall(bytes(size))
                    remaining = size
                    while remaining:
                        data = receiver.recv(remaining)
                        if not data:
                            raise RuntimeError('TCP calibration closed early')
                        remaining -= len(data)
                    expected.append({'protocol': 'TCP', 'source_port': sender.getsockname()[1],
                                     'destination_port': receiver.getsockname()[1], 'socket_bytes': size})
        end = qpc()
        time.sleep(1.2)
        trace.stop()
        report = trace.report(begin, end)
        totals = {}
        for item in expected:
            key = (item['protocol'], item['source_port'], item['destination_port'])
            totals[key] = totals.get(key, 0) + item['socket_bytes']
        calibration, errors = calibrate(report, totals)
        result.update(trace=report, calibration=calibration, calibration_errors=errors,
                      loopback_calibrated=bool(report['valid'] and not errors), passed=bool(report['valid'] and not errors))
    except Exception as error:
        result['error'] = str(error)
        result['winerror'] = getattr(error, 'winerror', None) or getattr(error, 'errno', None)
    finally:
        if trace and not trace.stopped:
            trace.stop()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--preflight', type=Path)
    mode.add_argument('--control', type=Path)
    arguments = parser.parse_args()
    if arguments.control:
        control_session(arguments.control)
        raise SystemExit(0)
    result = preflight(arguments.preflight)
    print(json.dumps({k: v for k, v in result.items() if k not in ('trace', 'calibration')}, ensure_ascii=False))
    raise SystemExit(0 if result['passed'] else 1)

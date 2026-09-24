"""Temporary local diagnostic transport; runs UE calls on the editor thread.

Expires after 60 minutes and is explicitly stopped after diagnosis. No settings
or startup files are changed. Replaces the failed listener in this one session.
"""
import contextlib
import io
import json
import os
import queue
import socket
import threading
import time
import traceback
import unreal


class SessionDiagnosticBridge:
    def __init__(self):
        self.pending = queue.Queue()
        self.closed = False
        self.expires = time.monotonic() + 3600
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 12029))
        self.listener.listen(2)
        self.listener.settimeout(1)
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        self.thread = threading.Thread(target=self.listen, daemon=True)
        self.thread.start()

    def listen(self):
        while not self.closed:
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with client:
                client.settimeout(5)
                try:
                    raw = b''
                    while len(raw) < 1048576:
                        chunk = client.recv(65536)
                        if not chunk:
                            break
                        raw += chunk
                        try:
                            request = json.loads(raw)
                            break
                        except (ValueError, UnicodeDecodeError):
                            continue
                    else:
                        raise ValueError('Diagnostic request is too large')
                    if request.get('type') != 'python':
                        raise ValueError('Only the existing Python diagnostic protocol is supported')
                    job = {'code': request['code'], 'ready': threading.Event()}
                    self.pending.put(job)
                    if not job['ready'].wait(30):
                        raise TimeoutError('Editor did not process diagnostic request')
                    client.sendall(json.dumps(job['result']).encode())
                except Exception as exc:
                    try:
                        client.sendall(json.dumps({'success': False, 'message': str(exc)}).encode())
                    except OSError:
                        pass

    def tick(self, delta):
        if self.closed or time.monotonic() > self.expires:
            self.stop()
            return
        try:
            job = self.pending.get_nowait()
        except queue.Empty:
            return
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                exec(job['code'], {'__name__': '__diagnostic__', 'unreal': unreal})
            job['result'] = {'success': True, 'pid': os.getpid(), 'output': output.getvalue()}
        except Exception:
            job['result'] = {'success': False, 'pid': os.getpid(), 'output': output.getvalue(), 'error': traceback.format_exc()}
        job['ready'].set()

    def stop(self):
        if self.closed:
            return
        self.closed = True
        self.listener.close()
        unreal.unregister_slate_post_tick_callback(self.handle)


if not getattr(unreal, '_codex_nav_diagnostic_bridge', None) or unreal._codex_nav_diagnostic_bridge.closed:
    unreal._codex_nav_diagnostic_bridge = SessionDiagnosticBridge()
    print('GULI_DIAGNOSTIC_INTERFACE_READY pid=' + str(os.getpid()))

"""Run an authoring script in the existing UE editor; never launches gameplay."""
import argparse
import json
import socket
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('script', type=Path)
parser.add_argument('--arguments', default='{}')
args = parser.parse_args()
source = args.script.read_text(encoding='utf8')
context = dict(commander_lod_arguments=json.loads(args.arguments), __file__=str(args.script.resolve()),
    __name__='commander_lod_authoring')
code = ('import json, traceback, unreal\n'
    'try:\n    exec(compile(' + repr(source) + ', ' + repr(str(args.script)) + ", 'exec'), " + repr(context) + ')\n'
    'except Exception:\n    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=False,traceback=traceback.format_exc())))\n')
with socket.create_connection(('127.0.0.1', 12029), timeout=8) as sock:
    sock.settimeout(240)
    sock.sendall(json.dumps({'type': 'python', 'code': code}).encode('utf8'))
    output = []
    while True:
        part = sock.recv(65536)
        if not part:
            break
        output.append(part)
raw = b''.join(output).decode('utf8', errors='replace')
try:
    result = json.loads(raw)
    if isinstance(result.get('result'), str):
        try:
            result['result'] = json.loads(result['result'])
        except json.JSONDecodeError:
            pass
    print(json.dumps(result, ensure_ascii=False, indent=2))
    inner = result.get('result')
    if not result.get('success', result.get('status') == 'success') or (isinstance(inner,dict) and inner.get('success') is False):
        raise SystemExit(1)
except json.JSONDecodeError:
    print(raw)
    raise SystemExit(1)

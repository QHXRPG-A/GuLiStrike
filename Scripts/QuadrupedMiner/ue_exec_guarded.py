"""Run one owned editor script with a machine-readable exception result."""
import json
import socket
import sys
from pathlib import Path

path = Path(sys.argv[1]).resolve()
source = path.read_text(encoding='utf8')
if source.lstrip().startswith('"""'):
    source = source.split('"""', 2)[2]
code = '''import json
import traceback
try:
    exec(compile(%r, %r, "exec"), {"__name__": "__main__", "__file__": %r})
except Exception:
    print(json.dumps({"success": False, "traceback": traceback.format_exc()}, ensure_ascii=False))
''' % (source, str(path), str(path))
with socket.create_connection(('127.0.0.1', 12029), timeout=60) as connection:
    connection.settimeout(300)
    connection.sendall(json.dumps({'type': 'python', 'code': code}).encode())
    data = bytearray()
    while True:
        try:
            chunk = connection.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        data.extend(chunk)
text = data.decode(errors='replace')
print(text)
result = json.loads(text)
nested = result.get('result')
if isinstance(nested, str) and nested.lstrip().startswith('{'):
    nested_result = json.loads(nested)
    if nested_result.get('success') is False:
        sys.exit(1)
if not result.get('success', False):
    sys.exit(1)

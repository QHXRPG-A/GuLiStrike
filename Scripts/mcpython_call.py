#!/usr/bin/env python3
"""Call a GenOrca UnrealMCPython action over TCP (127.0.0.1:12029).

Usage: python mcpython_call.py <module> <function> [json_args]
  e.g. python mcpython_call.py actor_actions ue_spawn_from_class {"class_path":"/Game/.../BP.BP_C","location":[0,0,100]}
"""
import json
import socket
import sys

HOST, PORT = "127.0.0.1", 12029


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    module, function = sys.argv[1], sys.argv[2]
    args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}

    cmd = {"type": "python_call", "module": module, "function": function, "args": args}
    with socket.create_connection((HOST, PORT), timeout=60) as sock:
        sock.settimeout(300)
        sock.sendall(json.dumps(cmd, ensure_ascii=False).encode("utf-8"))
        buf = b""
        while True:
            try:
                chunk = sock.recv(16384)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk

    try:
        print(json.dumps(json.loads(buf.decode("utf-8")), indent=2, ensure_ascii=False))
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(buf.decode("utf-8", errors="replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

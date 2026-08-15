#!/usr/bin/env python3
"""Execute Python code in the live UE editor via UnrealMCPython TCP (127.0.0.1:12029).

Usage: python ue_exec.py <code-file.py>   (or: echo "code" | python ue_exec.py -)
Results: prints raw response; scripts should write JSON to a file for reliable capture.
"""
import json
import socket
import sys

HOST, PORT = "127.0.0.1", 12029


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    src = sys.stdin.read() if sys.argv[1] == "-" else open(sys.argv[1], encoding="utf-8").read()
    code = src if not src.lstrip().startswith('"""') else src.split('"""', 2)[2]

    cmd = {"type": "python", "code": code}
    with socket.create_connection((HOST, PORT), timeout=60) as s:
        s.settimeout(300)
        s.sendall(json.dumps(cmd).encode())
        buf = b""
        while True:
            try:
                c = s.recv(65536)
            except socket.timeout:
                break
            if not c:
                break
            buf += c
    print(buf.decode(errors="replace")[:3000])
    return 0


if __name__ == "__main__":
    sys.exit(main())

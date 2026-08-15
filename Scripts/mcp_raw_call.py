#!/usr/bin/env python3
"""Send a raw command to the UnrealMCP plugin over TCP (127.0.0.1:55557).

Usage: python mcp_raw_call.py <command_type> [json_params]
  e.g. python mcp_raw_call.py spawn_blueprint_actor {"blueprint_name":"BP_TwinStickSpawner","actor_name":"Spawner_NE","location":[2000,2000,200]}
"""
import json
import socket
import sys

HOST, PORT = "127.0.0.1", 55557


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    command = sys.argv[1]
    params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}

    payload = json.dumps({"type": command, "params": params}).encode("utf-8")
    with socket.create_connection((HOST, PORT), timeout=10) as sock:
        sock.settimeout(120)
        sock.sendall(payload)
        chunks = []
        while True:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            chunks.append(chunk)

    raw = b"".join(chunks)
    try:
        print(json.dumps(json.loads(raw.decode("utf-8")), indent=2, ensure_ascii=False))
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(raw.decode("utf-8", errors="replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

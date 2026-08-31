"""Run an explicitly supplied Python snippet in the local UnrealMCPython editor.

This transport does not invoke VibeUE or auto-save packages. The snippet owns
its side effects and should finish with MCPythonHelper.submit_result(JSON).
"""

import argparse
import json
import socket
from pathlib import Path


def call_editor(code: str, timeout: float = 45.0):
    wrapped_code = (
        "import unreal, json, traceback\n"
        "try:\n"
        # UE's Python console parser can treat literal '.py' inside a long snippet
        # as a filename. A hex envelope keeps docs/paths out of that parser.
        f"    exec(bytes.fromhex('{code.encode('utf-8').hex()}').decode('utf-8'), globals())\n"
        "except Exception:\n"
        "    unreal.MCPythonHelper.submit_result(json.dumps({"
        "'success': False, 'traceback': traceback.format_exc()}))\n"
    )
    payload = json.dumps({"type": "python", "code": wrapped_code}).encode("utf-8")
    with socket.create_connection(("127.0.0.1", 12029), timeout=5.0) as client:
        client.settimeout(timeout)
        client.sendall(payload)
        received = bytearray()
        while True:
            chunk = client.recv(65536)
            if not chunk:
                break
            received.extend(chunk)
            try:
                response = json.loads(received.decode("utf-8"))
                break
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
    if not received:
        raise RuntimeError("UnrealMCPython returned an empty response")
    response = json.loads(received.decode("utf-8"))
    if isinstance(response.get("result"), str):
        try:
            response["result"] = json.loads(response["result"])
        except json.JSONDecodeError:
            pass
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--file", type=Path)
    source.add_argument("--code")
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    code = args.file.read_text(encoding="utf-8-sig") if args.file else args.code
    response = call_editor(code, args.timeout)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(response, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(response, ensure_ascii=True, indent=2))
    result = response.get("result")
    failed_result = isinstance(result, dict) and result.get("success") is False
    return 0 if response.get("success") and not failed_result else 1


if __name__ == "__main__":
    raise SystemExit(main())

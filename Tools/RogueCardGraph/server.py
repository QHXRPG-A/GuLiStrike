"""Local web entry point. Only the design draft is writable."""

import argparse
import hashlib
import json
import tempfile
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from graph import DRAFT_PATH, PROJECT, describe_changes, initial_graph, load_catalog, validate_graph

APP = "guli-rogue-card-graph"
ROOT = Path(__file__).resolve().parent
STATIC = {
    "/": ("index.html", "text/html"),
    "/style.css": ("style.css", "text/css"),
    "/app.js": ("app.js", "text/javascript"),
}


def read_draft(path):
    if not path.exists():
        return None, None
    data = path.read_bytes()
    saved = json.loads(data)
    if not isinstance(saved, dict) or saved.get("schema") != 1:
        raise ValueError("草稿版本无法读取。")
    return saved, hashlib.sha256(data).hexdigest()


def write_draft(path, saved):
    data = (json.dumps(saved, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, suffix=".tmp", delete=False) as file:
            temporary = Path(file.name)
            file.write(data)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return hashlib.sha256(data).hexdigest()


class GraphServer(ThreadingHTTPServer):
    def __init__(self, port, draft_path):
        super().__init__(("127.0.0.1", port), Handler)
        self.origin = f"http://127.0.0.1:{self.server_port}"
        self.draft_path = draft_path
        self.save_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_request(self, code="-", size="-"):
        if self.path != "/api/health" or code != 200:
            super().log_request(code, size)

    def accepts_host(self):
        if self.headers.get("Host") == self.server.origin.removeprefix("http://"):
            return True
        self.send_json(403, {"error": "请用启动工具显示的本地地址访问。"})
        return False

    def send_bytes(self, status, content_type, data):
        self.send_response(status)
        self.send_header("Content-Type", f"{content_type}; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, status, payload):
        self.send_bytes(status, "application/json", json.dumps(payload, ensure_ascii=False).encode("utf-8"))

    def do_GET(self):
        if not self.accepts_host():
            return
        if self.path == "/api/health":
            self.send_json(200, {"app": APP, "project": str(PROJECT)})
        elif self.path == "/api/graph":
            try:
                latest = load_catalog()
                saved, revision = read_draft(self.server.draft_path)
                base = saved["base"] if saved else latest
                graph = validate_graph(saved["graph"], base) if saved else initial_graph(base)
                response = {
                    "base": base, "graph": graph, "revision": revision,
                    "savedAt": saved["savedAt"] if saved else None,
                    "sourceChanged": base["fingerprint"] != latest["fingerprint"],
                }
            except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
                self.send_json(500, {"error": f"无法读取卡表或草稿：{error}"})
                return
            self.send_json(200, response)
        elif self.path in STATIC:
            name, content_type = STATIC[self.path]
            self.send_bytes(200, content_type, (ROOT / name).read_bytes())
        else:
            self.send_error(404)

    def do_POST(self):
        if not self.accepts_host():
            return
        if self.path != "/api/graph":
            self.send_error(404)
            return
        origin = self.headers.get("Origin")
        if origin and origin != self.server.origin:
            self.send_json(403, {"error": "请在工具启动的本地页面中保存。"})
            return
        if self.headers.get_content_type() != "application/json":
            self.send_json(415, {"error": "草稿必须以 JSON 提交。"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 6 * 1024 * 1024:
                raise ValueError("草稿请求大小无效。")
            request = json.loads(self.rfile.read(length))
            if not isinstance(request, dict):
                raise ValueError("草稿请求无效。")
        except ValueError as error:
            self.send_json(400, {"error": str(error)})
            return

        with self.server.save_lock:
            try:
                saved, revision = read_draft(self.server.draft_path)
                latest = load_catalog()
                base = saved["base"] if saved else latest
            except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
                self.send_json(500, {"error": f"无法读取保存基线：{error}"})
                return
            if request.get("revision") != revision:
                self.send_json(409, {"error": "另一页已经保存草稿。请重新打开工具查看最新版本。"})
                return
            if request.get("baseFingerprint") != base["fingerprint"]:
                self.send_json(409, {"error": "卡表基线已更新，请重新打开工具核对。"})
                return
            try:
                graph = validate_graph(request.get("graph"), base)
            except ValueError as error:
                self.send_json(422, {"error": str(error)})
                return
            updated = {
                "schema": 1,
                "savedAt": datetime.now(timezone.utc).isoformat(),
                "base": base, "graph": graph, "changes": describe_changes(base, graph),
            }
            try:
                revision = write_draft(self.server.draft_path, updated)
            except OSError as error:
                self.send_json(500, {"error": f"保存失败：{error}"})
                return
        self.send_json(200, {
            "revision": revision, "savedAt": updated["savedAt"], "changes": updated["changes"],
            "sourceChanged": base["fingerprint"] != latest["fingerprint"],
        })


def main():
    parser = argparse.ArgumentParser(description="GuLiStrike 肉鸽卡牌节点图")
    parser.add_argument("--port", type=int, default=4322)
    parser.add_argument("--draft", type=Path, default=DRAFT_PATH, help="指定草稿文件")
    args = parser.parse_args()
    with GraphServer(args.port, args.draft.resolve()) as server:
        print(server.origin, flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()

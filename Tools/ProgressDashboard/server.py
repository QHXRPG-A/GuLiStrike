#!/usr/bin/env python3
"""Local-only, read-only HTTP server for the GuLiStrike progress dashboard."""

from __future__ import annotations

import argparse
import importlib.util
import json
import mimetypes
import re
import sys
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, unquote, urlsplit


DASHBOARD_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = DASHBOARD_ROOT.parents[1]
STATIC_ROOT = DASHBOARD_ROOT / "dist" / "client"
PROGRESS_TOOL = PROJECT_ROOT / ".agents" / "skills" / "gulistrike-progress" / "scripts" / "progress_docs.py"
DOCUMENT_ID = re.compile(r"^[A-Za-z0-9-]+$")


def load_progress_module() -> Any:
    spec = importlib.util.spec_from_file_location("guli_progress_docs", PROGRESS_TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"无法加载进度工具：{PROGRESS_TOOL}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


progress_docs = load_progress_module()


class ProgressCache:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._signature: tuple[int, int, int] | None = None
        self._snapshot: dict[str, Any] = {}
        self._documents: dict[str, Any] = {}
        self._search_text: dict[str, str] = {}

    def _current_signature(self) -> tuple[int, int, int]:
        paths = list((PROJECT_ROOT / "Progress").rglob("*.md"))
        if not paths:
            return (0, 0, 0)
        stats = [path.stat() for path in paths]
        return (max(stat.st_mtime_ns for stat in stats), len(stats), sum(stat.st_size for stat in stats))

    def _refresh(self) -> None:
        signature = self._current_signature()
        if signature == self._signature:
            return
        documents = progress_docs.load_documents(PROJECT_ROOT)
        snapshot = progress_docs.build_snapshot(PROJECT_ROOT)
        by_id: dict[str, Any] = {}
        search_text: dict[str, str] = {}
        for document in documents:
            document_id = str(document.metadata.get("id", ""))
            if not document_id:
                continue
            by_id[document_id] = document
            meta = document.metadata
            search_text[document_id] = "\n".join(
                [
                    document_id,
                    str(meta.get("work_id", "")),
                    str(meta.get("title", "")),
                    str(meta.get("summary", "")),
                    str(meta.get("status_note", "")),
                    str(meta.get("next_action", "")),
                    " ".join(meta.get("areas", []) or []),
                    document.body,
                ]
            ).casefold()
        self._snapshot = snapshot
        self._documents = by_id
        self._search_text = search_text
        self._signature = signature

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            self._refresh()
            return self._snapshot

    def document(self, document_id: str) -> dict[str, Any] | None:
        with self._lock:
            self._refresh()
            document = self._documents.get(document_id)
            return document.public(include_body=True) if document else None

    def search(self, query: str, kind: str, status: str, area: str, limit: int) -> list[dict[str, Any]]:
        with self._lock:
            self._refresh()
            needle = query.casefold().strip()
            results: list[dict[str, Any]] = []
            for document_id, document in self._documents.items():
                metadata = document.metadata
                if kind and metadata.get("kind") != kind:
                    continue
                if status and metadata.get("status") != status:
                    continue
                if area and area not in (metadata.get("areas") or []):
                    continue
                haystack = self._search_text[document_id]
                if needle and needle not in haystack:
                    continue
                title = str(metadata.get("title", ""))
                summary = str(metadata.get("summary", ""))
                score = 0
                if needle:
                    score += 20 if needle in title.casefold() else 0
                    score += 10 if needle in summary.casefold() else 0
                    score += min(haystack.count(needle), 8)
                excerpt = summary
                if needle and needle in document.body.casefold():
                    folded = document.body.casefold()
                    position = folded.index(needle)
                    start = max(0, position - 90)
                    end = min(len(document.body), position + len(query) + 150)
                    excerpt = re.sub(r"\s+", " ", document.body[start:end]).strip()
                    if start:
                        excerpt = "…" + excerpt
                    if end < len(document.body):
                        excerpt += "…"
                public = document.public()
                public["score"] = score
                public["excerpt"] = excerpt
                results.append(public)
            results.sort(key=lambda item: (item["score"], str(item.get("updated", ""))), reverse=True)
            return results[:limit]


CACHE = ProgressCache()


class DashboardHandler(BaseHTTPRequestHandler):
    server_version = "GuLiProgress/1.0"

    def _headers(self, status: HTTPStatus, content_type: str, length: int) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(length))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
            "script-src 'self' 'unsafe-inline'; connect-src 'self'; object-src 'none'; base-uri 'none'",
        )
        self.end_headers()

    def _json(self, value: Any, status: HTTPStatus = HTTPStatus.OK) -> None:
        payload = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._headers(status, "application/json; charset=utf-8", len(payload))
        self.wfile.write(payload)

    def _method_not_allowed(self) -> None:
        self._json({"error": "read_only", "message": "此服务仅允许 GET 请求。"}, HTTPStatus.METHOD_NOT_ALLOWED)

    def do_POST(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_PUT(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_PATCH(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_DELETE(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_OPTIONS(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlsplit(self.path)
        try:
            if parsed.path == "/api/health":
                snapshot = CACHE.snapshot()
                self._json({"ok": True, "read_only": True, "revision": snapshot.get("revision")})
                return
            if parsed.path == "/api/snapshot":
                self._json(CACHE.snapshot())
                return
            if parsed.path == "/api/search":
                query = parse_qs(parsed.query)
                text = query.get("q", [""])[0][:200]
                kind = query.get("kind", [""])[0][:40]
                status = query.get("status", [""])[0][:40]
                area = query.get("area", [""])[0][:80]
                try:
                    limit = min(max(int(query.get("limit", ["50"])[0]), 1), 100)
                except ValueError:
                    limit = 50
                self._json({"query": text, "results": CACHE.search(text, kind, status, area, limit)})
                return
            if parsed.path.startswith("/api/documents/"):
                raw_id = parsed.path.removeprefix("/api/documents/")
                document_id = unquote(raw_id)
                if not DOCUMENT_ID.fullmatch(document_id):
                    self._json({"error": "invalid_document_id"}, HTTPStatus.BAD_REQUEST)
                    return
                document = CACHE.document(document_id)
                if document is None:
                    self._json({"error": "not_found"}, HTTPStatus.NOT_FOUND)
                    return
                self._json(document)
                return
            if parsed.path.startswith("/api/"):
                self._json({"error": "not_found"}, HTTPStatus.NOT_FOUND)
                return
            self._static(parsed.path)
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception as error:  # keep local diagnostics useful without exposing a traceback to the browser
            self.log_error("request failed: %s", error)
            self._json({"error": "internal_error", "message": str(error)}, HTTPStatus.INTERNAL_SERVER_ERROR)

    def _static(self, request_path: str) -> None:
        if not STATIC_ROOT.is_dir():
            self._json(
                {"error": "frontend_not_built", "message": "请先在 Tools/ProgressDashboard 运行 npm run build。"},
                HTTPStatus.SERVICE_UNAVAILABLE,
            )
            return
        relative = unquote(request_path).lstrip("/") or "index.html"
        candidate = (STATIC_ROOT / relative).resolve()
        try:
            candidate.relative_to(STATIC_ROOT.resolve())
        except ValueError:
            self._json({"error": "invalid_path"}, HTTPStatus.BAD_REQUEST)
            return
        if candidate.is_dir():
            candidate = candidate / "index.html"
        if not candidate.is_file():
            candidate = STATIC_ROOT / "index.html"
        payload = candidate.read_bytes()
        mime_type = mimetypes.guess_type(candidate.name)[0] or "application/octet-stream"
        if mime_type.startswith("text/") or mime_type in {"application/javascript", "application/json"}:
            mime_type += "; charset=utf-8"
        self._headers(HTTPStatus.OK, mime_type, len(payload))
        self.wfile.write(payload)

    def log_message(self, format_string: str, *args: Any) -> None:
        print(f"[{self.log_date_time_string()}] {format_string % args}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=4317)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), DashboardHandler)
    print(f"GuLiStrike Progress Dashboard: http://127.0.0.1:{args.port}")
    print("只读服务已启动；按 Ctrl+C 停止。")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务已停止。")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

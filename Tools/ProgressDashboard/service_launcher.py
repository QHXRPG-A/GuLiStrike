#!/usr/bin/env python3
"""任务计划程序入口：无窗口常驻运行 ProgressDashboard 服务。

- 幂等：如果 127.0.0.1:4317 已经健康，直接退出，避免重复实例。
- 日志：stdout/stderr 追加写入 service.log，超过 2MB 轮转为 service.log.old。
"""

from __future__ import annotations

import os
import runpy
import sys
import urllib.error
import urllib.request
from pathlib import Path

PORT = 4317
HERE = Path(__file__).resolve().parent
LOG_PATH = HERE / "service.log"
LOG_LIMIT = 2 * 1024 * 1024


def already_running() -> bool:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/api/health", timeout=2) as response:
            return response.status == 200
    except (OSError, urllib.error.URLError):
        return False


def main() -> int:
    if already_running():
        return 0
    if LOG_PATH.exists() and LOG_PATH.stat().st_size > LOG_LIMIT:
        LOG_PATH.replace(LOG_PATH.with_suffix(".log.old"))
    log = open(LOG_PATH, "a", buffering=1, encoding="utf-8", errors="replace")
    sys.stdout = log
    sys.stderr = log
    os.chdir(HERE)
    print(f"service_launcher: starting on port {PORT}, pid={os.getpid()}")
    sys.argv = [str(HERE / "server.py"), "--port", str(PORT)]
    try:
        runpy.run_path(sys.argv[0], run_name="__main__")
    except SystemExit as exit_request:
        code = exit_request.code
        return code if isinstance(code, int) else (0 if code is None else 1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

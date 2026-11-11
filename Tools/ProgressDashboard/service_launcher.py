#!/usr/bin/env python3
"""Keep the three local GuLiStrike web tools available from one scheduled task."""

from __future__ import annotations

import argparse
import http.client
import json
import logging
import os
import runpy
import socket
import subprocess
import sys
import threading
import time
import _winapi
from dataclasses import dataclass, field
from logging.handlers import RotatingFileHandler
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
LOG_LIMIT = 2 * 1024 * 1024
POLL_INTERVAL = 5
FAILED_PROBES_BEFORE_RESTART = 3


@dataclass
class Tool:
    name: str
    port: int
    app: str
    frontend: str
    endpoint: str = "/api/health"
    location_key: str = "project"
    location: Path = PROJECT
    worker: subprocess.Popen[bytes] | None = field(default=None, init=False)
    failed_probes: int = field(default=0, init=False)
    status: str = field(default="", init=False)

    @property
    def directory(self) -> Path:
        return PROJECT / "Tools" / self.name

    def report(self, status: str) -> None:
        if self.status != status:
            logging.info("%s on %s: %s", self.name, self.port, status)
            self.status = status

    def port_available(self) -> bool:
        # On Windows a connection to an unused loopback port can time out.
        try:
            with socket.socket() as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                listener.bind(("127.0.0.1", self.port))
        except OSError:
            return False
        return True

    def ready(self) -> bool:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
        try:
            connection.request("GET", self.endpoint)
            response = connection.getresponse()
            if response.status != 200:
                return False
            payload = json.loads(response.read(4096))
        except (OSError, http.client.HTTPException, ValueError):
            return False
        finally:
            connection.close()

        if not isinstance(payload, dict) or payload.get("app") != self.app:
            return False
        location = payload.get(self.location_key)
        return isinstance(location, str) and Path(location) == self.location

    def start(self) -> None:
        for name in ("server.py", self.frontend):
            if not (self.directory / name).is_file():
                self.report(f"missing {name}; prepare this tool with its start script")
                return

        log_path = self.directory / "keepalive-worker.log"
        try:
            if log_path.exists() and log_path.stat().st_size > LOG_LIMIT:
                log_path.replace(log_path.with_suffix(".previous.log"))
            with log_path.open("ab") as log:
                self.worker = subprocess.Popen(
                    [sys.executable, "-B", "-u", "-X", "utf8", str(Path(__file__).resolve()),
                     "--worker", self.name],
                    cwd=self.directory,
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
        except OSError as error:
            self.report(f"could not start: {error}")
            return
        self.report(f"started worker PID {self.worker.pid}")

    def stop(self) -> None:
        if self.worker is None:
            return
        worker = self.worker
        self.worker = None
        self.failed_probes = 0
        worker.terminate()
        worker.wait(timeout=3)

    def maintain(self) -> None:
        if self.worker is not None and (code := self.worker.poll()) is not None:
            self.report(f"worker exited with code {code}")
            self.stop()

        if self.worker is None and self.port_available():
            self.start()
        elif self.ready():
            self.failed_probes = 0
            self.report("ready" if self.worker else "ready (already running)")
        elif self.worker is not None:
            self.failed_probes += 1
            if self.failed_probes >= FAILED_PROBES_BEFORE_RESTART:
                self.report("worker failed repeated health checks; restarting")
                self.stop()
        else:
            self.report("port is unavailable and has no matching healthy service; waiting")


TOOLS = (
    Tool("ProgressDashboard", 4317, "guli-progress-dashboard", "dist/client/index.html", "/api/ready"),
    Tool("MapDesigner", 4321, "guli-map-designer", "dist/index.html",
         location_key="data_directory", location=PROJECT / "ArtSource" / "MapDesigner"),
    Tool("RogueCardGraph", 4322, "guli-rogue-card-graph", "index.html"),
)


def run_worker(tool: Tool) -> None:
    supervisor = _winapi.OpenProcess(_winapi.SYNCHRONIZE, False, os.getppid())

    def exit_when_supervisor_stops() -> None:
        # Waiting on the process keeps a blocking stdin read out of DLL initialization.
        try:
            _winapi.WaitForSingleObject(supervisor, _winapi.INFINITE)
        finally:
            _winapi.CloseHandle(supervisor)
            os._exit(0)

    threading.Thread(target=exit_when_supervisor_stops, daemon=True).start()
    sys.path.insert(0, str(tool.directory))
    sys.argv = [str(tool.directory / "server.py"), "--port", str(tool.port)]
    runpy.run_path(sys.argv[0], run_name="__main__")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", choices=[tool.name for tool in TOOLS], help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        run_worker(next(tool for tool in TOOLS if tool.name == args.worker))
        return 0

    handler = RotatingFileHandler(HERE / "keepalive.log", maxBytes=LOG_LIMIT, backupCount=1, encoding="utf-8")
    handler.namer = lambda name: f"{name}.log"
    logging.basicConfig(handlers=[handler], level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    logging.info("Supervisor started, PID %s", os.getpid())
    try:
        while True:
            for tool in TOOLS:
                tool.maintain()
            time.sleep(POLL_INTERVAL)
    except KeyboardInterrupt:
        pass
    except Exception:
        logging.exception("Supervisor failed")
        return 1
    finally:
        for tool in TOOLS:
            tool.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

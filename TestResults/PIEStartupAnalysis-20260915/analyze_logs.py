"""Summarize PIE startup from two explicitly named text logs; no directory scan."""

from __future__ import annotations

import hashlib
import json
import re
from datetime import datetime, timedelta
from pathlib import Path


OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
INPUTS = {
    "current-editor": ROOT / "Saved/Logs/GuLiStrike.log",
    "earlier-editor": ROOT / "TestResults/EngineeringAvoidance/PIE-final-native.log",
}
STAMP = re.compile(r"^\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]")


def stamp(line: str) -> datetime:
    match = STAMP.match(line)
    if not match:
        raise ValueError(f"No timestamp: {line[:100]}")
    return datetime.strptime(match.group(1), "%Y.%m.%d-%H.%M.%S:%f")


def summarize(label: str, path: Path) -> dict:
    raw = path.read_bytes()  # Only the two known .log text files above.
    snapshot = OUT / f"{label}.log"
    snapshot.write_bytes(raw)
    lines = raw.decode("utf-8-sig", errors="replace").splitlines()

    def event(fragment: str, begin: int = 0) -> dict:
        for index in range(begin, len(lines)):
            if fragment in lines[index]:
                return {"line": index + 1, "utc": stamp(lines[index]).isoformat(),
                        "text": lines[index]}
        raise ValueError(f"Missing event {fragment!r} in {path}")

    resource_init = event("waiting for dynamic navigation.")
    resource_ready = event("Resource world and navigation are Ready")
    spawned = event("independent server-authoritative Mass Soldiers")
    host_ready = event("Commander bootstrap ready:")
    client_ready = event("Commander bootstrap ready:", host_ready["line"])
    pie_announcements = [
        {"line": i + 1, "utc": stamp(line).isoformat(), "text": line}
        for i, line in enumerate(lines)
        if "PIE: PIE" in line and re.search(r"[\d.]+秒", line)
    ]
    server_announcement = pie_announcements[0]
    explicit_start = [
        {"line": i + 1, "utc": stamp(line).isoformat(), "text": line}
        for i, line in enumerate(lines[:resource_init["line"]])
        if "LogDebuggerCommands: Repeating last play command" in line
    ]
    if explicit_start:
        start = datetime.fromisoformat(explicit_start[-1]["utc"])
        start_basis = "Explicit editor play-command log marker"
    else:
        seconds = float(re.search(r"([\d.]+)秒", server_announcement["text"]).group(1))
        start = datetime.fromisoformat(server_announcement["utc"]) - timedelta(seconds=seconds)
        start_basis = "Inferred from UE's reported total PIE startup duration"

    def delta(a: datetime | dict, b: dict) -> float:
        left = a if isinstance(a, datetime) else datetime.fromisoformat(a["utc"])
        return round((datetime.fromisoformat(b["utc"]) - left).total_seconds(), 3)

    startup_lines = [line for line in lines if STAMP.match(line)
                     and start <= stamp(line) <= datetime.fromisoformat(client_ready["utc"])]
    compile_counts = {
        kind: sum("LogAsyncCompilation" in line and f"[{kind}]" in line
                  for line in startup_lines)
        for kind in ["TextureDerivedData", "StaticMesh"]
    }
    events = {
        "resource_initialized": resource_init, "resource_navigation_ready": resource_ready,
        "soldiers_spawned": spawned, "server_bootstrap_ready": host_ready,
        "client_bootstrap_ready": client_ready, "pie_announcements": pie_announcements,
    }
    for name, fragment in {
        "blueprint_check": "No blueprints needed recompiling",
        "world_duplicate": "PIE: StaticDuplicateObject took:",
        "world_init": "PIE: World Init took:",
        "client_load_map": "LogLoad: Took ",
    }.items():
        events[name] = event(fragment)
    host_total = delta(start, host_ready)
    nav_wait = delta(resource_init, resource_ready)
    return {
        "label": label, "source": str(path), "snapshot": str(snapshot),
        "snapshot_sha256": hashlib.sha256(raw).hexdigest(), "log_lines": len(lines),
        "start_utc": start.isoformat(), "start_basis": start_basis,
        "start_to_resource_initialized_s": delta(start, resource_init),
        "resource_navigation_wait_wall_s": nav_wait,
        "resource_ready_to_soldiers_s": delta(resource_ready, spawned),
        "resource_ready_to_host_bootstrap_s": delta(resource_ready, host_ready),
        "start_to_host_bootstrap_s": host_total,
        "start_to_client_bootstrap_s": delta(start, client_ready),
        "host_to_client_bootstrap_s": delta(host_ready, client_ready),
        "navigation_wait_share_of_host_startup_percent": round(nav_wait / host_total * 100, 2),
        "soldier_count": int(re.search(r"Spawned (\d+)", spawned["text"]).group(1)),
        "async_compile_memory_budget_messages": compile_counts,
        "events": events,
        "limits": [
            "The navigation wait is a wall-clock gate interval, not exclusive CPU time.",
            "Client loading and asset work overlap the navigation wait; do not add them again.",
            "Readiness markers measure gameplay bootstrap, not the first presented video frame.",
        ],
    }


if __name__ == "__main__":
    results = {label: summarize(label, path) for label, path in INPUTS.items()}
    (OUT / "timings.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    for label, row in results.items():
        print(json.dumps({key: value for key, value in row.items()
                          if key not in {"events", "limits"}}, ensure_ascii=True))

"""Static migration inventory. Does not load assets, build native code, or run the game."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

PATTERNS = {
    "authority": r"\b(?:HasAuthority|IsAuthorityWorld)\s*\(|\bNM_(?:Client|DedicatedServer)\b",
    "rpc": r"UFUNCTION\s*\([^\n]*(?:Server|Client|NetMulticast)",
    "replication": r"\bDOREPLIFETIME\w*\s*\(|\bReplicatedUsing\s*=",
    "transaction": r"\b(?:CommitDamage|CommitEffectDamage|ApplyServerDamage|Credit|Reserve|Refund|MineOneRaw|MineNodeRaw)\s*\(",
    "spawn": r"\b(?:SpawnActor\w*|TrySpawnAuthorityPopulation|RestartPlayer|InitializeServerMatchState)\s*\(",
    "clock": r"\b(?:GetTimeSeconds|GetWorldTimerManager|SetTimer|SetTimerForNextTick)\s*\(",
}
EXCLUDED = {"Intermediate", "Saved", "Binaries", "DerivedDataCache", "Downloads", ".git", "__pycache__"}

def collect(root: Path) -> dict:
    roots = [root / "Source"]
    plugins = root / "Plugins"
    if plugins.is_dir():
        roots.extend(p / "Source" for p in plugins.iterdir() if p.is_dir() and (p / "Source").is_dir())
    entries = []
    patterns = {k: re.compile(v) for k, v in PATTERNS.items()}
    for source in roots:
        for directory, directories, files in os.walk(source):
            directories[:] = sorted(d for d in directories if d not in EXCLUDED)
            for name in sorted(files):
                path = Path(directory) / name
                if path.suffix not in {".h", ".cpp"}:
                    continue
                raw = path.read_bytes()
                lines = raw.decode("utf-8-sig").splitlines()
                hits = [{"line": n, "kinds": kinds, "text": text.strip()}
                        for n, text in enumerate(lines, 1)
                        if (kinds := [key for key, pattern in patterns.items() if pattern.search(text)])]
                if hits:
                    entries.append({"path": path.relative_to(root).as_posix(),
                                    "sha256": hashlib.sha256(raw).hexdigest(),
                                    "development_only": "Tests" in path.parts or "Development" in path.parts,
                                    "review": "unclassified", "hits": hits})
    return {"schema": "guli-hybrid-authority-inventory/v1", "work_id": "WORK-20261011-002",
            "scope": "Source and immediate Plugins/*/Source only; Blueprint graph audit is separate",
            "files": entries, "file_count": len(entries), "hit_count": sum(len(e["hits"]) for e in entries)}

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    result = collect(root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("schema", "file_count", "hit_count")}, ensure_ascii=False))

if __name__ == "__main__":
    main()

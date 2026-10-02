"""Run one serial, checkpointed import stage against the currently open UE editor."""

import argparse
import json
from pathlib import Path

from commander_editor_python import call_editor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action")
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / "Scripts/import_combat_island.py"
    code = f"ISLAND_ACTION={args.action!r}\nISLAND_CAPTURE={args.capture!r}\n" + source.read_text(encoding="utf-8")
    response = call_editor(code, args.timeout)
    evidence = root / "ArtSource/Environment/GuLiStrike_CombatIsland_2300m_v1/UEImport/Evidence"
    evidence.mkdir(parents=True, exist_ok=True)
    filename = args.action.replace(":", "_") + ("_capture" if args.capture else "") + ".json"
    (evidence / filename).write_text(json.dumps(response, ensure_ascii=False, indent=2), encoding="utf-8")
    result = response.get("result")
    display = dict(result) if isinstance(result, dict) else result
    if isinstance(display, dict) and isinstance(display.get("samples"), list):
        display["sample_count"] = len(display.pop("samples"))
    print(json.dumps({"transport_success": response.get("success"), "result": display,
                      "evidence": str(evidence / filename)}, ensure_ascii=False, indent=2))
    return 0 if response.get("success") and isinstance(result, dict) and result.get("success") else 1


if __name__ == "__main__":
    raise SystemExit(main())

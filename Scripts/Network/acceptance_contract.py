"""Coverage and immutable inputs for the integrated Hybrid acceptance suite.

This is test orchestration, never a source of gameplay or ledger authority.
An assertion is complete only after a driver has attached observed evidence.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path


SCENARIOS = {
    "gameplay_merged": ("G", "gameplay", "Commander", (
        "mass_commands", "mass_combat", "wm01", "engineering", "construction",
        "replacement_population", "mining_transport", "inventory_composition",
        "production", "territory_supply", "skills", "rogue", "rogue_cancel", "teleport",
        "spell_fields", "fixed_reconnect", "fixed_role_rejections", "resource_group", "commander_ui", "production_blueprints", "generic_content", "machinegun_speed",
        "commander_selection_takeover", "selection_pinning", "idle_offload", "handoff_state",
        "ground_movement", "ground_combat", "ground_query_presentation", "ground_ui",
        "population_cancel_reject_death", "supply_loss_resume", "teleport_cancel",
        "combat_event_correspondence", "map_entry", "production_blueprint_execution",
        "ship_movement", "ship_equipment", "ship_combat", "wingman_combat", "wingman_air_combat", "wingman_ground_combat",
        "wingman_cadence", "air_query_presentation", "air_ui")),
    "fault_merged": ("F", "fault", "Commander", (
        "independent_progress", "transport_duplicate_reorder", "business_idempotence",
        "stale_authority_life_epoch", "pending_work_handoff", "newborn_handoff",
        "capability_handoff", "retained_source_work", "ground_ui_rebind", "ground_handoff_stability",
        "member_loss", "fixed_role_rejections", "wingman_vacancy_rebirth",
        "member_join", "service_disconnect", "service_restart", "commander_air_ui_rebind", "empty_team")),
}

COMMON_GAMEPLAY = ("unique_simulation", "stable_ownership", "no_receive_republish",
                   "additional_flights", "logic_only")
DEPENDENCIES = {
    "authority": ("unique_simulation", "stable_ownership", "mass_commands", "fixed_reconnect", "fixed_role_rejections", "resource_group",
                  "member_loss", "fixed_role_rejections", "stale_authority_life_epoch"),
    "transactions": ("mass_combat", "wm01", "ground_combat", "ship_combat", "wingman_combat", "wingman_air_combat", "wingman_ground_combat",
                     "construction", "replacement_population", "mining_transport", "inventory_composition",
                     "production", "territory_supply", "skills", "rogue", "rogue_cancel", "teleport", "spell_fields",
                     "population_cancel_reject_death", "supply_loss_resume", "teleport_cancel", "combat_event_correspondence",
                     "business_idempotence", "pending_work_handoff", "retained_source_work", "service_restart"),
    "handoff": tuple(check for value in SCENARIOS.values() if value[1] == "fault" for check in value[3]),
    "presentation": ("ground_query_presentation", "air_query_presentation", "production_blueprints",
                     "production_blueprint_execution", "map_entry", "no_receive_republish", "logic_only"),
}


def required_checks(case: str) -> tuple[str, ...]:
    _, gate, _, specific = SCENARIOS[case]
    return specific + (COMMON_GAMEPLAY if gate == "gameplay" else ())


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def freeze_baseline(root: Path, manifest: Path) -> dict:
    """Hash explicit code/artifact inputs; UE assets are inspected by the editor."""
    files = [manifest, root / "Config/DefaultGame.ini", root / "Config/DefaultEngine.ini", root / "GuLiStrike.uproject",
             root / "Config/Hybrid/AcceptanceProfile.json",
             root / "Config/Hybrid/AcceptanceAssetReadback.json",
             root / "Config/Custom/HybridAcceptance/DefaultGame.ini",
             root / "Config/Hybrid/ProductionBlueprintMigration.json",
             root / "Binaries/Win64/UnrealEditor.modules",
             root / "Binaries/Win64/UnrealEditor-GuLiStrike.dll",
             root / "Binaries/Win64/UnrealEditor-GuLiNetCore.dll",
             root / "Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe"]
    # Bounded source folders only. Never walk runtime caches or content assets.
    for folder in (root / "Scripts/Network", root / "Source/GuLiNetCore",
                   root / "Source/GuLiStrike", root / "Source/GuLiStrikeEditor",
                   root / "Tools/GuLiCoordinator/src"):
        for current, directories, names in os.walk(folder):
            directories[:] = [name for name in directories if name not in
                              ("Intermediate", "Saved", "Binaries", "DerivedDataCache", "Downloads", "__pycache__")]
            files.extend(Path(current) / name for name in names
                         if Path(name).suffix in (".py", ".cpp", ".h", ".inl", ".cs"))
    hashes = {p.relative_to(root).as_posix() if p.is_relative_to(root) else str(p): digest(p)
              for p in sorted(set(files))}
    definition = json.loads(manifest.read_text(encoding="utf-8-sig"))
    value = {"schema": "guli-acceptance-baseline/v3", "files": hashes,
             "manifest_configuration": definition.get("config_hash"),
             "manifest_map": definition.get("map_hash"), "seed": 20261011}
    value["identity"] = hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()
    return value


def validate_manifest_contract(manifest: dict, population: int) -> None:
    """Require registered mechanisms, not names invented for a coverage label."""
    strings, numbers = manifest.get("strings", {}), manifest.get("numbers", {})
    if (manifest.get("protocol_version"), manifest.get("schema"), manifest.get("handoff_version")) != (31, 24, 2):
        raise RuntimeError("The acceptance suite requires protocol 31 / manifest 24 / object handoff 2")
    if int(float(numbers.get("acceptance/population", -1))) != population:
        raise RuntimeError("Acceptance population differs from the exported manifest")
    definitions = {key.removeprefix("adapter/definition/"): value for key, value in strings.items()
                   if key.startswith("adapter/definition/")}
    missing = sorted({obj["definition"] for obj in manifest.get("objects", [])} - definitions.keys())
    if missing:
        raise RuntimeError("Startup definitions have no registered adapter: " + ", ".join(missing))
    for name, expected in (("simulation/logic_hz", 60), ("simulation/mass_hz", 10),
                           ("simulation/wingman_hz", 30), ("network/wingman/motion_hz", 10)):
        if name not in numbers or float(numbers[name]) != expected:
            raise RuntimeError("Unexpected production cadence: " + name)
    if not manifest.get("rules") or not definitions:
        raise RuntimeError("No exported gameplay rules or adapter definitions")


def validate_gate(directory: Path, gate: str, baseline: str) -> dict:
    report = json.loads((directory / (gate + "-gate.json")).read_text(encoding="utf-8"))
    if report.get("baseline") != baseline or report.get("status") != "passed":
        raise RuntimeError(f"{gate} gate is missing, incomplete or belongs to another build")
    expected = {case for case, value in SCENARIOS.items() if value[1] == gate}
    if set(report.get("scenarios", {})) != expected:
        raise RuntimeError(f"{gate} gate has incomplete scenario coverage")
    for case, record in report["scenarios"].items():
        result_path = Path(record["result"])
        if digest(result_path) != record["sha256"]:
            raise RuntimeError("Gate evidence changed after validation: " + str(result_path))
        result = json.loads(result_path.read_text(encoding="utf-8"))
        checks = result.get("checks", {})
        if result.get("baseline") != baseline or result.get("case") != case or result.get("status") != "passed" or any(checks.get(key, {}).get("status") != "passed"
                                                      for key in required_checks(case)):
            raise RuntimeError("Gate has unproven checks: " + case)
        for check in checks.values():
            for item in check.get("evidence", []):
                if not isinstance(item, dict) or digest(Path(item["path"])) != item["sha256"]:
                    raise RuntimeError("Gate assertion evidence is missing or changed: " + case)
    return report

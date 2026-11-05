"""Re-export the four current manifests after loading the matching native build.

Run via commander_editor_python.py. Validate staged files with both the editor
and the coordinator's offline manifest reader before publishing the files.
"""
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert not unreal.EditorLevelLibrary.get_pie_worlds(True), "Manifest export requires an idle editor"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split(".")[0] == "/Game/Maps/LVL_CommanderMassPrototype"
source = (root / "Source/GuLiNetCore/Public/GuLiNetTypes.h").read_text(encoding="utf-8-sig")
expected_protocol = int(re.search(r"ProtocolVersion\s*=\s*(\d+)", source).group(1))
profile_path = root / "Config/Hybrid/AcceptanceProfile.json"
profile_bytes = profile_path.read_bytes()
profile = json.loads(profile_bytes.decode("utf-8-sig"))
stage = root / "TestResults/Performance/20261105-network-stalls/manifests" / f"v{expected_protocol}"
stage.mkdir(parents=True, exist_ok=True)
coordinator = root / "Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe"
assert coordinator.is_file(), "Build the Release coordinator before publishing manifests"
prepared = []
runtime_contract = {}
profile_changes = []
ground_contract_fields = (("numbers", "adapter/family/GroundPlayer/state_version"),
                          ("strings", "ground/mobility_configuration"))
for population in (200, 600):
    for acceptance in (False, True):
        name = f"{'Acceptance' if acceptance else 'Runtime'}Manifest-{population}.json"
        target = root / "Config/Hybrid" / name
        original = target.read_bytes()
        temporary = stage / name
        api = unreal.GuLiHybridAuthoringLibrary
        result = json.loads(api.export_runtime_manifest_with_overrides(
            population, str(temporary), json.dumps(profile["overrides"])) if acceptance
            else api.export_runtime_manifest(population, str(temporary)))
        assert result.get("success"), result
        raw = temporary.read_bytes()
        manifest = json.loads(raw.decode("utf-8-sig"))
        assert manifest["protocol_version"] == expected_protocol, (
            "Editor still has the old native module loaded; existing manifests were preserved",
            manifest["protocol_version"], expected_protocol)
        validation = json.loads(api.validate_runtime_manifest(str(temporary)))
        assert validation.get("success"), validation
        checked = subprocess.run([str(coordinator), "--validate-manifest", "--manifest", str(temporary)],
                                 capture_output=True, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        if checked.returncode:
            raise RuntimeError(f"Coordinator rejected {name}: {checked.stderr.decode(errors='replace')}")
        previous = json.loads(original.decode("utf-8-sig"))
        if not acceptance:
            runtime_contract[population] = {(section, key): manifest[section][key] for section, key in ground_contract_fields}
        else:
            # These two generated acceptance fields can lag the production
            # GroundPlayer adapter. Accept only the values already verified in
            # the same population's unchanged production data and native export.
            for section, key in ground_contract_fields:
                new = manifest[section][key]
                old = previous[section][key]
                if old != new and new == runtime_contract[population][section, key]:
                    profile_changes.append({"file": name, "field": key, "previous": old, "current": new})
                    previous[section][key] = new
        changed = [key for key in sorted(previous.keys() | manifest.keys())
                   if key not in {"protocol_version", "config_hash", "map_hash"} and previous.get(key) != manifest.get(key)]
        if changed:
            raise RuntimeError(f"{name} also changes gameplay data {changed}. Review the staged export in {stage}.")
        info = {"path": str(target), "sha256": hashlib.sha256(raw).hexdigest(),
                "config_hash": manifest["config_hash"], "map_hash": manifest["map_hash"],
                "objects": len(manifest["objects"])}
        if acceptance:
            profile["manifests"][str(population)].update(info)
        prepared.append((target, original, raw, info))

assert profile_path.read_bytes() == profile_bytes, "Acceptance profile changed during export"
assert all(target.read_bytes() == original for target, original, _, _ in prepared), "Manifest changed during export"
def replace_bytes(target, value):
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False, suffix=".pending") as file:
        pending = Path(file.name)
        file.write(value)
    try:
        pending.replace(target)
    finally:
        pending.unlink(missing_ok=True)

updates = [(path, original, raw) for path, original, raw, _ in prepared]
updates.append((profile_path, profile_bytes, json.dumps(profile, ensure_ascii=False, indent=2).encode("utf-8")))
applied = []
try:
    for target, original, value in updates:
        if target.read_bytes() != original:
            raise RuntimeError(f"{target.name} changed during publication")
        replace_bytes(target, value)
        applied.append((target, original, value))
    assert all(target.read_bytes() == value for target, _, value in updates)
except Exception:
    for target, original, value in reversed(applied):
        if target.read_bytes() == value:
            replace_bytes(target, original)
    raise
unreal.MCPythonHelper.submit_result(json.dumps({
    "success": True, "protocol": expected_protocol,
    "scope": "Editor export, coordinator --validate-manifest and file readback",
    "updated_acceptance_contracts": profile_changes,
    "manifests": [info for _, _, _, info in prepared],
}, ensure_ascii=False))

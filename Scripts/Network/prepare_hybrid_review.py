"""Migrate reviewed production graphs, then save/read the original-map entry.

Editor authoring only. Native code must already be loaded. This script never
compiles Blueprints, loads unknown assets, launches PIE or starts a service.
Each save is recorded immediately; a failed later stage is not a full rollback.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os

import unreal


def canonical_hash(value):
    data = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(data.encode("utf-8")).hexdigest()


def prepare(manifest_file, room_id, development_identity, population=600, migration_plan=None):
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    output = project / "TestResults/HybridFullAcceptance/Implementation/review-authoring-readback.json"
    result = {"schema": "guli-hybrid-authoring/v2", "work_id": "WORK-20261011-002",
              "observed_at_utc": datetime.now(timezone.utc).isoformat(),
              "pid": os.getpid(), "success": False, "map": "/Game/Maps/LVL_CommanderMassPrototype",
              "native_build": "not_run", "blueprint_compile": "not_run", "gameplay": "not_run",
              "performance": "not_run", "blueprint_migration_complete": False,
              "scene_saved": False, "entity_readback": False, "applied_operations": []}

    def persist():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")

    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        # LevelEditorSubsystem is not available in an authoring commandlet.
        # Native inspection/mutation entry points check GEditor->PlayWorld.
        if not world or world.get_path_name().split(".")[0] != result["map"]:
            raise RuntimeError("Open the original prototype map in an idle editor first")
        library = getattr(unreal, "GuLiHybridAuthoringLibrary", None)
        required = ("validate_runtime_manifest", "inspect_production_blueprints",
                    "retarget_production_pure_call", "save_hybrid_review_entry", "read_hybrid_review_entry")
        missing = [name for name in required if not hasattr(library, name)]
        if not hasattr(unreal, "GuLiHybridNetworkReviewActor"):
            missing.append("GuLiHybridNetworkReviewActor")
        result["missing_native_interfaces"] = missing
        if missing:
            raise RuntimeError("Current editor has not loaded the authoring implementation: " + ", ".join(missing))
        validated = json.loads(library.validate_runtime_manifest(str(manifest_file)))
        result["manifest"] = validated
        if not validated.get("success"):
            raise RuntimeError("Manifest must be exported with the current native contracts")
        manifest_raw = Path(manifest_file).read_bytes()
        result["manifest_sha256"] = hashlib.sha256(manifest_raw).hexdigest()
        module_path = project / "Scripts/Network/inspect_production_blueprints.py"
        spec = importlib.util.spec_from_file_location("guli_production_readback", module_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        closure_file = output.with_name("review-production-dependency-readback.json")
        module.inspect(manifest_file, closure_file)
        closure = json.loads(closure_file.read_text(encoding="utf-8"))
        paths = [entry["asset"] for entry in closure["assets"]]
        if not paths or closure["missing_registry_packages"] or any(not a["loaded"] for a in closure["assets"]):
            raise RuntimeError("Production dependency readback is incomplete; no asset mutation attempted")
        report = json.loads(library.inspect_production_blueprints(paths))
        result["blueprints_before"] = report
        if not report.get("success"):
            raise RuntimeError("Native production Blueprint inspection failed")
        plan = json.loads(Path(migration_plan).read_text(encoding="utf-8-sig")) if migration_plan else {"operations": []}
        if migration_plan and plan.get("schema") != "guli-production-migration/v1":
            raise RuntimeError("Unsupported production migration plan")
        operations = plan.get("operations", [])
        if len(operations) > 512:
            raise RuntimeError("Too many production graph operations")
        for operation in operations:
            path = operation["asset"]
            if path not in paths or operation["kind"] != "retarget_pure_call":
                raise RuntimeError("Operation lies outside the inspected production scope")
            bp = unreal.find_object(None, path)
            readable = json.loads(unreal.MCPythonHelper.get_blueprint_graph_info(bp, operation["graph"]))
            # Freeze reviewed links/defaults, not merely a reusable function name.
            current = json.loads(library.inspect_production_blueprints([path]))
            graphs = [g for g in current["assets"][0]["graphs"] if g["graph"] == operation["graph"]]
            if len(graphs) != 1:
                raise RuntimeError("Migration graph disappeared or is ambiguous")
            graph = graphs[0]
            nodes = [n for n in graph["nodes"] if n["name"] == operation["node_name"]]
            if len(nodes) != 1:
                raise RuntimeError("Migration node disappeared or is ambiguous")
            node = nodes[0]
            if node.get("function") == operation["replacement_function"]:
                result["applied_operations"].append({"asset": path, "node": node["guid"], "state": "already_retargeted"})
                continue
            if canonical_hash(readable) != operation["source_graph_sha256"]:
                raise RuntimeError("Production graph differs from the reviewed migration plan")
            changed = json.loads(library.retarget_production_pure_call(
                path, operation["graph"], node["guid"], graph["fingerprint"],
                operation["expected_function"], operation["replacement_function"]))
            result["applied_operations"].append(changed)
            persist()
            if not changed.get("success"):
                raise RuntimeError("A production graph operation was not saved; see applied_operations")
        result["blueprints_after"] = json.loads(library.inspect_production_blueprints(paths))
        # Source edits retain unclassified findings and compile debt.
        result["blueprint_source_operations_saved"] = bool(operations)
        result["blueprint_migration_status"] = ("source_edits_saved_review_and_compile_pending"
                                                 if operations else "inspection_only")
        result["scene"] = json.loads(library.save_hybrid_review_entry(
            str(manifest_file), str(room_id), str(development_identity), int(population)))
        result["scene_saved"] = bool(result["scene"].get("saved"))
        persist()
        if not result["scene"].get("success"):
            raise RuntimeError("Review entry save/readback was incomplete")
        result["entity"] = json.loads(library.read_hybrid_review_entry())
        result["entity_readback"] = bool(result["entity"].get("success"))
        if not result["entity_readback"]:
            raise RuntimeError("Independent review entity readback failed")
        result["success"] = True
    except Exception as error:
        result["error"] = str(error)
    finally:
        persist()
    return result

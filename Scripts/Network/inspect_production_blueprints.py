"""Read production Blueprint dependencies/loaded graphs without compiling or loading assets.

Run in the editor through commander_editor_python. Missing loaded packages are
reported, never interpreted as an empty/safe graph. AssetRegistry queries are
bounded to dependencies of explicit roots; no directory or marketplace scan.
"""
from collections import deque
from pathlib import Path
import hashlib
import json
import os
import re

import unreal

MAP = "/Game/Maps/LVL_CommanderMassPrototype"
SCHEMA = "guli-production-blueprints/v1"
# Exported soft references may contain Unicode names and class wrappers.
ASSET_PATH = re.compile(r'''/Game/[^\s'"(),;=:\[\]{}]+''')
LEAF_CLASSES = {"StaticMesh", "SkeletalMesh", "Skeleton", "Texture2D", "Material",
                "MaterialInstanceConstant", "AnimSequence", "PhysicsAsset", "SoundWave"}


def inspect(manifest_file, output_file, extra_roots=()):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    # Native graph inspection checks the editor/PIE context. The UI-only level
    # subsystem is unavailable when this readback runs as a commandlet.
    if not world or world.get_path_name().split(".")[0] != MAP:
        raise RuntimeError("Open the original prototype map in an idle editor")
    raw = Path(manifest_file).read_bytes()
    manifest = json.loads(raw.decode("utf-8-sig"))
    loaded = {b.get_path_name(): b for b in unreal.ObjectIterator(unreal.Blueprint)
              if b.get_path_name().startswith("/Game/")
              and not b.get_path_name().startswith("/Game/Assets/")}
    roots = {MAP, *extra_roots}
    for value in manifest.get("strings", {}).values():
        if isinstance(value, str):
            roots.update(ASSET_PATH.findall(value))
    # Includes native CDO/UI references already loaded by this map, which need
    # not occur in the numerical manifest. Record their provenance explicitly.
    roots.update(loaded)
    queue = deque(sorted({p.split(".")[0] for p in roots}))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True, include_hard_package_references=True,
        include_searchable_names=False, include_soft_management_references=False,
        include_hard_management_references=False)
    visited, excluded, blueprints, missing_registry = set(), set(), set(), []
    while queue:
        package = queue.popleft()
        if package in visited:
            continue
        if not package.startswith("/Game/") or package.startswith("/Game/Assets/"):
            excluded.add(package)
            continue
        visited.add(package)
        if len(visited) > 4096:
            raise RuntimeError("Production dependency closure exceeds 4096 packages")
        assets = registry.get_assets_by_package_name(package)
        if not assets:
            missing_registry.append(package)
            continue
        leaf = True
        for data in assets:
            kind = str(data.asset_class_path.asset_name)
            path = str(data.package_name) + "." + str(data.asset_name)
            if "Blueprint" in kind and "GeneratedClass" not in kind:
                blueprints.add(path)
            leaf = leaf and kind in LEAF_CLASSES
        if not leaf:
            queue.extend(sorted(str(p) for p in registry.get_dependencies(package, options)))
    blueprints.update(loaded)
    graph_names = {p: set() for p in loaded}
    for graph in unreal.ObjectIterator(unreal.EdGraph):
        # Enumerate loaded reflection objects, not package files. Intermediate
        # generated graphs may be returned but are labelled by the helper.
        owner = graph.get_outer()
        while owner and not isinstance(owner, unreal.Blueprint):
            owner = owner.get_outer()
        if owner and owner.get_path_name() in graph_names:
            graph_names[owner.get_path_name()].add(graph.get_name())
    entries = []
    for path in sorted(blueprints):
        bp = loaded.get(path)
        entry = {"asset": path, "loaded": bp is not None, "graphs": [],
                 "migration": "pending_semantic_review" if bp else "requires_asset_load"}
        if bp:
            entry["variables"] = json.loads(unreal.MCPythonHelper.list_blueprint_variables(bp))
            for name in sorted(graph_names[path]):
                graph = json.loads(unreal.MCPythonHelper.get_blueprint_graph_info(bp, name))
                entry["graphs"].append(graph)
            # This older helper omits hidden pins/function flags. Its snapshot
            # supports human review, never an automatic full-migration claim.
            entry["inspection_limits"] = ["hidden_pins", "function_flags", "component_contract_versions"]
            canonical = json.dumps(entry, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            entry["readback_sha256"] = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
        entries.append(entry)
    result = {"schema": SCHEMA, "work_id": "WORK-20261011-002", "success": True,
              "pid": os.getpid(), "map": MAP, "manifest": str(manifest_file),
              "manifest_sha256": hashlib.sha256(raw).hexdigest(),
              "manifest_versions": {k: manifest.get(k) for k in ("schema", "protocol_version", "handoff_version")},
              "loaded_roots": sorted(loaded), "packages": len(visited), "excluded_packages": sorted(excluded),
              "missing_registry_packages": sorted(missing_registry), "assets": entries,
              "inspection_complete": False, "migration_complete": False,
              "modified_assets": False, "compiled": False, "gameplay_executed": False}
    output = Path(output_file)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return {"success": True, "report": str(output), "packages": len(visited),
            "blueprints": len(entries), "loaded": sum(e["loaded"] for e in entries),
            "missing": [e["asset"] for e in entries if not e["loaded"]],
            "migration_complete": False}

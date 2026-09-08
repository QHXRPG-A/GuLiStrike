"""Read-only capability probe for GuLiStrike's UE5.7 VFX authoring services.

Run directly for an import check, or through ``Scripts/ue_exec.py`` to inspect
the live Unreal Editor Python process. This script performs introspection only:
it never calls service methods or creates, compiles, saves, or dirties assets.
"""

from __future__ import annotations

import json
from typing import Any


REQUIRED_SERVICES = {
    "NiagaraService": (
        "search_systems",
        "summarize",
        "create_system",
        "add_emitter",
        "compile_with_results",
        "save_system",
    ),
    "NiagaraEmitterService": (
        "list_modules",
        "add_module",
        "list_renderers",
        "add_renderer",
        "get_module_input",
        "set_module_input",
    ),
    "NiagaraScratchPadService": (
        "create_scratch_module",
        "add_custom_hlsl_node",
        "connect_pins",
        "apply_changes",
    ),
    "MaterialService": (
        "get_material_info",
        "create_material",
        "compile_material",
        "save_material",
    ),
    "MaterialNodeService": (
        "export_material_graph",
        "get_material_diagnostics",
        "create_expression",
        "connect_to_output",
    ),
    "ScreenshotService": (
        "capture_asset_editor",
        "capture_editor_window",
        "get_open_editor_tabs",
    ),
}


def _safe_call(callable_object: Any, fallback: str = "unavailable") -> str:
    try:
        return str(callable_object())
    except Exception:
        return fallback


def build_report() -> dict[str, Any]:
    report: dict[str, Any] = {
        "read_only": True,
        "engine_version": "unavailable",
        "project_file": "unavailable",
        "unreal_module_available": False,
        "services": {},
        "ready": False,
    }

    try:
        import unreal  # type: ignore
    except Exception as exc:
        report["import_error"] = f"{type(exc).__name__}: {exc}"
        for class_name, methods in REQUIRED_SERVICES.items():
            report["services"][class_name] = {
                "available": False,
                "required_methods": {method: False for method in methods},
                "missing": list(methods),
            }
        return report

    report["unreal_module_available"] = True
    report["engine_version"] = _safe_call(unreal.SystemLibrary.get_engine_version)
    report["project_file"] = _safe_call(unreal.Paths.get_project_file_path)

    all_ready = True
    for class_name, methods in REQUIRED_SERVICES.items():
        service_class = getattr(unreal, class_name, None)
        method_status = {
            method: service_class is not None and callable(getattr(service_class, method, None))
            for method in methods
        }
        missing = [method for method, available in method_status.items() if not available]
        available = service_class is not None
        report["services"][class_name] = {
            "available": available,
            "required_methods": method_status,
            "missing": missing,
        }
        all_ready = all_ready and available and not missing

    report["ready"] = all_ready
    return report


print(json.dumps(build_report(), ensure_ascii=False, sort_keys=True))
